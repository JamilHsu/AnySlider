#pragma once
#include "mm_io/shared_memory.h"
#include "mm_io_io3_wiring.h"
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace anyslider
{
namespace jvs { class Transport; class Session; }
// 开关/灯位/输出接线集中定义在 mm_io_io3_wiring.h。

struct MmIoIo3Config
{
    bool enabled = false;
    bool led_use_hardware_position = true; // 灭灯跟随原始 P1 开关，不经过任何按键绑定
    uint32_t poll_interval_ms = 4;
    uint32_t timeout_ms = 1000;

};
struct MmIoIo3Frame
{
    uint16_t binding_switches = 0; // P1 + 合成 TEST；统一绑定输入
    uint16_t pressed_switches = 0; // 累积按下边沿
    uint16_t physical_switches = 0; // 原始 P1 high/low；不含合成 TEST 位
    bool connected = false;
};
class MmIoIo3Frontend
{
public:
    ~MmIoIo3Frontend();
    void Initialize(const MmIoIo3Config& config);
    void Shutdown();
    MmIoIo3Frame Consume();
    void SetLeds(uint32_t bitmap);
    bool ReadHardwareLampHeld(uint32_t& held); // 只读快照，不消费输入边沿；断连/过期返回 false
private:
    void Run(std::stop_token stop);
    void RunSession(std::stop_token stop, jvs::Transport& transport, jvs::Session& session);
    bool Wait(std::stop_token stop, uint32_t milliseconds);
    void Publish(uint16_t bindingSwitches, bool connected, uint16_t physicalSwitches = 0);
    MmIoIo3Config config_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    MmIoIo3Frame frame_;
    // 单个原子快照：高位为毫秒时间，低4位为物理灯按下掩码。
    // 0表示断开；Lamp hook 无需取得输入边沿的锁。
    std::atomic_uint64_t hardware_lamp_snapshot_ = 0;
    uint32_t led_bitmap_ = 0;
    bool led_available_ = false;
    std::chrono::steady_clock::time_point updated_{};
    // 最后声明，确保异常销毁时线程先结束，再销毁其依赖的同步对象。
    std::jthread worker_;
};
}
