#include <windows.h>
#include <detours.h>
#include "Dependencies/Signature.h"
#include "mm_io_touchscreen_test_hooks.h"
#include "anyslider_log.h"
#include "hook_validation.h"

namespace anyslider
{
#pragma region TestModeTouchHook

using TouchPanelSettingAvailableFn = bool(__fastcall *)();
static TouchPanelSettingAvailableFn originalTouchPanelSettingAvailable = nullptr;
static bool touchTestHooksInstalled = false;

static bool __fastcall HookedTouchPanelSettingAvailable()
{
    // 点亮原生主测试菜单中的 TOUCH PANEL SETTING
    return true;
}

bool InstallTouchScreenTestHooks(bool touchscreenEnabled)
{
    if (!touchscreenEnabled || touchTestHooksInstalled)
    {
        return true;
    }

    const auto &module = getModuleInfo();
    auto *base = static_cast<uint8_t *>(module.lpBaseOfDll);
    constexpr uint8_t bytes[] = {0x48, 0x83, 0xEC, 0x28, 0x33, 0xC9, 0xE8, 0xE5, 0x40, 0, 0, 0x83, 0xF8, 1};
    if (module.SizeOfImage != 0x23E73000 ||
        !ValidateHookBytes(base, 0x5EFFD0, bytes, sizeof(bytes), "Touchscreen availability"))
    {
        Log("Touchscreen: unsupported Test Mode entry; hook disabled");
        return false;
    }

    // sub_1405EFFD0: 判定 Touch Task 就绪函数
    originalTouchPanelSettingAvailable = reinterpret_cast<TouchPanelSettingAvailableFn>(base + 0x5effd0);

    LONG error = DetourTransactionBegin();
    if (error == NO_ERROR)
    {
        error = DetourUpdateThread(GetCurrentThread());
        if (error == NO_ERROR)
        {
            error = DetourAttach(reinterpret_cast<void **>(&originalTouchPanelSettingAvailable),
                                 reinterpret_cast<void *>(HookedTouchPanelSettingAvailable));
        }
        if (error == NO_ERROR)
        {
            error = DetourTransactionCommit();
        }
        else
        {
            DetourTransactionAbort();
        }
    }

    if (error == NO_ERROR)
    {
        touchTestHooksInstalled = true;
        Log("Touchscreen: Test Mode hooks installed (TOUCH PANEL SETTING enabled)");
        return true;
    }

    Log("Touchscreen: Failed to install Test Mode hooks: %ld", error);
    return false;
}

void UninstallTouchScreenTestHooks()
{
    if (!touchTestHooksInstalled)
    {
        return;
    }

    LONG error = DetourTransactionBegin();
    if (error == NO_ERROR)
    {
        error = DetourUpdateThread(GetCurrentThread());
        if (error == NO_ERROR && originalTouchPanelSettingAvailable)
        {
            error = DetourDetach(reinterpret_cast<void **>(&originalTouchPanelSettingAvailable),
                                 reinterpret_cast<void *>(HookedTouchPanelSettingAvailable));
        }
        if (error == NO_ERROR)
        {
            error = DetourTransactionCommit();
        }
        else
        {
            DetourTransactionAbort();
        }
    }

    if (error != NO_ERROR)
    {
        Log("Touchscreen: Test Mode detach failed: %ld; hook state retained", error);
        return;
    }
    touchTestHooksInstalled = false;
    originalTouchPanelSettingAvailable = nullptr;
}

#pragma endregion

} // namespace anyslider
