#include "pch.h"

#include "mm_io_config.h"

#include "Dependencies/toml.hpp"
#include "anyslider_log.h"
#include "mm_io/shared_memory.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace anyslider
{
namespace
{
std::string Trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c)
    {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c)
    {
        return std::isspace(c) != 0;
    }).base();
    return first >= last ? std::string{} : std::string(first, last);
}

std::string ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool TryParseInputKey(const std::string& text, int& virtualKey)
{
    const std::string key = Trim(text);
    if (key.size() == 1)
    {
        virtualKey = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(key[0])));
        return true;
    }

    const std::string normalized = ToUpper(key);
    if (normalized.starts_with("IO3_")) {
        static const std::unordered_map<std::string, uint16_t> switches = {
            {"IO3_TEST", jvs_switch::Test}, {"IO3_SERVICE", jvs_switch::Service},
            {"IO3_START", jvs_switch::Start}, {"IO3_TRIANGLE", jvs_switch::Triangle},
            {"IO3_SQUARE", jvs_switch::Square}, {"IO3_CROSS", jvs_switch::Cross},
            {"IO3_CIRCLE", jvs_switch::Circle}
        };
        const auto found = switches.find(normalized);
        if (found == switches.end()) return false;
        virtualKey = MmIoIo3KeyBase + std::countr_zero(found->second);
        return true;
    }
    static const std::unordered_map<std::string, int> namedKeys = {
        { "ENTER", VK_RETURN }, { "RETURN", VK_RETURN }, { "TAB", VK_TAB },
        { "BACK", VK_BACK }, { "BACKSPACE", VK_BACK }, { "SPACE", VK_SPACE },
        { "SPACEBAR", VK_SPACE }, { "ESC", VK_ESCAPE }, { "ESCAPE", VK_ESCAPE },
        { "UP", VK_UP }, { "DOWN", VK_DOWN }, { "LEFT", VK_LEFT }, { "RIGHT", VK_RIGHT },
        { "LEFTSHIFT", VK_LSHIFT }, { "LSHIFT", VK_LSHIFT },
        { "RIGHTSHIFT", VK_RSHIFT }, { "RSHIFT", VK_RSHIFT },
        { "LEFTCONTROL", VK_LCONTROL }, { "LCONTROL", VK_LCONTROL }, { "LCTRL", VK_LCONTROL },
        { "RIGHTCONTROL", VK_RCONTROL }, { "RCONTROL", VK_RCONTROL }, { "RCTRL", VK_RCONTROL },
        { "LEFTALT", VK_LMENU }, { "LALT", VK_LMENU },
        { "RIGHTALT", VK_RMENU }, { "RALT", VK_RMENU },
        { "INSERT", VK_INSERT }, { "INS", VK_INSERT }, { "DELETE", VK_DELETE }, { "DEL", VK_DELETE },
        { "HOME", VK_HOME }, { "END", VK_END }, { "PAGEUP", VK_PRIOR }, { "PAGEDOWN", VK_NEXT },
        { "COMMA", VK_OEM_COMMA }, { "PERIOD", VK_OEM_PERIOD }, { "SLASH", VK_OEM_2 },
        { "BACKSLASH", VK_OEM_5 }, { "SEMICOLON", VK_OEM_1 }, { "APOSTROPHE", VK_OEM_7 },
        { "LEFTBRACKET", VK_OEM_4 }, { "RIGHTBRACKET", VK_OEM_6 },
        { "PLUS", VK_ADD }, { "MINUS", VK_SUBTRACT }, { "MULTIPLY", VK_MULTIPLY }, { "DIVIDE", VK_DIVIDE },
        { "MOUSELEFT", VK_LBUTTON }, { "MOUSEMIDDLE", VK_MBUTTON }, { "MOUSERIGHT", VK_RBUTTON },
        { "MOUSEX1", VK_XBUTTON1 }, { "MOUSEX2", VK_XBUTTON2 },
    };
    if (const auto it = namedKeys.find(normalized); it != namedKeys.end())
    {
        virtualKey = it->second;
        return true;
    }

    if (normalized.size() == 2 && normalized[0] == 'F' && normalized[1] >= '1' && normalized[1] <= '9')
    {
        virtualKey = VK_F1 + normalized[1] - '1';
        return true;
    }
    if (normalized.size() == 3 && normalized[0] == 'F' && normalized[1] >= '1' && normalized[1] <= '2' &&
        normalized[2] >= '0' && normalized[2] <= '9')
    {
        const int number = (normalized[1] - '0') * 10 + normalized[2] - '0';
        if (number <= 24)
        {
            virtualKey = VK_F1 + number - 1;
            return true;
        }
    }
    if (normalized.size() == 7 && normalized.starts_with("NUMPAD") && normalized[6] >= '0' && normalized[6] <= '9')
    {
        virtualKey = VK_NUMPAD0 + normalized[6] - '0';
        return true;
    }
    if (normalized.size() == 4 && normalized.starts_with("0X"))
    {
        unsigned int value = 0;
        const auto [end, error] = std::from_chars(
            normalized.data() + 2,
            normalized.data() + normalized.size(),
            value,
            16);
        if (error == std::errc{} && end == normalized.data() + normalized.size() && value <= 0xFF)
        {
            virtualKey = static_cast<int>(value);
            return true;
        }
    }
    return false;
}

