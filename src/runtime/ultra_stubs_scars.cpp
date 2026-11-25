// SCARS-specific libultra internals that N64Recomp expects the runtime to
// provide but N64ModernRuntime does not implement.
//
// Why these exist: n64recomp's symbol_lists.cpp lists libultra internals in
// `N64Recomp::ignored_funcs`. N64Recomp therefore drops the game's own copy of
// each of those functions and rewrites every call into `<name>_recomp`, so the
// runtime has to supply them. N64ModernRuntime only supplies the subset the
// games it targets actually use (OS core, VI, SP, AI, PI, EEPROM, Rumble,
// controller and a no-pak PFS front end). S.C.A.R.S links in more of libultra:
// its Controller Pak file system, its VI manager, the timer interrupt handler,
// DP counters and the SI access helpers. These 22 symbols came out of the first
// forced link closure (see N64_PROJECT_STATE.md, session 2).
//
// Rules used here:
//   * pure bookkeeping with an exact runtime equivalent -> forward to it
//     (`__osPopThread`, `__osEnqueueThread`);
//   * state that a public runtime function already maintains -> reuse it
//     (`__osContGetInitData` -> `osContInit`);
//   * hardware paths the runtime does not emulate (Controller Pak, 64DD LEO,
//     raw SI DMA, DP counters) -> return the error the caller already handles
//     and log once, so a boot trace shows if the path is ever reached.

#include <cstdio>

#include <recomp.h>
#include <ultramodern/ultra64.h>
#include <ultramodern/ultramodern.hpp>

#include <librecomp/helpers.hpp>

namespace {

// PFS/libultra error codes (ultra64.h only defines a few of them).
constexpr s32 PFS_ERR_NOPACK = 1;

// Log once per symbol so a trace stays readable but nothing stays silent.
template <typename... Args>
void warn_once(bool& seen, const char* func, const char* fmt, Args... args) {
    if (seen) {
        return;
    }
    seen = true;
    std::fprintf(stderr, "[scars] %s: ", func);
    std::fprintf(stderr, fmt, args...);
    std::fprintf(stderr, " (logged once)\n");
    std::fflush(stderr);
}

bool seen_pfs_select_bank = false;
bool seen_cont_ram_read = false;
bool seen_cont_ram_write = false;
bool seen_check_id = false;
bool seen_check_pack_id = false;
bool seen_pfs_rw_inode = false;
bool seen_pfs_one_channel = false;
bool seen_pfs_one_channel_data = false;
bool seen_si_raw_dma = false;
bool seen_leo_resume = false;
bool seen_vi_get_current = false;
bool seen_vi_swap_context = false;
bool seen_timer_interrupt = false;
bool seen_pack_request = false;
bool seen_dp_counters = false;

} // namespace

// ---------------------------------------------------------------------------
// Thread queues: exact equivalents already exist in ultramodern.
//   void __osEnqueueThread(OSThread **queue, OSThread *t)
//   OSThread *__osPopThread(OSThread **queue)
// ---------------------------------------------------------------------------
extern "C" void __osEnqueueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_insert(rdram, static_cast<PTR(PTR(OSThread))>(ctx->r4),
                                     static_cast<PTR(OSThread)>(ctx->r5));
}

extern "C" void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = static_cast<gpr>(ultramodern::thread_queue_pop(rdram, static_cast<PTR(PTR(OSThread))>(ctx->r4)));
}

// ---------------------------------------------------------------------------
// Controller init: the runtime already owns the device table. osContInit is the
// public entry that fills both the present-controller bit pattern and the
// per-port status block from the same data.
//   void __osContGetInitData(u8 *pattern, OSContStatus *data)
// ---------------------------------------------------------------------------
extern "C" void __osContGetInitData_recomp(uint8_t* rdram, recomp_context* ctx) {
    u8* pattern = _arg<0, u8*>(rdram, ctx);
    PTR(OSContStatus) data = _arg<1, PTR(OSContStatus)>(rdram, ctx);
    osContInit(rdram, NULLPTR, pattern, data);
}

// ---------------------------------------------------------------------------
// Controller Pak / Transfer Pak file system. ultramodern reports "no pak" from
// the public osPfs* entry points, so these internals are only reachable if game
// code drives the pak protocol itself; report the same error it handles.
// ---------------------------------------------------------------------------
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    u8* pattern = _arg<1, u8*>(rdram, ctx);
    *pattern = 0;
    ctx->r2 = 0;
}

