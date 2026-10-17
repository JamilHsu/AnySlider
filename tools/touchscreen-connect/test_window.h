#pragma once
#include "windows_input.h"
namespace touchscreen_connect
{
// Non-activating lookup is deliberate: caller must verify foreground before injecting.
HWND CreateInputTestWindow();
void SetPreviewContact(HWND window, float x, float y);
void SetCalibrationTarget(HWND window, float x, float y, unsigned index);
}
