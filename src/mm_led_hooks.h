#pragma once
#include <string_view>
#include <functional>
#include "mm_io/shared_memory.h"
namespace anyslider {
bool InitializeMmLedHooks(std::wstring_view mappingName, std::function<void(const mmio::LedOutput&)> sink = {}, bool diagnostics = false);
void ShutdownMmLedHooks();
}
