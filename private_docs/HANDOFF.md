# AnySlider 项目进度与交接文档 (HANDOFF.md)

## 结论与当前状态

当前工作分支为 `io3`。项目已完成 **MegaMix+ v1.03 原生 Arcade LED 捕获与恢复（ABI 3.0 / v4）** 的代码重构与基础硬件打通。
根据实机环境运行测试，核心链路（滑条串口 0x02、IO3 JVS 0x32、v3 汇编桥防崩溃、选曲与长按侧灯）已基本就绪，但存在 **2 个灯光逻辑缺陷**，并提出了 **通过 IDA 逆向深入还原街机 AFT 原生场景驱动逻辑** 的需求。

---

## 一、已确认生效的功能 (Confirmed Working)

1. **滑条灯效联动**：
   - 选曲界面下 Slider 两端提示灯（Bits 4/5）正常闪烁，且能正确响应触摸与原生事件。
2. **机台侧灯与长按效果**：
   - 左右两边侧灯条在长按（Hold）时正确变色与闪烁；Web 端 AnySlider-Viewer 能实时展示 IO3 状态位（8-10 / 11-13）的 RGB 状态。
3. **选曲按键闪烁**：
   - 选歌界面下，四个按键正确进行三角（△）与圆形（○）的交替闪烁提示。
4. **游戏稳定性**：
   - v3 汇编执行桥（保存/恢复 `R10`, `R11`, `XMM0..5` 并提供 Windows x64 UNWIND 元数据）在数万次调用下零崩溃，彻底解决了清灯函数 `0x14042F330` 触发的 `c0000005 at 0x2ab355` 崩溃。

---

## 二、当前待解决的已知问题 (Outstanding Issues)

### 问题 1：按键点击时灭灯映射对调
- **现象描述**：
  - 待机/闪烁模式下灯位正常（△ 与 ○ 闪烁）。
  - 实际点击按键时，灭灯反馈发生错位：
    - 按下 **三角（△）** 键，实际灭掉的是 **方块（□）** 键灯；
    - 按下 **叉（×）** 键，实际灭掉的是 **圆形（○）** 键灯。
    - 即：△ 与 □ 互反，× 与 ○ 互反。
- **排查线索与初步逆向事实**：
  1. `DivaMegaMix.exe` 中 `TaskLampCtrl` 的更新函数 `0x14042F0A0`：
     - `*(TaskLampCtrl + 112) & 1` (Bit 0)  -> Cabinet 逻辑灯位 **3**
     - `*(TaskLampCtrl + 112) & 2` (Bit 1)  -> Cabinet 逻辑灯位 **6**
     - `*(TaskLampCtrl + 112) & 4` (Bit 2)  -> Cabinet 逻辑灯位 **4**
     - `*(TaskLampCtrl + 112) & 8` (Bit 3)  -> Cabinet 逻辑灯位 **7**
  2. `E:\\git\\AnySlider\\src\\mm_io_jvs.cpp` 中 `Session::WriteLeds` 当前硬编码映射：
     - `source[]      = {3, 4, 6, 7, 8, 9, 10, 11, 12, 13};`
     - `destination[] = {6, 3, 5, 2, 1, 0, 15, 14, 13, 12};`
     - 其中逻辑灯位 3 -> GPIO 6，4 -> GPIO 3，6 -> GPIO 5，7 -> GPIO 2。
  3. **怀疑点**：
     - 是 `WriteLeds` 中 `source` 到 `destination` 的 GPIO 对应关系配反，还是 MegaMix+ 针对手柄键位/确定取消规则修改了输入映射而灯光逻辑仍保留 AFT 格式？需要结合 IDA 逆向确认四个按键灯在 AFT 与 MegaMix 中的真实位定义。

### 问题 2：加载场景（Loading）Slider 未变成蓝色加载效果
- **现象描述**：
  - 加载乐曲过程中，Slider 依然维持选曲阶段的闪烁效果，没有像街机 AFT 原版一样切为纯蓝或蓝色流光加载灯效。
- **现状分析**：
  - 目前 `E:\\git\\AnySlider\\src\\mm_arcade_led.cpp` 的 `ApplyScene` 是在 `Slider pack Hook` 中被动补充灯效，仅对 `scene == 5 || scene == 6` (选曲) 与 `scene == 7` (游玩) 设置模式。
  - 加载场景（例如场景切入 PV/PV 初始化）并未设置专属的 Lamp 模式与 Slider 模式（通常 AFT 原生在加载阶段会通过特定 Mode 或数据包驱动滑条全蓝）。

---

## 三、IDA Remote 逆向环境与连接配置

逆向分析工具链已经就绪：
- **服务地址**：`http://mini-server.tail669c7d.ts.net:13337/mcp`
- **协议**：ZeroMCP (JSON-RPC 2.0 / POST)
- **当前 IDB 会话**：`97df165c`
- **目标二进制**：`/home/chieri/reverse-engineering/Project_DIVA_MegaMix/DivaMegaMix.exe-v103.i64` (Project DIVA MegaMix+ v1.03, ImageSize `0x23e73000`)
- **可用 MCP 方法**：`decompile`, `disasm`, `xrefs_to`, `lookup_funcs`, `list_funcs`, `get_bytes`, `patch` 等。

---

## 四、后续推进计划 (Next Steps)

1. **按键灯对调根因分析**：
   - 使用 IDA Remote 查看 `0x14042F1F0` 及 `0x14013E770`（GameState / Input 处理），确认 offset `112` 的 4 个 bit 分别代表哪个几何图形（△, □, ×, ○）。
   - 校验物理街机 IO3 837-14572 板载 GPIO 定义，修正 `E:\\git\\AnySlider\\src\\mm_io_jvs.cpp` 中的 `source[]` / `destination[]` 映射。
2. **加载场景灯光逆向 (AFT 街机行为复现)**：
   - 逆向分析游戏 Scene 状态机（`base + 0xcc61094`）在选曲到游玩过渡过程中的场景枚举值（如 Loading / Now Loading / PV Setup）。
   - 查找 AFT 中控制 Slider 纯蓝加载的指令生成点（检查 `SliderCtrlTask` 相关函数如 `0x1405a9ac0`、`0x1405a4aa0` 等调用处）。
3. **消除临时补丁，回归原生事件驱动**：
   - 将 `mm_arcade_led.cpp` 中的“轮询打补丁”逻辑升级为 Hook 游戏原生场景状态切换函数，在场景转换时精准通知原生 Lamp 任务和 Slider 任务。