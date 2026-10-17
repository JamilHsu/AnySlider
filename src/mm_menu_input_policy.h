#pragma once

#include <cstdint>

namespace anyslider
{
// 菜单策略只转换操作查询，不改设备状态、谱面输入或物理按键图标。
class PsMenuInputPolicy final
{
public:
    static constexpr uint32_t ConfirmAction = 10;
    static constexpr uint32_t CancelAction = 9;

    [[nodiscard]] static constexpr bool IsPlayStationLayout(uint32_t type) noexcept
    {
        // Only native PS-family controller types; keyboard/no-device are not PS layouts.
        return type == 2 || type == 3 || type == 7 || type == 8 || type == 9;
    }

    [[nodiscard]] static constexpr uint32_t ResolveAction(
        uint32_t action, uint32_t controllerType, bool enabled,
        bool menuSemanticQuery) noexcept
    {
        // 调用方必须证明这是菜单语义查询；仅凭 action 编号不能区分打歌。
        if (!enabled || !menuSemanticQuery || !IsPlayStationLayout(controllerType))
            return action;
        if (action == ConfirmAction)
            return CancelAction;
        if (action == CancelAction)
            return ConfirmAction;
        return action;
    }
};
}
