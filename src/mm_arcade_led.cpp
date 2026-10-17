#include "pch.h"
#include "mm_arcade_led.h"
#include "anyslider_log.h"
#include "hook_validation.h"
#include "mm_arcade_led_policy.h"
#include "mm_slider_animation.h"
#include "mm_led_setter_bridge.h"
#include <cstring>
#include <cstdint>

namespace anyslider
{
namespace
{

using RegisterSlider = uint64_t(__fastcall *)(void *);
using RegisterTask = uint64_t(__fastcall *)(void *, const char *, int);
using Registered = bool(__fastcall *)(void *);
using SliderMode = uint64_t(__fastcall *)(void *, int);
using LampButtons = uint64_t(__fastcall *)(void *, char);
using GetInputStateFn = void *(__fastcall *)(int);
using IsButtonDownFn = unsigned char(__fastcall *)(void *, int);
using FrameCounter = unsigned int(__fastcall *)();

RegisterSlider originalRegister = nullptr;
RegisterTask registerTask = nullptr;
Registered isRegistered = nullptr;
SliderMode setSliderMode = nullptr;
LampButtons originalLampButtons = nullptr;
LedSetterBridge lampButtonsBridge;

GetInputStateFn getInputState = nullptr;
IsButtonDownFn isButtonDown = nullptr;
FrameCounter frameCounter = nullptr;
bool ownedLoadingMode = false;
LoadingAnimation loadingAnimation;
uintptr_t loadingOwner = 0;
LedRgb loadingColor = CharacterLedColor(0);

uint8_t *base = nullptr;
ArcadeLedConfig settings;
HardwareLampReader readHardwareLamps;
bool installed = false;

// 拦截 Slider Task 注册入口，同步注册 Lamp Task
uint64_t __fastcall RegisterHook(void *slider)
{
    const auto result = originalRegister(slider);
    auto *lamp = base + 0x16e66a0;
    uintptr_t manager = 0;
    std::memcpy(&manager, base + 0xcc105f8, sizeof(manager));
    if (!manager)
    {
        Log("Arcade LED: task manager unavailable; registration skipped");
        return result;
    }
    uintptr_t vtable = 0;
    std::memcpy(&vtable, lamp, sizeof(vtable));
    if (settings.enabled && vtable == reinterpret_cast<uintptr_t>(base + 0xca6970) && !isRegistered(lamp))
    {
        const auto ok = registerTask(lamp, "LAMP CTRL TASK", 0);
        Log("Arcade LED: Lamp registration result=%llu registered=%d", ok, isRegistered(lamp) ? 1 : 0);
    }
    return result;
}

// Hook LampButtons (0x14042F490)：原生更新后仅替换四键按下反馈。
// 默认从 IO3 原始开关取按下状态；关闭选项或无有效快照时沿用游戏映射。
// 灯位、JVS 掩码和兼容游戏 ID 对照集中在 mm_io_io3_wiring.h。
uint64_t __fastcall LampButtonsHook(void *task, char a2)
{
    // 保留原生触摸端点、计时与高位输出，仅重算四键按下反馈。
    const auto result = originalLampButtons(task, a2);
    if (!task)
    {
        return result;
    }
    auto *bytes = static_cast<uint8_t *>(task);
    uint32_t blink = 0, mask = 0;
    std::memcpy(&blink, bytes + 124, 4);
    std::memcpy(&mask, bytes + 112, 4);
    uint32_t held = 0;
    const bool hardwareAvailable = readHardwareLamps && readHardwareLamps(held);
    auto *input = hardwareAvailable ? nullptr : getInputState(0);
    if (input)
    {
        for (int id = 7; id <= 10; ++id)
        {
            if (isButtonDown(input, id))
            {
                held |= 1u << id;
            }
        }
    }
    const uint32_t heldLamps = hardwareAvailable ? held : io3_lamp::HeldFromGame(held);
    mask = ApplyButtonLampFeedback(mask, blink, frameCounter(), heldLamps);
    std::memcpy(bytes + 112, &mask, 4);
    return result;
}

void RequestSliderEffect(void *slider, SliderEffect effect)
{
    int current = 0;
    std::memcpy(&current, static_cast<uint8_t *>(slider) + 128, sizeof(current));
    if (current != static_cast<int>(effect))
    {
        setSliderMode(slider, static_cast<int>(effect));
    }
}

// 加载灯效独立于菜单灯效：校验原生owner后读取角色和加载阶段。
// 本函数只读游戏内存并生成固定32格灯帧，不访问外部设备。
void ApplyLoadingEffect(void *slider, uint8_t *sliderBytes, uintptr_t pv, int scene)
{
    const uint64_t now = GetTickCount64();
    if (!ownedLoadingMode)
    {
        loadingAnimation.Begin(now);
        loadingOwner = 0;
        loadingColor = CharacterLedColor(0);
    }
    int stage = -1, substate = 0;
    if (pv && scene == 7)
    {
        uintptr_t owner = 0, loader = 0;
        std::memcpy(&owner, reinterpret_cast<void *>(pv + 8), sizeof(owner));
        std::memcpy(&substate, reinterpret_cast<void *>(pv + 20), 4);
        // 确认所有者，避免沿未识别布局读取堆指针。
        if (owner == reinterpret_cast<uintptr_t>(base + 0x12c2330))
        {
            if (loadingOwner && loadingOwner != owner)
            {
                loadingAnimation.Begin(now);
            }
            loadingOwner = owner;
            std::memcpy(&loader, reinterpret_cast<void *>(owner + 185312), sizeof(loader));
            if (loader)
            {
                uintptr_t loaderOwner = 0;
                std::memcpy(&loaderOwner, reinterpret_cast<void *>(loader), sizeof(loaderOwner));
                if (loaderOwner == owner)
                {
                    std::memcpy(&stage, reinterpret_cast<void *>(loader + 8), 4);
                }
            }
            // Stage 1 完成后角色槽才是当前曲目的已解析数据。
            if (stage >= 2 && stage <= 25)
            {
                int character = -1;
                std::memcpy(&character, reinterpret_cast<void *>(owner + 185328), 4);
                if (character >= 0 && character < 10)
                {
                    loadingColor = CharacterLedColor(character);
                }
            }
        }
    }
    loadingAnimation.ObserveStage(stage, substate >= 3);
    RequestSliderEffect(slider, SliderEffect::Solid);
    RenderCenterExpand(sliderBytes, loadingColor, loadingAnimation.Expansion(now, settings.loading_progress));
    ownedLoadingMode = true;
}

// 菜单只指定闪烁策略，四键按下灭灯仍由原生Lamp tick /反馈hook完成。
void SetMenuLampMode(uint8_t *lamp, uint32_t blink)
{
    constexpr int enabledMode = 1;
    std::memcpy(lamp + 116, &enabledMode, sizeof(enabledMode));
    std::memcpy(lamp + 124, &blink, sizeof(blink));
}

// 场景灯效同步与模拟
void ApplyScene(void *slider)
{
    if (!settings.enabled || !slider)
    {
        return;
    }

    int scene = 0;
    std::memcpy(&scene, base + 0xcc61094, sizeof(scene));
    auto *lamp = base + 0x16e66a0;
    uintptr_t manager = 0;
    std::memcpy(&manager, base + 0xcc105f8, sizeof(manager));

    if (!manager || !isRegistered(lamp))
    {
        return;
    }

    auto *sliderBytes = static_cast<uint8_t *>(slider);
    int currentSliderMode = 0;
    std::memcpy(&currentSliderMode, sliderBytes + 128, sizeof(currentSliderMode));

    int next = 0, transition = 0, pvPhase = -1;
    uintptr_t pv = 0;
    std::memcpy(&next, base + 0xcc61098, 4);
    std::memcpy(&transition, base + 0xcc610a0, 4);
    std::memcpy(&pv, base + 0xcc105a0, sizeof(pv));
    if (pv && scene == 7)
    {
        std::memcpy(&pvPhase, reinterpret_cast<void *>(pv + 16), 4);
    }
    const int phase = ClassifyLedPhase(scene, next, transition, pv != 0, pvPhase);
    if (phase == 1)
    {
        SetMenuLampMode(lamp, 0); // 立即停止选曲界面的两端提示灯与按键闪烁，防止加载开始时残留闪动
        ApplyLoadingEffect(slider, sliderBytes, pv, scene);
        return;
    }
    if (ownedLoadingMode)
    {
        // Only undo our mode if native score update has not already replaced it.
        if (currentSliderMode == 8)
        {
            RequestSliderEffect(slider, SliderEffect::Breathing);
        }
        ownedLoadingMode = false;
    }

    // 1. 选曲界面 (Music Select, scene == 5 || scene == 6)
    if (scene == 5 || scene == 6)
    {
        SetMenuLampMode(lamp, io3_lamp::SongSelectBlink); // 四键/两侧提示接线见 wiring.h

        if (currentSliderMode != 1)
        {
            RequestSliderEffect(slider, SliderEffect::Breathing); // 呼吸灯模式
        }
    }
    // 2. 主菜单 / 待机演示 (Title / Attract, scene == 3 || scene == 42)
    else if (scene == 3 || scene == 42)
    {
        SetMenuLampMode(lamp, 0);

        if (currentSliderMode != 2)
        {
            RequestSliderEffect(slider, SliderEffect::PaletteFlow); // 彩虹调色板流动模式
        }
    }
    else if (scene == 7)
    {
        // Phase 2 is post-play, not loading.
        if (phase == 3)
        {
            SetMenuLampMode(lamp, 0);
            if (currentSliderMode != 1)
            {
                RequestSliderEffect(slider, SliderEffect::Breathing);
            }
        }
        // Playing and unknown states leave native generation untouched.
    }
}

} // namespace

void ApplyArcadeLedScene(void *slider)
{
    if (installed)
    {
        ApplyScene(slider);
    }
}

bool InitializeArcadeLed(const ArcadeLedConfig &config, HardwareLampReader hardwareLampReader)
{
    if (installed)
    {
        return true;
    }
    if (!config.enabled)
    {
        return true;
    }

    base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);

