#include "pch.h"
#include "mm_arcade_test_mode.h"
#include "anyslider_log.h"
#include "hook_validation.h"
#include <cstring>
#include <atomic>

namespace anyslider
{
namespace
{
using TaskMainFn = char(__fastcall *)(void *);
using RegisteredFn = bool(__fastcall *)(void *);
using BuildMenuFn = int64_t(__fastcall *)();
using PollMenuFn = uint32_t(__fastcall *)();
uint8_t *base = nullptr;
RegisteredFn isRegistered = nullptr;
BuildMenuFn buildTopMenu = nullptr;
TaskMainFn originalTestModeMain = nullptr;
PollMenuFn originalPollMenu = nullptr;
using ManagerTickFn = int64_t(__fastcall *)();
ManagerTickFn originalManagerTick = nullptr;
void *taskTestModeInstance = nullptr;
ArcadeTestModeConfig currentConfig;
std::atomic_bool isInstalled = false;
// 以下生命周期与输入状态只在游戏任务线程访问，不需要跨线程抢锁。
bool owned = false;
bool pending = false;
bool closePending = false;
bool previousTestDown = false;
bool testDown = false;
bool waitForRelease = false;
thread_local bool suppressMenuInput = false;
bool exitRequested = false;
int previousSceneBeforeTest = 9; // 记录进入测试模式前的主模式，退出时优雅回退
ULONGLONG requestStarted = 0;
bool transitionWarningLogged = false;
constexpr ULONGLONG TransitionTimeoutMs = 15000;

void ResetOwnership()
{
    owned = false;
    pending = false;
    closePending = false;
    waitForRelease = false;
    exitRequested = false;
    requestStarted = 0;
    transitionWarningLogged = false;
}

uint8_t *SceneWatcher()
{
    using WatcherFn = uint8_t *(__fastcall *)();
    return reinterpret_cast<WatcherFn>(base + 0x2c4810)();
}

bool RequestScene(int target)
{
    using ChangeFn = void(__fastcall *)(int);
    reinterpret_cast<ChangeFn>(base + 0x2c4bb0)(target);
    auto *watcher = SceneWatcher();
    return *reinterpret_cast<int *>(watcher + 4) == target && watcher[16] != 0;
}

uint32_t __fastcall HookedPollMenu()
{
    return suppressMenuInput ? 0 : originalPollMenu();
}

bool ConfirmHeld()
{
    if (testDown)
    {
        return true;
    }
    using GetStateFn = void *(__fastcall *)(int);
    using DownFn = uint8_t(__fastcall *)(void *, uint64_t);
    auto getState = reinterpret_cast<GetStateFn>(base + 0x2ac970);
    auto down = reinterpret_cast<DownFn>(base + 0x2aafe0);
    constexpr uint64_t confirmBits[] = {0, 18, 7, 10, 55};
    for (int player = 0; player < 2; ++player)
    {
        void *state = getState(player);
        if (!state)
        {
            continue;
        }
        for (uint64_t bit : confirmBits)
        {
            if (down(state, bit))
            {
                return true;
            }
        }
    }
    return false;
}

bool BuildMenu(void *task)
{
    auto *bytes = static_cast<uint8_t *>(task);
    auto &begin = *reinterpret_cast<uint64_t *>(bytes + 120);
    auto &end = *reinterpret_cast<uint64_t *>(bytes + 128);
    auto &capacity = *reinterpret_cast<uint64_t *>(bytes + 136);
    // Never reuse a previous invocation's stack as newly initialized state.
    end = begin;
    *reinterpret_cast<uint64_t *>(bytes + 464) = reinterpret_cast<uint64_t>(bytes + 328);
    *reinterpret_cast<int *>(bytes + 108) = 0;
    const int64_t node = buildTopMenu();
    if (!node)
    {
        return false;
    }
    auto *factory = *reinterpret_cast<void **>(node + 48);
    if (!factory)
    {
        return false;
    }
    auto *vtable = *reinterpret_cast<uint64_t **>(factory);
    if (!vtable || !vtable[1])
    {
        return false;
    }
    using FactoryFn = int64_t(__fastcall *)(void *, int);
    int64_t instance = reinterpret_cast<FactoryFn>(vtable[1])(factory, 0);
    if (!instance)
    {
        return false;
    }
    if (end < capacity)
    {
        *reinterpret_cast<int64_t *>(end) = instance;
        end += 8;
    }
    else
    {
        using PushFn = void(__fastcall *)(void *, void *, int64_t *);
        reinterpret_cast<PushFn>(base + 0x273d60)(bytes + 120, reinterpret_cast<void *>(end), &instance);
    }
    if (!begin || end <= begin)
    {
        return false;
    }
    using InitFn = void(__fastcall *)(void *, int);
    reinterpret_cast<InitFn>(base + 0x5db810)(task, 0);
    auto *menu = *reinterpret_cast<uint8_t **>(end - 8);
    if (!menu)
    {
        return false;
    }
    // Native initialization selects EXIT by default. Select first actionable item.
    const uint64_t items = *reinterpret_cast<uint64_t *>(menu + 64);
    const uint64_t itemsEnd = *reinterpret_cast<uint64_t *>(menu + 72);
    if (!items || itemsEnd <= items)
    {
        return false;
    }
    for (uint64_t item = items, index = 0; item < itemsEnd; item += 48, ++index)
    {
        const int type = *reinterpret_cast<int *>(item);
        if (type >= 2 && type <= 5)
        {
            *reinterpret_cast<uint64_t *>(menu + 56) = index;
            // Release the outstanding properties request instead of abandoning it.
            using FreeDataFn = void(__fastcall *)(void *);
            reinterpret_cast<FreeDataFn>(base + 0x2a4e90)(bytes + 112);
            return true;
        }
    }
    return false;
}

char __fastcall HookedTaskTestModeMain(void *task)
{
    // The native substate factory, not this module, owns registration.
    if (owned && pending && *reinterpret_cast<int *>(SceneWatcher()) == 4 &&
        task == *reinterpret_cast<void **>(base + 0x11497e8))
    {
        taskTestModeInstance = task;
        pending = false;
        Log("Arcade Test Mode: native scene committed and task running");
    }
    if (!owned || pending || task != taskTestModeInstance)
    {
        return originalTestModeMain(task);
    }
    auto &stage = *reinterpret_cast<int *>(static_cast<uint8_t *>(task) + 104);
    if (closePending)
    {
        return 1;
    }
    if (stage == 4 || stage == 5)
    {
        // Do not execute native reboot/config-commit/shutdown/ChangeGameState path.
        // Request scene exit after the outer tick; native callbacks destroy the task.
        closePending = true;
        return 1;
    }
    if (stage == 0 || stage == 1)
    {
        if (!BuildMenu(task))
        {
            Log("Arcade Test Mode: initialization failed; closing owned task safely");
            closePending = true;
            return 1;
        }
        stage = 2;
        Log("Arcade Test Mode: menu initialized; waiting for opening input release");
    }
    const bool blocked = waitForRelease;
    if (waitForRelease && !ConfirmHeld())
    {
        waitForRelease = false;
    }
    // Suppress the release frame too: a short pulse may leave a tapped bit set.
    const bool previous = suppressMenuInput;
    suppressMenuInput = blocked;
    const char result = originalTestModeMain(task);
    suppressMenuInput = previous;
    return result;
}

void FinishClose()
{
    const int targetScene = (previousSceneBeforeTest != 4) ? previousSceneBeforeTest : 9;
    auto *watcher = SceneWatcher();
    // Never overwrite a native error or another subsystem's scene transition.
    if (*reinterpret_cast<int *>(watcher) != 4 || watcher[16] || exitRequested || !RequestScene(targetScene))
    {
        return;
    }
    // Restore service mode; substate29's exit callback owns task destruction.
    using ManagerFn = void *(__fastcall *)();
    void *manager = reinterpret_cast<ManagerFn>(base + 0x14cf30)();
    if (manager)
    {
        auto *vtable = *reinterpret_cast<uint64_t **>(manager);
        using ServiceFn = void(__fastcall *)(void *, uint8_t);
        reinterpret_cast<ServiceFn>(vtable[1])(manager, 0);
    }
    exitRequested = true;
    requestStarted = GetTickCount64();
    transitionWarningLogged = false;
    Log("Arcade Test Mode: requested return scene %d; awaiting native teardown", targetScene);
}

// 在原生任务遍历完成之后处理生命周期，避免遍历中创建/销毁任务。
// 返回true表示本帧正在退出或刚退出，不能立即重新打开菜单。
bool UpdateSceneTransition()
{
    // Recover canceled requests without stealing native error/scene transitions.
    auto *observedWatcher = SceneWatcher();
    const int observedScene = *reinterpret_cast<int *>(observedWatcher);
    const bool enteringTest = observedWatcher[16] && *reinterpret_cast<int *>(observedWatcher + 4) == 4;
    bool recovered = false;
    if (owned && observedScene != 4 && !enteringTest && !isRegistered(taskTestModeInstance))
    {
        Log("Arcade Test Mode: scene request canceled or native teardown completed in scene %d; ownership "
            "released",
            observedScene);
        ResetOwnership();
        recovered = true;
    }
    if (owned && pending && !transitionWarningLogged &&
        GetTickCount64() - requestStarted >= TransitionTimeoutMs)
    {
        transitionWarningLogged = true;
        Log("Arcade Test Mode: entry timed out; requesting safe return when scene4 is stable");
        // Keep ownership until native teardown, including a task registered late.
        closePending = true;
    }
    if (owned && exitRequested && !transitionWarningLogged &&
        GetTickCount64() - requestStarted >= TransitionTimeoutMs)
    {
        transitionWarningLogged = true;
        Log("Arcade Test Mode: exit still pending; retaining task protection, no forced scene change");
    }
    // Requests are issued after iteration; native scene callbacks own teardown.
    bool closing = recovered || (owned && closePending);
    if (owned && closePending)
    {
        FinishClose();
    }
    auto *watcher = SceneWatcher();
    const int targetScene = (previousSceneBeforeTest != 4) ? previousSceneBeforeTest : 9;
    if (owned && exitRequested && *reinterpret_cast<int *>(watcher) == targetScene && !watcher[16] &&
        !isRegistered(taskTestModeInstance))
    {
        ResetOwnership();
        closing = true; // Never reopen on the completion frame.
        Log("Arcade Test Mode: return scene %d committed; native task teardown complete", targetScene);
    }
    return closing;
}

// 只读取游戏合并后的TEST输入；不依赖键盘、IO3等具体硬件来源。
bool ReadTestButtonRisingEdge()
{
    // Observe the game's completed input state; no IO frontend dependencies.
    using GetStateFn = void *(__fastcall *)(int);
    using PredicateFn = uint8_t(__fastcall *)(void *, uint64_t);
    auto getState = reinterpret_cast<GetStateFn>(base + 0x2ac970);
    auto down = reinterpret_cast<PredicateFn>(base + 0x2aafe0);
    auto tapped = reinterpret_cast<PredicateFn>(base + 0x2aaf80);
    bool held = false, pressed = false;
    constexpr uint64_t testBits[] = {0, 18};
    for (int player = 0; player < 2; ++player)
    {
        void *state = getState(player);
        if (!state)
        {
            continue;
        }
        // TEST logical action0 and native TEST alias18, not face-button confirm.
        for (uint64_t bit : testBits)
        {
            held |= down(state, bit) != 0;
            pressed |= tapped(state, bit) != 0;
        }
    }
    testDown = held;
    const bool rising = (held || pressed) && !previousTestDown;
    previousTestDown = held;
    return rising;
}

int64_t __fastcall HookedManagerTick()
{
    const int64_t result = originalManagerTick();
    const bool closing = UpdateSceneTransition();
    const bool testPressed = ReadTestButtonRisingEdge();
    if (!closing && testPressed && currentConfig.trigger_on_jvs_test)
    {
        RequestArcadeTestMode();
    }
    return result;
}
} // namespace

bool InitializeArcadeTestMode(const ArcadeTestModeConfig &config)
{
    if (isInstalled.load(std::memory_order_acquire))
    {
        return true;
    }
    if (!config.enabled)
    {
        return false;
    }
    base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    if (!base)
    {
        return false;
    }
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    const uint8_t initBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18};
    const uint8_t mainBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
    const uint8_t pollBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33,
                                 0xDB, 0xE8, 0x23, 0xEA, 0xFF, 0xFF};
    const uint8_t tickBytes[] = {0x48, 0x89, 0x5C, 0x24, 8,    0x48, 0x89, 0x6C,
                                 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
    if (nt->OptionalHeader.SizeOfImage != 0x23e73000 ||
        !ValidateHookBytes(base, 0x5DBC90, pollBytes, sizeof(pollBytes), "Test Mode poll") ||
        !ValidateHookBytes(base, 0x2C9140, tickBytes, sizeof(tickBytes), "Test Mode manager tick") ||
        !ValidateHookBytes(base, 0x5dba60, initBytes, sizeof(initBytes), "mm_arcade_test_mode/initBytes") ||
        !ValidateHookBytes(base, 0x5dbd00, mainBytes, sizeof(mainBytes), "mm_arcade_test_mode/mainBytes"))
    {
        Log("Arcade Test Mode: unsupported build; skipped");
        return false;
    }
    currentConfig = config;
    isRegistered = reinterpret_cast<RegisteredFn>(base + 0x2c9a00);
    buildTopMenu = reinterpret_cast<BuildMenuFn>(base + 0x5edeb0);
    taskTestModeInstance = base + 0xcc00080;
    originalTestModeMain = reinterpret_cast<TaskMainFn>(base + 0x5dbd00);
    originalPollMenu = reinterpret_cast<PollMenuFn>(base + 0x5dbc90);
    originalManagerTick = reinterpret_cast<ManagerTickFn>(base + 0x2c9140);
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        Log("Arcade Test Mode: transaction unavailable: %ld", error);
        return false;
    }
    error = DetourUpdateThread(GetCurrentThread());
    if (error == NO_ERROR)
    {
        error = DetourAttach(reinterpret_cast<void **>(&originalTestModeMain),
                             reinterpret_cast<void *>(HookedTaskTestModeMain));
    }
    if (error == NO_ERROR)
    {
        error = DetourAttach(reinterpret_cast<void **>(&originalPollMenu),
                             reinterpret_cast<void *>(HookedPollMenu));
    }
    if (error == NO_ERROR)
    {
        error = DetourAttach(reinterpret_cast<void **>(&originalManagerTick),
                             reinterpret_cast<void *>(HookedManagerTick));
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
        Log("Arcade Test Mode: hook installation failed: %ld", error);
        return false;
    }
    previousTestDown = testDown = false;
    isInstalled.store(true, std::memory_order_release);
    return true;
}

