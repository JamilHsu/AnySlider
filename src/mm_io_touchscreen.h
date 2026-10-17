#pragma once
#include "mm_io_touchscreen_protocol.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace anyslider
{
enum class TouchConnectionState
{
    Disconnected,
    Connecting,
    Ready,
};

struct MmIoTouchscreenConfig
{
    bool enabled = false;
    std::string port = "COM1";
    bool invert_x = true, invert_y = true, swap_xy = false;
    int min_x = 0, max_x = 4095, min_y = 0, max_y = 4095;
};

class MmIoTouchscreenFrontend
{
  public:
    ~MmIoTouchscreenFrontend();
    void Initialize(const MmIoTouchscreenConfig &config);
    void Shutdown();
    bool Consume(elo::Event &event);
    void ResetContact();

    TouchConnectionState GetConnectionState() const noexcept { return state_.load(std::memory_order_acquire); }
    bool IsConnected() const noexcept { return state_.load(std::memory_order_acquire) != TouchConnectionState::Disconnected; }
    bool IsReady() const noexcept { return state_.load(std::memory_order_acquire) == TouchConnectionState::Ready; }

  private:
    class SerialChannel;
    void ReadReports(std::stop_token stop, SerialChannel &channel);
    void ResetContactLocked(std::chrono::steady_clock::time_point now);
    void Run(std::stop_token stop);
    void Publish(elo::Event event);
    void Disconnect();
    bool Wait(std::stop_token stop, unsigned ms);
    MmIoTouchscreenConfig config_;
    std::atomic<TouchConnectionState> state_{TouchConnectionState::Disconnected};
    std::jthread worker_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    struct QueuedEvent
    {
        elo::Event event;
        std::chrono::steady_clock::time_point time;
    };
    std::deque<QueuedEvent> events_;
    bool require_fresh_ = true;
    elo::Event last_{};
    std::chrono::steady_clock::time_point updated_{};
};

} // namespace anyslider
