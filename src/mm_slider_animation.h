#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

namespace anyslider {
enum class SliderEffect : int {
    Off = 0, Breathing = 1, PaletteFlow = 2, Gameplay = 3,
    Events = 4, Sections = 5, SectionExpand = 6, SectionFade = 7, Solid = 8
};
struct LedRgb { uint8_t red, green, blue; };
inline LedRgb CharacterLedColor(int character) {
    // 角色主题色；颜色与设备通道顺序分离。
    constexpr LedRgb colors[] = {
        {0,254,254}, {254,254,0}, {254,112,0}, {254,16,160},
        {254,218,100}, {170,182,254}, {0,8,254}, {196,8,8},
        {254,147,72}, {194,83,108}
    };
    return colors[character >= 0 && character < 10 ? character : 0];
}
class LoadingAnimation {
    uint32_t seen_ = 0;
    unsigned progress_ = 0;
    uint64_t started_ = 0;
public:
    void Begin(uint64_t now) { seen_ = 0; progress_ = 0; started_ = now; }
    unsigned ObserveStage(int stage, bool finished) {
        if (stage > 0 && stage <= 25) seen_ |= 1u << stage;
        progress_ = (std::max)(progress_, static_cast<unsigned>(std::popcount(seen_)) * 100u / 25u);
        if (finished || stage == 25) progress_ = 100;
        return progress_;
    }
    float Expansion(uint64_t now, bool progressDriven) const {
        // 时间展开是兼容动画；阶段百分比不是字节进度或剩余耗时。
        return progressDriven ? progress_ / 100.0f :
            (std::min)(1.0f, static_cast<float>(now >= started_ ? now - started_ : 0) / 533.0f);
    }
};
inline void RenderCenterExpand(uint8_t* task, LedRgb color, float expansion) {
    expansion = std::clamp(expansion, 0.0f, 1.0f);
    const float radius = 3.0f + 13.0f * expansion;
    const uint8_t brg[] = {color.blue, color.red, color.green};
    task[3390] = color.blue; task[3388] = color.red; task[3389] = color.green;
    for (unsigned cell = 0; cell < 32; ++cell) {
        const float distance = cell < 16 ? 15.5f - cell : cell - 15.5f;
        const float brightness = std::clamp(radius + 0.5f - distance, 0.0f, 1.0f);
        for (unsigned channel = 0; channel < 3; ++channel) {
            const unsigned index = cell * 3 + channel;
            const int32_t value = static_cast<int32_t>(brg[channel] * brightness), zero = 0;
            std::memcpy(task + 1020 + index * 4, &value, 4);
            std::memcpy(task + 1404 + index * 4, &zero, 4);
        }
    }
}
} // namespace anyslider
