# 正常进程退出清理

正常调用 Win32 `ExitProcess` 时，独立可选退出 hook 在系统终止线程/进入 DLL detach 前执行清理；`OnFrame` 在退出开始后停止发布/泵输入。安装失败仅记录警告，不阻挡核心 IO。清理幂等，不在 DllMain 中 join 线程，不支持运行中 FreeLibrary 热卸载。

顺序：IO detour 成功注销后停止 LED 诊断/LED publisher（发布最后黑帧）、关闭游戏状态读取；停止 Slider、IO3、JoyShock 线程，JoyShock 解除回调并 DisconnectAndDisposeAll，RAII 关闭串口/AMEX 句柄；关闭 MMIO mapping；停止 Raw Input 消息线程及自有窗口/事件句柄。最后停止触屏 worker、释放内部按下状态并恢复窗口过程/API hooks。

Raw Input 初始化失败也释放窗口/窗口类；核心 IO 初始化失败关闭已启动 Raw Input。线程等待超时保留线程/事件和状态供重试，不把未结束线程当已关闭。仅移除仍以本模块窗口为目标的 Raw Input usage，避免移除游戏后续替换的 process-wide 注册。设备关闭后清空灯输出缓存，防止重新初始化沿用旧帧。

异常边界：IO detach 失败保留 callback 依赖，避免 UAF；进程退出后操作系统回收残留句柄。TerminateProcess/强制结束/崩溃无法保证设备协议清理。黑帧发布不等于实机已收到最后灯指令；不强制重置硬件、不写 EEPROM。Test Mode 场景任务由游戏管理，进程退出不为清理而强切场景。仍不支持并发线程主动卸载模块。

验证：针对实际 CleanupBeforeExit 函数体的原生 C++ stub 检查通过（顺序、幂等）；变更 Windows x64 编译链接通过，git diff --check 通过。未做游戏实机退出/句柄监控验证。构建含当前其他工作树变更。
