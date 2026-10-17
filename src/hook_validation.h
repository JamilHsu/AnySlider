#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include "anyslider_log.h"

namespace anyslider {
// Exact validation is the default. A callable detour is not proof of matching
// semantics or ABI; chaining must be explicitly reviewed per entry point.
inline bool ValidateHookBytes(const uint8_t* base, uint32_t rva,
    const void* expected, size_t length, const char* name)
{
    std::array<uint8_t, 64> actual{};
    SIZE_T read = 0;
    if (!base || !expected || !length || length > actual.size() ||
        !ReadProcessMemory(GetCurrentProcess(), base + rva, actual.data(), length, &read) || read != length) {
        Log("Hook validation %s +%X: cannot read %zu bytes; no overwrite", name, rva, length);
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(expected);
    for (size_t i = 0; i < length; ++i) {
        if (actual[i] != bytes[i]) {
            Log("Hook validation %s +%X: mismatch at byte %zu expected=%02X actual=%02X entry=%02X %02X %02X; unsupported build or existing patch; no overwrite",
                name, rva, i, bytes[i], actual[i], actual[0], actual[1], actual[2]);
            return false;
        }
    }
    return true;
}
}