void ShutdownArcadeTestMode()
{
    if (!isInstalled.load(std::memory_order_acquire))
    {
        return;
    }
    // Do not recursively run task teardown from an arbitrary unload thread.
    if (owned)
    {
        Log("Arcade Test Mode: detach refused while owned task active; close menu first");
        return;
    }
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        Log("Arcade Test Mode: detach transaction unavailable: %ld", error);
        return;
    }
    error = DetourUpdateThread(GetCurrentThread());
    if (error == NO_ERROR)
    {
        error = DetourDetach(reinterpret_cast<void **>(&originalTestModeMain),
                             reinterpret_cast<void *>(HookedTaskTestModeMain));
    }
    if (error == NO_ERROR)
    {
        error = DetourDetach(reinterpret_cast<void **>(&originalPollMenu),
                             reinterpret_cast<void *>(HookedPollMenu));
    }
    if (error == NO_ERROR)
    {
        error = DetourDetach(reinterpret_cast<void **>(&originalManagerTick),
                             reinterpret_cast<void *>(HookedManagerTick));
    }
    if (error == NO_ERROR)
    {
        error = DetourTransactionCommit();
    }
    else
    {
        DetourTransactionAbort();
    }
    if (error == NO_ERROR)
    {
        isInstalled.store(false, std::memory_order_release);
    }
    else
    {
        Log("Arcade Test Mode: detach failed: %ld", error);
    }
}

