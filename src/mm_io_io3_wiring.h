#pragma once
#include <cstdint>

namespace anyslider {
// 三种编号不能混用：JVS 输入掩码、LampTask 灯位、cabinet 输出位/GPIO。
namespace jvs_switch {
inline constexpr uint16_t Test = 0x0001;     // 合成位；真实 TEST 来自 system byte 0x80，不是 P1 按键
inline constexpr uint16_t Service = 0x4000;  // P1 high 0x40: SERVICE
inline constexpr uint16_t Start = 0x8000;    // P1 high 0x80: START
inline constexpr uint16_t Triangle = 0x0200; // P1 high 0x02: △
inline constexpr uint16_t Square = 0x0100;   // P1 high 0x01: □
inline constexpr uint16_t Cross = 0x0080;    // P1 low  0x80: ×
inline constexpr uint16_t Circle = 0x0040;   // P1 low  0x40: ○
}
namespace io3_lamp {
inline constexpr uint32_t Triangle = 0x01; // Lamp bit0: △
inline constexpr uint32_t Square = 0x02;   // Lamp bit1: □
inline constexpr uint32_t Cross = 0x04;    // Lamp bit2: ×
inline constexpr uint32_t Circle = 0x08;   // Lamp bit3: ○
inline constexpr uint32_t Buttons = 0x0F;
inline constexpr uint32_t SliderLeft = 0x10;  // Lamp bit4: 滑条左端提示
inline constexpr uint32_t SliderRight = 0x20; // Lamp bit5: 滑条右端提示
inline constexpr uint32_t SongSelectBlink = Triangle | Circle | SliderLeft | SliderRight; // 0x39

// 未经过 io_io3_* 绑定或游戏 action alias 的真实开关 -> 按下灭灯位。
inline constexpr uint32_t HeldFromHardware(uint16_t switches) {
    return ((switches & jvs_switch::Triangle) ? Triangle : 0u) |
           ((switches & jvs_switch::Square) ? Square : 0u) |
           ((switches & jvs_switch::Cross) ? Cross : 0u) |
           ((switches & jvs_switch::Circle) ? Circle : 0u);
}
// 兼容原有游戏映射模式；ID 不是 JVS 掩码，也不是 GPIO 编号。
inline constexpr uint32_t HeldFromGame(uint32_t held) {
    return ((held & (1u << 8)) ? Triangle : 0u) |
           ((held & (1u << 10)) ? Square : 0u) |
           ((held & (1u << 7)) ? Cross : 0u) |
           ((held & (1u << 9)) ? Circle : 0u);
}
}
namespace io3_output {
struct Route { uint8_t cabinet_bit; uint8_t gpio_bit; };
// 保留已确认的接线；其他输出用途未确认，不猜测名字或发送未知输出。
inline constexpr Route Routes[] = {
    {3,6}, // △: Lamp 0x01 -> cabinet 0x0008 -> GPIO 0x0040
    {4,5}, // ×: Lamp 0x04 -> cabinet 0x0010 -> GPIO 0x0020
    {6,3}, // □: Lamp 0x02 -> cabinet 0x0040 -> GPIO 0x0008
    {7,2}, // ○: Lamp 0x08 -> cabinet 0x0080 -> GPIO 0x0004
    {8,1}, {9,0}, // cabinet 0x0100/0x0200 -> GPIO 0x0002/0x0001
    {10,15}, {11,14}, {12,13}, {13,12} // cabinet 0x0400..0x2000 -> GPIO 0x8000..0x1000
};
inline constexpr uint16_t ToGpio(uint32_t bitmap) {
    uint16_t gpio = 0;
    for (const auto& route : Routes)
        if (bitmap & (1u << route.cabinet_bit)) gpio |= uint16_t(1u << route.gpio_bit);
    return gpio;
}
}
}
