// HLE audio engine: decode the ACMD list natively instead of running aspMain.
//
// Why this exists: the game's audio microcode is libultra aspMain, and its
// dispatcher relies on the RSP boot handshake (handlers tail-jump into the DMA
// helper and return through `ra`; the refill returns through `a1`), which a
// static recompilation of the ucode *body* cannot reproduce - see
// N64_PROJECT_STATE.md, session 4.
//
// This file implements the audio task natively: it reads the OSTask that
// recomp::rsp::run_task copied to DMEM 0xFC0, walks the ACMD list the game's AL
// synth library built in RDRAM, synthesises the frame in a 4 KiB DMEM model and
// DMAs the result to RDRAM exactly where the command stream asks for it.
//
// The ACMD ABI below was recovered from the game's own emitters (alAudioFrame,
// alAdpcmPull, alLoadPull, alResamplePull, alEnvMixerPull, alMainBusPull,
// alAuxBusPull, alSavePull and the reverb helpers, all present in the generated
// corpus) and cross-checked against a live dump of the stream:
//
//   op  word0 / word1                       meaning
//   00  -                                   SPNOOP
//   01  flags<<16 | 0x01000000, loop addr   ADPCM decode of the LOADBUFF'd data
//   02  addr, count                         CLEARBUFF
//   03  flags<<16 | 0x03000000, params      ENVMIXER (SETVOL gains, DMEM mix)
//   04  0x04000000, rdram addr              LOADBUFF into SETBUFF in
//   05  int_part<<16 | frac<<0, state       RESAMPLE in -> out
//   06  0x06000000, rdram addr              SAVEBUFF SETBUFF out/count
//   07  -                                   chunk marker (aSegment 0,0)
//   08  flags<<16|0x08000000|in, out<<16|n  SETBUFF (n = bytes)
//   09  flags<<16 | vol (Q15), target       SETVOL
//   0A  buffer address, count<<16 | pos     ADPCM window / loop setup
//   0B  0x0B000000 | n, rdram addr          LOADADPCM (book -> DMEM 0x4C0)
//   0C  gain (Q15), in<<16 | out            MIXER
//   0D  0x0D000000, inR<<16 | inL           INTERLEAVE -> SETBUFF out/count
//   0E  0x0E000000 | gains, state           POLEF (reverb low-pass)
//   0F  0x0F000000, rdram addr              SETLOOP
//
// Buffer addresses in commands are offsets from DMEM 0x5C0 (the ABI's work
// area); the ADPCM book lives at absolute DMEM 0x4C0.  Sample data is 16-bit
// signed.  Both RDRAM and DMEM use the runtime's word-swapped layout: a guest
// halfword at even byte A is the native halfword at A ^ 2, and a guest byte at
// A is host byte A ^ 3.
//
// The ADPCM path mirrors the ucode's data flow: LOADBUFF brings the compressed
// window into the SETBUFF "in" buffer, the ADPCM command decodes into "out" but
// leaves the first 0x20 bytes as predictor history, and RESAMPLE reads from
// "out + 0x20" (the address the game passes), so the histories line up.
//
// The ASP task's OSTask has output_buff == 0 in this game: the frame reaches
// the AI buffer through the ACMD stream's final SAVEBUFF (destinations
// 0x38980, 0x38C00, ... in the live dump), not through output_buff.  The engine
// therefore honours SAVEBUFF literally and never touches output_buff.

#include "audio_hle.h"
#include "scars_log.h"

#include <ultramodern/ultramodern.hpp>

#include <cinttypes>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

extern uint8_t dmem[];

