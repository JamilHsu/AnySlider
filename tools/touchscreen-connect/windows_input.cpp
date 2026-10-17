#include "windows_input.h"
#include <algorithm>
#include <thread>

namespace touchscreen_connect
{
bool WindowsInput::Initialize(OutputMode mode)
{
    Release();
    mode_ = OutputMode::Console;
    error_ = ERROR_SUCCESS;
    if (mode == OutputMode::Touch && !InitializeTouchInjection(1, TOUCH_FEEDBACK_DEFAULT))
    {
        error_ = GetLastError();
        return false;
    }
    mode_ = mode;
    return true;
}

bool WindowsInput::TouchFrame(POINT position, UINT32 flags)
{
    POINTER_TOUCH_INFO contact{};
    contact.pointerInfo.pointerType = PT_TOUCH;
    contact.pointerInfo.pointerId = 1;
    contact.pointerInfo.ptPixelLocation = position;
    contact.pointerInfo.pointerFlags = flags;
    for (unsigned retry = 0; retry < 4; ++retry)
    {
        if (InjectTouchInput(1, &contact)) return true;
        error_ = GetLastError();
        if (error_ != ERROR_NOT_READY) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool WindowsInput::Submit(POINT point, bool down)
{
    error_ = ERROR_SUCCESS;
    if (mode_ == OutputMode::Console) return true;
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 1 || height <= 1 || point.x < left || point.y < top ||
        point.x >= left + width || point.y >= top + height)
    {
        error_ = ERROR_INVALID_PARAMETER;
        return false;
    }
    if (mode_ == OutputMode::Mouse)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = LONG((double(point.x) - left) * 65535.0 / (width - 1));
        input.mi.dy = LONG((double(point.y) - top) * 65535.0 / (height - 1));
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        if (down != down_) input.mi.dwFlags |= down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        if (SendInput(1, &input, sizeof(input)) != 1)
        {
            error_ = GetLastError();
            if (!error_) error_ = ERROR_GEN_FAILURE;
            return false;
        }
    }
    else
    {
        if (!down && !down_) return true;
        // UP must use the last successful UPDATE coordinates, including a tap
        // released at its original DOWN position.
        if (!down)
        {
            if (!TouchFrame(point, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT))
                return false;
            position_ = point;
        }
        const UINT32 flags = down ? (POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT |
            (down_ ? POINTER_FLAG_UPDATE : POINTER_FLAG_DOWN)) : POINTER_FLAG_UP;
        if (!TouchFrame(point, flags)) return false;
    }
    position_ = point;
    down_ = down;
    return true;
}

bool WindowsInput::Refresh()
{
    return !down_ || mode_ != OutputMode::Touch ||
        TouchFrame(position_, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
}

void WindowsInput::Release()
{
    if (!down_) return;
    // Mouse release must work even after display geometry changes.
    if (mode_ == OutputMode::Mouse)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        if (SendInput(1, &input, sizeof(input)) == 1) down_ = false;
        else error_ = GetLastError();
    }
    else if (TouchFrame(position_, POINTER_FLAG_CANCELED | POINTER_FLAG_UP)) down_ = false;
}
}
