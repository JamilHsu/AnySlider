# 配置说明与范例

[English](CONFIG.md) | [中文](CONFIG_CN.md) · [返回 README](README_CN.md)

## 使用方法

以 [config_cn.toml](config_cn.toml) 为配置模板；[config.toml](config.toml) 是仅翻译注释的英文版。运行时文件必须命名为 **`config.toml`**，放在 AnySlider 模组目录中，不能只修改 `config_cn.toml`。

## 配置项

### V1 版HORI手台映射

- `vid_pids = ["054C:05C4"]`：指定模拟为HORI手台的设备VID PID，留空则把所有设备当作HORI手台。 054C:05C4 是常见虚拟手柄驱动 ViGEm 的VID PID.
- `target_controller_type = 7`：映射设备目标 6 = HORI NSW-230 Mega39's, 7 = HORI PS4-161 Future Tone DX.

### IO 总开关与共享内存

- `io_enabled = false`：IO总开关
- `debug = false`：调试开关
- `io_shared_memory = "Local\\MMIO_SHARED_BUFFER"`：外部共享内存输入名，一般不用改
- `io_max_input_lease_ms = 500`：共享内存输入有效期，超时释放
- `io_exclusive_controller_input = false`：独占游戏输入（开启后屏蔽游戏原本的输入逻辑，只由mod提供输入）
- `io_keep_game_active_unfocused = false`：让游戏不在前台时保持运行

### 通用输入设置

- `io_controller_type = -1`：IO模拟的设备类型 -1=自动 (默认DualSense，自动切换任天堂布局). 2=DS4, 3=DualSense, 4=Nintendo, 5=Xbox, 6=HORI Mega39s, 7=HORI Future Tone.
- `io_slider_mode = "arcade"`：键盘LR、手柄左右摇杆的滑条模式： "arcade" 将键盘/手柄方向模拟在滑条上滑动（比如向右代表模拟从触摸区域左往右滑动） "joystick" 将键盘/手柄方向映射到游戏内置的左右输入。
- `io_arcade_slider_emu_cells_per_second = 32.0`：Arcade 模式下滑条模拟速度，单位为 cell/秒。 只对左右输入有效，对直接 32-cell 输入和 mouse_as_slider 模式无效
- `io_use_ps_circle_confirm = false`：游戏菜单使用○确认/×返回
- `io_swap_ox_sprite = true`：调换UI按键图例，需要开启O键确认才有效。（如果出现UI问题或者Mods冲突可以关闭）修改后需重启游戏。
- `io_show_mouse_with_controller = true`：在手柄接入时保持鼠标输入

### 键鼠输入设置

- `io_keyboard_mouse_frontend = false`：内置键盘/鼠标前端，游戏会将其作为手柄输入处理
- `io_block_keyboard_input = false`：屏蔽游戏自身的键盘/鼠标输入，避免产生重复输入
- `io_block_mouse_input = false`
- `io_use_mouse_as_slider = false`：使用鼠标输入来作为滑条使用，比如有两个旋钮的手台
- `io_mouse_slider_1_axis = "x"`：绑定的鼠标移动轴，一般分配到"x"或"y"，也可以是"wheel"滚轮 注：slider_1是左半边，slider_2是右半边的slider区域
- `io_mouse_slider_2_axis = "y"`
- `io_mouse_slider_1_invert = false`：是否开启方向反转
- `io_mouse_slider_2_invert = false`
- `io_mouse_slider_1_sensitivity = 1.0`：鼠标灵敏度
- `io_mouse_slider_2_sensitivity = 1.0`
- `io_mouse_slider_touch_hold_ms = 20`：停止移动多久后当作松开触摸
- `io_mouse_slider_device = ""`：指定鼠标设备的VID PID，留空就是所有鼠标

### 手柄输入设置

- `io_gamepad_frontend = false`：手柄前端，支持 DS4、DualSense、Pro Controller 和 Joy-Con
- `io_gamepad_device = "auto"`：自动识别手柄类型
- `io_gamepad_touchpad_slider = true`：使用DS4/DualSense的触摸板作为滑条输入
- `io_gamepad_touchpad_invert = false`：触控板左右反向
- `io_gamepad_stick_slider_deadzone = 0.5`：摇杆死区，范围为 0.1 到 0.95。

### IO3 / AMEX

