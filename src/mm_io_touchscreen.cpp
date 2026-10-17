#include <windows.h>
#include "mm_io_touchscreen.h"
#include "anyslider_log.h"
#include <exception>
#include <stdexcept>
#include "arcade_overlapped_io.h"

namespace anyslider
{
using Clock = std::chrono::steady_clock;

// 串口句柄仅由触屏worker使用；析构负责关闭连接。
class MmIoTouchscreenFrontend::SerialChannel
{
  public:
    ~SerialChannel()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            if (!CloseHandle(handle_))
            {
                Log("Touchscreen: close serial handle failed: Win32=%lu", GetLastError());
            }
        }
    }
    SerialChannel() = default;
    SerialChannel(const SerialChannel &) = delete;
    SerialChannel &operator=(const SerialChannel &) = delete;

    bool Open(const std::string &port)
    {
        // 只打开显式指定的端口，不扫描并复位其他串口设备。
        if (port.size() < 4 || port.compare(0, 3, "COM") != 0 ||
            !std::all_of(port.begin() + 3, port.end(),
                         [](char c)
                         {
                             return c >= '0' && c <= '9';
                         }))
        {
            Log("Touchscreen: invalid COM port: %s", port.c_str());
            return false;
        }
        const std::string path = "\\\\.\\" + port;
        handle_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
        {
            Log("Touchscreen: open %s failed: Win32=%lu", port.c_str(), GetLastError());
            return false;
        }
        DCB settings{};
        settings.DCBlength = sizeof(settings);
        if (!GetCommState(handle_, &settings))
        {
            return false;
        }
        settings.BaudRate = CBR_9600;
        settings.ByteSize = 8;
        settings.Parity = NOPARITY;
        settings.StopBits = ONESTOPBIT;
        settings.fBinary = TRUE;
        settings.fParity = FALSE;
        // AFT SBZV: 1406A1490 passes CTS=0 to 1405E31A0 (via 1405E2920).
        // Keep DTR/RTS asserted, but do not stall transmission waiting for CTS.
        settings.fOutxCtsFlow = FALSE;
        settings.fOutxDsrFlow = FALSE;
        settings.fDtrControl = DTR_CONTROL_ENABLE;
        settings.fDsrSensitivity = FALSE;
        settings.fTXContinueOnXoff = TRUE;
        settings.fOutX = FALSE;
        settings.fInX = FALSE;
        settings.fErrorChar = FALSE;
        settings.fNull = FALSE;
        settings.fRtsControl = RTS_CONTROL_ENABLE;
        settings.fAbortOnError = FALSE;
        COMMTIMEOUTS timeouts{};
        // 单个系统调用有界；线程停止不需要销毁仍被内核使用的缓冲区。
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 20;
        timeouts.WriteTotalTimeoutConstant = 100;
        if (!SetCommState(handle_, &settings) || !SetCommTimeouts(handle_, &timeouts) ||
            !PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR))
        {
            Log("Touchscreen: serial setup failed: Win32=%lu", GetLastError());
            return false;
        }
        Log("Touchscreen: opened %s (9600/8N1)", port.c_str());
        return true;
    }
    bool SetBaud(DWORD baud)
    {
        DCB settings{};
        settings.DCBlength = sizeof(settings);
        if (!GetCommState(handle_, &settings))
        {
            Log("Touchscreen: GetCommState failed: Win32=%lu", GetLastError());
            return false;
        }
        settings.BaudRate = baud;
        if (!SetCommState(handle_, &settings))
        {
            Log("Touchscreen: baud change to %lu failed: Win32=%lu", baud, GetLastError());
            return false;
        }
        Log("Touchscreen: host baud=%lu", baud);
        return true;
    }
    bool Send(uint8_t command, std::span<const uint8_t> payload = {})
    {
        const auto wire = elo::Encode(command, payload);
        size_t pos = 0;
        while (pos < wire.size())
        {
            DWORD written = 0;
            auto *remainingBytes = const_cast<uint8_t *>(wire.data() + pos);
            const DWORD remainingSize = static_cast<DWORD>(wire.size() - pos);
            const bool transferred = Transfer(false, remainingBytes, remainingSize, written);
            if (!transferred || written == 0)
            {
                Log("Touchscreen: send command=%02X failed: bytes=%zu/10 Win32=%lu",
                    command, pos, transferred ? ERROR_WRITE_FAULT : GetLastError());
                return false;
            }
            pos += written;
        }
        return true;
    }
    bool Read(std::array<uint8_t, 512> &bytes, DWORD &count)
    {
        return Transfer(true, bytes.data(), static_cast<DWORD>(bytes.size()), count);
    }

  private:
    bool Transfer(bool read, uint8_t *data, DWORD size, DWORD &count)
    {
        bool retained = false;
        const bool ok = ArcadeTransfer(handle_, read, data, size, 0, read ? 100 : 150, count, retained);
        if (retained)
        {
            handle_ = INVALID_HANDLE_VALUE; // Preserve handle with pending kernel I/O; stop this backend.
            throw std::runtime_error("driver did not complete cancellation; backend quarantined");
        }
        return ok || (read && GetLastError() == ERROR_TIMEOUT);
    }
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

