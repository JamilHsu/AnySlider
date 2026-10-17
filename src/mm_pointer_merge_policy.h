#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace anyslider
{
// v103 player input ABI. Never copy keyboard/face/direction bits or identity.
inline constexpr unsigned PointerPlayerStateSize = 6720;
inline constexpr unsigned PointerAnalogOffset = 0x100;
inline constexpr unsigned PointerAnalogSize = 0x30;
inline constexpr uint64_t PointerLeftButtonMask = uint64_t{1} << 17;
inline bool MergeExclusivePointerState(void* player, const void* pointer)
{
    constexpr uint64_t mask = (uint64_t{0xF} << 12) | (uint64_t{0x1F} << 17);
    bool active = false;
    for (const unsigned offset : {0x00u, 0x18u, 0x30u})
    {
        uint64_t source = 0, target = 0;
        std::memcpy(&source, static_cast<const uint8_t*>(pointer) + offset + 16, 8);
        source &= mask; // contacts140..143, mouse145..149; exclude any-key144.
        std::memcpy(&target, static_cast<uint8_t*>(player) + offset + 16, 8);
        target |= source;
        std::memcpy(static_cast<uint8_t*>(player) + offset + 16, &target, 8);
        active |= source != 0;
    }
    // analog8..19: pointer coordinates, deltas and wheel values, not sticks20..23.
    std::memcpy(static_cast<uint8_t*>(player) + PointerAnalogOffset,
        static_cast<const uint8_t*>(pointer) + PointerAnalogOffset, PointerAnalogSize);
    return active;
}
}
