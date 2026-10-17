#pragma once
#include <cstdint>
#include <functional>
namespace anyslider {
// 街机 LED 场景驱动与 Lamp 注册配置
struct ArcadeLedConfig {
    bool loading_progress = false; // false: 时间展开；true: 加载阶段进度展开
    bool enabled = false; // 是否启用街机 LED 场景驱动（自动注册 Lamp 并跟随场景同步 Slider/Lamp 模式）
};
// 回调只读 IO3 快照；返回 false 时沿用游戏映射。held 使用 LampTask bits0..3。
using HardwareLampReader = std::function<bool(uint32_t& held)>;
bool InitializeArcadeLed(const ArcadeLedConfig& config, HardwareLampReader hardwareLampReader = {});
void ShutdownArcadeLed();
void ApplyArcadeLedScene(void* slider);
}