- `io_io3_backend = false`：开启IO3连接（需要AMEX板）
- `io_io3_poll_interval_ms = 4`：拉取输入之间的等待时间，一般不用改
- `io_io3_timeout_ms = 1000`：AMEX超时时间
- `io_io3_led_use_hardware_position = true`：按键灯效跟随真实顺序，而不跟随游戏内按键绑定

需要安装AMEX驱动

### 街机滑条

- `io_arcade_slider_backend = false`：开启街机滑条连接
- `io_arcade_slider_port = "COM11"`：街机滑条端口，默认COM11
- `io_arcade_slider_dtr_rts = true`：部分USB转串口需要发送DTR-RTS才能开始通讯，如连接主板原生串口出现问题可以尝试关闭

### 街机串口触屏

- `io_arcade_touchscreen_backend = false`：开启街机触屏 SEGA838-14772 连接（WinTouch触屏不需要）
- `io_arcade_touchscreen_port = "COM1"`：街机触屏端口，默认COM1

用于 838-14772 串口触屏，不适用于普通 WinTouch
启动时会读取 EEPROM 中的屏幕校准数据，如果失败的话将会用默认校准。

### 街机灯效与 Test Mode

- `io_arcade_led = true`：模仿街机版本生成街机灯效
- `io_arcade_led_loading_progress = true`：使用歌曲加载进度动画
- `io_enable_test_mode = false`：开启街机TEST MODE菜单

MegaMix的灯效逻辑不完整，这里是根据尽量复原原版的LED灯效。
额外增加加载进度条灯效。

注：TEST MODE中的输入测试按键位置不一定和实际的一致。触屏设置暂时并不支持保存校准。

### 按键绑定

统一动作绑定，目前支持：键盘键/VK（如0x41）及IO3_Triangle/Square/Cross/Circle/Start/Service/Test。

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
- `io_key_slider_1_left = ["Q"]`：模拟滑条方向的按键
- `io_key_slider_1_right = ["E"]`
- `io_key_slider_2_left = ["U"]`
- `io_key_slider_2_right = ["O"]`
- `io_key_slider_01` 至 `io_key_slider_32`：均为 `[]`，分别绑定 32 个滑条区域。

绑定支持键名数组或逗号分隔字符串。可用字母、`F1`–`F24`、`Enter`、方向键、`LeftShift`、`NumPad0`、`Space`、`MouseLeft`、`"0x41"` 等 VK 字符串，以及列出的 `IO3_*` 名称。

多个按键配置会自动合并，全部松开才释放动作。

不设置时会使用默认按键，设置为`[]`则代表清空这个按键的所有配置。

### 诊断功能

- `io_input_diagnostics = false`
- `io_input_diagnostics_arcade = -1`：-1=normal native/slider decision, 0=force non-arcade, 1=force arcade.
- `io_led_diagnostics = false`

输入诊断需要同时开启 `debug = true` 与 `io_input_diagnostics = true`。正常使用保持 `io_input_diagnostics_arcade = -1`；诊断生效时，`0` 强制非街机，`1` 强制街机。普通日志追加到进程工作目录的 `anyslider.log`，LED 诊断在 DLL 旁输出独立文件。正常打歌不要开启高频诊断。

## 设备范例

### 街机整机：IO3 + Slider + 触屏

需要 Windows 装好 AMEX 驱动，连接 837-15280 AMEX 与 837-14572 IO3。

```toml
# IO总开关
io_enabled = true
# 模拟类型7 HORI控制器
io_controller_type = 7
# 独占输入
io_exclusive_controller_input = true
io_slider_mode = "arcade"
# 开启IO3
io_io3_backend = true
io_io3_poll_interval_ms = 4
io_io3_timeout_ms = 1000
# 按键灯效跟随真实顺序，而不跟随游戏内按键绑定
io_io3_led_use_hardware_position = true
# 开启街机滑条
io_arcade_slider_backend = true
# 指定滑条串口
io_arcade_slider_port = "COM11"
# 部分USB转串口需要发送DTR-RTS才能开始通讯，如连接主板原生串口出现问题可以尝试关闭
io_arcade_slider_dtr_rts = true
# 开启街机触屏
io_arcade_touchscreen_backend = true
# 触屏端口
io_arcade_touchscreen_port = "COM1"
# 开启灯效生成
io_arcade_led = true
io_arcade_led_loading_progress = true
# 开启测试菜单
io_enable_test_mode = true
# 使用手柄时开启光标UI输入
io_show_mouse_with_controller = true
# 使用O键确认（按照个人习惯使用）
io_use_ps_circle_confirm = true

# 开启键盘IO
io_keyboard_mouse_frontend = true
# 接管键盘输入
io_block_keyboard_input = true
io_block_mouse_input = false
# 游戏在后台保持运行
io_keep_game_active_unfocused = true

# 按键绑定
io_key_test = ["F1", "IO3_Test"]
io_key_service = ["F2", "IO3_Service"]
io_key_pause = ["Escape", "IO3_Start"]
io_key_start = []
io_key_triangle = ["IO3_Triangle"]
io_key_square = ["IO3_Square"]
io_key_cross = ["IO3_Cross"]
io_key_circle = ["IO3_Circle"]

# 根据需求开关调试
debug = false
io_input_diagnostics = false
io_input_diagnostics_arcade = -1
io_led_diagnostics = false
```

