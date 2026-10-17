#include <windows.h>
#include <setupapi.h>
#include "mm_io_io3.h"
#include "mm_io_jvs.h"
#include "anyslider_log.h"
#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <cstring>
#include <vector>

namespace anyslider
{
namespace
{
constexpr GUID JvsGuid{0xDB6BBB45, 0xCC96, 0x4288, {0xAA, 0x00, 0x6C, 0x00, 0xD7, 0x67, 0xBD, 0xBF}};
constexpr DWORD kHelloIoctl = 0x80006004;
constexpr DWORD kTransactIoctl = 0x8000E008;
constexpr DWORD kSenseIoctl = 0x8000600C;
constexpr uint32_t kReconnectWaitMs = 1000;
constexpr auto kInputLease = std::chrono::milliseconds(500);

// 即使分配或解析抛异常，也必须释放系统资源。
class ScopedHandle
{
  public:
    explicit ScopedHandle(HANDLE handle) : handle_(handle)
    {
    }
    ~ScopedHandle()
    {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
    }
    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;
    HANDLE Get() const
    {
        return handle_;
    }

  private:
    HANDLE handle_;
};
class ScopedDeviceInfo
{
  public:
    explicit ScopedDeviceInfo(HDEVINFO handle) : handle_(handle)
    {
    }
    ~ScopedDeviceInfo()
    {
        SetupDiDestroyDeviceInfoList(handle_);
    }
    ScopedDeviceInfo(const ScopedDeviceInfo &) = delete;
    ScopedDeviceInfo &operator=(const ScopedDeviceInfo &) = delete;

  private:
    HDEVINFO handle_;
};
class AmexTransport final : public jvs::Transport
{
  public:
    AmexTransport(uint32_t timeout, std::stop_token stop) : timeout_(timeout), stop_(stop)
    {
    }
    ~AmexTransport()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
    }
    AmexTransport(const AmexTransport &) = delete;
    AmexTransport &operator=(const AmexTransport &) = delete;
    bool Open()
    {
        HDEVINFO devices =
            SetupDiGetClassDevsW(&JvsGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devices == INVALID_HANDLE_VALUE)
        {
            Log("IO3: SetupAPI failed: %lu", GetLastError());
            return false;
        }
        ScopedDeviceInfo deviceInfo(devices);
        SP_DEVICE_INTERFACE_DATA info{};
        info.cbSize = sizeof(info);
        DWORD required = 0;
        bool opened = false;
        // 参考原初始化逻辑选择首个 JVS 接口；必须枚举设备路径，不能当作串口打开。
        if (SetupDiEnumDeviceInterfaces(devices, nullptr, &JvsGuid, 0, &info))
        {
            SetupDiGetDeviceInterfaceDetailW(devices, &info, nullptr, 0, &required, nullptr);
            if (required >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            {
                std::vector<uint8_t> buffer(required);
                auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
                detail->cbSize = sizeof(*detail);
                if (SetupDiGetDeviceInterfaceDetailW(devices, &info, detail, required, nullptr, nullptr))
                {
                    handle_ = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OVERLAPPED, nullptr);
                    opened = handle_ != INVALID_HANDLE_VALUE;
                    if (opened)
                    {
                        Log("IO3: opened AMEX JVS interface %ls", detail->DevicePath);
                    }
                    else
                    {
                        Log("IO3: CreateFile failed: Win32=%lu", GetLastError());
                    }
                }
            }
        }
        if (!opened)
        {
            Log("IO3: no accessible AMEX JVS interface (driver required)");
            return false;
        }
        std::array<uint8_t, 2> hello{};
        DWORD length = 0;
        if (Ioctl(kHelloIoctl, {}, hello, length) != jvs::Result::Ok)
        {
            return false;
        }
        if (length != hello.size())
        {
            Log("IO3: invalid HELLO completion length: %lu", length);
            return false;
        }
        Log("IO3: HELLO raw=%02X %02X", hello[0], hello[1]);
        return true;
    }
    jvs::Result Transact(std::span<const uint8_t> tx, jvs::Bytes &rx) override
    {
        rx.clear();
        if (tx.empty() || tx.size() > 255)
        {
            return jvs::Result::Error;
        }
        std::array<uint8_t, 255> buffer{};
        DWORD length = 0;
        auto result = Ioctl(kTransactIoctl, tx, buffer, length);
        if (result == jvs::Result::Ok && length <= buffer.size())
        {
            rx.assign(buffer.begin(), buffer.begin() + length);
        }
        else if (result == jvs::Result::Ok)
        {
            return jvs::Result::Error;
        }
        return result;
    }
    bool Sense(uint8_t &value) override
    {
        DWORD length = 0;
        std::span<uint8_t> output(&value, 1);
        const bool ok = Ioctl(kSenseIoctl, {}, output, length) == jvs::Result::Ok && length == 1;
        if (ok)
        {
            DebugLog("IO3: SENSE=%u", value);
        }
        return ok;
    }

