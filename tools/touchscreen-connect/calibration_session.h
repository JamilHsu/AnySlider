#pragma once
#include "windows_input.h"
#include <atomic>
#include <string>
namespace touchscreen_connect {
int RunCalibration(const std::string& port, const std::string& output, const std::atomic_bool& running, RECT display);
}
