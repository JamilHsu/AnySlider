#pragma once

#include "mm_io_keyboard.h"
#include "mm_io_io3.h"
#include "mm_io_arcade_slider.h"
#include "mm_io_touchscreen.h"
#include "mm_io_slider.h"
#include "mm_arcade_led.h"

#include <cstdint>
#include <string>

namespace anyslider
{
struct MmIoGamepadConfig
{
    bool enabled = false;
    std::string device = "auto";
    bool touchpad_slider = true;
    bool touchpad_invert = false;
    float stick_slider_deadzone = 0.5f;
};

struct MmIoConfig
{
    bool enabled = false;
    bool enable_test_mode = false;
    bool use_ps_circle_confirm = false;
    bool swap_ox_sprite = true;
    bool led_diagnostics = false;
    bool input_diagnostics = false;
    bool show_mouse_with_controller = true;
    int controller_type = -1; // -1=auto; native device identity0..10 (11=no device excluded)
    int input_diagnostics_arcade = -1; // -1=normal, 0/1=controlled comparison
    ArcadeLedConfig arcade_led;
    bool keyboard_mouse_frontend = false;
    bool keep_game_active_unfocused = false;
    bool block_keyboard_input = false;
    bool block_mouse_input = false;
    bool exclusive_controller_input = false;
    MmIoSliderMode slider_mode = MmIoSliderMode::Arcade;
    uint64_t max_input_lease_ms = 500;
    float arcade_slider_emu_cells_per_second = 32.0f;
    MmIoInputBindings input_bindings;
    MmIoMouseSliderConfig mouse_slider;
    MmIoGamepadConfig gamepad;
    MmIoIo3Config io3;
    MmIoArcadeSliderConfig arcade_slider;
    MmIoTouchscreenConfig touchscreen;
    std::wstring shared_memory_name = L"Local\\MMIO_SHARED_BUFFER";
};

bool LoadMmIoConfig(MmIoConfig& config);
}
