// Guest-visible hardware register state that translated code reads directly.
//
// S.C.A.R.S's music driver polls AI_LEN (0xA4500004) inline instead of calling
// osAiGetLength, so the value has to be real: libultra uses it to size the next
// audio frame, and a constant 0 makes the driver believe the FIFO is always
// drained.
//
// The model follows Kyujo's src/ultra/hw_ai.c, which documents the hardware:
// AI plays at osViClock / (AI_DACRATE + 1) sample frames per second, each frame
// being 4 bytes (stereo 16-bit); AI_LEN reads back the bytes still to play;
// AI_STATUS bit 30 is DMA busy, bit 31 is FIFO full. Two buffers are held at
// most - the one playing and one waiting.
//
// Here the audio is handed to the host by the runtime's osAiSetNextBuffer
// replacement, so instead of programming DACRATE we track the sample rate the
// game asked for and drain the queued bytes against the host clock.

#ifndef SCARS_GUEST_HW_H
#define SCARS_GUEST_HW_H

#include <cstdint>

namespace scars {

// Called once the guest hardware windows are mapped.
void guest_hw_attach(uint8_t* rdram);

// Called when the runtime accepts a buffer for playback (bytes = samples * 4).
void guest_hw_audio_queued(uint32_t bytes);

// Called from the VI callback to refresh the guest-visible registers.
void guest_hw_tick();

// Called when the game selects an AI sample rate.
void guest_hw_set_audio_rate(uint32_t hz);

// Bytes the guest would read back from AI_LEN right now.
uint32_t guest_hw_audio_remaining();

// The live RDRAM pointer (diagnostics only; null before guest_hw_attach).
uint8_t* guest_hw_rdram();

} // namespace scars

#endif // SCARS_GUEST_HW_H
