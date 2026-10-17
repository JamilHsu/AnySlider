#pragma once

#include <cstdint>

namespace anyslider
{
// 街机 TEST MENU / 按键与输入测试独立模块配置
struct ArcadeTestModeConfig
{
    bool enabled = false;               // 是否启用 Test Mode 唤起监听
    bool trigger_on_jvs_test = true;   // 是否在检测到 JVS Test 键按下时自动拉起
};

// 初始化独立 Test Mode 模块（在游戏初始化时安装）
bool InitializeArcadeTestMode(const ArcadeTestModeConfig& config = {});

// 卸载与清理独立 Test Mode 模块
void ShutdownArcadeTestMode();

// 主动请求调出原生 TEST MODE / INPUT TEST 任务
bool RequestArcadeTestMode();

// 检查当前 TEST MODE 是否已注册并运行中
bool IsArcadeTestModeActive();

} // namespace anyslider
