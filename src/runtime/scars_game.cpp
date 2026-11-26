#include "scars_game.h"
#include "guest_hw.h"
#include "guest_mmio.h"
#include "scars_log.h"

#include <atomic>

namespace scars {
namespace {

std::atomic<unsigned long long> entry_calls{0};

// MEM_W in recomp.h expects a sign-extended guest address; a plain 0x80000300
// would wrap to an offset 2 GiB above the start of RDRAM.
void write_guest_word(uint8_t* rdram, uint32_t address, int32_t value) {
    *reinterpret_cast<int32_t*>(&rdram[static_cast<uint64_t>(static_cast<int32_t>(address)) - 0xFFFFFFFF80000000ull]) = value;
}

} // namespace

void on_init(uint8_t* rdram, recomp_context* ctx) {
    (void) ctx;
    // librecomp's init() writes osTvType = 1 (NTSC) for every game and sets
    // osMemSize = 8 MiB. This is the Europe/PAL release: 0 = PAL, 1 = NTSC,
    // 2 = MPAL. With NTSC reported, the game's main thread takes its non-PAL
    // branch, returns immediately and the runtime ends up with no runnable
    // threads.
    write_guest_word(rdram, 0x80000300, 0);
    logf("osTvType = 0 (PAL Europe release)");

    // Guest code reads some hardware registers inline; make those windows real
    // memory before any translated code runs.
    map_guest_hardware_windows(rdram);
    guest_hw_attach(rdram);
}

void entry(uint8_t* rdram, recomp_context* ctx) {
    const unsigned long long call = entry_calls.fetch_add(1) + 1;
    logf("recomp_entrypoint call #%llu: sp=0x%08X ra=0x%08X", call,
         static_cast<uint32_t>(ctx->r29), static_cast<uint32_t>(ctx->r31));

    recomp_entrypoint(rdram, ctx);

    logf("recomp_entrypoint call #%llu returned: r2=0x%08X r29=0x%08X", call,
         static_cast<uint32_t>(ctx->r2), static_cast<uint32_t>(ctx->r29));
}

recomp::GameEntry make_game_entry() {
    recomp::GameEntry game{};
    game.rom_hash = kRomHash;
    game.internal_name = kInternalName;
    game.display_name = kDisplayName;
    game.game_id = std::u8string(reinterpret_cast<const char8_t*>(kInternalName), sizeof(kInternalName) - 1);
    game.save_type = kSaveType;
    game.is_enabled = true;
    // Guest pointers live in the low 4 GiB and are sign-extended: MEM_* and
    // TO_PTR subtract 0xFFFFFFFF80000000 from the register value. Passing the
    // address unsigned (+0x80200400) makes that subtraction wrap to a +2 GiB
    // offset instead of 0x200400. The generated lookup.cpp casts through
    // int32_t for exactly this reason.
    game.entrypoint_address = static_cast<gpr>(static_cast<int32_t>(kEntrypointAddress));
    game.entrypoint = &scars::entry;
    game.on_init_callback = &scars::on_init;
    return game;
}

} // namespace scars
