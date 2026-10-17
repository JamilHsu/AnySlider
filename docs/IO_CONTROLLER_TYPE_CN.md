# IO 模拟设备类型

正式参数 `io_controller_type = -1`，不依赖 debug/input diagnostics。

- -1：保持原有自动选择；默认 DualSense3，活跃JoyShock设备自动识别。
- 2：DS4；3：DualSense；4：Nintendo；5：Xbox。
- 6：HORI Mega39s，Nintendo图例类别；7：HORI Future Tone，PS图例类别。
- 允许原生编号0..10；0/1/8/9的产品语义未完整核实。10走文字类别，需要原生键盘绑定元数据，不能视为已经完整实现键盘模拟。11代表无控制器，不接受。

类型只改变IO注入身份，不交换已注入动作位或改变JoyShock物理映射。非独占时继续沿用原有虚拟输入接管条件，不强行覆盖空闲原生设备。target_controller_type仍独立控制DirectInput VID/PID注册。

旧诊断设备类型参数已直接删除，无兼容解析或迁移提示。io_input_diagnostics_arcade仍是诊断参数。

绑定回退保留有效原生绑定，只补缺失的设备0绑定；4/6使用Nintendo表，其余手柄类型使用PS位置表，10不伪造PS绑定。

`io_show_mouse_with_controller = true` 默认打开，独立于keyboard前端。hook原生设备身份鼠标门控0x1402B1140，只强开+1921，保留场景/玩法+1920以及io_block_mouse_input过滤。关闭选项不安装此hook。版本/入口不匹配或可选 hook 安装事务失败时，仅关闭鼠标覆盖，核心 IO 保持加载。鼠标与诊断/实验菜单 action hook 分别在核心成功后独立事务安装；卸载失败保留安装状态、原函数及前端资源，不清空仍可能被调用的指针。不会把控制器身份改成11，也不会强制UI切成键盘图例。源码与编译验证不代表全部UI点击已恢复；须实机验证。

静态补核：原生0x1402ABB30仍复制键鼠接触块，控制器身份覆盖与鼠标坐标并非同一个合并过程。现有MergeSelectedDeviceHook独占时持续覆盖身份，非独占时有AnySlider活动或无控制器身份11也覆盖；这会阻止/竞争原生键鼠图例切换。当前策略保留所选控制器图例，并开放鼠标门控，不全局恢复自动身份切换。左键向下的原生别名/消费者需按路径区分。

独占路径现已增加指针白名单合并：原生键鼠合并仅写入隔离缓冲区，再将 raw140..143 接触、raw145..149 鼠标按钮/滚轮的 tapped/released/held 与 analog8..19 指针坐标/增量/滚轮复制到统一 IO 状态，不合并物理键盘、any-key144、手柄轴或设备身份。仅 player0 的主合并上下文执行；物理鼠标需 show_mouse=true 且 block_mouse=false，可信触屏启用时也允许此路径。保持虚拟手柄身份，不依靠关闭独占。修正原生 MergeConnectedDevice 第四参数 primaryContext 的传递，避免丢失坐标复制上下文。input diagnostics 可输出 PointerMerge 点击边沿和坐标。

原生通用 MenuSelection 使用 raw145 tapped 和 analog8/9 命中测试，无 controller-type 检查；非当前项点击可能先选中，而不是立即确认。独占合并已做原生 C++白名单/隔离检查及 Windows x64 编译链接，用户已实测确认独占模式下鼠标点击有效；尚未逐项记录主菜单、自定义、设置、暂停，以及额外导航/重复确认的完整验收。
