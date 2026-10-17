#pragma once

#include "anyslider_log.h"
#include <cstdint>
#include <atomic>

namespace anyslider
{
inline std::atomic_bool inputDiagnosticsEnabled = false;
// All correlated records are emitted on the game's input thread.
inline thread_local uint64_t inputDiagnosticFrame = 0;
inline thread_local uint64_t inputDiagnosticWindowEnd = 0;
inline thread_local uint32_t inputDiagnosticRecordCount = 0;
inline thread_local bool inputDiagnosticArcade = false;

inline bool InputDiagnosticsEnabled()
{
    return inputDiagnosticsEnabled.load(std::memory_order_relaxed) && IsDebugLoggingEnabled();
}

inline bool InputDiagnosticWindow()
{
    return InputDiagnosticsEnabled() && inputDiagnosticFrame != 0 &&
        inputDiagnosticFrame <= inputDiagnosticWindowEnd;
}

// Bounded output per input-edge window, including false query results.
inline bool InputDiagnosticRecord()
{
    if (!InputDiagnosticWindow()) return false;
    if (inputDiagnosticRecordCount < 256)
    {
        ++inputDiagnosticRecordCount;
        return true;
    }
    if (inputDiagnosticRecordCount == 256)
    {
        ++inputDiagnosticRecordCount;
        DebugLog("InputDiag frame=%llu TRUNCATED window record limit=256", inputDiagnosticFrame);
    }
    return false;
}
}
