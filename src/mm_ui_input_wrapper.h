#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace anyslider::ui_input {
enum class TappedWrapperKind { Unsupported, Native, IndirectDetour };

// v1.03 Input_CheckActionTapped at +2AB260:
// LEA R8, IsButtonTapped; JMP Input_CheckAction.
// Reloaded can replace the 7-byte LEA with JMP qword ptr [abs32].
// Only classify its shape here; the caller must validate the pointer and
// executable destination before chaining. Never rewrite the external hook.
inline TappedWrapperKind ClassifyTappedWrapper(const std::array<uint8_t, 12>& bytes)
{
    constexpr std::array<uint8_t, 12> native = {
        0x4C,0x8D,0x05,0x19,0xFD,0xFF,0xFF,0xE9,0x34,0xFF,0xFF,0xFF};
    if (bytes == native) return TappedWrapperKind::Native;
    for (std::size_t i = 7; i < native.size(); ++i)
        if (bytes[i] != native[i]) return TappedWrapperKind::Unsupported;
    return bytes[0] == 0xFF && bytes[1] == 0x24 && bytes[2] == 0x25
        ? TappedWrapperKind::IndirectDetour : TappedWrapperKind::Unsupported;
}
}
