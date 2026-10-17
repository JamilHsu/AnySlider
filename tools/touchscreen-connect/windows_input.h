#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>

namespace touchscreen_connect
{
enum class OutputMode { Console, Mouse, Touch };
// Session-local synthetic input. Caller owns arming, message pump and coordinate mapping.
class WindowsInput
{
public:
    bool Initialize(OutputMode mode);
    bool Submit(POINT screenPosition, bool down);
    bool Refresh();
    void Release();
    ~WindowsInput() { Release(); }
    DWORD LastError() const { return error_; }
private:
    bool TouchFrame(POINT position, UINT32 flags);
    OutputMode mode_ = OutputMode::Console;
    bool down_ = false;
    POINT position_{};
    DWORD error_ = ERROR_SUCCESS;
};
}
