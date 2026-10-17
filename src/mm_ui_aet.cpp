// MegaMix v103: instance-local native sprite replacements, never global draw swaps.
#include "pch.h"
#include "mm_ui_aet.h"
#include "Dependencies/Signature.h"
#include "anyslider_log.h"
#include <detours.h>
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace anyslider::ui_aet {
namespace {
struct Node {
    Node* left;
    Node* parent;
    Node* right;
    uint8_t color;
    uint8_t nil;
    uint8_t padding[2];
    uint32_t key;
    uint32_t value;
    uint32_t tail;
};
struct Tree { Node* head; uint64_t size; };
static_assert(offsetof(Node, key) == 0x1c && sizeof(Node) == 0x28);
static_assert(sizeof(Tree) == 16);
struct Rule { uint32_t aetScene; uint32_t cross; uint32_t circle; uint16_t pages; bool compact; };
constexpr Rule rules[] = {
    {1314, 59479, 59475, 0x7ce, false}, // 01/02/03/06/07/08/09/10
    {1328, 59535, 59530, 0x00e, false},
    {1316, 60005, 60002, 0x03e, false},
    {1308, 59671, 59672, 0x00e, true},
    {1298, 59593, 61145, 0x002, false},
    {1317, 59324, 59322, 0x00e, false},
    {1315, 59287, 59286, 0x01e, false},
    {1299, 59625, 59631, 0x006, false},
    {1306, 59782, 59779, 0x130, false}, // 04/05/08
};
using Init = double (__fastcall*)(void*, const void*, void*, void*, void*, float*, float*, int);
using Copy = void (__fastcall*)(Tree*, const Tree*, uint8_t);
Init original = nullptr;
Copy copy = nullptr;
using Lookup = const uint8_t* (__fastcall*)(void*, uint32_t);
Lookup lookup = nullptr;
bool installed = false;
bool conflictLogged = false;

const Rule* FindRule(uint32_t id, const char* name) {
    if (!name) return nullptr;
    for (const auto& rule : rules) {
        const char* prefix = rule.compact ? "keyhelp_ps_" : "key_help_ps_";
        const size_t length = std::strlen(prefix);
        if (std::strncmp(name, prefix, length)) continue;
        const char* suffix = name + length;
        if (std::strlen(suffix) != 2) return nullptr;
        if (suffix[0] != '0' || suffix[1] < '1' || suffix[1] > '9') {
            if (std::strcmp(suffix, "10")) return nullptr;
        }
        const unsigned page = suffix[0] == '1' ? 10u : unsigned(suffix[1] - '0');
        if (!(rule.pages & (1u << page))) continue;
        // 294590 is AET scene lookup despite the historical SprDb IDA label.
        // Use AetInfo.Id, NOT AetSetInfo.Id or SpriteSetId.
        const auto info = lookup(nullptr, rule.aetScene);
        if (info && id == *reinterpret_cast<const uint32_t*>(info + 24)) return &rule;
    }
    return nullptr;
}

double __fastcall Hook(void* instance, const void* args, void* scene, void* composition,
    void* layer, float* start, float* end, int index) {
    const double result = original(instance, args, scene, composition, layer, start, end, index);
    const auto bytes = static_cast<const uint8_t*>(args);
    const auto rule = FindRule(*reinterpret_cast<const uint32_t*>(bytes),
        *reinterpret_cast<const char* const*>(bytes + 8));
    if (!rule) return result;
    auto destination = reinterpret_cast<Tree*>(static_cast<uint8_t*>(instance) + 592);
    // Preserve any game's/mod's existing replacement policy. No allocator/CRT crossing:
    // the native deep-copy routine allocates nodes; native instance destruction frees them.
    if (destination->size != 0) {
        if (!conflictLogged) {
            conflictLogged = true;
            Log("PS AET replacement skipped: existing sprite_replace map; preserving external policy.");
        }
        return result;
    }
    Node sentinel{}, root{}, child{};
    sentinel.left = &root; sentinel.parent = &root; sentinel.right = &child;
    sentinel.color = 1; sentinel.nil = 1;
    root.left = &sentinel; root.parent = &sentinel; root.right = &child;
    root.color = 1;
    root.key = rule->cross; root.value = rule->circle;
    child.left = &sentinel; child.parent = &root; child.right = &sentinel;
    child.key = rule->circle; child.value = rule->cross;
    // Build sorted native-compatible tree regardless of which symbol has the lower ID.
    if (root.key > child.key) {
        root.left = &child; root.right = &sentinel;
        sentinel.left = &child; sentinel.right = &root;
    }
    const Tree source{&sentinel, 2};
    copy(destination, &source, 0);
    return result;
}
}

bool Install() {
    if (installed) return true;
    const auto module = getModuleInfo();
    if (!module.lpBaseOfDll || module.SizeOfImage != 0x23E73000) return false;
    auto base = static_cast<uint8_t*>(module.lpBaseOfDll);
    const uint8_t signature[] = {0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x83,0xec,0x50};
    const uint8_t copySignature[] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xda};
    const uint8_t lookupSignature[] = {0x89,0x54,0x24,0x10,0x48,0x83,0xec,0x38};
    if (std::memcmp(base + 0x28e4f0, signature, sizeof(signature)) ||
        std::memcmp(base + 0x1af650, copySignature, sizeof(copySignature)) ||
        std::memcmp(base + 0x294590, lookupSignature, sizeof(lookupSignature))) {
        Log("PS AET hook rejected: v103 native function signature mismatch.");
        return false;
    }
    original = reinterpret_cast<Init>(base + 0x28e4f0);
    copy = reinterpret_cast<Copy>(base + 0x1af650);
    lookup = reinterpret_cast<Lookup>(base + 0x294590);
    LONG status = DetourTransactionBegin();
    if (status != NO_ERROR) return false;
    status = DetourUpdateThread(GetCurrentThread());
    if (status == NO_ERROR) status = DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(Hook));
    if (status != NO_ERROR) { DetourTransactionAbort(); return false; }
    status = DetourTransactionCommit();
    installed = status == NO_ERROR;
    if (installed) conflictLogged = false;
    Log("PS AET instance replacement hook: %s", installed ? "installed" : "failed");
    return installed;
}

bool Uninstall() {
    if (!installed) return true;
    LONG status = DetourTransactionBegin();
    if (status != NO_ERROR) return false;
    status = DetourUpdateThread(GetCurrentThread());
    if (status == NO_ERROR) status = DetourDetach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(Hook));
    if (status != NO_ERROR) { DetourTransactionAbort(); return false; }
    status = DetourTransactionCommit();
    if (status == NO_ERROR) installed = false;
    return status == NO_ERROR;
}
}
