#pragma once

namespace anyslider
{
// 安装触屏测试菜单挂钩（点亮 TOUCH PANEL SETTING 菜单项）
bool InstallTouchScreenTestHooks(bool touchscreenEnabled);
void UninstallTouchScreenTestHooks();
} // namespace anyslider
