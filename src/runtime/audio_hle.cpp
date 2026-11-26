// HLE audio engine - not implemented yet.
//
// This file is the work area for the native audio implementation described in
// audio_hle.h. It is deliberately a separate translation unit from
// trace_backend.cpp so the audio work and the RSP/task work can proceed in
// parallel without touching the same files.
//
// What the implementation has to do (from the ucode disassembly and the ACMD
// ABI, all recorded in N64_PROJECT_STATE.md):
//   1. read the OSTask from DMEM 0xFC0 (run_task copies it there, byte-swapped
//      like DMEM itself - use RSP_MEM_* or byte-swap explicitly);
//   2. walk the ACMD list at task->t.data_ptr for task->t.data_size bytes;
//   3. implement at least: SPNOOP(0), ADPCM(1), CLEARBUFF(2), ENVMIXER(3),
//      LOADBUFF(4), RESAMPLE(5), SEGMENT(6), SETBUFF(7), SETVOL(8),
//      SETTARGET(9), SETLOOP(10), INTERL(11), POLEF(12), MIXER(13);
//   4. write PCM to task->t.output_buff and publish the length in
//      task->t.output_buff_size, then return RspExitReason::Broke.

#include "audio_hle.h"

namespace scars {

bool audio_hle_ready() {
    return false;
}

RspExitReason audio_hle_run(uint8_t* rdram, uint32_t ucode_addr) {
    (void) rdram;
    (void) ucode_addr;
    return RspExitReason::Unsupported;
}

} // namespace scars