void AppendDelimitedKeys(const std::string& text, MmIoKeyBinding& binding, const char* configKey)
{
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find(',', start);
        const std::string key = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!Trim(key).empty())
        {
            int virtualKey = 0;
            if (TryParseInputKey(key, virtualKey))
            {
                binding.push_back(virtualKey);
            }
            else
            {
                Log("Invalid input binding %s=%s", configKey, key.c_str());
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
}

void LoadInputBinding(const toml::table& document, const char* configKey, MmIoKeyBinding& binding)
{
    const auto node = document[configKey];
    if (!node)
    {
        return;
    }

    MmIoKeyBinding parsed;
    if (const toml::array* values = node.as_array())
    {
        for (const toml::node& value : *values)
        {
            if (const auto key = value.value<std::string>())
            {
                AppendDelimitedKeys(*key, parsed, configKey);
            }
            else
            {
                Log("Invalid input binding array entry for %s", configKey);
            }
        }
    }
    else if (const auto value = node.value<std::string>())
    {
        AppendDelimitedKeys(*value, parsed, configKey);
    }
    else
    {
        Log("Invalid input binding value for %s", configKey);
        return;
    }
    binding = std::move(parsed);
}

bool TryParseMouseAxis(const std::string& text, MmIoMouseAxis& axis)
{
    const std::string normalized = ToUpper(Trim(text));
    if (normalized == "X") { axis = MmIoMouseAxis::X; return true; }
    if (normalized == "Y") { axis = MmIoMouseAxis::Y; return true; }
    if (normalized == "WHEEL") { axis = MmIoMouseAxis::Wheel; return true; }
    return false;
}

bool TryParseSliderMode(const std::string& text, MmIoSliderMode& mode)
{
    const std::string normalized = ToUpper(Trim(text));
    if (normalized == "ARCADE")
    {
        mode = MmIoSliderMode::Arcade;
        return true;
    }
    if (normalized == "JOYSTICK")
    {
        mode = MmIoSliderMode::Joystick;
        return true;
    }
    return false;
}

bool IsHexWord(std::string_view text)
{
    if (text.size() != 4)
    {
        return false;
    }
    unsigned int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return error == std::errc{} && end == text.data() + text.size();
}

std::string NormalizeMouseSliderDeviceFilter(const std::string& value)
{
    const std::string normalized = ToUpper(Trim(value));
    if (normalized.size() == 9 && normalized[4] == ':' &&
        IsHexWord(std::string_view(normalized).substr(0, 4)) &&
        IsHexWord(std::string_view(normalized).substr(5, 4)))
    {
        return "VID_" + normalized.substr(0, 4) + "&PID_" + normalized.substr(5, 4);
    }
    return value;
}

float LoadMouseSensitivity(const toml::table& document, const char* key)
{
    const float value = document[key].value_or(1.0f);
    if (value < 0.01f || value > 100.0f)
    {
        Log("Invalid %s; using 1.0.", key);
        return 1.0f;
    }
    return value;
}

// 加载鼠标方向模拟slider的配置
void LoadMouseSliderConfig(const toml::table& document, MmIoConfig& config)
{
    auto& mouse = config.mouse_slider;
    // 使用鼠标输入来作为滑条使用
    mouse.enabled = document["io_use_mouse_as_slider"].value_or(false);
    // 绑定的鼠标移动轴，一般分配到"x"或"y"，也可以是"wheel"滚轮
    // 注：slider_1是左半边，slider_2是右半边的slider区域
    const std::string axis1 = document["io_mouse_slider_1_axis"].value_or(std::string("x"));
    const std::string axis2 = document["io_mouse_slider_2_axis"].value_or(std::string("y"));
    if (!TryParseMouseAxis(axis1, mouse.slider_1.axis))
    {
        Log("Invalid io_mouse_slider_1_axis=%s; using x.", axis1.c_str());
        mouse.slider_1.axis = MmIoMouseAxis::X;
    }
    if (!TryParseMouseAxis(axis2, mouse.slider_2.axis))
    {
        Log("Invalid io_mouse_slider_2_axis=%s; using y.", axis2.c_str());
        mouse.slider_2.axis = MmIoMouseAxis::Y;
    }
    // 是否开启方向反转
    mouse.slider_1.invert = document["io_mouse_slider_1_invert"].value_or(false);
    mouse.slider_2.invert = document["io_mouse_slider_2_invert"].value_or(false);
    // 鼠标灵敏度
    mouse.slider_1.sensitivity = LoadMouseSensitivity(document, "io_mouse_slider_1_sensitivity");
    mouse.slider_2.sensitivity = LoadMouseSensitivity(document, "io_mouse_slider_2_sensitivity");

    mouse.counts_per_cycle = document["io_mouse_slider_counts_per_cycle"].value_or(256.0f);
    if (mouse.counts_per_cycle < 1.0f || mouse.counts_per_cycle > 100000.0f)
    {
        Log("Invalid io_mouse_slider_counts_per_cycle; using 256.");
        mouse.counts_per_cycle = 256.0f;
    }
    constexpr uint32_t kMaxMouseSliderTouchHoldMs = 5000;
    // 停止移动多久后，当作松开触摸
    mouse.touch_hold_ms = document["io_mouse_slider_touch_hold_ms"].value_or<uint32_t>(20);
    if (mouse.touch_hold_ms > kMaxMouseSliderTouchHoldMs)
    {
        Log("Invalid io_mouse_slider_touch_hold_ms=%u; using 20.", mouse.touch_hold_ms);
        mouse.touch_hold_ms = 20;
    }
    // 指定鼠标设备的VID PID，留空就是所有鼠标
    const std::string device = document["io_mouse_slider_device"].value_or(std::string{});
    const std::string device_filter = NormalizeMouseSliderDeviceFilter(device);
    mouse.device_filter.assign(device_filter.begin(), device_filter.end());
    if (mouse.enabled && !config.keyboard_mouse_frontend)
    {
        Log("Mouse slider is enabled but io_keyboard_mouse_frontend is disabled; ignoring mouse slider input.");
    }
}

// 手柄IO配置
void LoadGamepadConfig(const toml::table& document, MmIoConfig& config)
{
    auto& gamepad = config.gamepad;
    // 开启手柄输入前端
    gamepad.enabled = document["io_gamepad_frontend"].value_or(false);
    // 指定手柄设备
    gamepad.device = document["io_gamepad_device"].value_or(std::string("auto"));
    // 使用DualShock4和DualSense的触摸板来当作Slider
    gamepad.touchpad_slider = document["io_gamepad_touchpad_slider"].value_or(true);
    // 反转触摸Slider方向
    gamepad.touchpad_invert = document["io_gamepad_touchpad_invert"].value_or(false);
    // 摇杆死区设置
    gamepad.stick_slider_deadzone = document["io_gamepad_stick_slider_deadzone"].value_or(0.5f);
    if (gamepad.stick_slider_deadzone < 0.1f || gamepad.stick_slider_deadzone > 0.95f)
    {
        Log("Invalid io_gamepad_stick_slider_deadzone; using 0.5.");
        gamepad.stick_slider_deadzone = 0.5f;
    }
}

void LoadInputBindings(const toml::table& document, MmIoInputBindings& bindings)
{
    LoadInputBinding(document, "io_key_test", bindings.test);
    LoadInputBinding(document, "io_key_service", bindings.service);
    LoadInputBinding(document, "io_key_pause", bindings.pause);
    LoadInputBinding(document, "io_key_start", bindings.start);
    LoadInputBinding(document, "io_key_dpad_up", bindings.dpad_up);
    LoadInputBinding(document, "io_key_dpad_down", bindings.dpad_down);
    LoadInputBinding(document, "io_key_dpad_left", bindings.dpad_left);
    LoadInputBinding(document, "io_key_dpad_right", bindings.dpad_right);
    LoadInputBinding(document, "io_key_triangle", bindings.triangle);
    LoadInputBinding(document, "io_key_square", bindings.square);
    LoadInputBinding(document, "io_key_cross", bindings.cross);
    LoadInputBinding(document, "io_key_circle", bindings.circle);
    LoadInputBinding(document, "io_key_l1", bindings.l1);
    LoadInputBinding(document, "io_key_r1", bindings.r1);
    LoadInputBinding(document, "io_key_l2", bindings.l2);
    LoadInputBinding(document, "io_key_r2", bindings.r2);
    LoadInputBinding(document, "io_key_select", bindings.select);
    LoadInputBinding(document, "io_key_l3", bindings.l3);
    LoadInputBinding(document, "io_key_r3", bindings.r3);
    LoadInputBinding(document, "io_key_slider_1_left", bindings.slider_1_left);
    LoadInputBinding(document, "io_key_slider_1_right", bindings.slider_1_right);
    LoadInputBinding(document, "io_key_slider_2_left", bindings.slider_2_left);
    LoadInputBinding(document, "io_key_slider_2_right", bindings.slider_2_right);

    for (size_t index = 0; index < bindings.slider_cells.size(); ++index)
    {
        char configKey[32];
        sprintf_s(configKey, "io_key_slider_%02zu", index + 1);
        LoadInputBinding(document, configKey, bindings.slider_cells[index]);
    }
}


}