    const uint8_t reg[] = {0x45, 0x33, 0xc0, 0x48, 0x8d, 0x15, 0xa6, 0x23,
                           0x71, 0,    0xe9, 0xc1, 0x4b, 0xd2, 0xff};
    const uint8_t task[] = {0x45, 0x8b, 0xc8, 0x4c, 0x8b, 0xc2, 0x48, 0x8b, 0x15, 0x2b, 0x6f, 0x94, 0x0c};
    const uint8_t check[] = {0x48, 0x8b, 0x15, 0xf1, 0x6b, 0x94, 0x0c, 0x48, 0x8b, 0x12};
    const uint8_t modeBytes[] = {0x83, 0xb9, 0x80, 0, 0, 0, 0x06, 0x74, 0x05, 0x83, 0xfa, 0x07, 0x74, 0x2b};
    const uint8_t lampBtnBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c,
                                    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};

    const uint8_t inputBytes[] = {0x4c, 0x63, 0xc2, 0x49, 0x81, 0xf8, 0xa2, 0, 0, 0, 0x73, 0x1a};
    const uint8_t phaseBytes[] = {0x39, 0x51, 0x10, 0x74, 0x08, 0x89, 0x51, 0x10};
    if (nt->OptionalHeader.SizeOfImage != 0x23e73000 ||
        !ValidateHookBytes(base, 0x2aafe0, inputBytes, sizeof(inputBytes), "mm_arcade_led/inputBytes") ||
        !ValidateHookBytes(base, 0x268540, phaseBytes, sizeof(phaseBytes), "mm_arcade_led/phaseBytes") ||
        !ValidateHookBytes(base, 0x5a4af0, reg, sizeof(reg), "mm_arcade_led/reg") ||
        !ValidateHookBytes(base, 0x2c96c0, task, sizeof(task), "mm_arcade_led/task") ||
        !ValidateHookBytes(base, 0x2c9a00, check, sizeof(check), "mm_arcade_led/check") ||
        !ValidateHookBytes(base, 0x5a4aa0, modeBytes, sizeof(modeBytes), "mm_arcade_led/modeBytes") ||
        !ValidateHookBytes(base, 0x42f490, lampBtnBytes, sizeof(lampBtnBytes), "mm_arcade_led/lampBtnBytes"))
    {
        Log("Arcade LED: unsupported signatures; no restoration installed");
        return false;
    }

    settings = config;
    readHardwareLamps = std::move(hardwareLampReader);
    originalRegister = reinterpret_cast<RegisterSlider>(base + 0x5a4af0);
    registerTask = reinterpret_cast<RegisterTask>(base + 0x2c96c0);
    isRegistered = reinterpret_cast<Registered>(base + 0x2c9a00);
    setSliderMode = reinterpret_cast<SliderMode>(base + 0x5a4aa0);
    originalLampButtons = reinterpret_cast<LampButtons>(base + 0x42f490);

    getInputState = reinterpret_cast<GetInputStateFn>(base + 0x2ac970);
    isButtonDown = reinterpret_cast<IsButtonDownFn>(base + 0x2aafe0);
    frameCounter = reinterpret_cast<FrameCounter>(base + 0x2b7670);

    if (!lampButtonsBridge.Create(reinterpret_cast<void *>(LampButtonsHook)))
    {
        return false;
    }
    LONG e = DetourTransactionBegin();
    if (e)
    {
        Log("Arcade LED: attach transaction unavailable %ld", e);
        return false;
    }
    if (!e)
    {
        e = DetourUpdateThread(GetCurrentThread());
    }
    if (!e)
    {
        e = DetourAttach(reinterpret_cast<void **>(&originalRegister),
                         reinterpret_cast<void *>(RegisterHook));
    }
    if (!e)
    {
        e = DetourAttach(reinterpret_cast<void **>(&originalLampButtons), lampButtonsBridge.Entry());
    }

    if (!e)
    {
        e = DetourTransactionCommit();
    }
    else
    {
        DetourTransactionAbort();
    }

    if (e)
    {
        Log("Arcade LED: attach failed %ld", e);
        return false;
    }

    installed = true;
    Log("Arcade LED: restoration installed (enabled=%d)", settings.enabled);
    return true;
}

void ShutdownArcadeLed()
{
    if (!installed)
    {
        return;
    }
    LONG e = DetourTransactionBegin();
    if (e)
    {
        Log("Arcade LED: detach transaction unavailable %ld", e);
        return;
    }
    if (!e)
    {
        e = DetourUpdateThread(GetCurrentThread());
    }
    if (!e)
    {
        e = DetourDetach(reinterpret_cast<void **>(&originalRegister),
                         reinterpret_cast<void *>(RegisterHook));
    }
    if (!e)
    {
        e = DetourDetach(reinterpret_cast<void **>(&originalLampButtons), lampButtonsBridge.Entry());
    }

    if (!e)
    {
        e = DetourTransactionCommit();
    }
    else
    {
        DetourTransactionAbort();
    }

    if (!e)
    {
        installed = false;
        readHardwareLamps = {};
    }
    else
    {
        Log("Arcade LED: detach failed %ld", e);
    }
}

} // namespace anyslider