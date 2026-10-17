# AnySlider

[English](README.md) | [中文](README_CN.md)

Allows any controller to use Project DIVA Mega Mix+'s **Arcade Controller** mode. Developed for game version **1.03**.

![CoverArt](docs/Cover.jpg)

## Installation

1. Install DIVA Mod Loader and download AnySlider from [Releases](https://github.com/samnyan/AnySlider/releases).
2. Extract the mod into the game's `mods` directory, keeping `AnySlider.dll` and `config.toml` together in the AnySlider mod folder.
3. Edit `config.toml` for your device, then restart the game.

## Features

### V1: HORI controller emulation

Remaps matching DirectInput controllers to an official HORI controller type, allowing them to use the game's native Arcade Controller mode.

```toml
vid_pids = ["054C:05C4"]
target_controller_type = 7
io_enabled = false
```

- `054C:05C4` is commonly used by ViGEm virtual controllers. Replace it with your device's VID:PID.
- Target `6` is HORI NSW-230 Mega39's; `7` is HORI PS4-161 Future Tone DX.
- An explicit `vid_pids = []` matches all DirectInput joysticks/gamepads, not keyboards or mice. A missing or invalid list does not enable this behavior.

**Try disabling Steam Input if not working.**

### V2: Replacement Input System

Provides input through hooks instead of relying only on the game's native device handling:

- Keyboard bindings and mouse (Emulate as controller).
  - Using dual-knob controller as slider input
- DS4, DualSense, Pro Controller and Joy-Con input.
  - DS4/DualSense touchpad sliders.
  - **Use ○ button as confrim**
- Left/right directional slider emulation or direct 32-cell input.
- External input through shared memory.
- Arcade IO3 buttons, Arcade serial sliders and Arcade touchscreens.
  - Arcade-style lighting, loading animation and an optional Test Mode menu.

**V2 IO and all device frontends are disabled in the shipped configuration.** Enable `io_enabled` and the frontend you need. For example, to use a DS4/DualSense touchpad:

```toml
io_enabled = true
io_gamepad_frontend = true
io_gamepad_touchpad_slider = true
```

See [CONFIG.md](CONFIG.md) for all shipped options, device examples, bindings and troubleshooting.

## Input behavior

- AnySlider presents frontend input as controller input, including keyboard bindings. If you change the in-game controller bindings, adjust your AnySlider action bindings accordingly.
- Directional input uses `io_slider_mode`: `"arcade"` simulates movement across the slider; `"joystick"` uses the game's built-in left/right input. Emulation speed does not affect direct 32-cell or mouse slider input.
- The game's Arcade Controller setting does not Required. It will work anyways.

## Building

Build the `AnySlider.vcxproj` project on Windows.

For a Linux cross-build, install LLVM (`clang-cl`, `lld-link`, `llvm-ml`), Python 3 and an [xwin](https://github.com/Jake-Shadle/xwin) SDK/CRT tree. Initialize the repository dependencies, including the x64 Detours library, then run:

```bash
XWIN_DIR=/path/to/xwin ./cross_build.sh
```

## References

- [ReDIVA](https://github.com/korenkonder/ReDIVA) — Used as a reference for understanding input handling.
- [PD-Loader](https://github.com/PDModdingCommunity/PD-Loader) — TLAC served as a reference for the input hooking approach.
- [JoyShockLibrary](https://github.com/jibbsmart/JoyShockLibrary) — Integrated to provide gamepad input support.

**Note: This project was developed with the assistance of AI.**