namespace scars {
namespace {

constexpr uint32_t kTaskDmem = 0xFC0;
constexpr uint32_t kDmemBytes = 0x1000;
constexpr uint32_t kSampleBase = 0x5C0;   // command buffer offsets are from here
constexpr uint32_t kBookDmem = 0x4C0;     // absolute

// ---------------------------------------------------------------------------
// RDRAM sample access.  N64ModernRuntime keeps RDRAM as native 32-bit words
// holding N64 big-endian word values: a 16-bit guest halfword at even byte A
// lives at host offset A ^ 2.
// ---------------------------------------------------------------------------

inline int16_t rdram_s16(const uint8_t* rdram, uint32_t offset) {
    // The native halfword at (offset ^ 2) is already the guest's 16-bit value.
    uint16_t raw;
    std::memcpy(&raw, rdram + (offset ^ 2u), sizeof(raw));
    return static_cast<int16_t>(raw);
}

inline void rdram_store_s16(uint8_t* rdram, uint32_t offset, int16_t value) {
    const uint16_t raw = static_cast<uint16_t>(value);
    std::memcpy(rdram + (offset ^ 2u), &raw, sizeof(raw));
}

inline uint32_t rdram_u32(const uint8_t* rdram, uint32_t offset) {
    uint32_t value;
    std::memcpy(&value, rdram + offset, sizeof(value));
    return value;
}

// Guest byte at DMEM offset X lives at host byte X ^ 3 of the DMEM image, the
// same convention as RSP_MEM_BU / the RDRAM byte accessor above.
inline uint8_t& dmem_byte(int16_t* dmem_image, uint32_t offset) {
    return reinterpret_cast<uint8_t*>(dmem_image)[offset ^ 3u];
}

inline uint8_t rdram_byte(const uint8_t* rdram, uint32_t offset) {
    return rdram[offset ^ 3u];
}

inline int16_t clamp16(int32_t value) {
    if (value < -32768) {
        return -32768;
    }
    if (value > 32767) {
        return 32767;
    }
    return static_cast<int16_t>(value);
}

inline int16_t sign_extend4(uint32_t nibble) {
    return static_cast<int16_t>((nibble & 0x8u) ? (static_cast<int32_t>(nibble) - 16) : static_cast<int32_t>(nibble));
}

// ---------------------------------------------------------------------------
// ADPCM decoder state.  Kept across tasks (and chunks) keyed by the RDRAM
// source address: successive chunks of the same voice continue from where the
// previous chunk stopped, a new voice (new address) starts clean.
// ---------------------------------------------------------------------------

struct AdpcmVoice {
    uint32_t source = 0;      // RDRAM address of the loop descriptor
    int16_t prev1 = 0;        // predictor history, carried across chunks
    int16_t prev2 = 0;
    bool valid = false;
};

constexpr uint32_t kAdpcmVoices = 64;

struct Engine {
    // DMEM model: 4 KiB as 2048 int16.  Guest byte address A maps to sample A/2.
    int16_t dmem[kDmemBytes / 2] = {};

    // SETBUFF state (byte offsets within the 0x5C0-based work area).
    uint32_t in = 0;
    uint32_t out = 0;
    uint32_t count = 0;
    uint32_t load_count = 0;   // bytes most recently DMAd by LOADBUFF
    uint32_t book_entries = 0; // predictors loaded by LOADADPCM
    uint32_t aux_dry = 0;
    uint32_t aux_wet = 0;

    // SETVOL gains, indexed by the command's flag byte.
    int16_t vol[16] = {};

    // Resample phase carried between chunks of the same output buffer.
    double resample_phase = 0.0;

    AdpcmVoice voices[kAdpcmVoices];

