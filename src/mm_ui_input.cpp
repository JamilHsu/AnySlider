#include "pch.h"
#include "mm_ui_input.h"
#include "mm_ui_input_policy.h"
#include "mm_ui_input_wrapper.h"
#include "Dependencies/Signature.h"
#include "anyslider_log.h"
#include "hook_validation.h"
#include <intrin.h>
#include <array>
#include <cstring>

namespace anyslider::ui_input
{
namespace
{
using Query = uint32_t(__fastcall *)(void *, uint64_t, Predicate);
Query originalActionQuery = nullptr;
Options options;
bool actionHookInstalled = false;
uint8_t *base = nullptr;
thread_local unsigned queryLogCount = 0;
struct CallPatch
{
    const char *name;
    uint32_t rva;
    std::array<uint8_t, 5> before{}, after{};
    bool applied = false;
};
CallPatch patches[] = {{"Customize top", 0x69B723},     {"Customize submenu", 0x69994F},
                       {"Song detail/start", 0x6F87E9}, {"PV module", 0x70AB65},
                       {"PV options", 0x70C1BB},        {"Settings control", 0x64B9C9}};
std::array<uint8_t, 5> BuildRelativeCall(uint32_t site, uint32_t target)
{
    std::array<uint8_t, 5> bytes{0xE8};
    const int32_t displacement = static_cast<int32_t>(target - (site + 5));
    std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    return bytes;
}
bool ValidateTappedWrapper()
{
    std::array<uint8_t, 12> bytes{};
    std::memcpy(bytes.data(), base + 0x2AB260, bytes.size());
    const auto kind = ClassifyTappedWrapper(bytes);
    if (kind == TappedWrapperKind::Native)
    {
        return true;
    }
    if (kind == TappedWrapperKind::IndirectDetour)
    {
        int32_t displacement = 0;
        std::memcpy(&displacement, bytes.data() + 3, sizeof(displacement));
        const auto slot = static_cast<uintptr_t>(static_cast<intptr_t>(displacement));
        uintptr_t target = 0;
        SIZE_T read = 0;
        MEMORY_BASIC_INFORMATION memory{};
        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(slot), &target,
                              sizeof(target), &read) &&
            read == sizeof(target) && target != reinterpret_cast<uintptr_t>(base + 0x2AB260) &&
            VirtualQuery(reinterpret_cast<const void *>(target), &memory, sizeof(memory)) &&
            memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        {
            const DWORD access = memory.Protect & 0xFF;
            if (access == PAGE_EXECUTE || access == PAGE_EXECUTE_READ || access == PAGE_EXECUTE_READWRITE ||
                access == PAGE_EXECUTE_WRITECOPY)
            {
                Log("UI input Tapped wrapper external indirect detour target=%p; chaining existing entry "
                    "without overwrite",
                    reinterpret_cast<void *>(target));
                return true;
            }
        }
    }
    Log("UI input Tapped wrapper unsupported/unreadable detour at +2AB260: %02X %02X %02X %02X %02X %02X "
        "%02X %02X %02X %02X %02X %02X; no hooks installed",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
        bytes[10], bytes[11]);
    return false;
}
// 安装前核对原始指令，卸载前核对本模块写入的指令；不覆盖其他mod。
bool WriteCallPatch(CallPatch &patch, bool restore)
{
    const auto &expected = restore ? patch.after : patch.before;
    const auto &replacement = restore ? patch.before : patch.after;
    auto *address = base + patch.rva;
    if (std::memcmp(address, expected.data(), expected.size()))
    {
        Log("UI input %s ownership mismatch at +%X (%s); no overwrite", restore ? "restore" : "install",
            patch.rva, patch.name);
        return false;
    }
    DWORD protection;
    if (!VirtualProtect(address, replacement.size(), PAGE_EXECUTE_READWRITE, &protection))
    {
        Log("UI input memory protection failed at +%X: %lu", patch.rva, GetLastError());
        return false;
    }
    std::memcpy(address, replacement.data(), replacement.size());
    patch.applied = !restore;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, replacement.size()) != FALSE;
    DWORD ignored;
    const bool protectedAgain = VirtualProtect(address, replacement.size(), protection, &ignored) != FALSE;
    if (!flushed || !protectedAgain)
    {
        Log("UI input write finalization failed at +%X", patch.rva);
    }
    return flushed && protectedAgain;
}
uint32_t __fastcall ActionQueryHook(void *state, uint64_t action, Predicate predicate)
{
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - reinterpret_cast<uintptr_t>(base);
    const uint32_t type =
        state ? *reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(state) + 0x2E8) : 11;
    const auto mapped = ResolvePsCircleAction(action, type, options.ps_circle_confirm);
    const uint32_t result = originalActionQuery(state, mapped, predicate) & 0xFFu;
    if (options.ps_circle_confirm && (action == 9 || action == 10) && queryLogCount < 64 &&
        (result || queryLogCount < 8))
    {
        ++queryLogCount;
        Log("PsCircle query scope=common-action caller=+%llX type=%u requested=%llu mapped=%llu result=%u "
            "record=%u/64",
            static_cast<unsigned long long>(caller), type, action, mapped, result, queryLogCount);
        if (queryLogCount == 64)
        {
            Log("PsCircle query logging capped; policy remains active.");
        }
    }
    if (options.observer)
    {
        options.observer(state, mapped, predicate, result, caller);
    }
    return result;
}
// 一个事务只负责通用Action查询hook；失败时保留现有安装状态。
bool UpdateActionHook(bool detach)
{
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        return false;
    }
    error = DetourUpdateThread(GetCurrentThread());
    if (error == NO_ERROR)
    {
        if (detach)
        {
            error = DetourDetach(reinterpret_cast<void **>(&originalActionQuery),
                                 reinterpret_cast<void *>(ActionQueryHook));
        }
        else
        {
            error = DetourAttach(reinterpret_cast<void **>(&originalActionQuery),
                                 reinterpret_cast<void *>(ActionQueryHook));
        }
    }
    if (error == NO_ERROR)
    {
        error = DetourTransactionCommit();
    }
    else
    {
        DetourTransactionAbort();
    }
    if (error != NO_ERROR)
    {
        Log("UI input detour %s failed: %ld", detach ? "detach" : "attach", error);
    }
    return error == NO_ERROR;
}
} // namespace
bool IsInstalled()
{
    return actionHookInstalled;
}
bool Uninstall()
{
    // Preflight all owned instructions before restoring any of them.
    for (auto &patch : patches)
    {
        if (patch.applied && std::memcmp(base + patch.rva, patch.after.data(), 5))
        {
            Log("UI input uninstall conflict +%X; hooks retained", patch.rva);
            return false;
        }
    }
    for (auto &patch : patches)
    {
        if (patch.applied && !WriteCallPatch(patch, true))
        {
            return false;
        }
    }
    if (actionHookInstalled && !UpdateActionHook(true))
    {
        return false;
    }
    actionHookInstalled = false;
    originalActionQuery = nullptr;
    options = {};
    queryLogCount = 0;
    return true;
}
bool Install(const Options &requested)
{
    if (actionHookInstalled)
    {
        return options.ps_circle_confirm == requested.ps_circle_confirm &&
               options.observer == requested.observer;
    }
    if (!requested.ps_circle_confirm && !requested.observer)
    {
        return true;
    }
    const auto &module = getModuleInfo();
    base = static_cast<uint8_t *>(module.lpBaseOfDll);
    constexpr uint8_t signature[] = {0x40, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x38, 0x4D,
                                     0x8B, 0xF0, 0x48, 0x8B, 0xE9, 0x4D, 0x85, 0xC0, 0x75};
    if (module.SizeOfImage != 0x23E73000 ||
        !ValidateHookBytes(base, 0x2AB1A0, signature, sizeof(signature), "mm_ui_input/signature"))
    {
        Log("UI input unsupported build/common Action entry; optional module disabled");
        return false;
    }
    if (requested.ps_circle_confirm)
    {
        if (!ValidateTappedWrapper())
        {
            return false;
        }
        for (auto &patch : patches)
        {
            patch.before = BuildRelativeCall(patch.rva, 0x2AAF80);
            patch.after = BuildRelativeCall(patch.rva, 0x2AB260);
            if (std::memcmp(base + patch.rva, patch.before.data(), 5))
            {
                Log("UI input validation failed +%X (%s); no hooks installed", patch.rva, patch.name);
                return false;
            }
        }
    }
    options = requested;
    originalActionQuery = reinterpret_cast<Query>(base + 0x2AB1A0);
    if (!UpdateActionHook(false))
    {
        originalActionQuery = nullptr;
        options = {};
        return false;
    }
    actionHookInstalled = true;
    if (requested.ps_circle_confirm)
    {
        for (auto &patch : patches)
        {
            if (!WriteCallPatch(patch, false))
            {
                Log("UI input install failed; rolling back optional module");
                if (!Uninstall())
                {
                    Log("UI input rollback incomplete; owned hooks retained, do not unload DLL");
                }
                return false;
            }
        }
    }
    Log("UI input installed: ps-circle=%d action-hook=1 migrated-calls=%u prompts=unchanged",
        options.ps_circle_confirm, options.ps_circle_confirm ? 6u : 0u);
    return true;
}
} // namespace anyslider::ui_input
