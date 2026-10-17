# 街机触屏输入工具

把街机触屏转换成鼠标输入当作普通触摸屏使用。

双击 `TouchscreenConnect.exe` 即可运行，默认连接 COM1、读取主板 EEPROM 校准并启用鼠标移动、左键点击和拖动，等同于：

```cmd
TouchscreenConnect.exe --port "COM1" --output mouse --calibration eeprom --arm
```

不需要 TOML 配置文件。启动时会打印实际注册成功的 Ctrl+Alt+F1～F12 退出快捷键；全部不可用时仍继续运行，可切回控制台按 Ctrl+C 停止。

## 可选参数

传入任何参数时使用诊断默认值（COM1、console、raw），不会自动启用鼠标控制；需要鼠标输入时请使用上面的完整命令。

- `--port "COM1"`：触屏串口，默认 COM1。
- `--output console|mouse|touch`：输出坐标、模拟鼠标或 Windows 单点触控。
- `--arm`：允许 mouse/touch 操作桌面。
- `--calibration raw|eeprom|file:<路径>`：直接归一化、读取主板校准或加载校准文件。raw 不做游戏里的双轴反转；eeprom 和校准文件映射包含 AFT 双轴反转。EEPROM 读取失败会回退 raw，请检查启动日志，避免方向错误。
- `--list-monitors`：列出显示器。
- `--monitor N`：选择显示器，1 为主屏；显示配置变化后请重新选择。
- `--simulate tap-drag`：无触屏输入测试；配合 mouse/touch 时仅向自有测试窗口注入。
- `--emulate-elo COM21`：在用户准备的虚拟串口对另一端模拟 Elo 设备，不要指向真实板卡。
- `--calibrate calibration.bin`：五点校准和预览，Enter 保存文件、Esc 取消，不写机台。
- `--update-image eeprom-copy.bin --calibration file:calibration.bin --backup original-backup.bin`：更新离线 EEPROM 镜像副本，备份路径必须未存在；不是硬件写入。
- `--help` / `-h`：显示参数帮助。

## 使用注意

程序运行时会占用触屏串口。通过本工具的鼠标输入使用 MM+ 时不影响游戏运行；如果 AnySlider 也启用了同一个物理触屏串口，则需关闭其中一路，不能同时占用。如果运行 AFT，需要开启触屏模拟（鼠标输入），否则无法通过本工具点击。

初始化需要等待复位；P ACK 超时后会继续切换 19200 查询，出现 `Elo SmartSet ready` 表示参数查询成功。

桌面输入不支持 UAC 安全桌面，普通权限程序不保证能操作高权限窗口。本工具不是虚拟 HID 驱动。真实 EEPROM 写入仍禁用。
