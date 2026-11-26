// HLE audio engine: the third option for the audio ucode.
//
// Why this exists: the game's audio microcode is libultra aspMain, and its
// dispatcher relies on the RSP boot handshake (handlers tail-jump into the DMA
// helper and return through `ra`; the refill returns through `a1`), which a
// static recompilation of the ucode *body* cannot reproduce - see
// N64_PROJECT_STATE.md, session 4, for the disassembly and the two failing
// conventions.
//
// So the audio task is implemented natively instead: decode the ACMD stream the
// game builds in RDRAM, synthesise the frame, and hand the samples to the
// runtime. The signature is the same as recompiled microcode, which is what
// makes it a drop-in: `recomp::rsp::run_task` copies the OSTask into DMEM 0xFC0
// before calling it, so the task can be read from there.
//
// Contract:
//   - called on the SP Task Thread with a task whose type is M_AUDTASK;
//   - read the task from `recomp::dmem + 0xFC0` (byte-swapped the same way as
//     the RSP sees it: use RSP_MEM_* helpers);
//   - write the frame to `task->t.output_buff` (RDRAM offset) and set
//     `task->t.output_buff_size` to the produced byte count, exactly as aspMain
//     does; the game then calls osAiSetNextBuffer with it;
//   - return RspExitReason::Broke when the task completed.
//
// Until it is implemented, `scars_audio_hle` reports Unsupported so the runtime
// keeps using the tracing stub and the game stays silent but alive. Enable with
// SCARS_AUDIO_HLE=1 (see trace_backend.cpp).

#ifndef SCARS_AUDIO_HLE_H
#define SCARS_AUDIO_HLE_H

#include <cstdint>

#include <librecomp/rsp.hpp>

namespace scars {

// True when the HLE engine is finished enough to take audio tasks.
bool audio_hle_ready();

// RspUcodeFunc-shaped entry point. Reads the OSTask from DMEM 0xFC0.
RspExitReason audio_hle_run(uint8_t* rdram, uint32_t ucode_addr);

} // namespace scars

#endif // SCARS_AUDIO_HLE_H
