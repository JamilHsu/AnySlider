# AnySlider

[English](README.md) | [中文](README_CN.md)

让任何控制器支持 Project DIVA Mega Mix+ 的**街机控制器**模式。基于游戏 **1.03** 版本开发。

![CoverArt](docs/Cover.jpg)

## 安装

1. 安装 DIVA Mod Loader，从 [Releases](https://github.com/samnyan/AnySlider/releases) 下载 AnySlider。
2. 解压到游戏的 `mods` 目录，确保 `AnySlider.dll` 和 `config.toml` 位于同一个 AnySlider 模组文件夹中。
3. 按设备修改 `config.toml`，然后重启游戏。

[config_cn.toml](config_cn.toml) 为中文注释版，[config.toml](config.toml) 为英文注释版。

根据你的需要，重命名成 `config.toml` 开始使用。**程序只读取 `config.toml`，不会直接读取 `config_cn.toml`。**

## 功能介绍

### V1：HORI 手台模拟

将匹配的 DirectInput 控制器识别为官方 HORI 手台，从而使用游戏原生的街机控制器模式。因为游戏内具有白名单，即使手台能够模拟发送街机Slider信号，也必须要有正确的VID和PID才识别。这个Mod功能可以解除限制。

```toml
vid_pids = ["054C:05C4"]
target_controller_type = 7
io_enabled = false
```

- `054C:05C4` 是 ViGEm 虚拟手柄常用的 VID:PID，可替换为自己的设备 ID。
- 目标类型 `6` 为 HORI NSW-230 Mega39's，`7` 为 HORI PS4-161 Future Tone DX。
- 设置 `vid_pids = []` 会匹配全部 DirectInput 摇杆/手柄。

**如果不正确，建议关闭 Steam Input。**

### V2：输入模块替换

通过 Mod 提供输入，不再使用游戏内置输入处理。支持的输入设备和功能：

- 键盘、鼠标（模拟成手柄输入模式）。
  - 通过鼠标模拟Slider，实现旋钮手台当作Slider使用
- DS4、DualSense、Pro Controller 和 Joy-Con 输入
  - DS4/DualSense 支持触摸板滑条。
  - **使用○键确认**
- 模拟滑条滑动，或直接输入 32 个滑条区域。
  - 可以实现加速滑条
- 外部应用通过共享内存提供输入。
- 街机 IO3 输入、街机串口滑条和街机触屏。
  - 同时支持街机灯效、加载动画及可选 Test Mode 菜单。

**随附配置默认关闭 V2 IO 和所有设备前端。** 使用时开启 `io_enabled` 以及需要的前端。例如 DS4/DualSense 触摸板：

```toml
io_enabled = true
io_gamepad_frontend = true
io_gamepad_touchpad_slider = true
```

全部配置项、设备示例、绑定与排错说明见 [CONFIG_CN.md](CONFIG_CN.md)。

## 输入注意事项

- AnySlider 的前端输入会被游戏作为手柄输入处理，键盘绑定也一样。若修改了游戏内手柄绑定，应相应调整 AnySlider 的动作绑定。
- `io_slider_mode` 决定方向输入的处理：`"arcade"` 模拟滑条上的移动，`"joystick"` 使用游戏内置左右输入。
- 不需要开启游戏的“街机控制器”选项，通过Mod的输入不受开关影响。

## 构建

Windows下构建 `AnySlider.vcxproj`。

Linux交叉构建需要LLVM（`clang-cl`、`lld-link`、`llvm-ml`）、Python 3及[xwin](https://github.com/Jake-Shadle/xwin) SDK/CRT目录。准备好仓库依赖（包括x64 Detours库）后运行：

```bash
XWIN_DIR=/path/to/xwin ./cross_build.sh
```

## 参考项目

- [ReDIVA](https://github.com/korenkonder/ReDIVA) — 用于参考和理解游戏的输入处理逻辑。
- [PD-Loader](https://github.com/PDModdingCommunity/PD-Loader) — 输入 Hook 的设计思路参考了 TLAC 的实现。
- [JoyShockLibrary](https://github.com/jibbsmart/JoyShockLibrary) — 集成JoyShockLibrary以提供手柄输入支持。

**Note: 本项目开发过程中使用到了AI的帮助.**
