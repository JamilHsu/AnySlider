#include "pch.h"
#include "mm_led_diagnostics.h"
#ifndef ANYSLIDER_NO_LED_DIAGNOSTICS
#include "anyslider_log.h"
#include "hook_validation.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>
#include <intrin.h>
namespace anyslider { namespace {
using Tick = char(__fastcall*)(void*);
using Unary = uint64_t(__fastcall*)(void*);
using Side = uint64_t(__fastcall*)(void*, int);
using Buttons = uint64_t(__fastcall*)(void*, char);
using Mode = uint64_t(__fastcall*)(void*, int);
Tick lampTick = nullptr, sliderTick = nullptr;
Unary lampDispatch = nullptr, sliderGenerate = nullptr;
Buttons lampButtons = nullptr;
Side lampSide = nullptr;
Mode sliderMode = nullptr;
std::atomic_bool enabled = false;
bool installed = false;
// 诊断数据不是输入：竞争时丢样本，绝不让游戏帧等日志线程。
std::mutex mutex;
std::jthread writer;
std::wstring directory;
struct Sample {
    uint64_t lampTicks=0, dispatches=0, buttons=0, sides=0;
    uint64_t sliderTicks=0, generates=0, packs=0, outputs=0, modes=0;
    uintptr_t lamp=0, slider=0, outputState=0, modeCaller=0;
    int lampState=0, lampResult=0, lampMode=0, eligibility=0;
    uint32_t lampMask=0, blinkMask=0, storedColor=0, sideColor=0;
    int sliderState=0, sliderResult=0, delay=0, sliderModeValue=0, inputValid=0;
    int ready=0, timeout=0, nonzero=0, prefix=0, lastId=-1, lastOn=0, requestedMode=-1;
    uint32_t bitmap=0;
    uint64_t lastLampMs=0, lastSliderMs=0, lastOutputMs=0;
} sample;
template<class T> T Read(const void* p, size_t offset) {
    T value{}; SIZE_T n=0;
    if(p && (!ReadProcessMemory(GetCurrentProcess(), static_cast<const uint8_t*>(p)+offset, &value, sizeof(value), &n) || n!=sizeof(value))) return T{};
    return value;
}
void Lamp(void* task) {
    sample.lamp=reinterpret_cast<uintptr_t>(task);
    sample.lampState=Read<int>(task,104); sample.lampResult=Read<int>(task,108);
    sample.lampMask=Read<uint32_t>(task,112); sample.lampMode=Read<int>(task,116);
    sample.storedColor=Read<uint32_t>(task,120); sample.blinkMask=Read<uint32_t>(task,124);
    sample.eligibility=Read<int>(task,136);
}
void Slider(void* task) {
    sample.slider=reinterpret_cast<uintptr_t>(task);
    sample.sliderState=Read<int>(task,112); sample.sliderResult=Read<int>(task,116);
    sample.delay=Read<int>(task,120); sample.sliderModeValue=Read<int>(task,128);
    sample.inputValid=Read<int>(task,144);
    const auto device=Read<void*>(task,104);
    sample.ready=Read<int>(device,6168); sample.timeout=Read<int>(device,6176);
}
char __fastcall LampTickHook(void* t) {
    auto r=lampTick(t); if(enabled.load()) {std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return r; ++sample.lampTicks; Lamp(t); sample.lastLampMs=GetTickCount64();} return r;
}
uint64_t __fastcall LampDispatchHook(void* t) {
    auto r=lampDispatch(t); if(enabled.load()) {std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return r; ++sample.dispatches; Lamp(t);} return r;
}
uint64_t __fastcall LampButtonsHook(void* t,char b) {
    auto r=lampButtons(t,b); if(enabled.load()) {std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return r; ++sample.buttons; Lamp(t);} return r;
}
uint64_t __fastcall LampSideHook(void* t,int c) {
    auto r=lampSide(t,c); if(enabled.load()) {std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return r; ++sample.sides; sample.sideColor=static_cast<uint32_t>(c);} return r;
}
char __fastcall SliderTickHook(void* t) {
    auto r=sliderTick(t); if(enabled.load()) {std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return r; ++sample.sliderTicks; Slider(t); sample.lastSliderMs=GetTickCount64();} return r;
}
uint64_t __fastcall SliderGenerateHook(void* t) {
    auto r=sliderGenerate(t); if(enabled.load()) {std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return r; ++sample.generates; Slider(t);} return r;
}
uint64_t __fastcall SliderModeHook(void* t,int m) {
    auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto result=sliderMode(t,m); if(enabled.load()) {std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return result; ++sample.modes; sample.requestedMode=m; sample.modeCaller=caller-reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); Slider(t);}
    return result;
}
struct Hook {size_t rva; const char* bytes; size_t length; void** original; void* replacement;};
Hook hooks[] = {
    {0x42f0a0,"\x40\x57\x48\x83\xec\x20\x48\x8b\xf9\xe8\x42\x01\x00\x00",14,reinterpret_cast<void**>(&lampTick),reinterpret_cast<void*>(LampTickHook)},
    {0x42f1f0,"\x40\x53\x48\x83\xec\x40\x48\x8b\x05\x6b\xd1\x96\x00",13,reinterpret_cast<void**>(&lampDispatch),reinterpret_cast<void*>(LampDispatchHook)},
    {0x42f490,"\x48\x89\x5c\x24\x10\x48\x89\x6c\x24\x18\x48\x89\x74\x24\x20",15,reinterpret_cast<void**>(&lampButtons),reinterpret_cast<void*>(LampButtonsHook)},
    {0x42f3b0,"\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20",10,reinterpret_cast<void**>(&lampSide),reinterpret_cast<void*>(LampSideHook)},
    {0x5a4880,"\x48\x89\x5c\x24\x10\x57\x48\x83\xec\x60",10,reinterpret_cast<void**>(&sliderTick),reinterpret_cast<void*>(SliderTickHook)},
    {0x5a4ce0,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9\xe8\x32\x01\x00\x00",14,reinterpret_cast<void**>(&sliderGenerate),reinterpret_cast<void*>(SliderGenerateHook)},
    {0x5a4aa0,"\x83\xb9\x80\x00\x00\x00\x06\x74\x05\x83\xfa\x07\x74\x2b",14,reinterpret_cast<void**>(&sliderMode),reinterpret_cast<void*>(SliderModeHook)}
};
LONG Transaction(bool attach) {
    LONG e=DetourTransactionBegin();
    if(e) return e; // 未取得事务时不能abort其他模块的事务。
    if(!e) e=DetourUpdateThread(GetCurrentThread());
    for(auto& h:hooks) if(!e) e=attach?DetourAttach(h.original,h.replacement):DetourDetach(h.original,h.replacement);
    if(!e) e=DetourTransactionCommit(); else DetourTransactionAbort(); return e;
}
void Write(FILE* f,const Sample& s) {
    std::fprintf(f,"t=%llu enabled=%d lamp[tick=%llu dispatch=%llu buttons=%llu side=%llu ptr=%llx state=%d result=%d mode=%d eligibility=%d mask=%x blink=%x stored=%08x sideColor=%08x lastMs=%llu] slider[tick=%llu generate=%llu pack=%llu modeCalls=%llu ptr=%llx state=%d result=%d delay=%d mode=%d valid=%d ready=%d timeout=%d nonzero=%d prefix=%02x requested=%d callerRva=%llx lastMs=%llu] output[calls=%llu ptr=%llx bitmap=%x id=%d on=%d lastMs=%llu]\n",
      GetTickCount64(),enabled.load()?1:0,s.lampTicks,s.dispatches,s.buttons,s.sides,s.lamp,s.lampState,s.lampResult,s.lampMode,s.eligibility,s.lampMask,s.blinkMask,s.storedColor,s.sideColor,s.lastLampMs,
      s.sliderTicks,s.generates,s.packs,s.modes,s.slider,s.sliderState,s.sliderResult,s.delay,s.sliderModeValue,s.inputValid,s.ready,s.timeout,s.nonzero,s.prefix,s.requestedMode,s.modeCaller,s.lastSliderMs,
      s.outputs,s.outputState,s.bitmap,s.lastId,s.lastOn,s.lastOutputMs);
    const auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    int tuple[4]{}; SIZE_T n=0;
    const bool sceneOk=ReadProcessMemory(GetCurrentProcess(),base+0xcc61094,tuple,sizeof(tuple),&n) && n==sizeof(tuple);
    const auto pv=Read<void*>(base,0xcc5ef18);
    std::fprintf(f,"scene valid=%d current=%d next=%d previous=%d change=%d pv=%llx select=%d\n",sceneOk?1:0,tuple[0],tuple[1],tuple[2],tuple[3],reinterpret_cast<uintptr_t>(pv),pv?Read<int>(pv,108):-1);
    std::fflush(f);
}
}
void ObserveLedPack(void* task) {
    if(!enabled.load()) return;
    std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return; ++sample.packs; Slider(task);
    auto device=Read<void*>(task,104); sample.nonzero=0; sample.prefix=Read<uint8_t>(device,6300);
    uint8_t payload[96]{}; SIZE_T n=0;
    if(device && ReadProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(device)+6301,payload,sizeof(payload),&n) && n==sizeof(payload))
        for(auto channel:payload) if(channel) ++sample.nonzero;
}
void ObserveLedOutput(void* state,int id,char on) {
    if(!enabled.load()) return;
    std::unique_lock l(mutex, std::try_to_lock); if (!l.owns_lock()) return; ++sample.outputs; sample.outputState=reinterpret_cast<uintptr_t>(state);
    sample.bitmap=Read<uint32_t>(state,216); sample.lastId=id; sample.lastOn=on!=0; sample.lastOutputMs=GetTickCount64();
}
void StartLedDiagnostics(bool requested) {
    if(!requested || installed) return;
    auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->OptionalHeader.SizeOfImage!=0x23e73000) return;
    for(auto& h:hooks) {
        if(!ValidateHookBytes(base, static_cast<uint32_t>(h.rva), h.bytes, h.length, "LED diagnostics")) {Log("LED diagnostics: signature mismatch RVA=%zx; no diagnostic hooks installed",h.rva); return;}
        *h.original=base+h.rva;
    }
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&StartLedDiagnostics),&module);
    wchar_t path[32768]{}; if(!module || !GetModuleFileNameW(module,path,32768)) return;
    directory=path; directory.resize(directory.find_last_of(L"\\/")+1);
    FILE* f=nullptr;
    if(_wfopen_s(&f,(directory+L"AnySlider-led-debug.log").c_str(),L"w") || !f) {Log("LED diagnostics: cannot open log; disabled");return;}
    auto e=Transaction(true); if(e) {std::fclose(f);Log("LED diagnostics: attach failed %ld",e);return;}
    installed=true; {std::lock_guard l(mutex);sample={};} enabled=true;
    std::fprintf(f,"AnySlider LED diagnostics v1, MegaMix v103, read-only. Sampled counts (contended captures skipped); zero with a stale lastMs is NOT a fresh zero sample. Create AnySlider-led-debug.stop beside DLL to pause; remove to resume.\n");
    try {writer=std::jthread([f](std::stop_token stop) {
        while(!stop.stop_requested()) {
            enabled.store(GetFileAttributesW((directory+L"AnySlider-led-debug.stop").c_str())==INVALID_FILE_ATTRIBUTES);
            Sample copy; {std::lock_guard l(mutex);copy=sample;} Write(f,copy);
            if(std::ftell(f)>8*1024*1024) {enabled=false; Log("LED diagnostics: 8 MiB limit reached; restart to collect another session"); break;}
            for(int i=0;i<10 && !stop.stop_requested();++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Sample copy; {std::lock_guard l(mutex);copy=sample;} Write(f,copy);std::fclose(f);
    });} catch(...) {enabled=false; Transaction(false);installed=false;std::fclose(f);Log("LED diagnostics: writer startup failed");return;}
    Log("LED diagnostics: read-only hooks installed; log beside DLL; stop-file pause supported");
}
void StopLedDiagnostics() {
    enabled=false;
    if(writer.joinable()) {writer.request_stop();writer.join();}
    if(installed) {auto e=Transaction(false);if(!e) installed=false;else Log("LED diagnostics: detach failed %ld",e);}
}
}
#else
namespace anyslider {
void StartLedDiagnostics(bool) {} void StopLedDiagnostics() {}
void ObserveLedPack(void*) {} void ObserveLedOutput(void*,int,char) {}
}
#endif
