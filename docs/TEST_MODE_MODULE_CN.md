# 可选原生 Test Mode 模块

Test Mode 为实验诊断功能，所有任务、输入观察、菜单门控及退出hook都放在 `src/mm_arcade_test_mode.cpp/.h` 内。IO采样、合并、输入注入、设备前端和LED模块不引用此模块；不新增IO回调或测试模式判断。

配置开关为`io_enable_test_mode=false`，位于默认配置街机部分底部；需`io_enabled=true`，修改后重启。关闭时不安装Test Mode或触屏测试菜单hook，设备采样不受影响。唯一模块接入点仍在`mod.cpp`，配置加载后条件调用`InitializeArcadeTestMode(testModeConfig)`；IO采样/合并模块不引用Test Mode。若从构建中彻底移除源码，同时删除头文件include并移除vcxproj/filters中的该模块条目。

模块自行hook TaskManager tick，原始tick完成后通过游戏 `GetInputState` 和 `IsButtonDown/IsButtonTapped` 读取两玩家的TEST位0/18。输入来源无关：键盘/IO3/共享内存仍由既有IO路径注入，模块只观察游戏接收结果。场景请求发生在调度遍历完成后，通过主模式4/子状态29原生生命周期创建测试任务；不再手动注册 overlay。退出请求返回进入前的主模式，不保证恢复原子页面；打开输入释放门控保留。其他确认键7/10/55不用于打开菜单。

验证：启用版Windows x64编译/链接通过；临时移除mod.cpp中的初始化调用与include，且不链接mm_arcade_test_mode.obj，禁用版也成功链接。IO源文件中没有Test Mode符号依赖。临时禁用版验证代码和产物已清理。

异常处理：请求被其他场景替换且任务已经注销时释放模块占用；进入等待超过15秒后尝试通过原生生命周期安全返回，保留对迟到任务的保护。退出遇到其他原生切场景或错误请求时不抢占；等待超时仅记录一次警告，不强写状态或放行原生 shutdown。

验证边界：用户已验证正常 TEST MENU；新增异常恢复仅做针对性 stub 验证与Windows编译链接，不代表故障场景实机验收。模块存在时仍不支持游戏运行期间动态卸载，构建时取消初始化与运行时卸载是不同问题。DLL包含当前其他工作树改动。
