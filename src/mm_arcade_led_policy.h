#pragma once
#include <cstdint>
#include <cstring>
#include "mm_io_io3_wiring.h"

namespace anyslider {

// Native substate 7 includes preload, play and results.
inline int ClassifyLedPhase(int scene, int next, int transition, bool hasPv, int phase) {
    if ((scene == 5 || scene == 6) && next == 7 && transition == 2) return 1;
    if (scene == 7 && hasPv && phase >= 0 && phase <= 2) return phase + 1;
    return 0;
}

// 只重算四键：保留原生 bit4/5 的闪烁、sensor36/37 触摸抑制及其他高位。
inline uint32_t ApplyButtonLampFeedback(uint32_t nativeMask, uint32_t blink,
                                       uint32_t tick, uint32_t heldLamps) {
    const uint32_t buttons = ((tick / 24u & 1u) ? io3_lamp::Buttons :
        (io3_lamp::Buttons & ~blink)) & ~heldLamps;
    return (nativeMask & ~io3_lamp::Buttons) | (buttons & io3_lamp::Buttons);
}

inline uint32_t RemappedButtonLamps(uint32_t nativeMask, uint32_t blink,
                                  uint32_t tick, uint32_t held) {
    return ApplyButtonLampFeedback(nativeMask, blink, tick, io3_lamp::HeldFromGame(held));
}

inline void LoadingBlueFrame(uint8_t* task) {
    // Solid generator field order +3390,+3388,+3389 -> pending BRG.
    task[3390] = 254;
    task[3388] = 0;
    task[3389] = 0;
    // Pack hook runs after generation: replace this completed base frame too.
    for (unsigned i = 0; i < 96; ++i) {
        const int32_t channel = i % 3 == 0 ? 254 : 0;
        const int32_t zero = 0;
        std::memcpy(task + 1020 + 4 * i, &channel, sizeof(channel));
        std::memcpy(task + 1404 + 4 * i, &zero, sizeof(zero));
    }
}

} // namespace anyslider