  private:
    jvs::Result Ioctl(DWORD code, std::span<const uint8_t> input, std::span<uint8_t> output, DWORD &length)
    {
        if (stop_.stop_requested())
        {
            return jvs::Result::Error;
        }
        OVERLAPPED overlapped{};
        ScopedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.Get())
        {
            Log("IO3: CreateEvent failed: Win32=%lu", GetLastError());
            return jvs::Result::Error;
        }
        overlapped.hEvent = event.Get();
        BOOL ok = DeviceIoControl(handle_, code, const_cast<uint8_t *>(input.data()),
                                  static_cast<DWORD>(input.size()), output.data(),
                                  static_cast<DWORD>(output.size()), &length, &overlapped);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        bool cancelled = false;
        if (!ok && error == ERROR_IO_PENDING)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_);
            while (true)
            {
                DWORD wait = WaitForSingleObject(overlapped.hEvent, 10);
                if (wait == WAIT_OBJECT_0)
                {
                    ok = GetOverlappedResult(handle_, &overlapped, &length, FALSE);
                    error = ok ? ERROR_SUCCESS : GetLastError();
                    break;
                }
                if (wait == WAIT_FAILED || stop_.stop_requested() ||
                    std::chrono::steady_clock::now() >= deadline)
                {
                    Log("IO3: IOCTL 0x%08lX cancelled (%s)", code,
                        stop_.stop_requested() ? "shutdown"
                        : wait == WAIT_FAILED  ? "wait failed"
                                               : "timeout");
                    CancelIoEx(handle_, &overlapped);
                    // 取消请求不等于完成：内核结束访问前，缓冲区与 OVERLAPPED 必须保持有效。
                    GetOverlappedResult(handle_, &overlapped, &length, TRUE);
                    error = ERROR_OPERATION_ABORTED;
                    cancelled = true;
                    break;
                }
            }
        }
        if (cancelled)
        {
            return jvs::Result::Error;
        }
        if (ok)
        {
            return jvs::Result::Ok;
        }
        if (error == ERROR_NO_DATA_DETECTED)
        {
            return jvs::Result::NoData;
        }
        Log("IO3: IOCTL 0x%08lX failed: Win32=%lu", code, error);
        return jvs::Result::Error;
    }
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    uint32_t timeout_;
    std::stop_token stop_;
};
} // namespace

