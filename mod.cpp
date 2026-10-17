#include "pch.h"

#include "src/anyslider_log.h"
#include "src/mm_io_config.h"
#include "src/mm_game_state.h"
#include "src/mm_io_hooks.h"
#include "src/mm_io_window_hooks.h"
#include "src/vid_pid_remap.h"
#include "src/mm_arcade_test_mode.h"

#include <atomic>

namespace {
using ExitProcessFn = void(WINAPI*)(UINT);
ExitProcessFn originalExitProcess = ExitProcess;
std::atomic_bool exiting = false;

void CleanupBeforeExit()
{
    // Direct Win32 process exit cleanup before DLL detach/global destruction.
    // Never join worker threads from DllMain / DLL_PROCESS_DETACH.
    if (!exiting.exchange(true, std::memory_order_acq_rel))
    {
        anyslider::ShutdownMmIoHooks();
        anyslider::ShutdownMmIoWindowHooks();
    }
}

void WINAPI ExitProcessHook(UINT exitCode)
{
    CleanupBeforeExit();
    originalExitProcess(exitCode);
}

void InstallExitCleanup()
{
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        anyslider::Log("Exit cleanup transaction unavailable: %ld; core IO unchanged", error);
        return;
    }
    error = DetourUpdateThread(GetCurrentThread());
    if (error == NO_ERROR)
        error = DetourAttach(reinterpret_cast<void**>(&originalExitProcess),
            reinterpret_cast<void*>(ExitProcessHook));
    if (error == NO_ERROR) error = DetourTransactionCommit();
    else DetourTransactionAbort();
    if (error != NO_ERROR)
        anyslider::Log("Exit cleanup hook unavailable: %ld; core IO unchanged", error);
}
}

struct IDXGISwapChain;

extern "C"
{
// DIVA Mod Loader invokes this immediately before IDXGISwapChain::Present.
__declspec(dllexport) void OnFrame(IDXGISwapChain*)
{
    if (exiting.load(std::memory_order_acquire)) return;
    anyslider::UpdateMmIoWindowHooks();
    anyslider::UpdateMmGameState();
}

// DIVA Mod Loader invokes this from WinMain after game globals are initialized.
__declspec(dllexport) void Init()
{
    static bool initialized = false;
    if (initialized)
    {
        return;
    }
    initialized = true;

    anyslider::Log("Initializing...");
    InstallExitCleanup();
    anyslider::MmIoConfig mmIoConfig;
    if (!anyslider::LoadMmIoConfig(mmIoConfig))
    {
        return;
    }
    if (!anyslider::InitializeVidPidRemap(
            mmIoConfig.enabled && mmIoConfig.exclusive_controller_input))
    {
        anyslider::Log("VID/PID remapping is unavailable for this executable version.");
    }
    if (!mmIoConfig.enabled)
    {
        anyslider::Log("Native IO disabled by io_enabled=false.");
        return;
    }
    // 可选模块只在总IO与Test Mode开关同时开启时安装；失败不阻断核心IO。
    if (mmIoConfig.enable_test_mode)
    {
        anyslider::ArcadeTestModeConfig testModeConfig;
        testModeConfig.enabled = true;
        anyslider::InitializeArcadeTestMode(testModeConfig);
    }
    if (!anyslider::InitializeMmIoWindowHooks(mmIoConfig))
    {
        anyslider::Log("MMIO window hooks are unavailable.");
    }
    if (!anyslider::InitializeMmIoHooks(mmIoConfig))
    {
        anyslider::Log("MMIO backend is unavailable for this executable version.");
    }
}
}
