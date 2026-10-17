#include "windows_input.h"
#include "test_window.h"
#include "elo_emulator.h"
#include "calibration_session.h"
#include "display_mapping.h"
#include <charconv>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

#include "src/mm_io_touchscreen.h"
#include "src/arcade_eeprom.h"
#include "src/arcade_eeprom_write.h"
#include "src/touchscreen_mapping.h"
#include "src/anyslider_log.h"

namespace
{

std::atomic_bool g_running{true};

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_running.store(false, std::memory_order_release);
        return TRUE;
    default:
        return FALSE;
    }
}

void PrintUsage(const char *exeName)
{
    std::printf(
        "TouchscreenConnect - Standalone Elo SmartSet Touchscreen Host\n\n"
        "Usage:\n"
        "  %s [options]\n\n"
        "  No arguments: --port COM1 --output mouse --calibration eeprom --arm\n"
        "  With arguments: defaults below apply.\n\n"
        "Options:\n"
        "  --port <COMx>          Serial port of Elo touchscreen (default: COM1)\n"
        "  --output <mode>        Output: console | mouse | touch (default: console)\n"
        "  --calibration <mode>   Calibration: raw | eeprom | file:<path> (default: raw)\n"
        "  --monitor <index>      Select display (1=primary); --list-monitors lists bounds\n"
        "  --arm                 Explicitly enable desktop injection; stop hotkey printed at startup\n"
        "  --simulate tap-drag   Finite no-device test; injection targets its own window\n"
        "  --update-image <file> Update offline EEPROM image; requires file calibration and --backup <new-file>\n"
        "  --calibrate <file>    Five-point selected-screen calibration; file only\n"
        "  --emulate-elo COMx    Virtual Elo peer on user-configured COM pair\n"
        "  --help, -h             Display this help message and exit\n\n"
        "Notes:\n"
        "  - raw mode: Prints uncalibrated raw coordinates and direct [0, 1] normalization.\n"
        "  - eeprom mode: Reads motherboard EEPROM CalibData (read-only) with fallback to raw.\n"
        "  - Default output is read-only console logging without Windows input injection.\n"
        "  - Press Ctrl+C to safely disconnect and stop.\n",
        exeName);
}

