/* kyk_slot.h — a slot the audio callback reads, rebuilt by the control loop.
 *
 * The morph target is expanded into its own slot (shell/alchemy/main.cpp), and
 * the audio callback blends towards whatever the slot's index says is there.
 * The index stayed valid while the slot was rewritten under it, so for the
 * 10 ms a tabulated world takes to expand the callback blended towards a
 * world half old and half new, at full level (the 2026-09-13 review). The
 * index is now withdrawn first and published last, each behind a barrier:
 * while the slot is being written the callback sees no target at all.
 *
 * Pure, so the suite can hold it to that (tests/slot_check.cpp). */
#pragma once
#include <atomic>
#include <cstdint>

namespace kyk {

inline void SlotBarrier()
{
#if defined(__arm__)
    __asm__ volatile("dmb" ::: "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

/* idx: what the callback reads, 0xFF for nothing. `build` writes the slot and
   says whether it succeeded; the slot is published as `want` only then */
template <class Build>
inline bool RebuildSlot(volatile uint8_t& idx, uint8_t want, Build build)
{
    idx = 0xFFu;
    SlotBarrier();
    const bool ok = build();
    SlotBarrier();
    idx = ok ? want : 0xFFu;
    return ok;
}

} // namespace kyk