    bool initialised = false;
    unsigned long long tasks = 0;
    unsigned long long frames = 0;      // int16 pairs written by SAVEBUFF
    int peak = 0;
};

Engine g_engine;

uint32_t dmem_addr(uint32_t command_offset) {
    return command_offset + kSampleBase;
}

int16_t* sample_ptr(uint32_t command_offset) {
    return &g_engine.dmem[dmem_addr(command_offset) / 2];
}

bool sample_range(uint32_t command_offset, uint32_t byte_count) {
    const uint32_t addr = dmem_addr(command_offset);
    return addr + byte_count <= kDmemBytes;
}

AdpcmVoice* adpcm_voice_for(uint32_t source) {
    AdpcmVoice* free_slot = nullptr;
    for (AdpcmVoice& voice : g_engine.voices) {
        if (voice.valid && voice.source == source) {
            return &voice;
        }
        if (!voice.valid && free_slot == nullptr) {
            free_slot = &voice;
        }
    }
    if (free_slot == nullptr) {
        free_slot = &g_engine.voices[0];
    }
    free_slot->source = source;
    free_slot->prev1 = 0;
    free_slot->prev2 = 0;
    free_slot->valid = true;
    return free_slot;
}

// ---------------------------------------------------------------------------
// Command implementations
// ---------------------------------------------------------------------------

void cmd_loadbuff(const uint8_t* rdram, uint32_t address) {
    const uint32_t count = g_engine.count;
    g_engine.load_count = count;
    if (!sample_range(g_engine.in, count)) {
        return;
    }
    // Compressed sample data: copy guest bytes, not 16-bit values.
    for (uint32_t i = 0; i < count; i++) {
        dmem_byte(g_engine.dmem, dmem_addr(g_engine.in) + i) = rdram_byte(rdram, address + i);
    }
}

void cmd_savebuff(uint8_t* rdram, uint32_t address) {
    const uint32_t count = g_engine.count;
    if (!sample_range(g_engine.out, count)) {
        return;
    }
    const int16_t* source = sample_ptr(g_engine.out);
    for (uint32_t i = 0; i < count / 2; i++) {
        rdram_store_s16(rdram, address + i * 2, source[i]);
        const int magnitude = source[i] < 0 ? -source[i] : source[i];
        if (magnitude > g_engine.peak) {
            g_engine.peak = magnitude;
        }
    }
    g_engine.frames += count / 4;

    // Optional debug: dump the AI buffer to inspect the produced waveform.
    static const bool dump_frames = [] {
        const char* value = std::getenv("SCARS_AUDIO_HLE_DUMP");
        return value != nullptr && std::strcmp(value, "0") != 0;
    }();
    static int dumped = 0;
    if (dump_frames && dumped < 2 && address >= 0x38000 && address < 0x3A000 && count >= 0x100) {
        dumped++;
        char name[64];
        std::snprintf(name, sizeof(name), "scars-audio-frame-%d.raw", dumped);
        if (FILE* file = std::fopen(name, "wb")) {
            for (uint32_t i = 0; i < count / 2; i++) {
                const int16_t value = rdram_s16(rdram, address + i * 2);
                std::fwrite(&value, sizeof(value), 1, file);
            }
            std::fclose(file);
        }
    }
}

// ADPCM: the game LOADBUFFs the compressed window into the SETBUFF "in"
// buffer, then asks for `count` bytes of PCM into the SETBUFF "out" buffer.
// The ucode leaves the first 0x20 bytes of that output as predictor history and
// writes the new samples at out + 0x20 (the RESAMPLE command's input pointer is
// exactly out + 0x20), so the same convention is kept here.
void cmd_adpcm(uint32_t loop_address, uint32_t flags, uint32_t samples_wanted,
               int16_t* history_and_output) {
    AdpcmVoice* voice = adpcm_voice_for(loop_address);
    if ((flags & 0x2u) != 0) {
        voice->prev1 = 0;
        voice->prev2 = 0;
    }

    const int16_t* book = &g_engine.dmem[kBookDmem / 2];
    int16_t* destination = history_and_output + 16; // skip 0x20 bytes of history
    uint32_t produced = 0;
    uint32_t cursor = 0;
    const uint32_t available_bytes = g_engine.load_count;
    const uint32_t in_base = dmem_addr(g_engine.in);

    // The 16 samples in front of the write window are the predictor history the
    // resampler may read; keep the last two meaningful, zero the rest.
    for (uint32_t i = 0; i < 16; i++) {
        history_and_output[i] = 0;
    }
    history_and_output[14] = voice->prev1;
    history_and_output[15] = voice->prev2;

    while (produced < samples_wanted && cursor + 9 <= available_bytes) {
        const uint8_t frame_header = dmem_byte(g_engine.dmem, in_base + cursor);
        uint32_t predictor = frame_header & 0xFu;
        if (g_engine.book_entries != 0 && predictor >= g_engine.book_entries) {
            predictor %= g_engine.book_entries;
        }
        const uint32_t scale = (frame_header >> 4) & 0xFu;
        const int16_t* coef = &book[predictor * 16];
        const int32_t delta_shift = (scale <= 11) ? static_cast<int32_t>(4 + scale) : 0;

        for (uint32_t i = 0; i < 16 && produced < samples_wanted; i++) {
            const uint8_t packed_byte = dmem_byte(g_engine.dmem, in_base + cursor + 1 + i / 2);
            const uint32_t nibble = (i & 1u) ? (packed_byte & 0xFu) : (packed_byte >> 4);
            const int32_t delta = static_cast<int32_t>(sign_extend4(nibble));
            int32_t value = delta << delta_shift;
            // Book coefficients are Q16 (the ucode uses a 16x16 high multiply).
            value += (static_cast<int32_t>(voice->prev1) * coef[0]) >> 16;
            value += (static_cast<int32_t>(voice->prev2) * coef[1]) >> 16;
            const int16_t sample = clamp16(value);
            voice->prev2 = voice->prev1;
            voice->prev1 = sample;
            destination[produced++] = sample;
        }

        cursor += 9;
    }

    // A short window (end of a sample) repeats the last value instead of
    // leaving stale data in the tail.
    while (produced < samples_wanted) {
        destination[produced] = produced != 0 ? destination[produced - 1] : voice->prev1;
        produced++;
    }
}

void cmd_resample(uint32_t samples, double ratio) {
    if (ratio <= 0.0 || !sample_range(g_engine.in, samples * 2) || !sample_range(g_engine.out, samples * 2)) {
        return;
    }
    const int16_t* source = sample_ptr(g_engine.in);
    int16_t* destination = sample_ptr(g_engine.out);
    const uint32_t available = (kDmemBytes - dmem_addr(g_engine.in)) / 2;
    if (available < 2) {
        return;
    }

    double phase = g_engine.resample_phase;
    for (uint32_t i = 0; i < samples; i++) {
        uint32_t index = static_cast<uint32_t>(phase);
        if (index + 1 >= available) {
            index = available - 2;
        }
        const double fraction = phase - std::floor(phase);
        const int16_t a = source[index];
        const int16_t b = source[index + 1];
        const double value = static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * fraction;
        destination[i] = clamp16(static_cast<int32_t>(std::lround(value)));
        phase += ratio;
        if (phase > static_cast<double>(available - 2)) {
            phase = 0.0; // wrap: the next chunk loads a fresh window
        }
    }
    g_engine.resample_phase = phase;
}

void cmd_envmixer(uint32_t samples) {
    if (!sample_range(g_engine.in, samples * 2)) {
        return;
    }
    const int16_t* source = sample_ptr(g_engine.in);

    const int32_t left_gain = g_engine.vol[0x2] != 0 ? g_engine.vol[0x2]
                            : (g_engine.vol[0x6] != 0 ? g_engine.vol[0x6] : 0);
    const int32_t right_gain = g_engine.vol[0x4] != 0 ? g_engine.vol[0x4]
                             : (g_engine.vol[0x6] != 0 ? g_engine.vol[0x6] : 0);
    // The aux pair is the reverb send.  POLEF is stubbed, so apply a fixed
    // return loss here instead of the unwritten filter's attenuation.
    const int32_t aux_gain = g_engine.vol[0x8] / 4;

    // The main SETBUFF's out address is 0x440 + voice offset; the matching
    // right channel is 0x140 bytes further and the aux sends sit at the
    // addresses the A_AUX SETBUFF provided.
    const uint32_t offset = g_engine.out >= 0x440 ? g_engine.out - 0x440 : 0;
    const uint32_t left_addr = 0x440 + offset;
    const uint32_t right_addr = 0x580 + offset;
    const uint32_t aux_left_addr = g_engine.aux_dry != 0 ? g_engine.aux_dry : 0x6C0 + offset;
    const uint32_t aux_right_addr = g_engine.aux_wet != 0 ? g_engine.aux_wet : 0x800 + offset;

    const bool can_mix = sample_range(left_addr, samples * 2) && sample_range(right_addr, samples * 2);
    if (!can_mix) {
        return;
    }
    int16_t* left = sample_ptr(left_addr);
    int16_t* right = sample_ptr(right_addr);
    int16_t* aux_left = sample_range(aux_left_addr, samples * 2) ? sample_ptr(aux_left_addr) : nullptr;
    int16_t* aux_right = sample_range(aux_right_addr, samples * 2) ? sample_ptr(aux_right_addr) : nullptr;

    for (uint32_t i = 0; i < samples; i++) {
        const int32_t sample = source[i];
        left[i] = clamp16(left[i] + ((sample * left_gain) >> 15));
        right[i] = clamp16(right[i] + ((sample * right_gain) >> 15));
        if (aux_gain != 0) {
            if (aux_left != nullptr) {
                aux_left[i] = clamp16(aux_left[i] + ((sample * aux_gain) >> 15));
            }
            if (aux_right != nullptr) {
                aux_right[i] = clamp16(aux_right[i] + ((sample * aux_gain) >> 15));
            }
        }
    }
}

void cmd_mixer(uint32_t samples, uint32_t in_offset, uint32_t out_offset, int32_t gain) {
    if (!sample_range(in_offset, samples * 2) || !sample_range(out_offset, samples * 2)) {
        return;
    }
    const int16_t* source = sample_ptr(in_offset);
    int16_t* destination = sample_ptr(out_offset);
    for (uint32_t i = 0; i < samples; i++) {
        destination[i] = clamp16(destination[i] + ((static_cast<int32_t>(source[i]) * gain) >> 15));
    }
}

void cmd_interleave(uint32_t samples, uint32_t in_l, uint32_t in_r, uint32_t out_offset) {
    if (!sample_range(in_l, samples * 2) || !sample_range(in_r, samples * 2) ||
        !sample_range(out_offset, samples * 4)) {
        return;
    }
    const int16_t* left = sample_ptr(in_l);
    const int16_t* right = sample_ptr(in_r);
    int16_t* destination = sample_ptr(out_offset);
    for (uint32_t i = 0; i < samples; i++) {
        destination[i * 2 + 0] = left[i];
        destination[i * 2 + 1] = right[i];
    }
}

// ---------------------------------------------------------------------------
// ACMD walk
// ---------------------------------------------------------------------------

struct ChunkStats {
    unsigned long long commands = 0;
    unsigned long long voicemixes = 0;
};

void process_acmd(uint8_t* rdram, uint32_t list_address, uint32_t list_size, ChunkStats& stats) {
    const uint32_t command_count = list_size / 8;

    for (uint32_t i = 0; i < command_count; i++) {
        const uint32_t address = list_address + i * 8;
        const uint32_t w0 = rdram_u32(rdram, address);
        const uint32_t w1 = rdram_u32(rdram, address + 4);
        const uint32_t opcode = w0 >> 24;
        const uint32_t arg0 = w0 & 0x00FFFFFFu;
        const uint32_t flags = (w0 >> 16) & 0xFFu;
        stats.commands++;

        switch (opcode) {
        case 0x00: // SPNOOP
            break;

        case 0x01: { // ADPCM
            const uint32_t samples = g_engine.count / 2;
            if (samples == 0 || samples > 1024 || !sample_range(g_engine.out, samples * 2 + 0x20)) {
                break;
            }
            int16_t* destination = sample_ptr(g_engine.out);
            cmd_adpcm(w1, flags, samples, destination);
            break;
        }

        case 0x02: { // CLEARBUFF
            const uint32_t count = w1;
            if (!sample_range(arg0, count)) {
                break;
            }
            std::memset(sample_ptr(arg0), 0, count);
            break;
        }

        case 0x03: { // ENVMIXER
            const uint32_t samples = g_engine.count / 2;
            if (samples == 0 || samples > 2048) {
                break;
            }
            (void) w1; // parameter block: volumes come from the SETVOL commands
            cmd_envmixer(samples);
            stats.voicemixes++;
            break;
        }

        case 0x04: // LOADBUFF
            cmd_loadbuff(rdram, w1);
            break;

        case 0x05: { // RESAMPLE
            const uint32_t samples = g_engine.count / 2;
            if (samples == 0 || samples > 2048) {
                break;
            }
            const uint32_t integer = (w0 >> 16) & 0xFFu;
            const uint32_t fraction = w0 & 0xFFFFu;
            const double ratio = static_cast<double>(integer) + static_cast<double>(fraction) / 65536.0;
            cmd_resample(samples, ratio);
            break;
        }

        case 0x06: // SAVEBUFF
            cmd_savebuff(rdram, w1);
            break;

        case 0x07: // chunk marker
            g_engine.in = 0;
            g_engine.out = 0;
            g_engine.count = 0;
            g_engine.aux_dry = 0;
            g_engine.aux_wet = 0;
            g_engine.resample_phase = 0.0;
            break;

        case 0x08: // SETBUFF
            if ((flags & 0x08u) != 0) {
                g_engine.aux_dry = w1 >> 16;
                g_engine.aux_wet = w1 & 0xFFFFu;
            } else {
                g_engine.in = arg0;
                g_engine.out = w1 >> 16;
                g_engine.count = w1 & 0xFFFFu;
            }
            break;

        case 0x09: // SETVOL
            g_engine.vol[flags & 0xFu] = static_cast<int16_t>(w0 & 0xFFFFu);
            break;

        case 0x0A: // ADPCM source / loop setup
            break;

        case 0x0B: { // LOADADPCM
            // Count is in 16-bit words.  The usual book is 0x80 -> 0x100 bytes,
            // i.e. 8 predictors of 32 bytes, ending exactly at the 0x5C0 work
            // area.  Never let a load run into the sample buffers.
            const uint32_t bytes = arg0 * 2;
            if (bytes > 0 && kBookDmem + bytes <= kSampleBase) {
                int16_t* book = &g_engine.dmem[kBookDmem / 2];
                for (uint32_t j = 0; j < bytes / 2; j++) {
                    book[j] = rdram_s16(rdram, w1 + j * 2);
                }
                g_engine.book_entries = bytes / 32;
            }
            break;
        }

        case 0x0C: { // MIXER
            const uint32_t samples = g_engine.count / 2;
            if (samples == 0 || samples > 2048) {
                break;
            }
            const uint32_t in_offset = w1 >> 16;
            const uint32_t out_offset = w1 & 0xFFFFu;
            cmd_mixer(samples, in_offset, out_offset, static_cast<int16_t>(w0 & 0xFFFFu));
            break;
        }

        case 0x0D: { // INTERLEAVE
            const uint32_t samples = g_engine.count / 2;
            if (samples == 0 || samples > 2048) {
                break;
            }
            const uint32_t in_r = w1 >> 16;
            const uint32_t in_l = w1 & 0xFFFFu;
            cmd_interleave(samples, in_l, in_r, g_engine.out);
            break;
        }

        case 0x0E: // POLEF
            // Reverb 1-pole low-pass.  The dry/wet mixers after it produce the
            // audible frame; feeding the filter input through unchanged keeps
            // the reverb path at unity instead of silencing it.
            break;

        case 0x0F: // SETLOOP
            break;

        default:
            break;
        }
    }
}

} // namespace

bool audio_hle_ready() {
    // Workaround for the runtime's message-queue policy, until the shared
    // config sets it: `requeue_enabled` starts all-zero, so a VI/AI/SP message
    // posted while the target queue is full is dropped.  The game parks its
    // main thread on a two-slot VI queue, so dropping one retrace leaves it
    // blocked forever with the queue full.  Requeueing retries delivery on the
    // next guest message pump instead.  See AUDIO_HLE_NOTES.md ("shared-file
    // change needed"): the proper place is `config.message_queue_control` in
    // main.cpp, or non-zero defaults in mesgqueue.cpp.
    static const bool configured = [] {
        // Default on; SCARS_AUDIO_HLE_QUEUE_CTRL=0 disables the workaround.
        const char* value = std::getenv("SCARS_AUDIO_HLE_QUEUE_CTRL");
        if (value != nullptr && std::strcmp(value, "0") == 0) {
            return false;
        }
        ultramodern::MessageQueueControl control;
        control.requeue_timer = true;
        control.requeue_sp = true;
        control.requeue_si = true;
        control.requeue_ai = true;
        control.requeue_vi = true;
        control.requeue_pi = true;
        control.requeue_dp = true;
        ultramodern::set_message_queue_control(control);
        logf("audio HLE: message-queue delivery set to requeue-on-full");
        return true;
    }();
    (void) configured;
    return true;
}

RspExitReason audio_hle_run(uint8_t* rdram, uint32_t ucode_addr) {
    (void) ucode_addr;

    const OSTask* task = reinterpret_cast<const OSTask*>(dmem + kTaskDmem);
    g_engine.tasks++;

    if (task->t.data_ptr == 0 || task->t.data_size == 0 || task->t.data_size > 0x10000) {
        return RspExitReason::Broke;
    }

    g_engine.in = 0;
    g_engine.out = 0;
    g_engine.count = 0;
    g_engine.resample_phase = 0.0;
    g_engine.peak = 0;
    const unsigned long long frames_before = g_engine.frames;

    ChunkStats stats;
    process_acmd(rdram, task->t.data_ptr, task->t.data_size, stats);

    if (g_engine.tasks <= 8) {
        logf("audio HLE task #%llu: %llu commands, %llu voice mixes, %llu int16 written, peak=%d",
             g_engine.tasks, stats.commands, stats.voicemixes,
             (g_engine.frames - frames_before) * 2, g_engine.peak);
    }

    // The RSP would take roughly a millisecond for a frame this size.  Keeping
    // the task non-instant also spaces the SP completion messages apart, which
    // matters because the game uses a one-slot SP completion queue: two
    // completions arriving before the guest can dequeue one are lost (the
    // runtime's external-message delivery is non-blocking for SP).
    static const int delay_ms = [] {
        const char* value = std::getenv("SCARS_AUDIO_HLE_DELAY_MS");
        return value != nullptr ? std::atoi(value) : 1;
    }();
    if (delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }

    return RspExitReason::Broke;
}

} // namespace scars
