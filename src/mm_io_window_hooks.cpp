#include "pch.h"

#include "mm_io_window_hooks.h"

#include "anyslider_log.h"
#include "mm_io_keyboard.h"
#include "arcade_eeprom.h"
#include "mm_io_touchscreen_test_hooks.h"
#include "touchscreen_mapping.h"

#include <atomic>
#include <vector>

namespace anyslider
{
namespace
{
using GetAsyncKeyStateFunction = SHORT(WINAPI*)(int virtualKey);
using GetForegroundWindowFunction = HWND(WINAPI*)();

MmIoConfig windowHookConfig;
GetAsyncKeyStateFunction originalGetAsyncKeyState = GetAsyncKeyState;
GetForegroundWindowFunction originalGetForegroundWindow = GetForegroundWindow;
using GetCursorPosFunction = BOOL(WINAPI*)(LPPOINT);
GetCursorPosFunction originalGetCursorPos = GetCursorPos;
MmIoTouchscreenFrontend touchscreen;
arcade_eeprom::TouchCalibration touchCalibration;
bool requireFreshTouch = false;
std::atomic_bool touchDown = false;
std::atomic_bool touchPositionValid = false;
std::atomic<LONG> touchScreenX = 0, touchScreenY = 0;
WNDPROC originalWindowProcedure = nullptr;
HWND gameWindow = nullptr;
bool windowApiHooksInstalled = false;
std::atomic_bool windowHooksRunning = false;
std::atomic_bool windowClosing = false;
ULONGLONG nextWindowAttempt = 0;
HWND lastFailedWindow = nullptr;
DWORD lastWindowError = ERROR_SUCCESS;
thread_local bool allowKeyboardFrontendPoll = false;

bool ShouldBypassFocusLoss()
{
    return windowHookConfig.keep_game_active_unfocused;
}

bool ShouldBlockKeyboardInput()
{
    return windowHookConfig.block_keyboard_input;
}

bool ShouldBlockMouseInput()
{
    return windowHookConfig.block_mouse_input;
}

bool IsKeyboardVirtualKey(int virtualKey)
{
    return
        (virtualKey >= VK_BACK && virtualKey <= VK_CAPITAL) ||
        (virtualKey >= VK_KANA && virtualKey <= VK_MODECHANGE) ||
        (virtualKey >= VK_SPACE && virtualKey <= VK_HELP) ||
        (virtualKey >= '0' && virtualKey <= 'Z') ||
        (virtualKey >= VK_LWIN && virtualKey <= VK_SLEEP) ||
        (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_F24) ||
        (virtualKey >= VK_NUMLOCK && virtualKey <= VK_SCROLL) ||
        (virtualKey >= VK_LSHIFT && virtualKey <= VK_RMENU) ||
        (virtualKey >= VK_BROWSER_BACK && virtualKey <= VK_LAUNCH_APP2) ||
        (virtualKey >= VK_OEM_1 && virtualKey <= VK_OEM_8) ||
        virtualKey == VK_OEM_102 ||
        virtualKey == VK_PROCESSKEY ||
        virtualKey == VK_PACKET ||
        (virtualKey >= VK_ATTN && virtualKey <= VK_PLAY);
}

bool IsMouseVirtualKey(int virtualKey)
{
    return virtualKey == VK_LBUTTON || virtualKey == VK_RBUTTON ||
        virtualKey == VK_MBUTTON || virtualKey == VK_XBUTTON1 ||
        virtualKey == VK_XBUTTON2;
}

bool IsKeyboardMouseVirtualKey(int virtualKey)
{
    return IsKeyboardVirtualKey(virtualKey) || IsMouseVirtualKey(virtualKey);
}

SHORT WINAPI GetAsyncKeyStateHook(int virtualKey)
{
    if (windowHookConfig.touchscreen.enabled && virtualKey == VK_LBUTTON && touchDown &&
        originalGetForegroundWindow() == gameWindow)
        return static_cast<SHORT>(0x8000);
    if (((windowHookConfig.block_keyboard_input && IsKeyboardVirtualKey(virtualKey)) ||
         (windowHookConfig.block_mouse_input && IsMouseVirtualKey(virtualKey))) &&
        !allowKeyboardFrontendPoll)
    {
        return 0;
    }
    return originalGetAsyncKeyState(virtualKey);
}

BOOL WINAPI GetCursorPosHook(LPPOINT point)
{
    if (point && touchPositionValid && originalGetForegroundWindow() == gameWindow) { *point = POINT{touchScreenX.load(), touchScreenY.load()}; return TRUE; }
    return originalGetCursorPos(point);
}

HWND WINAPI GetForegroundWindowHook()
{
    if (ShouldBypassFocusLoss() && gameWindow)
    {
        return gameWindow;
    }
    return originalGetForegroundWindow();
}

bool IsKeyboardMessage(UINT message)
{
    switch (message)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        return true;
    default:
        return false;
    }
}

bool IsMouseMessage(UINT message)
{
    switch (message)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK GameWindowProcedureHook(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    // 主窗口销毁之后禁止OnFrame重新寻找/安装窗口hook。
    // 不在WM_CLOSE停止：游戏可能拒绝关闭；WM_DESTROY才表示销毁已开始。
    if (message == WM_DESTROY || message == WM_NCDESTROY)
    {
        windowClosing.store(true, std::memory_order_release);
        touchDown = false;
        touchPositionValid = false;
        return CallWindowProcW(originalWindowProcedure, window, message, wParam, lParam);
    }
    // 清理后若仍在其他模组的WndProc链中，只透传，不再过滤输入。
    if (!windowHooksRunning.load(std::memory_order_acquire))
        return CallWindowProcW(originalWindowProcedure, window, message, wParam, lParam);
    if (message == WM_MOUSEMOVE && !touchDown && !ShouldBlockMouseInput())
        touchPositionValid = false;
    if (ShouldBypassFocusLoss())
    {
        if ((message == WM_ACTIVATEAPP && wParam == FALSE) ||
            (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) ||
            message == WM_KILLFOCUS)
        {
            return 0;
        }
    }

    if (ShouldBlockKeyboardInput() && IsKeyboardMessage(message))
    {
        return 0;
    }

    if (ShouldBlockMouseInput() && IsMouseMessage(message))
    {
        return 0;
    }

    return CallWindowProcW(originalWindowProcedure, window, message, wParam, lParam);
}

BOOL CALLBACK FindGameWindow(HWND candidate, LPARAM parameter)
{
    if (!IsWindowVisible(candidate) || GetWindow(candidate, GW_OWNER))
    {
        return TRUE;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(candidate, &processId);
    if (processId != GetCurrentProcessId())
    {
        return TRUE;
    }

    RECT rect{};
    GetClientRect(candidate, &rect);
    const auto area = static_cast<int64_t>(rect.right - rect.left) * (rect.bottom - rect.top);
    auto* best = reinterpret_cast<std::pair<HWND, int64_t>*>(parameter);
    if (area > best->second)
    {
        *best = { candidate, area };
    }
    return TRUE;
}

HWND FindMainGameWindow()
{
    std::pair<HWND, int64_t> best{ nullptr, 0 };
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&best));
    return best.first;
}

void ReleaseTouchscreen()
{
    const bool wasDown = touchDown; touchDown = false; touchPositionValid = false;
    if (wasDown && originalWindowProcedure && gameWindow && IsWindow(gameWindow))
    {
        POINT point{touchScreenX.load(), touchScreenY.load()}; ScreenToClient(gameWindow, &point);
        CallWindowProcW(originalWindowProcedure, gameWindow, WM_LBUTTONUP, 0,
            MAKELPARAM(point.x, point.y));
    }
}

void PumpTouchscreen()
{
    // Real foreground API, never the focus-loss bypass. No clicks in other applications.
    const bool focused = originalGetForegroundWindow() == gameWindow;
    elo::Event event;
    if (!focused)
    {
        ReleaseTouchscreen();
        requireFreshTouch = true;
        touchscreen.ResetContact();
        return;
    }
    // One queued event per game update preserves short DOWN/UP across frames.
    if (!touchscreen.Consume(event)) return;
    if (requireFreshTouch)
    {
        if (event.initial || !event.down) requireFreshTouch = false;
        else return;
    }
    RECT rect{}; if (!GetClientRect(gameWindow, &rect) || rect.right <= 0 || rect.bottom <= 0)
    { ReleaseTouchscreen(); return; }
    const auto norm = MapTouchToNormalized(event, windowHookConfig.touchscreen, touchCalibration);
    POINT point{
        LONG(norm.x * (rect.right - 1)),
        LONG(norm.y * (rect.bottom - 1))};
    POINT screenPoint = point;
    if (!ClientToScreen(gameWindow, &screenPoint)) { ReleaseTouchscreen(); return; }
    touchScreenX = screenPoint.x; touchScreenY = screenPoint.y;
    const bool wasDown = touchDown; touchDown = event.down; touchPositionValid = true;
    const LPARAM position = MAKELPARAM(point.x, point.y);
    // Call the original procedure: trusted internal input survives mouse blocking.
    CallWindowProcW(originalWindowProcedure, gameWindow, WM_MOUSEMOVE, touchDown ? MK_LBUTTON : 0, position);
    if (wasDown != touchDown)
        CallWindowProcW(originalWindowProcedure, gameWindow, touchDown ? WM_LBUTTONDOWN : WM_LBUTTONUP,
            touchDown ? MK_LBUTTON : 0, position);
}

void DumpRegisteredRawInputDevices()
{
    UINT count = 0;
    if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != 0)
    {
        Log("GetRegisteredRawInputDevices count failed: %lu", GetLastError());
        return;
    }
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count == 0)
    {
        DebugLog("No Raw Input registrations found.");
        return;
    }
    UINT actual = count;
    if (GetRegisteredRawInputDevices(devices.data(), &actual, sizeof(RAWINPUTDEVICE)) == static_cast<UINT>(-1))
    {
        Log("GetRegisteredRawInputDevices failed: %lu", GetLastError());
        return;
    }
    for (const auto& device : devices)
    {
        DebugLog("Raw Input registration: page=%04X usage=%04X flags=%08lX hwnd=%p",
            device.usUsagePage, device.usUsage, device.dwFlags, device.hwndTarget);
    }
}
}

