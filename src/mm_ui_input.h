#pragma once
#include <cstdint>

namespace anyslider::ui_input {
using Predicate = uint8_t(__fastcall*)(void*, uint64_t);
using Observer = void(*)(void* state, uint64_t action, Predicate predicate,
    uint32_t result, uintptr_t callerRva);
struct Options {
    bool ps_circle_confirm = false;
    Observer observer = nullptr;
};
// Startup/shutdown only; not safe for arbitrary concurrent hot toggling.
bool Install(const Options& options);
bool Uninstall();
bool IsInstalled();
}
