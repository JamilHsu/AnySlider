#pragma once

#include "mm_io_touchscreen_protocol.h"
#include "mm_io_touchscreen.h"
#include "arcade_eeprom.h"
#include <algorithm>
#include <utility>

namespace anyslider
{

struct NormalizedPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

// 提取自mm_io_window_hooks.cpp：EEPROM优先、手动配置fallback，保持原语义。
inline NormalizedPoint MapTouchToNormalized(
    uint16_t raw_x,
    uint16_t raw_y,
    const MmIoTouchscreenConfig &config,
    const arcade_eeprom::TouchCalibration &eepromCalib)
{
    if (eepromCalib.valid)
    {
        // EEPROM table already contains AFT inversion and axis calibration. Manual overrides are fallback-only.
        return {
            eepromCalib.MapX(raw_x),
            eepromCalib.MapY(raw_y)
        };
    }

    uint16_t x = raw_x;
    uint16_t y = raw_y;
    if (config.swap_xy)
    {
        std::swap(x, y);
    }
    return {
        elo::Normalize(x, config.min_x, config.max_x, config.invert_x),
        elo::Normalize(y, config.min_y, config.max_y, config.invert_y)
    };
}

inline NormalizedPoint MapTouchToNormalized(
    const elo::Event &event,
    const MmIoTouchscreenConfig &config,
    const arcade_eeprom::TouchCalibration &eepromCalib)
{
    return MapTouchToNormalized(event.x, event.y, config, eepromCalib);
}

} // namespace anyslider
