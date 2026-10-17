#include "../../src/hook_validation.h"
#include <cstdio>
int main()
{
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!page) return 1;
    const uint8_t expected[] = {0x48, 0x83, 0xEC, 0x28};
    std::memcpy(page + 16, expected, sizeof(expected));
    if (!anyslider::ValidateHookBytes(page, 16, expected, sizeof(expected), "test exact")) return 2;
    page[17] ^= 1;
    if (anyslider::ValidateHookBytes(page, 16, expected, sizeof(expected), "test mismatch")) return 3;
    if (page[17] != (expected[1] ^ 1)) return 4; // validator must not patch
    DWORD old = 0;
    if (!VirtualProtect(page, 4096, PAGE_NOACCESS, &old)) return 5;
    if (anyslider::ValidateHookBytes(page, 16, expected, sizeof(expected), "test unreadable")) return 6;
    if (anyslider::ValidateHookBytes(nullptr, 0, expected, sizeof(expected), "test null")) return 7;
    if (anyslider::ValidateHookBytes(page, 0, expected, 65, "test oversized")) return 8;
    VirtualFree(page, 0, MEM_RELEASE);
    std::puts("PASS: exact validation, mismatch/unreadable/null/oversized rejected, no writes");
}
