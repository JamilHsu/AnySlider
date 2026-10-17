#pragma once

#include "mm_io_keyboard.h"

#include <array>

namespace anyslider
{
struct MmIoRawKeyboardSnapshot
{
    std::array<bool, MmIoBindingKeyCount> down{};
    std::array<bool, MmIoBindingKeyCount> pressed{};
};

bool InitializeMmIoRawInput(
    const MmIoMouseSliderConfig& config,
    bool includeMouse);
void ShutdownMmIoRawInput();
MmIoRawMouseDelta ConsumeMmIoRawMouseDelta();
void ResetMmIoRawMouseInput();
MmIoRawKeyboardSnapshot ConsumeMmIoRawKeyboardSnapshot();
bool InitializeMmIoRawKeyboard();
bool IsMmIoRawKeyboardDown(int virtualKey);
bool ConsumeMmIoRawKeyboardPressed(int virtualKey);
}
