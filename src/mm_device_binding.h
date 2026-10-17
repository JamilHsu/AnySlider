#pragma once
#include <cstdint>
namespace anyslider
{
// Device metadata only: never exchanges raw input or menu semantics.
// PS布局
inline uint32_t MapDualSenseBindingIndex(uint32_t action)
{
    switch (action)
    {
    case 3: return 0; // D-pad Up
    case 4: return 2; // D-pad Down
    case 5: return 1; // D-pad Left
    case 6: return 3; // D-pad Right
    case 7: return 5; // Square / Xbox X
    case 8: return 4; // Triangle / Xbox Y
    case 9: return 7; // Circle / Xbox B
    case 10: return 6; // Cross / Xbox A
    case 11: return 8; // L1 / LB
    case 12: return 9; // R1 / RB
    case 13: return 10; // L2 / LT
    case 14: return 11; // R2 / RT
    case 15: return 12; // Select / Share
    case 16: return 13; // L3 / left-stick click
    case 17: return 14; // R3 / right-stick click
    case 160: return 15; // Pause / Options
    default: return UINT32_MAX;
    }
}

// 任天堂布局
inline uint32_t MapNintendoBindingIndex(uint32_t action)
{
    switch (action)
    {
    case 7: return 4; // Square / Xbox X / Nintendo X
    case 8: return 5; // Triangle / Xbox Y / Nintendo Y
    case 9: return 6; // Circle / Xbox B / Nintendo B
    case 10: return 7; // Cross / Xbox A / Nintendo A
    default: return MapDualSenseBindingIndex(action);
    }
}

inline uint32_t ResolveVirtualDeviceBinding(uint32_t type, uint32_t action, uint32_t device, uint32_t original)
{
    if (device != 0 || original != UINT32_MAX) return original;
    // Keyboard/text identity must not fabricate PS button indices.
    if (type == 10 || type == 11) return original;
    return (type == 4 || type == 6) ? MapNintendoBindingIndex(action) : MapDualSenseBindingIndex(action);
}
}