bool RequestArcadeTestMode()
{
    if (!isInstalled.load(std::memory_order_acquire) || !currentConfig.enabled || pending || owned ||
        IsArcadeTestModeActive())
    {
        return false;
    }
    auto *watcher = SceneWatcher();
    // Only enter from stable normal menu scenes, not boot, gameplay or transitions.
    const int scene = *reinterpret_cast<int *>(watcher);
    if ((scene != 1 && scene != 6 && scene != 7 && scene != 8 && scene != 9) || watcher[16] || watcher[44] ||
        *reinterpret_cast<int *>(watcher + 12) != 1 || *reinterpret_cast<int *>(watcher + 40) != 1)
    {
        return false;
    }
    auto *nativeInstance = *reinterpret_cast<void **>(base + 0x11497e8);
    taskTestModeInstance = nativeInstance ? nativeInstance : base + 0xcc00080;
    if (isRegistered(taskTestModeInstance))
    {
        return false;
    }
    previousSceneBeforeTest = scene;
    pending = owned = waitForRelease = true;
    exitRequested = closePending = false;
    requestStarted = GetTickCount64();
    transitionWarningLogged = false;
    if (!RequestScene(4))
    {
        ResetOwnership();
        Log("Arcade Test Mode: scene request rejected");
        return false;
    }
    Log("Arcade Test Mode: requested native TEST_MODE scene4");
    return true;
}

bool IsArcadeTestModeActive()
{
    return isInstalled.load(std::memory_order_acquire) && taskTestModeInstance && isRegistered &&
           isRegistered(taskTestModeInstance);
}
} // namespace anyslider
