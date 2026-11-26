#include "guest_hw.h"
#include "scars_log.h"

#include <algorithm>
#include <chrono>

namespace scars {
namespace {

// Guest hardware register addresses (KSEG1).
constexpr int32_t kAiDramAddr = static_cast<int32_t>(0xA4500000);
constexpr int32_t kAiLen = static_cast<int32_t>(0xA4500004);
constexpr int32_t kAiControl = static_cast<int32_t>(0xA4500008);
constexpr int32_t kAiDacRate = static_cast<int32_t>(0xA4500010);
constexpr int32_t kAiStatus = static_cast<int32_t>(0xA450000C);

constexpr uint32_t kAiStatusDmaBusy = 0x40000000u;  // bit 30: a buffer is playing
constexpr uint32_t kAiStatusFifoFull = 0x80000000u; // bit 31: two buffers held

using clock = std::chrono::steady_clock;

struct State {
    uint8_t* rdram = nullptr;
    uint32_t sample_rate = 32000;
    uint32_t queued_bytes = 0;
    clock::time_point queued_at{};
    bool playing = false;
    unsigned long long ticks = 0;
};

State state;

size_t rdram_offset(int32_t guest_address) {
    return static_cast<size_t>(static_cast<uint64_t>(static_cast<int64_t>(guest_address)) - 0xFFFFFFFF80000000ull);
}

void write_reg(int32_t guest_address, uint32_t value) {
    if (state.rdram == nullptr) {
        return;
    }
    *reinterpret_cast<uint32_t*>(state.rdram + rdram_offset(guest_address)) = value;
}

// Bytes consumed since the current buffer was queued.
uint32_t consumed_bytes() {
    if (!state.playing) {
        return 0;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - state.queued_at).count();
    const unsigned long long frames = static_cast<unsigned long long>(elapsed) * state.sample_rate / 1000000000ull;
    const unsigned long long consumed = (frames * 4ull) & ~7ull;
    return consumed >= state.queued_bytes ? state.queued_bytes : static_cast<uint32_t>(consumed);
}

void publish() {
    const uint32_t remaining = state.playing ? state.queued_bytes - consumed_bytes() : 0;
    if (remaining == 0) {
        state.playing = false;
    }

    // The game reads AI_LEN to decide how much audio it may queue next.
    write_reg(kAiLen, remaining);
    // Reads of the write-only address/control registers mirror AI_LEN.
    write_reg(kAiDramAddr, 0);
    write_reg(kAiControl, 1);
    write_reg(kAiDacRate, 0);
    // Bit 30 while a buffer plays. Bit 31 (FIFO full) is never set: the runtime
    // hands buffers to the host immediately, so a second slot never waits.
    write_reg(kAiStatus, remaining != 0 ? kAiStatusDmaBusy : 0);
}

} // namespace

void guest_hw_attach(uint8_t* rdram) {
    // Keep any sample rate the runtime already configured during preinit.
    state.rdram = rdram;
    state.queued_bytes = 0;
    state.playing = false;
    publish();
    logf("guest AI registers initialised (rate %u Hz)", state.sample_rate);
}

void guest_hw_set_audio_rate(uint32_t hz) {
    if (hz == 0) {
        return;
    }
    state.sample_rate = hz;
    logf("guest AI sample rate = %u Hz", hz);
}

void guest_hw_audio_queued(uint32_t bytes) {
    if (state.rdram == nullptr || bytes == 0) {
        return;
    }

    const uint32_t remaining = state.playing ? state.queued_bytes - consumed_bytes() : 0;
    if (!state.playing || remaining == 0) {
        // Nothing in flight: this buffer starts now.
        state.queued_bytes = bytes;
        state.queued_at = clock::now();
        state.playing = true;
    } else {
        // Keep counting from the current buffer's start, with the new one added
        // behind it (the hardware queues exactly one successor).
        state.queued_bytes = remaining + bytes;
    }
    publish();
}

uint8_t* guest_hw_rdram() {
    return state.rdram;
}

uint32_t guest_hw_audio_remaining() {
    return state.playing ? state.queued_bytes - consumed_bytes() : 0;
}

void guest_hw_tick() {
    if (state.rdram == nullptr) {
        return;
    }
    state.ticks++;
    publish();
}

} // namespace scars
