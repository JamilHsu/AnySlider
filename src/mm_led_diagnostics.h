#pragma once
#include <cstdint>
namespace anyslider {
// Build with ANYSLIDER_NO_LED_DIAGNOSTICS to remove the optional implementation.
void StartLedDiagnostics(bool requested);
void StopLedDiagnostics();
void ObserveLedPack(void* task);
void ObserveLedOutput(void* state, int id, char on);
}