bool InitializeMmIoWindowHooks(const MmIoConfig& config)
{
    if (windowApiHooksInstalled) return true;
    windowHooksRunning = false;
    windowHookConfig = config;
    windowClosing = false;
    nextWindowAttempt = 0;
    lastFailedWindow = nullptr;
    lastWindowError = ERROR_SUCCESS;
    const bool blockAnyInput = config.block_keyboard_input || config.block_mouse_input;
    if (!blockAnyInput && !config.keep_game_active_unfocused && !config.touchscreen.enabled)
    {
        return true;
    }

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        Log("Window hook transaction unavailable: %ld", error);
        return false; // 未取得事务，不得Abort其他模块事务。
    }
    if (error == NO_ERROR)
    {
        error = DetourUpdateThread(GetCurrentThread());
    }
    if (error == NO_ERROR && config.keep_game_active_unfocused)
    {
        error = DetourAttach(
            reinterpret_cast<void**>(&originalGetForegroundWindow),
            reinterpret_cast<void*>(GetForegroundWindowHook));
    }
    if (error == NO_ERROR && (blockAnyInput || config.touchscreen.enabled))
    {
        error = DetourAttach(
            reinterpret_cast<void**>(&originalGetAsyncKeyState),
            reinterpret_cast<void*>(GetAsyncKeyStateHook));
    }
    if (error == NO_ERROR && config.touchscreen.enabled)
        error = DetourAttach(reinterpret_cast<void**>(&originalGetCursorPos), reinterpret_cast<void*>(GetCursorPosHook));
    if (error == NO_ERROR)
    {
        error = DetourTransactionCommit();
    }
    else
    {
        DetourTransactionAbort();
    }
    if (error != NO_ERROR)
    {
        Log("Could not install GetAsyncKeyState hook: %ld", error);
        return false;
    }

    windowApiHooksInstalled = true;
    touchCalibration = {};
    if (config.touchscreen.enabled)
    {
        DebugLog("Touchscreen: backend enabled; port=%s", config.touchscreen.port.c_str());
        if (!arcade_eeprom::ReadTouchCalibration(touchCalibration))
            DebugLog("Touchscreen: calibration source=manual fallback; X=[%d,%d] Y=[%d,%d] invert=(%d,%d) swap_xy=%d",
                config.touchscreen.min_x, config.touchscreen.max_x, config.touchscreen.min_y, config.touchscreen.max_y,
                config.touchscreen.invert_x, config.touchscreen.invert_y, config.touchscreen.swap_xy);
    }
    touchscreen.Initialize(config.touchscreen);
    InstallTouchScreenTestHooks(config.touchscreen.enabled && config.enable_test_mode);
    windowHooksRunning.store(true, std::memory_order_release);
    return true;
}