MmIoTouchscreenFrontend::~MmIoTouchscreenFrontend()
{
    Shutdown();
}
void MmIoTouchscreenFrontend::Initialize(const MmIoTouchscreenConfig &config)
{
    Shutdown();
    config_ = config;
    if (config.enabled)
    {
        state_.store(TouchConnectionState::Connecting, std::memory_order_release);
        worker_ = std::jthread(
            [this](std::stop_token stop)
            {
                Run(stop);
            });
    }
    else
    {
        state_.store(TouchConnectionState::Disconnected, std::memory_order_release);
    }
}
void MmIoTouchscreenFrontend::Shutdown()
{
    if (worker_.joinable())
    {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
    std::lock_guard lock(mutex_);
    events_.clear();
    last_ = {};
    require_fresh_ = true;
    state_.store(TouchConnectionState::Disconnected, std::memory_order_release);
}
bool MmIoTouchscreenFrontend::Wait(std::stop_token stop, unsigned ms)
{
    std::unique_lock lock(mutex_);
    wake_.wait_for(lock, stop, std::chrono::milliseconds(ms),
                   []
                   {
                       return false;
                   });
    return !stop.stop_requested();
}
// worker发布，游戏线程Consume；队列保留DOWN/UP，连续移动只更新位置。
void MmIoTouchscreenFrontend::Publish(elo::Event event)
{
    std::lock_guard lock(mutex_);
    const auto now = Clock::now();
    if (require_fresh_)
    {
        if (event.down && !event.initial)
        {
            return;
        }
        require_fresh_ = false;
    }
    updated_ = now;
    if (!events_.empty() && event.down == last_.down && events_.back().event.down == event.down)
    {
        event.initial |= events_.back().event.initial;
        // Preserve the first edge time; STREAM must not renew an unconsumed DOWN.
        events_.back().event = event;
    }
    else if (events_.size() >= 128)
    {
        events_.clear();
        event.down = false;
        event.initial = false;
        require_fresh_ = true;
        Log("Touchscreen: input queue overflow; requiring fresh contact");
        events_.push_back({event, now});
    }
    else
    {
        events_.push_back({event, now});
    }
    last_ = event;
}
// 调用方已经持有mutex_，此处不能再次加锁。
// 清掉旧触摸并排入一次释放；后续STREAM不能冒充新的DOWN。
void MmIoTouchscreenFrontend::ResetContactLocked(Clock::time_point now)
{
    events_.clear();
    last_.down = false;
    last_.initial = false;
    require_fresh_ = true;
    events_.push_back({last_, now});
}

void MmIoTouchscreenFrontend::ResetContact()
{
    std::lock_guard lock(mutex_);
    ResetContactLocked(Clock::now());
}
void MmIoTouchscreenFrontend::Disconnect()
{
    state_.store(TouchConnectionState::Disconnected, std::memory_order_release);
    ResetContact();
}
bool MmIoTouchscreenFrontend::Consume(elo::Event &event)
{
    std::lock_guard lock(mutex_);
    const auto now = Clock::now();
    const bool queuedEventExpired = !events_.empty() && now - events_.front().time > std::chrono::seconds(2);
    const bool heldContactExpired = last_.down && now - updated_ > std::chrono::seconds(2);
    if (queuedEventExpired || heldContactExpired)
    {
        ResetContactLocked(now);
        DebugLog("Touchscreen: stale contact discarded; requiring fresh contact");
    }
    if (events_.empty())
    {
        return false;
    }
    event = events_.front().event;
    events_.pop_front();
    return true;
}
// 完成复位和参数发送后，按ACK → 参数响应 → 触摸报告的顺序消费串口。
// 只有worker运行此方法；Publish只把事件交给游戏线程，不在这里调用窗口API。
void MmIoTouchscreenFrontend::ReadReports(std::stop_token stop, SerialChannel &channel)
{
    elo::Decoder decoder;
    enum class Stage
    {
        Ack,
        Parameters,
        Ready
    };
    Stage stage = Stage::Ack;
    bool parameterFallback = false;
    size_t receivedBytes = 0;
    size_t receivedPackets = 0;
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!stop.stop_requested())
    {
        if (stage != Stage::Ready && Clock::now() > deadline)
        {
            Log("Touchscreen: initialization timeout: stage=%s bytes=%zu packets=%zu",
                stage == Stage::Ack ? "P ACK (9600)" : "p response", receivedBytes, receivedPackets);
            // AFT state6 waits for transaction idle, including ACK timeout.
            // Continue to the parameter query; only its valid response establishes Ready.
            if (stage == Stage::Ack)
            {
                Log("Touchscreen: P ACK timeout; continuing AFT parameter query at 19200");
                decoder = {};
                if (!channel.SetBaud(CBR_19200) || !channel.Send('p'))
                {
                    break;
                }
                stage = Stage::Parameters;
                deadline = Clock::now() + std::chrono::seconds(3);
                continue;
            }
            // SBZV switches the HOST to 19200 after P transaction completion, despite wire P=6.
            // Standard SmartSet/segatools stays at 9600. Retry only the read-only
            // parameter query there, without another reset or settings write.
            if (stage == Stage::Parameters && !parameterFallback)
            {
                parameterFallback = true;
                decoder = {};
                if (!channel.SetBaud(CBR_9600) || !channel.Send('p'))
                {
                    break;
                }
                Log("Touchscreen: retry parameter query at 9600");
                deadline = Clock::now() + std::chrono::seconds(3);
                continue;
            }
            break;
        }
        std::array<uint8_t, 512> bytes{};
        DWORD count = 0;
        if (!channel.Read(bytes, count))
        {
            Log("Touchscreen: read failed: %lu", GetLastError());
            break;
        }
        bool failed = false;
        receivedBytes += count;
        for (const auto &p : decoder.Feed(std::span(bytes.data(), count)))
        {
            ++receivedPackets;
            if (stage != Stage::Ready && receivedPackets <= 16)
            {
                Log("Touchscreen: init RX cmd=%02X data=%02X %02X %02X %02X",
                    p[1], p[2], p[3], p[4], p[5]);
            }
            if (p[1] == 'A')
            {
                for (size_t i = 2; i < 6; ++i)
                {
                    if (p[i] != '0')
                    {
                        failed = true;
                    }
                }
                if (failed)
                {
                    Log("Touchscreen: controller ACK error");
                    break;
                }
                if (stage == Stage::Ack)
                {
                    if (!channel.SetBaud(CBR_19200) || !channel.Send('p'))
                    {
                        failed = true;
                        break;
                    }
                    stage = Stage::Parameters;
                    deadline = Clock::now() + std::chrono::seconds(3);
                }
            }
            else if (p[1] == 'P' && stage == Stage::Parameters)
            {
                if (p[2] != '0' || (p[3] & 7) != 6)
                {
                    Log("Touchscreen: unexpected serial parameters");
                    failed = true;
                    break;
                }
                stage = Stage::Ready;
                state_.store(TouchConnectionState::Ready, std::memory_order_release);
                Log("Touchscreen: Elo SmartSet ready");
            }
            else if (stage == Stage::Ready)
            {
                elo::Event event;
                if (elo::DecodeTouch(p, event))
                {
                    DebugLog("Touchscreen: raw x=%u y=%u down=%d", event.x, event.y, event.down);
                    Publish(event);
                }
            }
        }
        if (failed)
        {
            break;
        }
    }
}

