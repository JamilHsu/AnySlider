# 键盘输入诊断（MegaMix+ v103）

## 开关与边界

`io_input_diagnostics=true` **且** `debug=true` 才启用诊断及实验覆盖；`io_enabled=true` 是输入 Hook 前提。重启游戏应用配置。

日志走stdout / Mod Loader并镜像到进程工作目录`anyslider.log`，搜索`InputDiag`。帧号是 AnySlider 的输入刷新序号，不是渲染帧号。按下/释放后的短窗口（当前刷新及后3次）观察 action7..10，包括 false；每窗口最多256条主记录，满额明确显示 `TRUNCATED`。当前 query 仅记录原生结果，不再代理 predicate 或输出逐 bit check 明细；共享 binding/prompt 观察 hook 默认不安装。调试会增加时序开销，不用于性能/极短按精度结论。

诊断 action Hook 只透传原参数/返回值，不替换或额外调用 predicate、不猜 alias 树。显式开启 `io_use_ps_circle_confirm` 时，另外启用通过v103入口/调用点校验的独立UI策略（通用Action9/10交换与六处旧UI确认迁移）。

设备 binding 策略已统一为保留有效原生结果、仅补设备0缺失 binding，不受诊断或菜单偏好开关影响。`io_use_ps_circle_confirm` 默认关闭；开启对PS身份2/3/7/8/9的通用Action9/10查询交换，并迁移六处旧UI确认调用，不改原始位与绑定，图例不变。旧的全局 getter 交换不构成完整○确认。

设备身份与诊断控制（仅 arcade 覆盖要求双开关）：
- 设备类型使用正式参数 `io_controller_type=-1`（自动）、3（DualSense）、4（Nintendo）；不依赖诊断开关。旧诊断类型参数已删除。改变虚拟身份及缺失 binding fallback，不改变注入动作位；与 DirectInput VID/PID 参数独立。
- `io_input_diagnostics_arcade=-1` 保持正常决策；0/1强制最终Arcade查询结果。仅用于短菜单实验，影响该查询所有调用方，不在强制模式打歌。

## 复用日志

- 既有 bound input press扩充：binding名字、**数字bit**、来源、VK/统一IO3代码、诊断帧号。Slider方向尚未变成bit时显示UINT32_MAX，不能把它当真实按钮编号。
- merged：三word tapped/held/released，包含释放与跨输入源合并后的状态。
- inject-before/after：状态指针，face bits7..10的原生/最终状态。
- device：原生类型、原生返回、输出类型、独占/虚拟活动、state+0x2E8快照。该state快照取于替换返回之前，不能当最终类型；以effective-type判断本次输出。
- arcade：原生判断、slider贡献、最终判断与覆盖值，变化才输出。
- binding：action/device、原生/最终binding、类型、caller RVA；变化才输出。
- resolved-binding：原生action→设备binding getter输出，短窗口输出，保持行为。
- query/check：游戏查询action、predicate RVA、caller RVA、原生返回以及实际literal/alias bit和predicate结果。

predicate RVA参考v103：Tapped +2AAF80、Down +2AAFE0；其他predicate保留RVA待IDA定位。公共wrapper可能尾跳转，caller RVA用于定位最终调用方。角色literal/alias按已复核公共函数调用顺序标注。

未安装保存enum/sprite/PVslot的新增Hook；绑键页图标和打歌slot须由屏幕观察补充。不能把resolved-binding日志说成最终sprite或存档enum的直接证据。

## 固定公共配置

在原config.toml中替换对应值，勿重复追加同名key：

```toml
debug = true
io_enabled = true
io_input_diagnostics = true
io_keyboard_mouse_frontend = true
io_block_keyboard_input = true
io_block_mouse_input = false
io_gamepad_frontend = false
io_io3_backend = false
io_arcade_slider_backend = false
io_use_mouse_as_slider = false
io_exclusive_controller_input = true
io_slider_mode = "joystick"
io_use_ps_circle_confirm = false
io_controller_type = 3
io_input_diagnostics_arcade = 0
io_key_triangle = "I"
io_key_square = "J"
io_key_cross = "K"
io_key_circle = "L"
```

没有外部MMIO写入程序/其他改键mod。保留同一存档，不重置游戏绑定。I/J/K/L故意显式指定，便于与日志VK关联；如本地使用这些键绑定其他动作，先解除重叠。

## 按顺序测试

每组重启并另存日志：

1. A基线PS：以上公共配置。
2. B基线Nintendo：只改controller_type=4。对照A检验类型/图标/alias/实际行为。
3. C基线Arcade：回到A，只改arcade=1。对照A检验alias禁用影响。不打歌。
旧 D/E 组用于调查历史 getter 干扰，不适用于重构后的版本；无需重复测试。

每组选择同样选曲界面/弹窗，I/J/K/L分别按下约0.2秒，完全释放并间隔1秒，记录各自确认/返回/无动作及屏幕提示。会离开当前菜单时先恢复到同一界面再按下个键。再进入绑键页观察四键识别/图标，不覆盖原打歌绑定。A/B可另在同首歌观察四键slot，C不打歌。

若游戏有原生确认键切换选项：在A条件下只切换该选项，额外日志和截图对比。不要与类型/Arcade变化同时做。A/B/C用于定位主要链路；○确认模块已实现，仍需逐页面验证行为；当前不改菜单提示。

## 键盘可以证明什么

足以追踪VK→MMIO数字bit→原生InputState→实际游戏action查询→literal/alias结果，判断虚拟类型/Arcade/getter策略是否改变菜单行为。配合屏幕观察，可以确定下一步O确认实现应改哪一层。

不能证明真实DirectInput设备的注册布局、IO3接线、物理灯或硬件短按时序。不能从仅键盘类型覆盖推出真实Nintendo/PS设备行为。真实硬件以后补验证，不阻塞菜单软件链的定位。
