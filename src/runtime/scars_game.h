// The game entry this runtime knows how to boot.
//
// Everything ROM-specific lives here: the hash the runtime validates against,
// the boot entry point, the display names, and the wrapper that traces the
// generated entry point. `tools/rom_info.py` prints the same hash for a local
// dump, so the two can always be cross-checked.

#ifndef SCARS_GAME_H
#define SCARS_GAME_H

#include <cstdint>

#include <librecomp/game.hpp>
#include <recomp.h>

namespace scars {

// XXH3-64 over the whole ROM image (see N64_PROJECT_STATE.md, "ROM").
inline constexpr uint64_t kRomHash = 0x4C035BA764A51971ull;

// The game's boot entry point; also where the first 1 MiB image is mapped.
inline constexpr uint32_t kEntrypointAddress = 0x80200400;

inline constexpr char kInternalName[] = "SCARS";
inline constexpr char kDisplayName[] = "S.C.A.R.S";

// Saves are not implemented yet; the ROM imports osPfs* (Controller Pak) and
// osMotor* (rumble) but the cartridge save type is still undetermined.
// AllowAll is the placeholder that keeps every save path enabled.
inline constexpr recomp::SaveType kSaveType = recomp::SaveType::AllowAll;

inline constexpr char kDefaultRomPath[] = "data/scars.z64";

// Provided by the generated corpus (build/generated/lookup.cpp + funcs_*.c).
extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);

// Registered as GameEntry::entrypoint. Traces, then runs the generated code.
void entry(uint8_t* rdram, recomp_context* ctx);

// Registered as GameEntry::on_init_callback; fixes up per-ROM state that the
// runtime initialises with defaults for other games.
void on_init(uint8_t* rdram, recomp_context* ctx);

recomp::GameEntry make_game_entry();

} // namespace scars

#endif // SCARS_GAME_H