bool LoadMmIoConfig(MmIoConfig& config)
{
    try
    {
        const toml::table document = toml::parse_file("config.toml");
        // 调试开关
        SetDebugLoggingEnabled(document["debug"].value_or(false));
        // IO总开关
        config.enabled = document["io_enabled"].value_or(false);

        // ====
        // 通用IO功能
        // ====

        // 共享内存输入名
        const std::string mappingName = document["io_shared_memory"]
            .value_or(std::string("Local\\MMIO_SHARED_BUFFER"));
        config.shared_memory_name.assign(mappingName.begin(), mappingName.end());
        if (config.shared_memory_name.empty())
        {
            config.shared_memory_name = mmio::kSharedMemoryName;
        }
        // 最大IO超时时间（超时就会释放当前输入）
        config.max_input_lease_ms = document["io_max_input_lease_ms"].value_or<uint64_t>(500);
        if (config.max_input_lease_ms == 0 || config.max_input_lease_ms > 10'000)
        {
            Log("Invalid io_max_input_lease_ms; using 500 ms.");
            config.max_input_lease_ms = 500;
        }
        // 独占游戏输入（开启后屏蔽游戏原本的输入逻辑，只由mod提供输入）
        config.exclusive_controller_input = document["io_exclusive_controller_input"].value_or(false);
        // 让游戏不在前台时保持运行
        config.keep_game_active_unfocused = document["io_keep_game_active_unfocused"].value_or(false);

        // ====
        // 基础IO配置
        // ====

        // IO模拟的设备类型
        config.controller_type = document["io_controller_type"].value_or(-1);
        if (config.controller_type < -1 || config.controller_type > 10)
        {
            Log("Invalid io_controller_type; using -1 (auto).");
            config.controller_type = -1;
        }
        // 滑条模式
        const std::string sliderMode = document["io_slider_mode"].value_or(std::string("arcade"));
        if (!TryParseSliderMode(sliderMode, config.slider_mode))
        {
            Log("Invalid io_slider_mode=%s; using arcade.", sliderMode.c_str());
            config.slider_mode = MmIoSliderMode::Arcade;
        }
        // Arcade 模式下滑条模拟速度，单位为 cell/秒。
        // 只对左右输入有效，对直接 32 - cell 输入和 mouse_as_slider 模式无效
        config.arcade_slider_emu_cells_per_second =
            document["io_arcade_slider_emu_cells_per_second"].value_or(32.0f);
        if (config.arcade_slider_emu_cells_per_second <= 0.0f ||
            config.arcade_slider_emu_cells_per_second > 64.0f)
        {
            Log("Invalid io_arcade_slider_emu_cells_per_second; using 32.");
            config.arcade_slider_emu_cells_per_second = 32.0f;
        }
        // 使用O来作为确认按键
        config.use_ps_circle_confirm = document["io_use_ps_circle_confirm"].value_or(false);
        config.swap_ox_sprite = document["io_swap_ox_sprite"].value_or(true);
        // 在手柄接入时保持鼠标输入
        config.show_mouse_with_controller = document["io_show_mouse_with_controller"].value_or(true);

        // ====
        // 键鼠IO配置
        // ====

        // 开启键盘鼠标输入前端
        config.keyboard_mouse_frontend = document["io_keyboard_mouse_frontend"].value_or(false);
        // 屏蔽游戏自身的键盘输入
        config.block_keyboard_input = document["io_block_keyboard_input"].value_or(false);
        // 屏蔽游戏自身的鼠标输入
        config.block_mouse_input = document["io_block_mouse_input"].value_or(false);
        // 鼠标滑条设置
        LoadMouseSliderConfig(document, config);

        // ====
        // 手柄IO配置
        // ====

        LoadGamepadConfig(document, config);

        // ====
        // 街机IO配置
        // 分为IO3(+AMEX)、Slider、触屏三个部分
        // ====

        // 开启街机IO3输入（需要AMEX板）
        config.io3.enabled = document["io_io3_backend"].value_or(false);
        // Polling速率
        config.io3.poll_interval_ms = document["io_io3_poll_interval_ms"].value_or<uint32_t>(4);
        // 超时时间
        config.io3.timeout_ms = document["io_io3_timeout_ms"].value_or<uint32_t>(1000);
        if (config.io3.poll_interval_ms < 1 || config.io3.poll_interval_ms > 100)
        {
            Log("Invalid io_io3_poll_interval_ms; using 4.");
            config.io3.poll_interval_ms = 4;
        }
        if (config.io3.timeout_ms < 10 || config.io3.timeout_ms > 10000)
        {
            Log("Invalid io_io3_timeout_ms; using 1000.");
            config.io3.timeout_ms = 1000;
        }
        // 街机灯效跟随实际按键位置，而不是绑定位置（默认开启）
        config.io3.led_use_hardware_position = document["io_io3_led_use_hardware_position"].value_or(true);

        // 开启街机滑条
        config.arcade_slider.enabled = document["io_arcade_slider_backend"].value_or(false);
        // 街机滑条端口，默认COM11
        config.arcade_slider.port = document["io_arcade_slider_port"].value_or(std::string("COM11"));
        // 街机滑条连接前发送DTR-RTS
        config.arcade_slider.assert_dtr_rts = document["io_arcade_slider_dtr_rts"].value_or(true);


        // 开启街机触屏连接
        config.touchscreen.enabled = document["io_arcade_touchscreen_backend"].value_or(false);
        // 街机触屏端口，默认COM1
        config.touchscreen.port = document["io_arcade_touchscreen_port"].value_or(std::string("COM1"));
        // 手动兜底校准：仅EEPROM校准不可用时生效，默认双轴取反
        config.touchscreen.invert_x = document["io_arcade_touchscreen_invert_x"].value_or(true);
        config.touchscreen.invert_y = document["io_arcade_touchscreen_invert_y"].value_or(true);
        // 街机触屏交换XY轴，不需要打开
        config.touchscreen.swap_xy = document["io_arcade_touchscreen_swap_xy"].value_or(false);
        // 街机触屏兜底用的原始坐标范围，默认读取EEPROM校准
        config.touchscreen.min_x = document["io_arcade_touchscreen_min_x"].value_or(0);
        config.touchscreen.max_x = document["io_arcade_touchscreen_max_x"].value_or(4095);
        config.touchscreen.min_y = document["io_arcade_touchscreen_min_y"].value_or(0);
        config.touchscreen.max_y = document["io_arcade_touchscreen_max_y"].value_or(4095);
        if (config.touchscreen.min_x < 0 || config.touchscreen.max_x > 4095 ||
            config.touchscreen.min_y < 0 || config.touchscreen.max_y > 4095 ||
            config.touchscreen.min_x >= config.touchscreen.max_x ||
            config.touchscreen.min_y >= config.touchscreen.max_y)
        {
            Log("Touchscreen: invalid calibration bounds; disabling backend");
            config.touchscreen.enabled = false;
        }

        // 控制街机灯效跟随场景生成
        config.arcade_led.enabled = document["io_arcade_led"].value_or(true);
        // 滑条LED歌曲加载百分比动画
        config.arcade_led.loading_progress = document["io_arcade_led_loading_progress"].value_or(false);

        // 可选原生测试菜单；只在启动时安装，不影响设备采样。
        config.enable_test_mode = document["io_enable_test_mode"].value_or(false);

        // 测试功能
        // LED诊断
        config.led_diagnostics = document["io_led_diagnostics"].value_or(false);
        // 输入诊断
        config.input_diagnostics = document["io_input_diagnostics"].value_or(false) && IsDebugLoggingEnabled();
        if (document["io_input_diagnostics"].value_or(false) && !IsDebugLoggingEnabled())
            Log("io_input_diagnostics ignored: requires debug=true.");
        config.input_diagnostics_arcade = document["io_input_diagnostics_arcade"].value_or(-1);
        if (config.input_diagnostics_arcade < -1 || config.input_diagnostics_arcade > 1)
        {
            Log("Invalid diagnostic arcade override; using -1 (normal).");
            config.input_diagnostics_arcade = -1;
        }

        // 输入按键绑定
        config.input_bindings = DefaultMmIoInputBindings();
        LoadInputBindings(document, config.input_bindings);

        return true;
    }
    catch (const std::exception& exception)
    {
        Log("Could not read MMIO configuration: %s", exception.what());
        return false;
    }
}
}