void UpdateMmIoWindowHooks()
{
    if (!windowHooksRunning.load(std::memory_order_acquire) ||
        windowClosing.load(std::memory_order_acquire)) return;
    const bool blockAnyInput = windowHookConfig.block_keyboard_input || windowHookConfig.block_mouse_input;
    if (!windowHookConfig.keep_game_active_unfocused && !blockAnyInput && !windowHookConfig.touchscreen.enabled)
    {
        return;
    }

    if (originalWindowProcedure && gameWindow && IsWindow(gameWindow))
    {
        if (windowHookConfig.touchscreen.enabled) PumpTouchscreen();
        return;
    }
    const auto now = GetTickCount64();
    if (now < nextWindowAttempt) return;
    nextWindowAttempt = now + 1000; // 找不到/安装失败时每秒重试，不在每帧枚举并刷日志。
    ReleaseTouchscreen();
    touchscreen.ResetContact();
    requireFreshTouch = true;
    originalWindowProcedure = nullptr;
    gameWindow = FindMainGameWindow();
    if (!gameWindow)
    {
        return;
    }

    // 枚举后窗口可能已销毁或HWND被复用，再确认属于当前进程。
    DWORD owner = 0;
    const DWORD thread = GetWindowThreadProcessId(gameWindow, &owner);
    if (!thread || owner != GetCurrentProcessId() || !IsWindow(gameWindow) || windowClosing.load())
    {
        gameWindow = nullptr;
        return;
    }
    SetLastError(ERROR_SUCCESS);
    originalWindowProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        gameWindow,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(GameWindowProcedureHook)));
    if (!originalWindowProcedure)
    {
        const DWORD error = GetLastError(); // 必须在其他Win32调用之前取错误码。
        if (lastFailedWindow != gameWindow || lastWindowError != error)
        {
            Log("Could not install game window hook: %lu (hwnd=%p owner=%lu thread=%lu caller=%lu); retry in 1s",
                error, gameWindow, owner, thread, GetCurrentThreadId());
            lastFailedWindow = gameWindow;
            lastWindowError = error;
        }
        gameWindow = nullptr;
        return;
    }
    lastFailedWindow = nullptr;
    lastWindowError = ERROR_SUCCESS;

}
void ShutdownMmIoWindowHooks()
{
    windowHooksRunning.store(false, std::memory_order_release);
    windowClosing.store(true, std::memory_order_release);
    UninstallTouchScreenTestHooks();
    touchscreen.Shutdown();
    ReleaseTouchscreen();
    if (originalWindowProcedure && gameWindow && IsWindow(gameWindow))
    {
        // 不覆盖其他模组后装的WndProc：它可能仍通过我们的hook转发。
        const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gameWindow, GWLP_WNDPROC));
        if (current == GameWindowProcedureHook)
        {
            SetLastError(ERROR_SUCCESS);
            const auto previous = SetWindowLongPtrW(gameWindow, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(originalWindowProcedure));
            if (!previous && GetLastError() != ERROR_SUCCESS)
                Log("Could not restore game window procedure: %lu; forwarding state retained", GetLastError());
            else
            {
                originalWindowProcedure = nullptr;
                gameWindow = nullptr;
            }
        }
        // 若别的模组在链顶，保留原procedure供残留callback使用；不支持热卸载。
    }
    else
    {
        originalWindowProcedure = nullptr;
        gameWindow = nullptr;
    }
    if (!windowApiHooksInstalled) return;
    const bool blockAnyInput = windowHookConfig.block_keyboard_input || windowHookConfig.block_mouse_input;
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        Log("Window detach transaction unavailable: %ld; hook state retained", error);
        return;
    }
    if (error == NO_ERROR) error = DetourUpdateThread(GetCurrentThread());
    if (error == NO_ERROR && windowHookConfig.keep_game_active_unfocused)
        error = DetourDetach(reinterpret_cast<void**>(&originalGetForegroundWindow), reinterpret_cast<void*>(GetForegroundWindowHook));
    if (error == NO_ERROR && (blockAnyInput || windowHookConfig.touchscreen.enabled))
        error = DetourDetach(reinterpret_cast<void**>(&originalGetAsyncKeyState), reinterpret_cast<void*>(GetAsyncKeyStateHook));
    if (error == NO_ERROR && windowHookConfig.touchscreen.enabled)
        error = DetourDetach(reinterpret_cast<void**>(&originalGetCursorPos), reinterpret_cast<void*>(GetCursorPosHook));
    if (error == NO_ERROR) error = DetourTransactionCommit(); else DetourTransactionAbort();
    if (error != NO_ERROR)
    {
        Log("Could not remove window hooks: %ld; hook state retained", error);
        return;
    }
    windowApiHooksInstalled = false;
    // WndProc可能仍被别的模组引用，不清空其转发配置。
    if (!originalWindowProcedure) windowHookConfig = {};
}
}
