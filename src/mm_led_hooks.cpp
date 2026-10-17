#include "pch.h"
#include "mm_led_hooks.h"
#include "mm_led_diagnostics.h"
#include "mm_arcade_led.h"
#include "mm_led_setter_bridge.h"
#include "mm_io_shared_memory.h"
#include "mm_io/led_output.h"
#include "anyslider_log.h"
#include "hook_validation.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <cstring>

namespace anyslider {
namespace {
using Pack = uint64_t(__fastcall*)(void*);
using SetOutput = uint64_t(__fastcall*)(void*, int, char);
Pack originalPack = nullptr;
SetOutput originalSetOutput = nullptr;
std::mutex stateMutex;
mmio::LedOutput latest{};
std::atomic_uint32_t latestCabinetBitmap = 0;
std::atomic_bool active = false;
std::atomic_uint64_t packCount = 0, ioCount = 0;
MmIoSharedMemory mapping;
std::jthread publisher; // 先结束线程，再销毁其依赖。
bool installed = false;
LedSetterBridge setterBridge;

uint64_t __fastcall PackHook(void* task) {
    ApplyArcadeLedScene(task);
    const auto result = originalPack(task);
    ObserveLedPack(task);
    if (active.load(std::memory_order_acquire)) {
        void* device = nullptr;
        std::memcpy(&device, static_cast<uint8_t*>(task) + 104, sizeof(device));
        if (device) {
            const auto* payload = static_cast<const uint8_t*>(device) + 6300;
            if (payload[0] == 0x3f) {
                // 灯帧允许丢弃一次采样，游戏线程不能等待后台发布线程。
                std::unique_lock lock(stateMutex, std::try_to_lock);
                if (!lock.owns_lock()) return result;
                std::memcpy(latest.slider, payload, sizeof(latest.slider));
                ++packCount;
            }
        }
    }
    return result;
}
uint64_t __fastcall SetOutputHook(void* state, int id, char on) {
    const auto result = originalSetOutput(state, id, on);
    ObserveLedOutput(state, id, on);
    if (active.load(std::memory_order_acquire) && state && id >= 0 && id < 22) {
        uint32_t bitmap;
        std::memcpy(&bitmap, static_cast<uint8_t*>(state) + 216, sizeof(bitmap));
        // 柜体位图是单个整数：原子发布既不等锁，也不丢最后一次变化。
        latestCabinetBitmap.store(bitmap & 0x3fffff, std::memory_order_relaxed);
        ++ioCount;
    }
    return result;
}

// 唯一跨进程/外设发布入口：Hook 只更新内存，后台线程最多约60Hz发送。
// 只保留最新帧，不积压历史灯帧，避免设备慢时形成延迟队列。
void RunPublisher(std::stop_token stop,
    const std::function<void(const mmio::LedOutput&)>& sink)
{
    while (!stop.stop_requested()) {
        mmio::LedOutput frame;
        { std::lock_guard lock(stateMutex); frame = latest; }
        // 不在游戏Hook中发布跨进程快照，也不等待设备IO。
        frame.io3_bitmap = latestCabinetBitmap.load(std::memory_order_relaxed);
        frame.timestamp_ms = MmIoNowMilliseconds();
        if (packCount.load() || ioCount.load()) {
            mmio::PublishLedOutput(*mapping.Get(), frame);
            if (sink) sink(frame);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    mmio::LedOutput black{}; black.slider[0] = 0x3f;
    black.timestamp_ms = MmIoNowMilliseconds();
    mmio::PublishLedOutput(*mapping.Get(), black);
    if (sink) sink(black);
}
}
bool InitializeMmLedHooks(std::wstring_view name, std::function<void(const mmio::LedOutput&)> sink, bool diagnostics) {
    if (installed) return true;
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const uint8_t packBytes[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x10,0x33,0xff,0xbb,0xfe,0,0,0};
    const uint8_t ioBytes[] = {0x4c,0x63,0xca,0x45,0x84,0xc0,0x74,0x29,0x49,0x83,0xf9,0x16,0x73,0x4e};
    // 未识别的版本不安装固定入口，避免错误修改代码。
    if (nt->OptionalHeader.SizeOfImage != 0x23e73000 ||
        !ValidateHookBytes(base, 0x5a60f0, packBytes, sizeof(packBytes), "mm_led_hooks/packBytes") ||
        !ValidateHookBytes(base, 0x2ab300, ioBytes, sizeof(ioBytes), "mm_led_hooks/ioBytes")) {
        Log("LED: unsupported game build; output capture disabled");
        return false;
    }
    if (!mapping.OpenOrCreate(name, 0)) return false;
    latest = {}; latest.slider[0] = 0x3f;
    packCount = 0; ioCount = 0; latestCabinetBitmap = 0;
    mmio::OutputSequence(*mapping.Get()).store(0, std::memory_order_seq_cst);
    originalPack = reinterpret_cast<Pack>(base + 0x5a60f0);
    originalSetOutput = reinterpret_cast<SetOutput>(base + 0x2ab300);
    if (!setterBridge.Create(reinterpret_cast<void*>(SetOutputHook))) {mapping.Close(); Log("LED: setter ABI bridge allocation failed"); return false;}
    LONG error = DetourTransactionBegin();
    if (error) { mapping.Close(); Log("LED: transaction unavailable: %ld", error); return false; }
    if (!error) error = DetourUpdateThread(GetCurrentThread());
    if (!error) error = DetourAttach(reinterpret_cast<void**>(&originalPack), reinterpret_cast<void*>(PackHook));
    if (!error) error = DetourAttach(reinterpret_cast<void**>(&originalSetOutput), setterBridge.Entry());
    if (!error) error = DetourTransactionCommit(); else DetourTransactionAbort();
    if (error) { mapping.Close(); Log("LED: hook installation failed: %ld", error); return false; }
    installed = true;
    active.store(true, std::memory_order_release);
    try {
        publisher = std::jthread([sink](std::stop_token stop) {
            RunPublisher(stop, sink);
        });
    } catch (...) { ShutdownMmLedHooks(); return false; }
    StartLedDiagnostics(diagnostics);
    Log("LED: native pending-payload and cabinet output capture installed");
    return true;
}
void ShutdownMmLedHooks() {
    StopLedDiagnostics();
    ShutdownArcadeLed();
    active.store(false, std::memory_order_release);
    if (!installed) return;
    LONG error = DetourTransactionBegin();
    if (error) { Log("LED: detach transaction unavailable: %ld", error); return; }
    if (!error) error = DetourUpdateThread(GetCurrentThread());
    if (!error) error = DetourDetach(reinterpret_cast<void**>(&originalPack), reinterpret_cast<void*>(PackHook));
    if (!error) error = DetourDetach(reinterpret_cast<void**>(&originalSetOutput), setterBridge.Entry());
    if (!error) error = DetourTransactionCommit(); else DetourTransactionAbort();
    if (!error) installed = false;
    else Log("LED: hook detach failed: %ld", error);
    if (publisher.joinable()) { publisher.request_stop(); publisher.join(); }
    mapping.Close();
    Log("LED: capture calls slider=%llu cabinet=%llu", static_cast<unsigned long long>(packCount.load()), static_cast<unsigned long long>(ioCount.load()));
}
}
