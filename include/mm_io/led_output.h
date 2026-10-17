#pragma once
#include "shared_memory.h"
#include <atomic>
#include <bit>
#include <cstring>
#include <limits>

namespace anyslider::mmio
{
// Windows x64 shared mappings: all participants must use these aligned,
// lock-free accesses (or equivalent Interlocked operations), not memcpy.
static_assert(std::atomic_ref<int64_t>::is_always_lock_free);
static_assert(std::atomic_ref<uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<uint32_t>::is_always_lock_free);
static_assert(std::atomic_ref<uint8_t>::is_always_lock_free);
inline std::atomic_ref<int64_t> OutputSequence(SharedBuffer& b)
{
    return std::atomic_ref<int64_t>(*const_cast<int64_t*>(&b.output_sequence));
}
inline void PublishLedOutput(SharedBuffer& b, const LedOutput& frame)
{
    auto seq = OutputSequence(b);
    const auto previous = seq.load(std::memory_order_seq_cst);
    // Single writer; refuse abandoned odd state or counter exhaustion.
    if ((previous & 1) || previous < 0 || previous > (std::numeric_limits<int64_t>::max)() - 2) return;
    seq.store(previous + 1, std::memory_order_seq_cst);
    std::atomic_ref<uint64_t>(b.output.timestamp_ms).store(frame.timestamp_ms, std::memory_order_seq_cst);
    std::atomic_ref<uint32_t>(b.output.io3_bitmap).store(frame.io3_bitmap, std::memory_order_seq_cst);
    for (size_t i = 0; i < 97; ++i)
        std::atomic_ref<uint8_t>(b.output.slider[i]).store(frame.slider[i], std::memory_order_seq_cst);
    for (auto& byte : b.output.reserved)
        std::atomic_ref<uint8_t>(byte).store(0, std::memory_order_seq_cst);
    seq.store(previous + 2, std::memory_order_seq_cst);
}
inline bool TryReadLedOutput(SharedBuffer& b, LedOutput& frame)
{
    const auto before = OutputSequence(b).load(std::memory_order_seq_cst);
    if (before <= 0 || (before & 1)) return false;
    LedOutput candidate{};
    candidate.timestamp_ms = std::atomic_ref<uint64_t>(b.output.timestamp_ms).load(std::memory_order_seq_cst);
    candidate.io3_bitmap = std::atomic_ref<uint32_t>(b.output.io3_bitmap).load(std::memory_order_seq_cst);
    for (size_t i = 0; i < 97; ++i)
        candidate.slider[i] = std::atomic_ref<uint8_t>(b.output.slider[i]).load(std::memory_order_seq_cst);
    if (OutputSequence(b).load(std::memory_order_seq_cst) != before) return false;
    frame = candidate;
    return true;
}
}
