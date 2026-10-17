#include <windows.h>
#include "mm_io_arcade_slider.h"
#include "anyslider_log.h"
#include <algorithm>
#include <array>
#include <exception>
#include <atomic>
#include <functional>

namespace anyslider
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto kCommandRetry = std::chrono::milliseconds(100);
constexpr auto kHandshakeTimeout = std::chrono::seconds(2);
constexpr auto kReportLease = std::chrono::milliseconds(500);

class SerialChannel
{
  public:
    ~SerialChannel()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
    }
    SerialChannel() = default;
    SerialChannel(const SerialChannel &) = delete;
    SerialChannel &operator=(const SerialChannel &) = delete;

    bool Open(const std::string &port, bool assertDtrRts)
    {
        // 只打开显式指定的端口，不扫描并复位其他串口设备。
        if (port.size() < 4 || port.compare(0, 3, "COM") != 0 ||
            !std::all_of(port.begin() + 3, port.end(),
                         [](char c)
                         {
                             return c >= '0' && c <= '9';
                         }))
        {
            Log("Slider: invalid COM port: %s", port.c_str());
            return false;
        }
        const std::string path = "\\\\.\\" + port;
        handle_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
        {
            Log("Slider: open %s failed: Win32=%lu", port.c_str(), GetLastError());
            return false;
        }
        DCB settings{};
        settings.DCBlength = sizeof(settings);
        if (!GetCommState(handle_, &settings))
        {
            return false;
        }
        settings.BaudRate = CBR_115200;
        settings.ByteSize = 8;
        settings.Parity = NOPARITY;
        settings.StopBits = ONESTOPBIT;
        settings.fBinary = TRUE;
        settings.fParity = FALSE;
        settings.fOutxCtsFlow = FALSE;
        settings.fOutxDsrFlow = FALSE;
        settings.fDtrControl = DTR_CONTROL_DISABLE;
        settings.fDsrSensitivity = FALSE;
        settings.fTXContinueOnXoff = TRUE;
        settings.fOutX = FALSE;
        settings.fInX = FALSE;
        settings.fErrorChar = FALSE;
        settings.fNull = FALSE;
        settings.fRtsControl = RTS_CONTROL_DISABLE;
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
            Log("Slider: serial setup failed: Win32=%lu", GetLastError());
            return false;
        }
        // 部分 USB 串口设备需要先拉高控制线，才能接收后续初始化命令。
        if (assertDtrRts && (!EscapeCommFunction(handle_, SETDTR) || !EscapeCommFunction(handle_, SETRTS)))
        {
            Log("Slider: DTR/RTS setup failed: Win32=%lu", GetLastError());
            return false;
        }
        Log("Slider: opened %s (115200/8N1, DTR/RTS=%s)", port.c_str(), assertDtrRts ? "on" : "off");
        return true;
    }
    bool Send(uint8_t command, std::span<const uint8_t> payload = {})
    {
        const auto wire = slider::Encode(command, payload);
        size_t pos = 0;
        while (pos < wire.size())
        {
            DWORD written = 0;
            auto *remainingBytes = const_cast<uint8_t *>(wire.data() + pos);
            const DWORD remainingSize = static_cast<DWORD>(wire.size() - pos);
            const bool transferred = Transfer(false, remainingBytes, remainingSize, written);
            if (!transferred || written == 0)
            {
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
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent)
        {
            return false;
        }
        BOOL ok = read ? ReadFile(handle_, data, size, &count, &operation)
                       : WriteFile(handle_, data, size, &count, &operation);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            const DWORD waited = WaitForSingleObject(operation.hEvent, read ? 100 : 150);
            if (waited != WAIT_OBJECT_0)
            {
                CancelIoEx(handle_, &operation);
                // 取消后仍须等待内核完成，才能释放OVERLAPPED和buffer。
                GetOverlappedResult(handle_, &operation, &count, TRUE);
                ok = FALSE;
                if (read && waited == WAIT_TIMEOUT)
                {
                    count = 0;
                    ok = TRUE;
                }
            }
            else
            {
                ok = GetOverlappedResult(handle_, &operation, &count, FALSE);
            }
        }
        CloseHandle(operation.hEvent);
        return ok != FALSE;
    }
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// Streaming期间只有此线程写灯；恢复固定间隔发送，不以设备ACK作为下一帧门槛。
void RunLedOutput(std::stop_token stop, SerialChannel &channel,
                  const std::function<bool(std::array<uint8_t, 97> &)> &snapshot,
                  std::atomic_bool &failed)
{
    std::array<uint8_t, 97> sent{};
    bool haveSent = false;
    while (!stop.stop_requested() && !failed.load())
    {
        std::array<uint8_t, 97> payload{};
        if (snapshot(payload) && (!haveSent || payload != sent))
        {
            if (!channel.Send(0x02, payload))
            {
                failed.store(true);
                break;
            }
            sent = payload;
            haveSent = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (haveSent)
    {
        std::array<uint8_t, 97> black{};
        black[0] = 0x3f;
        channel.Send(0x02, black);
    }
}
} // namespace

MmIoArcadeSliderFrontend::~MmIoArcadeSliderFrontend()
{
    Shutdown();
}
void MmIoArcadeSliderFrontend::Initialize(const MmIoArcadeSliderConfig &config)
{
    Shutdown();
    config_ = config;
    if (config_.enabled)
    {
        worker_ = std::jthread(
            [this](std::stop_token stop)
            {
                Run(stop);
            });
    }
}
void MmIoArcadeSliderFrontend::Shutdown()
{
    if (worker_.joinable())
    {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
    std::lock_guard lock(mutex_);
    frame_ = {};
    led_payload_ = {};
    led_available_ = false;
}
bool MmIoArcadeSliderFrontend::Wait(std::stop_token stop, uint32_t milliseconds)
{
    std::unique_lock lock(mutex_);
    wake_.wait_for(lock, stop, std::chrono::milliseconds(milliseconds),
                   []
                   {
                       return false;
                   });
    return !stop.stop_requested();
}
void MmIoArcadeSliderFrontend::Publish(const std::array<uint8_t, mmio::kTouchCellCount> &cells,
                                       bool connected)
{
    std::lock_guard lock(mutex_);
    std::copy(cells.begin(), cells.end(), frame_.touch_cells);
    frame_.connected = connected;
    updated_ = Clock::now();
}
void MmIoArcadeSliderFrontend::SetLeds(const uint8_t (&payload)[97])
{
    if (payload[0] != 0x3f)
    {
        return;
    }
    std::lock_guard lock(mutex_);
    std::copy(std::begin(payload), std::end(payload), led_payload_.begin());
    led_available_ = true;
}
MmIoArcadeSliderFrame MmIoArcadeSliderFrontend::Consume()
{
    std::lock_guard lock(mutex_);
    if (frame_.connected && Clock::now() - updated_ > kReportLease)
    {
        frame_ = {};
    }
    return frame_;
}
void MmIoArcadeSliderFrontend::Run(std::stop_token stop)
{
    while (!stop.stop_requested())
    {
        try
        {
            SerialChannel channel;
            if (channel.Open(config_.port, config_.assert_dtr_rts))
            {
                // 参考原初始化逻辑：RESET → 硬件信息 → byte 报告；首个报告才表示输入就绪。
                // 原逻辑按更新次数重试，此处使用独立单调时钟，不绑定游戏帧率。
                enum class Stage
                {
                    Reset,
                    Info,
                    Enable,
                    Streaming
                };
                Stage stage = Stage::Reset;
                uint8_t pending = 0x10;
                auto deadline = Clock::now() + kHandshakeTimeout;
                auto retry = Clock::now();
                auto lastReport = Clock::now();
                slider::Decoder decoder;
                bool failed = false;
                std::atomic_bool outputFailed = false;
                // 最后声明：异常展开时先停止并join线程，再销毁引用的原子状态。
                std::jthread output;
                while (!stop.stop_requested() && !failed && !outputFailed.load())
                {
                    const auto now = Clock::now();
                    if (stage != Stage::Streaming && now >= deadline)
                    {
                        Log("Slider: handshake timeout (command=%02X)", pending);
                        break;
                    }
                    if (stage == Stage::Streaming && now - lastReport >= kReportLease)
                    {
                        Log("Slider: touch report timeout");
                        break;
                    }
                    if (stage != Stage::Streaming && now >= retry)
                    {
                        if (!channel.Send(pending))
                        {
                            break;
                        }
                        retry = now + kCommandRetry;
                    }
                    std::array<uint8_t, 512> bytes{};
                    DWORD count = 0;
                    if (!channel.Read(bytes, count))
                    {
                        Log("Slider: read failed: Win32=%lu", GetLastError());
                        break;
                    }
                    for (const auto &packet : decoder.Feed(std::span(bytes.data(), count)))
                    {
                        if (packet.command == 0xEE)
                        {
                            Log("Slider: device error (payload length=%zu)", packet.payload.size());
                            failed = true;
                            break;
                        }
                        if (stage == Stage::Reset && packet.command == 0x10 && packet.payload.empty())
                        {
                            stage = Stage::Info;
                            pending = 0xF0;
                        }
                        else if (stage == Stage::Info && packet.command == 0xF0)
                        {
                            if (packet.payload.size() < 18)
                            {
                                continue;
                            }
                            constexpr std::array<uint8_t, 5> model{'1', '5', '2', '7', '5'};
                            if (!std::equal(model.begin(), model.end(), packet.payload.begin()))
                            {
                                Log("Slider: unsupported hardware model");
                                failed = true;
                                break;
                            }
                            Log("Slider: board=837-15275 version=%u", packet.payload[15]);
                            stage = Stage::Enable;
                            pending = 0x03;
                        }
                        else if ((stage == Stage::Enable || stage == Stage::Streaming) &&
                                 packet.command == 0x01 && packet.payload.size() == mmio::kTouchCellCount)
                        {
                            std::array<uint8_t, mmio::kTouchCellCount> cells{};
                            std::copy(packet.payload.begin(), packet.payload.end(), cells.begin());
                            Publish(cells, true);
                            lastReport = Clock::now();
                            if (stage != Stage::Streaming)
                            {
                                output = std::jthread(
                                    [this, &channel, &outputFailed](std::stop_token outputStop)
                                    {
                                        RunLedOutput(
                                            outputStop, channel,
                                            [this](std::array<uint8_t, 97> &payload)
                                            {
                                                std::lock_guard lock(mutex_);
                                                payload = led_payload_;
                                                return led_available_;
                                            },
                                            outputFailed);
                                    });
                            }
                            stage = Stage::Streaming;
                            continue;
                        }
                        else
                        {
                            continue;
                        }
                        deadline = Clock::now() + kHandshakeTimeout;
                        retry = Clock::now();
                    }
                }
                if (output.joinable())
                {
                    output.request_stop();
                    output.join();
                }
                Publish({}, false);
                // 端口仍有效时尽量停止报告；写入失败不阻碍资源释放。
                channel.Send(0x04);
            }
        }
        catch (const std::exception &exception)
        {
            Log("Slider: worker error: %s", exception.what());
        }
        Publish({}, false);
        if (!Wait(stop, 1000))
        {
            break;
        }
    }
}
} // namespace anyslider
