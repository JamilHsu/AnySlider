#include "../../src/mm_ui_input_wrapper.h"
#include "../../src/mm_ui_input_policy.h"
#include <cstdio>
using namespace anyslider::ui_input;
int main()
{
    const std::array<uint8_t, 12> native = {
        0x4C,0x8D,0x05,0x19,0xFD,0xFF,0xFF,0xE9,0x34,0xFF,0xFF,0xFF};
    const std::array<uint8_t, 12> chained = {
        0xFF,0x24,0x25,0x08,0x02,0x7B,0x08,0xE9,0x34,0xFF,0xFF,0xFF};
    if (ClassifyTappedWrapper(native) != TappedWrapperKind::Native ||
        ClassifyTappedWrapper(chained) != TappedWrapperKind::IndirectDetour)
        return 1;
    for (size_t i = 0; i < native.size(); ++i) {
        auto changed = native;
        changed[i] ^= 1;
        if (ClassifyTappedWrapper(changed) != TappedWrapperKind::Unsupported) return 2;
    }
    for (size_t i = 0; i < chained.size(); ++i) {
        if (i >= 3 && i < 7) continue; // variable address, validated at runtime
        auto changed = chained;
        changed[i] ^= 1;
        if (ClassifyTappedWrapper(changed) != TappedWrapperKind::Unsupported) return 3;
    }
    for (uint32_t type = 0; type < 12; ++type) {
        for (uint64_t action = 0; action < 162; ++action) {
            const bool ps = type == 2 || type == 3 || type == 7 || type == 8 || type == 9;
            const auto expected = ps && action == 9 ? 10 : ps && action == 10 ? 9 : action;
            if (anyslider::ResolvePsCircleAction(action, type, true) != expected ||
                anyslider::ResolvePsCircleAction(action, type, false) != action) return 4;
        }
    }
    std::puts("PASS: native/chained wrapper validation, unknown mutations rejected, PS action policy");
}