bool IsValidComPort(std::string_view port)
{
    if (port.size() < 4 || port.substr(0, 3) != "COM")
    {
        return false;
    }
    for (size_t i = 3; i < port.size(); ++i)
    {
        if (port[i] < '0' || port[i] > '9')
        {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    std::string port = "COM1";
    // Double-click preset; explicit command-line sessions retain diagnostic defaults.
    const bool doubleClickPreset = argc == 1;
    std::string outputMode = doubleClickPreset ? "mouse" : "console";
    std::string calibrationMode = doubleClickPreset ? "eeprom" : "raw";
    unsigned monitorIndex = 1;
    bool listMonitors = false;
    bool armed = doubleClickPreset;
    bool simulate = false;
    bool portExplicit = false;
    std::string emulatorPort;
    std::string calibrationOutput;
    std::string imagePath, backupPath;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            // Must not access device or EEPROM on help
            PrintUsage(argv[0] ? argv[0] : "TouchscreenConnect.exe");
            return 0;
        }
        else if (arg == "--list-monitors") listMonitors = true;
        else if (arg == "--monitor")
        {
            if (++i >= argc) return 1;
            std::string_view value=argv[i];
            auto result=std::from_chars(value.data(),value.data()+value.size(),monitorIndex);
            if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || monitorIndex==0)
            {std::fprintf(stderr,"Invalid monitor index\n");return 1;}
        }
        else if (arg == "--arm") armed = true;
        else if (arg == "--simulate")
        {
            if (++i >= argc || std::string_view(argv[i]) != "tap-drag")
            {
                std::fprintf(stderr, "Error: --simulate requires tap-drag\n");
                return 1;
            }
            simulate = true;
        }
        else if (arg == "--update-image" || arg == "--backup")
        {
            if (++i >= argc) return 1;
            (arg == "--update-image" ? imagePath : backupPath) = argv[i];
        }
        else if (arg == "--calibrate")
        {
            if (++i >= argc) return 1;
            calibrationOutput = argv[i];
        }
        else if (arg == "--emulate-elo")
        {
            if (++i >= argc || !IsValidComPort(argv[i])) return 1;
            emulatorPort = argv[i];
        }
        else if (arg == "--port")
        {
            if (++i >= argc)
            {
                std::fprintf(stderr, "Error: --port requires an argument (e.g. COM1)\n");
                return 1;
            }
            port = argv[i];
            portExplicit = true;
            if (!IsValidComPort(port))
            {
                std::fprintf(stderr, "Error: invalid COM port name '%s' (expected COM1, COM2, ...)\n", port.c_str());
                return 1;
            }
        }
        else if (arg == "--output")
        {
            if (++i >= argc)
            {
                std::fprintf(stderr, "Error: --output requires an argument (e.g. console)\n");
                return 1;
            }
            outputMode = argv[i];
            if (outputMode != "console" && outputMode != "mouse" && outputMode != "touch")
            {
                std::fprintf(stderr, "Error: unsupported output mode '%s'. Choose console, mouse or touch.\n",
                             outputMode.c_str());
                return 1;
            }
        }
        else if (arg == "--calibration")
        {
            if (++i >= argc)
            {
                std::fprintf(stderr, "Error: --calibration requires an argument (raw | eeprom)\n");
                return 1;
            }
            calibrationMode = argv[i];
            if (calibrationMode != "raw" && calibrationMode != "eeprom" && !calibrationMode.starts_with("file:"))
            {
                std::fprintf(stderr, "Error: unsupported calibration mode '%s'. Choose 'raw' or 'eeprom'.\n",
                             calibrationMode.c_str());
                return 1;
            }
        }
        else
        {
            std::fprintf(stderr, "Error: unrecognized option '%s'. Use --help for usage.\n", argv[i]);
            return 1;
        }
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto displays=touchscreen_connect::Displays();
    if(listMonitors)
    {
        for(size_t i=0;i<displays.size();++i){const auto&r=displays[i].bounds;
            std::printf("%zu: (%ld,%ld)-(%ld,%ld)%s\n",i+1,r.left,r.top,r.right,r.bottom,displays[i].primary?" primary":"");}
        return displays.empty()?1:0;
    }
    if(monitorIndex>displays.size()) {std::fprintf(stderr,"Monitor unavailable\n");return 1;}
    RECT selectedDisplay=displays[monitorIndex-1].bounds;

    if ((simulate && portExplicit) || (!emulatorPort.empty() && (simulate || portExplicit || outputMode != "console" || calibrationMode != "raw")))
    { std::fprintf(stderr, "Conflicting input modes\n"); return 1; }
    if (!imagePath.empty())
    {
        if (!calibrationMode.starts_with("file:") || backupPath.empty() || simulate ||
            !emulatorPort.empty() || !calibrationOutput.empty() || portExplicit || outputMode != "console")
        { std::fprintf(stderr, "Image update requires --calibration file:<path> --backup <new-path>, no device modes\n"); return 1; }
        anyslider::arcade_eeprom::TouchCalibration calib{};
        if (!anyslider::arcade_eeprom::LoadCalibrationFromFile(calibrationMode.substr(5), calib)) return 1;
        std::string error;
        if (!anyslider::arcade_eeprom::UpdateEepromImageFile(imagePath, calib, &error, &backupPath))
        { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        std::puts("Offline CalibData updated and read back. No hardware access.");
        return 0;
    }
    if (!backupPath.empty()) { std::fprintf(stderr, "--backup requires --update-image\n"); return 1; }
    if (!calibrationOutput.empty())
    {
        if (simulate || !emulatorPort.empty() || outputMode != "console" || calibrationMode != "raw")
        { std::fprintf(stderr, "Calibration cannot be combined with simulation/injection/import\n"); return 1; }
        if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) return 1;
        return touchscreen_connect::RunCalibration(port, calibrationOutput, g_running, selectedDisplay);
    }
    if (!emulatorPort.empty())
    {
        if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) return 1;
        return touchscreen_connect::RunEloEmulator(emulatorPort, g_running);
    }
    if (outputMode != "console" && !armed)
    {
        std::fprintf(stderr, "Error: desktop input requires --arm; stop hotkey printed at startup injection.\n");
        return 1;
    }
    HWND testWindow = nullptr;
    if (simulate && outputMode != "console")
    {
        testWindow = touchscreen_connect::CreateInputTestWindow();
        if (!testWindow) { std::fprintf(stderr, "Cannot create test window\n"); return 1; }
    }
    if (outputMode != "console")
    {
        bool registered = false;
        DWORD hotkeyError = ERROR_SUCCESS;
        for (UINT key = VK_F1; key <= VK_F12; ++key)
        {
            if (RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, key))
            {
                std::printf("Emergency stop: Ctrl+Alt+F%u\n", key - VK_F1 + 1);
                registered = true;
                break;
            }
            hotkeyError = GetLastError();
        }
        if (!registered)
        {
            std::fprintf(stderr, "Warning: no global stop hotkey available (Win32=%lu). "
                "Continuing without one; focus this console and press Ctrl+C to stop.\n", hotkeyError);
        }
    }
    touchscreen_connect::WindowsInput input;
    auto mode = outputMode == "mouse" ? touchscreen_connect::OutputMode::Mouse :
        outputMode == "touch" ? touchscreen_connect::OutputMode::Touch : touchscreen_connect::OutputMode::Console;
    if (!input.Initialize(mode))
    {
        std::fprintf(stderr, "Error: input initialization failed (%lu)\n", input.LastError());
        return 1;
    }

    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
    {
        std::fprintf(stderr, "Warning: failed to register console control handler (%lu)\n", GetLastError());
    }

    std::printf("[TouchscreenConnect] Initializing host...\n");
    std::printf("  Port:        %s\n", port.c_str());
    std::printf("  Output:      %s\n", outputMode.c_str());
    std::printf("  Calibration: %s\n", calibrationMode.c_str());

    anyslider::arcade_eeprom::TouchCalibration eepromCalib{};
    if (calibrationMode == "eeprom")
    {
        std::printf("[TouchscreenConnect] Probing motherboard EEPROM for AFT calibration...\n");
        if (anyslider::arcade_eeprom::ReadTouchCalibration(eepromCalib) && eepromCalib.valid)
        {
            std::printf("[TouchscreenConnect] EEPROM calibration applied successfully:\n");
            std::printf("  X control points: [%d->%d, %d->%d, %d->%d, %d->%d, %d->%d]\n",
                        eepromCalib.x[0].raw, eepromCalib.x[0].target,
                        eepromCalib.x[1].raw, eepromCalib.x[1].target,
                        eepromCalib.x[2].raw, eepromCalib.x[2].target,
                        eepromCalib.x[3].raw, eepromCalib.x[3].target,
                        eepromCalib.x[4].raw, eepromCalib.x[4].target);
            std::printf("  Y control points: [%d->%d, %d->%d, %d->%d, %d->%d, %d->%d]\n",
                        eepromCalib.y[0].raw, eepromCalib.y[0].target,
                        eepromCalib.y[1].raw, eepromCalib.y[1].target,
                        eepromCalib.y[2].raw, eepromCalib.y[2].target,
                        eepromCalib.y[3].raw, eepromCalib.y[3].target,
                        eepromCalib.y[4].raw, eepromCalib.y[4].target);
        }
        else
        {
            std::printf("[TouchscreenConnect] Motherboard EEPROM calibration unavailable; falling back to manual/raw calibration.\n");
        }
    }

    if (calibrationMode.starts_with("file:") &&
        !anyslider::arcade_eeprom::LoadCalibrationFromFile(calibrationMode.substr(5), eepromCalib))
    { std::fprintf(stderr, "Invalid calibration file\n"); return 1; }

    anyslider::MmIoTouchscreenConfig config{};
    config.enabled = true;
    config.port = port;
    config.invert_x = false;
    config.invert_y = false;
    config.swap_xy = false;
    config.min_x = 0;
    config.max_x = 4095;
    config.min_y = 0;
    config.max_y = 4095;

    anyslider::MmIoTouchscreenFrontend frontend;
    if (!simulate) frontend.Initialize(config);

    std::printf("[TouchscreenConnect] Frontend started on %s. Waiting for touch reports (Ctrl+C to quit)...\n",
                port.c_str());

    bool wasDown = false;
    bool injectionFailed = false;
    unsigned simulationStep = 0;
    auto nextSimulation = std::chrono::steady_clock::now();
    auto lastDeviceEvent = nextSimulation;
    auto nextRefresh = nextSimulation;
    auto consume = [&](anyslider::elo::Event &event)
    {
        if (!simulate) return frontend.Consume(event);
        if (simulationStep == 5) { g_running = false; return false; }
        if (std::chrono::steady_clock::now() < nextSimulation) return false;
        const anyslider::elo::Event sequence[] = {{1000,1000,true,true}, {1000,1000,false,false},
            {2000,2000,true,true}, {3000,3000,true,false}, {3000,3000,false,false}};
        event = sequence[simulationStep++];
        nextSimulation = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
        return true;
    };
    while (g_running.load(std::memory_order_acquire))
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_HOTKEY || message.message == WM_QUIT) g_running = false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!g_running) break;
        if (testWindow && GetForegroundWindow() != testWindow)
        {
            input.Release();
            if (simulationStep != 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        anyslider::elo::Event event;
        while (consume(event))
        {
            lastDeviceEvent = std::chrono::steady_clock::now();
            const auto norm = anyslider::MapTouchToNormalized(event, config, eepromCalib);
            const char *type = "STREAM";
            if (!wasDown && event.down)
            {
                type = "DOWN";
            }
            else if (wasDown && !event.down)
            {
                type = "UP";
            }
            wasDown = event.down;

            std::printf("[%s] raw=(%4u, %4u) norm=(%.4f, %.4f) initial=%d down=%d\n",
                        type, event.x, event.y, norm.x, norm.y,
                        event.initial ? 1 : 0, event.down ? 1 : 0);
            std::fflush(stdout);

            POINT pixel=touchscreen_connect::MapDisplay(norm.x,norm.y,selectedDisplay);
            if (testWindow)
            {
                RECT client{};
                if (!GetClientRect(testWindow, &client) || GetForegroundWindow() != testWindow)
                { g_running = false; break; }
                pixel = {LONG(norm.x * (client.right - 1)), LONG(norm.y * (client.bottom - 1))};
                if (!ClientToScreen(testWindow, &pixel)) { g_running = false; break; }
            }
            if (!input.Submit(pixel, event.down))
            {
                std::fprintf(stderr, "Input injection failed: %lu\n", input.LastError());
                injectionFailed = true;
                g_running = false;
                break;
            }
        }
        const auto currentDisplays=touchscreen_connect::Displays();
        if(outputMode != "console" && (monitorIndex>currentDisplays.size() ||
            !touchscreen_connect::SameBounds(selectedDisplay,currentDisplays[monitorIndex-1].bounds)))
        { input.Release(); std::fprintf(stderr,"Display configuration changed; restart to reselect target\n"); injectionFailed=true; break; }
        const auto now = std::chrono::steady_clock::now();
        if (wasDown && now - lastDeviceEvent > std::chrono::seconds(2))
        { input.Release(); wasDown = false; frontend.ResetContact(); }
        if (now >= nextRefresh)
        {
            if (!input.Refresh()) { injectionFailed = true; std::fprintf(stderr, "Touch refresh failed: %lu\n", input.LastError()); break; }
            nextRefresh = now + std::chrono::milliseconds(16);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::printf("\n[TouchscreenConnect] Stop signal received. Shutting down...\n");
    input.Release();
    if (outputMode != "console") UnregisterHotKey(nullptr, 1);
    frontend.Shutdown();
    std::printf("[TouchscreenConnect] Safe shutdown complete.\n");
    return injectionFailed ? 1 : 0;
}
