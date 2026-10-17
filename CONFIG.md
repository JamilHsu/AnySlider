# Configuration Reference and Examples

[English](CONFIG.md) | [中文](CONFIG_CN.md) · [Back to README](README.md)

## How to configure

[config_cn.toml](config_cn.toml) is the configuration template; [config.toml](config.toml) is the English version with only the comments translated. The runtime file must be named **`config.toml`** and placed in the AnySlider mod directory. Editing `config_cn.toml` alone is not enough.

## Configuration options

### V1 HORI controller mapping

- `vid_pids = ["054C:05C4"]` — Specify the VID:PIDs of devices to emulate as HORI controllers; leave the list empty to match all devices. 054C:05C4 is a common VID:PID used by the ViGEm virtual controller driver.
- `target_controller_type = 7` — Target controller type for device remapping. 6 = HORI NSW-230 Mega39's, 7 = HORI PS4-161 Future Tone DX.

### IO master switch and shared memory

- `io_enabled = false` — Master IO switch.
- `debug = false` — Debug logging.
- `io_shared_memory = "Local\\MMIO_SHARED_BUFFER"` — External shared-memory input name; normally no change is needed.
- `io_max_input_lease_ms = 500` — Input lease duration in milliseconds; release input when the lease expires.
- `io_exclusive_controller_input = false` — Exclusive game input (blocks the game's native input path so input is supplied only by the mod).
- `io_keep_game_active_unfocused = false` — Keep the game running when it is not in the foreground.

### General input settings

- `io_controller_type = -1` — Controller type emulated by IO. -1 = auto (DualSense by default; automatically switches to the Nintendo layout). 2=DS4, 3=DualSense, 4=Nintendo, 5=Xbox, 6=HORI Mega39s, 7=HORI Future Tone.
- `io_slider_mode = "arcade"` — Slider mode for keyboard left/right input and the left/right gamepad sticks: "arcade" emulates keyboard/gamepad directions as movement across the slider (e.g. right simulates a swipe from left to right). "joystick" maps keyboard/gamepad directions to the game's built-in left/right input.
- `io_arcade_slider_emu_cells_per_second = 32.0` — Slider emulation speed in Arcade mode, in cells per second. Only affects left/right directional input, not direct 32-cell input or mouse_as_slider mode.
- `io_use_ps_circle_confirm = false` — Use Circle to confirm and Cross to go back in game menus.
- `io_swap_ox_sprite = true` — Swap UI button icons only when Circle confirmation is enabled. Disable for UI issues or mod conflicts; restart the game after changing it.
- `io_show_mouse_with_controller = true` — Keep mouse input available when a controller is connected.

### Keyboard/mouse input settings

- `io_keyboard_mouse_frontend = false` — Built-in keyboard/mouse frontend; the game processes it as controller input.
- `io_block_keyboard_input = false` — Block the game's native keyboard/mouse input to prevent duplicate input.
- `io_block_mouse_input = false`
- `io_use_mouse_as_slider = false` — Use mouse input as a slider, for example with a controller that has two knobs.
- `io_mouse_slider_1_axis = "x"` — Mouse axis to bind: usually "x" or "y"; "wheel" is also supported. Note: slider_1 is the left half and slider_2 is the right half of the slider.
- `io_mouse_slider_2_axis = "y"`
- `io_mouse_slider_1_invert = false` — Reverse the direction.
- `io_mouse_slider_2_invert = false`
- `io_mouse_slider_1_sensitivity = 1.0` — Mouse sensitivity.
- `io_mouse_slider_2_sensitivity = 1.0`
- `io_mouse_slider_touch_hold_ms = 20` — Time in milliseconds after movement stops before releasing the simulated touch.
- `io_mouse_slider_device = ""` — Mouse device VID:PID; leave empty to accept all mice.

### Gamepad input settings

- `io_gamepad_frontend = false` — Gamepad frontend; supports DS4, DualSense, Pro Controller and Joy-Con.
- `io_gamepad_device = "auto"` — Automatically detect the controller type.
- `io_gamepad_touchpad_slider = true` — Use the DS4/DualSense touchpad as slider input.
- `io_gamepad_touchpad_invert = false` — Reverse the touchpad left/right direction.
- `io_gamepad_stick_slider_deadzone = 0.5` — Stick deadzone, from 0.1 to 0.95.

### IO3 / AMEX

- `io_io3_backend = false` — Enable the IO3 connection (requires an AMEX board).
- `io_io3_poll_interval_ms = 4` — Wait time between input polls in milliseconds; normally no adjustment is needed.
- `io_io3_timeout_ms = 1000` — AMEX timeout in milliseconds.
- `io_io3_led_use_hardware_position = true` — Button lamp feedback follows physical positions rather than in-game button bindings.

The AMEX driver must be installed.

### Arcade slider

- `io_arcade_slider_backend = false` — Enable the arcade slider connection.
- `io_arcade_slider_port = "COM11"` — Arcade slider serial port; defaults to COM11.
- `io_arcade_slider_dtr_rts = true` — Some USB-to-serial adapters require DTR/RTS assertion to communicate; try disabling this if a motherboard-native serial port has connection issues.

### Arcade serial touchscreen

- `io_arcade_touchscreen_backend = false` — Enable the SEGA838-14772 arcade touchscreen connection (not needed for WinTouch touchscreens).
- `io_arcade_touchscreen_port = "COM1"` — Arcade touchscreen serial port; defaults to COM1.

For 838-14772 serial touchscreens, not ordinary WinTouch devices.
Screen calibration data is read from EEPROM at startup. If reading fails, the default calibration is used.

### Arcade lighting and Test Mode

- `io_arcade_led = true` — Generate lighting effects modeled after the arcade version.
- `io_arcade_led_loading_progress = true` — Use the song-loading progress animation.
- `io_enable_test_mode = false` — Enable the arcade TEST MODE menu.

MegaMix's lighting logic is incomplete; this aims to reproduce the original arcade LED effects as closely as possible.
A loading progress lighting effect is also added.

Note: Button positions in TEST MODE's input test may not match the actual physical positions. Touchscreen settings do not currently support saving calibration.

### Key bindings

Unified action bindings currently support keyboard keys/VK codes (e.g. 0x41) and IO3_Triangle/Square/Cross/Circle/Start/Service/Test.

- `io_key_test = ["F1", "IO3_Test"]`
- `io_key_service = ["F2", "IO3_Service"]`
- `io_key_pause = ["Escape", "IO3_Start"]`
- `io_key_start = ["Enter"]`
- `io_key_dpad_up = ["Up"]`
- `io_key_dpad_down = ["Down"]`
- `io_key_dpad_left = ["Left"]`
- `io_key_dpad_right = ["Right"]`
- `io_key_triangle = ["W", "IO3_Triangle"]`
- `io_key_square = ["A", "IO3_Square"]`
- `io_key_cross = ["S", "IO3_Cross"]`
- `io_key_circle = ["D", "IO3_Circle"]`
- `io_key_l1 = ["Z"]`
- `io_key_r1 = ["X"]`
- `io_key_l2 = []`
- `io_key_r2 = []`
- `io_key_select = []`
- `io_key_l3 = []`
- `io_key_r3 = []`
- `io_key_slider_1_left = ["Q"]` — Keys for emulating slider directions.
- `io_key_slider_1_right = ["E"]`
- `io_key_slider_2_left = ["U"]`
- `io_key_slider_2_right = ["O"]`
- `io_key_slider_01` through `io_key_slider_32`: all `[]`; bind individual slider cells.

Bindings accept arrays of key names or a comma-delimited string. Supported inputs include letters, `F1`–`F24`, `Enter`, arrows, `LeftShift`, `NumPad0`, `Space`, `MouseLeft`, VK strings such as `"0x41"`, and the listed `IO3_*` names.

Multiple configured keys are combined; the action is released only when all keys are released.

Omitting a binding uses the default keys. Setting it to `[]` clears all configured inputs for that action.

### Diagnostics

- `io_input_diagnostics = false`
- `io_input_diagnostics_arcade = -1` — -1=normal native/slider decision, 0=force non-arcade, 1=force arcade.
- `io_led_diagnostics = false`

Input diagnostics require both `debug = true` and `io_input_diagnostics = true`. Keep `io_input_diagnostics_arcade = -1` for normal use; `0` forces non-arcade and `1` forces arcade while diagnostics are active. Ordinary logs append to `anyslider.log` in the process working directory; LED diagnostics use separate files beside the DLL. Avoid high-rate diagnostics during normal gameplay.

## Device examples

### Cabinet: IO3 + slider + touchscreen

Install the AMEX driver on Windows and connect an 837-15280 AMEX board and an 837-14572 IO3.

```toml
# Master IO switch.
io_enabled = true
# Emulate a type 7 HORI controller.
io_controller_type = 7
# Exclusive input.
io_exclusive_controller_input = true
io_slider_mode = "arcade"
# Enable IO3.
io_io3_backend = true
io_io3_poll_interval_ms = 4
io_io3_timeout_ms = 1000
# Button lamp feedback follows physical positions rather than in-game button bindings.
io_io3_led_use_hardware_position = true
# Enable the arcade slider.
io_arcade_slider_backend = true
# Specify the slider serial port.
io_arcade_slider_port = "COM11"
# Some USB-to-serial adapters require DTR/RTS assertion to communicate; try disabling this if a motherboard-native serial port has connection issues.
io_arcade_slider_dtr_rts = true
# Enable the arcade touchscreen.
io_arcade_touchscreen_backend = true
# Touchscreen serial port.
io_arcade_touchscreen_port = "COM1"
# Enable lighting effects.
io_arcade_led = true
io_arcade_led_loading_progress = true
# Enable the test menu.
io_enable_test_mode = true
# Enable cursor-based UI input when using a controller.
io_show_mouse_with_controller = true
# Use Circle to confirm (choose according to your preference).
io_use_ps_circle_confirm = true

# Enable keyboard IO.
io_keyboard_mouse_frontend = true
# Take over keyboard input.
io_block_keyboard_input = true
io_block_mouse_input = false
# Keep the game running in the background.
io_keep_game_active_unfocused = true

# Key bindings.
io_key_test = ["F1", "IO3_Test"]
io_key_service = ["F2", "IO3_Service"]
io_key_pause = ["Escape", "IO3_Start"]
io_key_start = []
io_key_triangle = ["IO3_Triangle"]
io_key_square = ["IO3_Square"]
io_key_cross = ["IO3_Cross"]
io_key_circle = ["IO3_Circle"]

# Enable or disable debugging as needed.
debug = false
io_input_diagnostics = false
io_input_diagnostics_arcade = -1
io_led_diagnostics = false
```

Change COM11/COM1 to match the actual hardware ports.
IO3 uses the AMEX driver, not a serial port.

### GAMO2 FAUCETWO SDVX Controller

For the FAUCETWO controller, use HID keyboard/mouse mode (hold START + FX-L + FX-R + BT-C for three seconds).

```toml
# Enable the core IO features.
io_enabled = true

# Keep the game running when it is not the foreground window.
io_keep_game_active_unfocused = true

# Enable keyboard/mouse IO.
io_keyboard_mouse_frontend = true
# Block the game's own keyboard input to avoid duplicate input; keep mouse/touchscreen active for UI.
io_block_keyboard_input = true
io_block_mouse_input = false

# Use Arcade mode for keyboard LR and gamepad left/right slider input.
io_slider_mode = "arcade"
# Slider emulation speed in Arcade mode; adjust as needed.
io_arcade_slider_emu_cells_per_second = 32.0

# Use mouse input as slider input; this must be enabled.
io_use_mouse_as_slider = true
# Specify the FAUCETWO device ID to filter out other mouse input.
io_mouse_slider_device = "0E8F:1118"


# FAUCETWO key configuration.
io_key_triangle = ["D"]
io_key_square = ["F"]
io_key_cross = ["J"]
io_key_circle = ["K"]
```

### Using a DualShock 4/DualSense Touchpad as a Slider

```toml
# Enable the core IO features.
io_enabled = true

# Gamepad frontend. Supports DS4, DualSense, Pro Controller, and Joy-Con.
io_gamepad_frontend = true

# Use a DS4/DualSense touchpad as slider input.
io_gamepad_touchpad_slider = true
```

### Simulating High-Speed Sliders in Arcade Controller Mode

Enable the keyboard or gamepad frontend.

```toml
io_enabled = true
io_keyboard_mouse_frontend = true

# Slider mode: "arcade" emulates keyboard/gamepad directions as slider movement.
io_slider_mode = "arcade"

# Slider emulation speed in Arcade mode, measured in cells per second.
# Only affects left/right directional input; it does not affect direct 32-cell input or mouse_as_slider mode.
io_arcade_slider_emu_cells_per_second = 32.0

# The keyboard mode requires the following key bindings:
io_key_slider_1_left = ["Q"]
io_key_slider_1_right = ["E"]
io_key_slider_2_left = ["U"]
io_key_slider_2_right = ["O"]
```


#### Manual touchscreen calibration

These parameters are used only when EEPROM cannot be read or its data is invalid. The range is 0 ≤ min < max ≤ 4095.

```toml
io_arcade_touchscreen_invert_x = true
io_arcade_touchscreen_invert_y = true
io_arcade_touchscreen_swap_xy = false
io_arcade_touchscreen_min_x = 0
io_arcade_touchscreen_max_x = 4095
io_arcade_touchscreen_min_y = 0
io_arcade_touchscreen_max_y = 4095
```

## External shared-memory input

Enable `io_enabled = true`. The external producer must use the name in `io_shared_memory` and refresh input within `io_max_input_lease_ms`. Shared memory does not require keyboard, gamepad or arcade hardware frontends. Enable `io_keep_game_active_unfocused` if you need to operate another local window while the game keeps running.

See [shared_memory.h](include/mm_io/shared_memory.h) for the protocol and [led_output.h](include/mm_io/led_output.h) for lighting output.
