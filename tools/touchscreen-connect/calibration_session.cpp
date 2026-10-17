#include "calibration_session.h"
#include "test_window.h"
#include "display_mapping.h"
#include "src/mm_io_touchscreen.h"
#include "src/arcade_eeprom.h"
#include <cstdio>
#include <filesystem>
namespace touchscreen_connect {
int RunCalibration(const std::string& port, const std::string& output, const std::atomic_bool& running, RECT display)
{
    namespace e = anyslider::arcade_eeprom;
    if (std::filesystem::exists(output)) { std::fprintf(stderr,"Refusing to overwrite calibration file\n"); return 1; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND window=CreateInputTestWindow(); if(!window) return 1;
    const int width=display.right-display.left, height=display.bottom-display.top;
    if(width<2 || height<2 || width>32768 || height>32768) {DestroyWindow(window);return 1;}
    SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);
    SetWindowPos(window,HWND_TOP,display.left,display.top,width,height,SWP_FRAMECHANGED);
    std::array<e::SamplePoint,5> samples{};
    unsigned index=0; bool awaitUp=false, collected=false, accepted=false, quit=false;
    auto target=e::StandardTargetRatios[0]; SetCalibrationTarget(window,target.x,target.y,0);
    anyslider::MmIoTouchscreenConfig config{};config.enabled=true;config.port=port;
    anyslider::MmIoTouchscreenFrontend frontend;frontend.Initialize(config);
    e::TouchCalibration calibration{};
    std::puts("Calibration captures RAW serial contacts only; mouse clicks do not count. Esc cancels. No EEPROM writes.");
    while(running && !quit)
    {
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))
        {
            if(msg.message==WM_QUIT || (msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE)) quit=true;
            if(collected && msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN && GetForegroundWindow()==window)
            {accepted=true;quit=true;}
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
        if(quit) break;
        if(GetForegroundWindow()!=window)
        { frontend.ResetContact();awaitUp=true;std::this_thread::sleep_for(std::chrono::milliseconds(10));continue; }
        anyslider::elo::Event event;
        while(frontend.Consume(event))
        {
            if(collected)
            {
                if(event.down) {
                    const float x=calibration.MapX(event.x), y=calibration.MapY(event.y);
                    SetPreviewContact(window,x,y);
                    std::printf("PREVIEW raw=(%u,%u) mapped=(%.4f,%.4f)\n",event.x,event.y,x,y);
                }
                continue;
            }
            if(awaitUp)
            {
                if(event.down) continue;
                awaitUp=false;
                if(index==5)
                {
                    if(!e::GenerateCalibration(samples,int16_t(width-1),int16_t(height-1),calibration,false))
                    {std::fprintf(stderr,"Invalid calibration geometry; no file saved\n");quit=true;break;}
                    e::LogTouchCalibration(calibration,"new samples (not committed)");
                    SetCalibrationTarget(window,0.5f,0.5f,0);
                    SetWindowTextW(window,L"Preview: touch center/corners, Enter saves, Esc cancels. No EEPROM write.");
                    collected=true;
                }
                else {auto p=e::StandardTargetRatios[index];SetCalibrationTarget(window,p.x,p.y,index);}
                continue;
            }
            if(event.down && event.initial)
            {
                samples[index]={int16_t(event.x),int16_t(event.y)};
                std::printf("Sample %u raw=(%u,%u); release before next target\n",index+1,event.x,event.y);
                ++index;awaitUp=true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    frontend.Shutdown();
    if(IsWindow(window))DestroyWindow(window);
    if(!accepted) {std::puts("Calibration cancelled; nothing saved");return 1;}
    return e::SaveCalibrationToFile(output,calibration,e::CalibrationFileFormat::Binary64)?0:1;
}
}