extern "C" void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_pfs_select_bank, "__osPfsSelectBank", "no Controller Pak support");
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_cont_ram_read, "__osContRamRead", "no Controller Pak support");
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_cont_ram_write, "__osContRamWrite", "no Controller Pak support");
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osCheckId_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_check_id, "__osCheckId", "no Controller Pak support");
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osCheckPackId_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_check_pack_id, "__osCheckPackId", "no Controller Pak support");
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osPfsRWInode_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_pfs_rw_inode, "__osPfsRWInode", "no Controller Pak support");
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osPfsRequestOneChannel_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_pfs_one_channel, "__osPfsRequestOneChannel", "no Controller Pak support");
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osPfsGetOneChannelData_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_pfs_one_channel_data, "__osPfsGetOneChannelData", "no Controller Pak support");
    ctx->r2 = PFS_ERR_NOPACK;
}

// ---------------------------------------------------------------------------
// Raw SI transfer. Every real SI transaction in the runtime goes through
// ultramodern::send_si_message(); a guest that drives the controller channel
// protocol itself would hand a request block to this function and then wait on
// its own completion message. Nothing calls it on the boot path today, so fail
// loudly instead of faking a completion that will never arrive.
// ---------------------------------------------------------------------------
extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_si_raw_dma, "__osSiRawStartDma", "raw SI DMA is not emulated");
    ctx->r2 = 0;
}

extern "C" void __osSiCreateAccessQueue_recomp(uint8_t* rdram, recomp_context* ctx) {
    // The SI access queue only serialises SI transfers. The runtime performs all
    // SI work inside send_si_message() under its own lock, and the matching
    // get/rel access calls below are no-ops, so nothing ever waits on it.
    (void) rdram;
    (void) ctx;
}

extern "C" void __osSiGetAccess_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    (void) ctx;
}

extern "C" void __osSiRelAccess_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    (void) ctx;
}

extern "C" void __osPackRequestData_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Builds the 56-byte SI request block that __osSiRawStartDma would send.
    // Harmless on its own; logged to prove the pak path is not silently active.
    (void) rdram;
    warn_once(seen_pack_request, "__osPackRequestData", "pak request block built but never sent");
}

// ---------------------------------------------------------------------------
// 64DD LEO. This cartridge has no disk drive; the leo module is linked but its
// interrupt handler is unreachable.
// ---------------------------------------------------------------------------
extern "C" void __osLeoAbnormalResume_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_leo_resume, "__osLeoAbnormalResume", "no 64DD LEO hardware");
}

// ---------------------------------------------------------------------------
// VI manager internals. osCreateViManager is replaced by a no-op in the
// runtime, so the guest's vi manager thread is never started, but its code is
// still translated and therefore still has to link. If these are ever reached
// the VI state would be double-owned (guest and ultramodern), which is a bug
// worth seeing in a trace rather than working around silently.
// ---------------------------------------------------------------------------
extern "C" void __osViGetCurrentContext_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    warn_once(seen_vi_get_current, "__osViGetCurrentContext", "guest VI manager is not running");
    ctx->r2 = 0;
}

extern "C" void __osViSwapContext_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    (void) ctx;
    warn_once(seen_vi_swap_context, "__osViSwapContext", "guest VI manager is not running");
}

extern "C" void __osTimerInterrupt_recomp(uint8_t* rdram, recomp_context* ctx) {
    // The runtime implements osSetTimer/osStopTimer itself and never calls the
    // guest timer list handler; this exists only for the translated vi manager.
    (void) rdram;
    (void) ctx;
    warn_once(seen_timer_interrupt, "__osTimerInterrupt", "guest timer handler is not running");
}

// ---------------------------------------------------------------------------
// DP counters. The runtime tracks frame completion, not the RDP's four
// performance counters, and does not expose them. Return zeros so a timing or
// profiling call site sees plausible numbers instead of reading host memory at
// an unmapped bus address.
//   void osDpGetCounters(u32 *cnt)
// ---------------------------------------------------------------------------
extern "C" void osDpGetCounters_recomp(uint8_t* rdram, recomp_context* ctx) {
    u32* counters = _arg<0, u32*>(rdram, ctx);
    counters[0] = 0; // clock counter
    counters[1] = 0; // command counter
    counters[2] = 0; // tmem counter
    counters[3] = 0; // pipe/busy counter
    warn_once(seen_dp_counters, "osDpGetCounters", "RDP performance counters are not tracked");
    ctx->r2 = 0;
}
