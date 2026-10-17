#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

struct Snapshot {
    uint64_t gpr[6];
    uint8_t xmm[6][16];
    uint32_t result;
};
static_assert(offsetof(Snapshot, xmm) == 0x30);
static_assert(offsetof(Snapshot, result) == 0x90);
extern "C" {
    uint32_t testReturn = 0;
    uint32_t implStackAlignment = 0;
    void ProbeArcadeQuery(Snapshot*);
    uint32_t IsArcadeControllerEnabledHook();
}

int main()
{
    for (uint32_t result : {0u, 1u}) {
        Snapshot snapshot{};
        testReturn = result;
        ProbeArcadeQuery(&snapshot);
        for (unsigned i = 0; i < 6; ++i) {
            if (snapshot.gpr[i] != 0x1122334455667701ull + i) {
                std::printf("FAIL: GPR %u was clobbered\n", i);
                return 1;
            }
            for (uint8_t byte : snapshot.xmm[i]) {
                if (byte != 0xFF) {
                    std::printf("FAIL: XMM%u was clobbered\n", i);
                    return 1;
                }
            }
        }
        if (snapshot.result != result || implStackAlignment != 8) {
            std::puts("FAIL: return value or call stack alignment");
            return 1;
        }
    }

    // Exercise Windows unwinding at the first instruction after SUB RSP.
    const auto entry = reinterpret_cast<DWORD64>(&IsArcadeControllerEnabledHook);
    DWORD64 imageBase = 0;
    auto* function = RtlLookupFunctionEntry(entry, &imageBase, nullptr);
    if (!function) {
        std::puts("FAIL: missing unwind entry");
        return 1;
    }
    alignas(16) DWORD64 stack[24]{};
    constexpr DWORD64 returnAddress = 0x12345678;
    stack[23] = returnAddress; // saved RIP above the 0xB8-byte frame
    CONTEXT context{};
    context.Rip = entry + 7;
    context.Rsp = reinterpret_cast<DWORD64>(stack);
    PVOID handlerData = nullptr;
    DWORD64 establisherFrame = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function,
        &context, &handlerData, &establisherFrame, nullptr);
    if (context.Rip != returnAddress ||
        context.Rsp != reinterpret_cast<DWORD64>(stack + 24)) {
        std::puts("FAIL: unwind did not recover caller RIP/RSP");
        return 1;
    }
    std::puts("PASS: RCX/RDX/R8-R11, XMM0-5, false/true results, shadow space, alignment, unwind");
    return 0;
}
