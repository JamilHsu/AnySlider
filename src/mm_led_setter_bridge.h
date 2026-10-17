#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>
namespace anyslider {
// 原生调用者跨调用保留 R10/R11 和 SIMD 临时值；普通 C++ ABI 不保证这些。
// 单次栈分配及 Windows unwind 元数据使异常栈回溯仍可穿过桥接层。
class LedSetterBridge {
    uint8_t* page_ = nullptr;
    RUNTIME_FUNCTION function_{};
public:
    LedSetterBridge() = default;
    LedSetterBridge(const LedSetterBridge&) = delete;
    LedSetterBridge& operator=(const LedSetterBridge&) = delete;
    void* Entry() const { return page_; }
    bool Create(void* target) {
        if(page_) return true;
        page_=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!page_) return false;
        size_t n=0;
        auto byte=[&](uint8_t b){page_[n++]=b;};
        auto bytes=[&](std::initializer_list<uint8_t> v){for(auto b:v)byte(b);};
        bytes({0x48,0x81,0xec,0xa8,0,0,0}); // sub rsp,168; aligned + shadow space
        bytes({0x4c,0x89,0x94,0x24,0x80,0,0,0});
        bytes({0x4c,0x89,0x9c,0x24,0x88,0,0,0});
        for(uint8_t i=0;i<6;++i) bytes({0xf3,0x0f,0x7f,static_cast<uint8_t>(0x44+i*8),0x24,static_cast<uint8_t>(32+i*16)});
        bytes({0x48,0xb8}); const auto address=reinterpret_cast<uintptr_t>(target);
        std::memcpy(page_+n,&address,8);n+=8;bytes({0xff,0xd0});
        for(uint8_t i=0;i<6;++i) bytes({0xf3,0x0f,0x6f,static_cast<uint8_t>(0x44+i*8),0x24,static_cast<uint8_t>(32+i*16)});
        bytes({0x4c,0x8b,0x94,0x24,0x80,0,0,0});
        bytes({0x4c,0x8b,0x9c,0x24,0x88,0,0,0});
        bytes({0x48,0x81,0xc4,0xa8,0,0,0,0xc3});
        const uint8_t unwind[]={1,7,2,0,7,1,21,0}; // UWOP_ALLOC_LARGE: 21*8
        std::memcpy(page_+512,unwind,sizeof(unwind));
        function_={0,static_cast<DWORD>(n),512};
        DWORD old=0;
        if(!VirtualProtect(page_,4096,PAGE_EXECUTE_READ,&old)) {VirtualFree(page_,0,MEM_RELEASE);page_=nullptr;return false;}
        FlushInstructionCache(GetCurrentProcess(),page_,n);
        if(!RtlAddFunctionTable(&function_,1,reinterpret_cast<DWORD64>(page_))) {VirtualFree(page_,0,MEM_RELEASE);page_=nullptr;return false;}
        return true;
    }
    // 桥接代码保留至进程退出，避免卸钩边界上仍在执行的线程访问释放页。
};
}
