// HLE audio engine - first pass: dump the task and the ACMD stream.
//
// Temporary diagnostic build: it accepts the audio task, records what the game
// asked the RSP to do, and returns Broke without producing samples. Used to
// derive the command layouts from the real stream.

#include "audio_hle.h"
#include "scars_log.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern uint8_t dmem[];

namespace scars {
namespace {

constexpr uint32_t kTaskDmem = 0xFC0;

const OSTask* task_in_dmem() {
    return reinterpret_cast<const OSTask*>(dmem + kTaskDmem);
}

uint32_t rdram_word(const uint8_t* rdram, uint32_t physical) {
    uint32_t value;
    std::memcpy(&value, rdram + physical, sizeof(value));
    return value;
}

void dump_task(unsigned long long index, const OSTask* task, const uint8_t* rdram) {
    if (index > 3) {
        return;
    }

    FILE* file = std::fopen("scars-audio-dump.txt", index <= 1 ? "w" : "a");
    if (file == nullptr) {
        return;
    }

    std::fprintf(file, "=== audio task #%llu ===\n", index);
    std::fprintf(file,
                 "type=%u flags=0x%X ucode=0x%08X size=0x%X ucode_data=0x%08X dsize=0x%X\n"
                 "dram_stack=0x%08X ssize=0x%X output_buff=0x%08X output_buff_size=0x%08X\n"
                 "data_ptr=0x%08X data_size=0x%X yield=0x%08X ysize=0x%X\n",
                 task->t.type, task->t.flags, task->t.ucode, task->t.ucode_size, task->t.ucode_data,
                 task->t.ucode_data_size, task->t.dram_stack, task->t.dram_stack_size, task->t.output_buff,
                 task->t.output_buff_size, task->t.data_ptr, task->t.data_size, task->t.yield_data_ptr,
                 task->t.yield_data_size);

    const uint32_t words = task->t.data_size / 4;
    std::fprintf(file, "acmd words=%u\n", words);
    for (uint32_t i = 0; i < words; i += 8) {
        std::fprintf(file, "%08X: ", task->t.data_ptr + i * 4);
        const uint32_t count = (words - i) < 8 ? (words - i) : 8;
        for (uint32_t j = 0; j < count; j++) {
            std::fprintf(file, "%08X ", rdram_word(rdram, task->t.data_ptr + (i + j) * 4));
        }
        std::fprintf(file, "\n");
    }

    std::fclose(file);
}

} // namespace

bool audio_hle_ready() {
    return true;
}

RspExitReason audio_hle_run(uint8_t* rdram, uint32_t ucode_addr) {
    (void) ucode_addr;

    static unsigned long long tasks = 0;
    const unsigned long long index = ++tasks;
    const OSTask* task = task_in_dmem();

    dump_task(index, task, rdram);

    log_throttled("audio_hle", 6,
                  "audio HLE task #%llu: type=%u data_ptr=0x%08X data_size=0x%X output=0x%08X out_size_ptr=0x%08X",
                  index, task->t.type, task->t.data_ptr, task->t.data_size, task->t.output_buff,
                  task->t.output_buff_size);

    return RspExitReason::Broke;
}

} // namespace scars