MmIoIo3Frontend::~MmIoIo3Frontend()
{
    Shutdown();
}
void MmIoIo3Frontend::Initialize(const MmIoIo3Config &config)
{
    Shutdown();
    config_ = config;
    if (config.enabled)
    {
        worker_ = std::jthread(
            [this](std::stop_token stop)
            {
                Run(stop);
            });
    }
}
void MmIoIo3Frontend::Shutdown()
{
    if (worker_.joinable())
    {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
    std::lock_guard lock(mutex_);
    frame_ = {};
    hardware_lamp_snapshot_.store(0, std::memory_order_relaxed);
    led_bitmap_ = 0;
    led_available_ = false;
}
bool MmIoIo3Frontend::Wait(std::stop_token stop, uint32_t milliseconds)
{
    std::unique_lock lock(mutex_);
    wake_.wait_for(lock, stop, std::chrono::milliseconds(milliseconds),
                   []
                   {
                       return false;
                   });
    return !stop.stop_requested();
}
void MmIoIo3Frontend::Publish(uint16_t bindingSwitches, bool connected, uint16_t physicalSwitches)
{
    std::lock_guard lock(mutex_);
    frame_.pressed_switches |= bindingSwitches & ~frame_.binding_switches;
    frame_.binding_switches = connected ? bindingSwitches : 0;
    frame_.physical_switches = connected ? physicalSwitches : 0;
    frame_.connected = connected;
    updated_ = std::chrono::steady_clock::now();
    const uint64_t timestamp = GetTickCount64();
    hardware_lamp_snapshot_.store(connected ? (timestamp << 4) | io3_lamp::HeldFromHardware(physicalSwitches)
                                            : 0,
                                  std::memory_order_relaxed);
}
bool MmIoIo3Frontend::ReadHardwareLampHeld(uint32_t &held)
{
    // 不消费边沿、不等待采集线程；一次读取获得一致的状态与时间。
    const uint64_t snapshot = hardware_lamp_snapshot_.load(std::memory_order_relaxed);
    held = 0;
    if (!snapshot || GetTickCount64() - (snapshot >> 4) > 500)
    {
        return false;
    }
    held = static_cast<uint32_t>(snapshot & 0xF);
    return true;
}

void MmIoIo3Frontend::SetLeds(uint32_t bitmap)
{
    std::lock_guard lock(mutex_);
    led_bitmap_ = bitmap;
    led_available_ = true;
}
MmIoIo3Frame MmIoIo3Frontend::Consume()
{
    std::lock_guard lock(mutex_);
    if (frame_.connected && std::chrono::steady_clock::now() - updated_ > kInputLease)
    {
        frame_.binding_switches = 0;
        frame_.pressed_switches = 0;
        frame_.physical_switches = 0;
        frame_.connected = false;
    }
    const auto result = frame_;
    frame_.pressed_switches = 0;
    return result;
}
// 连接成功后的工作循环：先读取输入，再按需写灯，最后可中断地等待。
// transport/session 由同一个线程独占；mutex_ 只保护小快照，不包住驱动IO。
// 保持单一请求/回复owner：不要为灯写另开并发JVS事务，否则可能消费错回复。
void MmIoIo3Frontend::RunSession(std::stop_token stop, jvs::Transport &transport, jvs::Session &session)
{
    for (const auto &node : session.Nodes())
    {
        Log("IO3: node=%u id=%s players=%u switches=%u versions=%02X/%02X/%02X", node.address,
            node.id.c_str(), node.players, node.switches, node.command_version, node.jvs_version,
            node.communication_version);
    }
    bool ledSent = false;
    uint32_t sentBitmap = 0;
    auto nextLed = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!stop.stop_requested() && session.Poll(transport))
    {
        Publish(session.BindingSwitches(), true, session.PhysicalSwitches());
        uint32_t bitmap;
        bool available;
        {
            std::lock_guard lock(mutex_);
            bitmap = led_bitmap_;
            available = led_available_;
        }
        const auto now = std::chrono::steady_clock::now();
        // 单一请求/回复owner；每次先poll，再消费最新灯帧，不累计历史。
        if (available && now >= nextLed && (!ledSent || bitmap != sentBitmap))
        {
            if (!session.WriteLeds(transport, bitmap))
            {
                break;
            }
            sentBitmap = bitmap;
            ledSent = true;
            // 从完成时间开始限速；慢设备留出至少3倍写入耗时的输入窗口。
            // 不用发送前的now：否则慢写返回时限额早已过期，容易连续写灯。
            const auto completed = std::chrono::steady_clock::now();
            nextLed =
                completed + (std::max)(std::chrono::steady_clock::duration(std::chrono::milliseconds(16)),
                                       (completed - now) * 3);
            // 写灯期间的输入可能已变化，完成后立即补采样，不再先睡poll_interval。
            if (stop.stop_requested() || !session.Poll(transport))
            {
                break;
            }
            Publish(session.BindingSwitches(), true, session.PhysicalSwitches());
        }
        if (!Wait(stop, config_.poll_interval_ms))
        {
            break;
        }
    }
}

// 外层只管理连接、异常与重连；设备句柄随每次会话RAII释放。
void MmIoIo3Frontend::Run(std::stop_token stop)
{
    while (!stop.stop_requested())
    {

        try
        {
            AmexTransport transport(config_.timeout_ms, stop);
            jvs::Session session;
            if (transport.Open() && session.Initialize(transport,
                                                       [this, stop](uint32_t ms)
                                                       {
                                                           return Wait(stop, ms);
                                                       }))
            {
                RunSession(stop, transport, session);
            }
            if (!stop.stop_requested() && !session.Error().empty())
            {
                Log("IO3: %s", session.Error().c_str());
            }
        }
        catch (const std::exception &exception)
        {
            Log("IO3: worker error: %s", exception.what());
        }
        Publish(0, false);
        if (!Wait(stop, kReconnectWaitMs))
        {
            break;
        }
    }
}
} // namespace anyslider
