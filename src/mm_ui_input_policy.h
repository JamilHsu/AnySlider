#pragma once
#include <cstdint>
namespace anyslider {
inline constexpr bool IsPsCircleLayout(uint32_t type) {
    return type == 2 || type == 3 || type == 7 || type == 8 || type == 9;
}
// Query-only policy; raw input, capture, PV slots and prompts stay unchanged.
inline constexpr uint64_t ResolvePsCircleAction(uint64_t action, uint32_t type, bool enabled) {
    if (!enabled || !IsPsCircleLayout(type)) return action;
    return action == 10 ? 9 : action == 9 ? 10 : action;
}
}
