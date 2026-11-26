// Guest-visible hardware windows.
//
// A statically recompiled instruction that loads from a physical hardware
// register becomes an ordinary memory read at `rdram + (guest_addr - 0xFFFFFFFF80000000)`.
// For the SP/DP/MI/VI/AI/PI/SI register blocks that lands around 576-592 MiB
// into the RDRAM allocation, which `recomp::start` leaves PAGE_NOACCESS as a
// guard region, so the read faults.
//
// N64ModernRuntime never has to deal with this for the games it ships with:
// their libultra calls `osAiGetLength()` and friends, which the runtime
// replaces. S.C.A.R.S inlines some of those reads in game code, for example
// reading AI_LEN (0xA4500004) directly to pace audio in its music driver, so
// the window has to be addressable.
//
// For now the windows are plain zeroed memory: reads return 0 and writes are
// discarded. That is enough to let the game progress, but it is not register
// emulation - AI_LEN will always read as "buffer drained". Real values belong
// with the RSP/audio work (Phase 3); the constants and the failure mode are
// documented in N64_PROJECT_STATE.md.

#ifndef SCARS_GUEST_MMIO_H
#define SCARS_GUEST_MMIO_H

#include <cstdint>

namespace scars {

// Makes the guest hardware windows accessible and preloads the cart ROM alias.
// Safe to call once per game start with the live RDRAM pointer.
void map_guest_hardware_windows(uint8_t* rdram);

} // namespace scars

#endif // SCARS_GUEST_MMIO_H