COM11/COM1按实机端口修改。
IO3使用AMEX驱动，不是串口。

### GAMO2 FAUCETWO SDVX 手台

FAUCETWO 手台请使用 HID 键盘鼠标模式 (长按 START + FX-L + FX-R + BT-C 三秒)

```toml
# 开启核心IO功能
io_enabled = true

# 让游戏不在前台时保持运行
io_keep_game_active_unfocused = true

# 开启键鼠IO
io_keyboard_mouse_frontend = true
# 屏蔽游戏自身的键盘输入，避免产生重复输入；鼠标/触摸屏输入保持放行以操作UI
io_block_keyboard_input = true
io_block_mouse_input = false

# 键盘LR、手柄左右滑条模式使用arcade
io_slider_mode = "arcade"
# Arcade 模式下滑条模拟速度，可以自行增减
io_arcade_slider_emu_cells_per_second = 32.0

# 使用鼠标输入来作为滑条使用，需要开启
io_use_mouse_as_slider = true
# 指定 FAUCETWO 的设备ID，过滤掉其他鼠标输入
io_mouse_slider_device = "0E8F:1118"


# FAUCETWO 按键配置
io_key_triangle = ["D"]
io_key_square = ["F"]
io_key_cross = ["J"]
io_key_circle = ["K"]
```

### DualShock4/DualSense 手柄触摸板当作滑条使用

```toml
# 开启核心IO功能
io_enabled = true

# 手柄前端，支持 DS4、DualSense、Pro Controller 和 Joy-Con。
io_gamepad_frontend = true

# 使用DS4/DualSense的触摸板作为滑条输入
io_gamepad_touchpad_slider = true
```

### 模拟街机控制器模式的高速滑条

开启键盘或手柄前端

```toml
io_enabled = true
io_keyboard_mouse_frontend = true

# 滑条模式："arcade" 将键盘/手柄方向模拟在滑条上滑动
io_slider_mode = "arcade"

# Arcade 模式下滑条模拟速度，单位为 cell/秒。
# 只对左右输入有效，对直接 32-cell 输入和 mouse_as_slider 模式无效
io_arcade_slider_emu_cells_per_second = 32.0

# 键盘模式需要绑定以下按键：
io_key_slider_1_left = ["Q"]
io_key_slider_1_right = ["E"]
io_key_slider_2_left = ["U"]
io_key_slider_2_right = ["O"]
```

#### 触屏手动校准

只有EEPROM不可读或数据无效时才使用这组参数；范围是0 ≤ min < max ≤ 4095。

```toml
io_arcade_touchscreen_invert_x = true
io_arcade_touchscreen_invert_y = true
io_arcade_touchscreen_swap_xy = false
io_arcade_touchscreen_min_x = 0
io_arcade_touchscreen_max_x = 4095
io_arcade_touchscreen_min_y = 0
io_arcade_touchscreen_max_y = 4095
```

## 外部共享内存输入

开启 `io_enabled = true`，外部程序使用 `io_shared_memory` 指定的名称，并按 `io_max_input_lease_ms` 持续更新输入。无需为了共享内存开启键鼠、手柄或街机硬件前端。需要切到本机外部窗口操作时，可开启 `io_keep_game_active_unfocused`。

协议定义见 [shared_memory.h](include/mm_io/shared_memory.h)，灯效输出接口见 [led_output.h](include/mm_io/led_output.h)。