void MmIoTouchscreenFrontend::Run(std::stop_token stop)
{
    while (!stop.stop_requested())
    {
        try
        {
            SerialChannel channel;
            if (channel.Open(config_.port))
            {
                state_.store(TouchConnectionState::Connecting, std::memory_order_release);
                // AFT 1406A22A0: initial 300 ticks, R0 twice with 300 ticks
                // after each, then P/ACK and a host baud switch before query p.
                // Use cancellable wall-clock waits (300 ticks at nominal 60 Hz).
                bool resetOk = Wait(stop, 5000);
                const std::array<uint8_t, 1> reset{'0'};
                for (unsigned attempt = 0; resetOk && attempt < 2; ++attempt)
                {
                    Log("Touchscreen: hard reset %u/2 at 9600", attempt + 1);
                    resetOk = channel.Send('R', reset) && Wait(stop, 5000);
                }
                if (!resetOk)
                {
                    Disconnect();
                    if (!Wait(stop, 1000))
                    {
                        break;
                    }
                    continue;
                }
                const std::array<uint8_t, 3> params{'0', 6, 4};
                Log("Touchscreen: setting serial parameters P 30 06 04; waiting for ACK");
                if (!channel.Send('P', params))
                {
                    Disconnect();
                    if (!Wait(stop, 1000))
                    {
                        break;
                    }
                    continue;
                }
                ReadReports(stop, channel);
            }
        }
        catch (const std::exception &e)
        {
            Log("Touchscreen: worker stopped: %s", e.what());
            break;
        }
        Disconnect();
        if (!Wait(stop, 1000))
        {
            break;
        }
    }
    Disconnect();
}

} // namespace anyslider
