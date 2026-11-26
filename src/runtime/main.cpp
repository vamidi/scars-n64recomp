// SCARS: Recompiled runtime entry point.
//
// Boot order matters and mirrors what librecomp expects:
//
//   1. register_config_path  - where saves, the stored ROM and mods live
//   2. register_game         - declares the ROM hash and entry point
//   3. select_rom            - validates the user's dump and stores a copy
//   4. start_game            - marks the game as runnable
//   5. start                 - creates the renderer/VI/task threads, allocates
//                              RDRAM and runs the entry point on the game thread
//
// `recomp::start` must be the last of these and must run before anything calls
// ultramodern::preinit; it does that itself on its game thread, then blocks
// until the game exits.
//
// The callbacks wired here come from trace_backend: Phase 1 logs everything and
// renders nothing. RT64 replaces the renderer in Phase 2, RSPRecomp replaces
// the RSP stub in Phase 3.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <librecomp/game.hpp>
#include <librecomp/rsp.hpp>
#include <ultramodern/renderer_context.hpp>
#include <ultramodern/ultra64.h>

#include "crash_handler.h"
#include "scars_game.h"
#include "scars_log.h"
#include "trace_backend.h"

#ifdef SCARS_RENDERER_SDL_RT64
#include "rt64_renderer.h"
#include "sdl_frontend.h"
#endif

#ifndef SCARS_VERSION
#define SCARS_VERSION "unknown"
#endif

namespace {

// Directory holding the executable. Used so a double-clicked build finds
// data/ and writes its log next to itself instead of in whatever the shell
// happened to set as the working directory.
std::filesystem::path executable_directory(char* argv0) {
    std::error_code ec;
    if (argv0 != nullptr && argv0[0] != '\0') {
        const std::filesystem::path absolute = std::filesystem::absolute(argv0, ec);
        if (!ec) {
            return absolute.parent_path();
        }
    }
    return std::filesystem::current_path(ec);
}

std::filesystem::path user_directory(const std::filesystem::path& exe_dir) {
    if (const char* override_dir = std::getenv("SCARS_USER_DIR"); override_dir != nullptr && override_dir[0] != '\0') {
        return override_dir;
    }
    // Next to the executable: Explorer's working directory is not a place to
    // write saves. Falls back to the working directory for in-tree runs.
    return exe_dir / "user";
}

// Candidate ROM locations, most specific first. `argv[1]` and SCARS_ROM win;
// otherwise the data/ folder is looked for next to the exe and up the tree so
// build/<preset>/bin/scars.exe finds the repo's data/.
std::vector<std::filesystem::path> rom_candidates(int argc, char** argv,
                                                  const std::filesystem::path& exe_dir) {
    std::vector<std::filesystem::path> candidates;
    if (argc > 1 && argv[1][0] != '\0') {
        candidates.emplace_back(argv[1]);
    }
    if (const char* override_rom = std::getenv("SCARS_ROM"); override_rom != nullptr && override_rom[0] != '\0') {
        candidates.emplace_back(override_rom);
    }

    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    candidates.emplace_back(cwd / scars::kDefaultRomPath);

    std::filesystem::path dir = exe_dir;
    for (int depth = 0; depth < 5; ++depth) {
        candidates.emplace_back(dir / scars::kDefaultRomPath);
        candidates.emplace_back(dir / "scars.z64");
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return candidates;
}

const char* rom_error_text(recomp::RomValidationError error) {
    switch (error) {
    case recomp::RomValidationError::Good:
        return "ROM accepted";
    case recomp::RomValidationError::FailedToOpen:
        return "the file could not be opened";
    case recomp::RomValidationError::NotARom:
        return "the file is not a Nintendo 64 ROM image (.z64/.n64/.v64)";
    case recomp::RomValidationError::IncorrectRom:
        return "this is a different game";
    case recomp::RomValidationError::NotYet:
        return "this is a version of S.C.A.R.S that is not supported";
    case recomp::RomValidationError::IncorrectVersion:
        return "this is a different revision of S.C.A.R.S (expected the Europe/PAL release)";
    case recomp::RomValidationError::OtherError:
    default:
        return "the ROM could not be validated";
    }
}

} // namespace

int main(int argc, char** argv) {
    scars::install_crash_handler();

    const std::filesystem::path exe_dir = executable_directory(argc > 0 ? argv[0] : nullptr);
    // Mirror the log next to the exe so a run that fails before it can be read
    // (double-clicked, console closes on exit) still leaves evidence.
    scars::log_open_file((exe_dir / "scars-log.txt").string().c_str());
    // Generated and runtime code prints progress with printf; keep it unbuffered
    // so a fault does not swallow the last lines.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("SCARS: Recompiled %s\n", SCARS_VERSION);

#ifdef SCARS_LINK_GENERATED
    const std::filesystem::path user_dir = user_directory(exe_dir);
    std::error_code ec;
    std::filesystem::create_directories(user_dir, ec);
    if (ec) {
        scars::logf("cannot create user directory '%s': %s", user_dir.string().c_str(), ec.message().c_str());
        scars::pause_before_exit();
        return 1;
    }

    recomp::register_config_path(user_dir);

    const recomp::GameEntry game = scars::make_game_entry();
    recomp::register_game(game);

    recomp::RomValidationError rom_result = recomp::RomValidationError::FailedToOpen;
    std::filesystem::path tried_rom;
    for (const std::filesystem::path& candidate : rom_candidates(argc, argv, exe_dir)) {
        std::error_code exists_error;
        if (!std::filesystem::exists(candidate, exists_error)) {
            continue;
        }
        scars::logf("validating ROM '%s' (XXH3-64 0x%016llX)", candidate.string().c_str(),
                    static_cast<unsigned long long>(scars::kRomHash));
        rom_result = recomp::select_rom(candidate, game.game_id);
        tried_rom = candidate;
        if (rom_result == recomp::RomValidationError::Good) {
            break;
        }
        // A present-but-wrong file is a real answer: do not silently try others.
        if (rom_result != recomp::RomValidationError::FailedToOpen) {
            break;
        }
    }

    if (rom_result != recomp::RomValidationError::Good) {
        if (tried_rom.empty()) {
            scars::logf("no ROM found. Put the Europe/PAL S.C.A.R.S dump at 'data/scars.z64' "
                        "next to the executable, or pass its path as the first argument "
                        "(or set SCARS_ROM).");
        } else {
            scars::logf("ROM rejected (%s): %s", tried_rom.string().c_str(), rom_error_text(rom_result));
        }
        scars::pause_before_exit();
        return 1;
    }

    recomp::Configuration config{};
    config.argc = argc;
    config.argv = argv;
    config.project_version = recomp::Version(0, 1, 0);

    config.rsp_callbacks = recomp::rsp::callbacks_t{
        .get_rsp_microcode = &scars::get_trace_rsp_microcode,
    };

#ifdef SCARS_RENDERER_SDL_RT64
    if (!scars::sdl_init()) {
        scars::logf("SDL could not start; falling back to the headless trace renderer");
    }

    config.renderer_callbacks = ultramodern::renderer::callbacks_t{
        .create_render_context = &scars::create_rt64_renderer,
    };

    config.gfx_callbacks = ultramodern::gfx_callbacks_t{
        .create_gfx = nullptr,
        .create_window = &scars::sdl_create_window,
        .update_gfx = &scars::sdl_pump_events,
    };
#else
    config.renderer_callbacks = ultramodern::renderer::callbacks_t{
        .create_render_context = &scars::create_trace_render_context,
    };

    config.gfx_callbacks = ultramodern::gfx_callbacks_t{
        .create_gfx = nullptr,
        .create_window = &scars::create_no_window,
        .update_gfx = nullptr,
    };
#endif

    config.audio_callbacks = ultramodern::audio_callbacks_t{
        .queue_samples = &scars::audio_queue_samples,
        .get_frames_remaining = &scars::audio_frames_remaining,
        .set_frequency = &scars::audio_set_frequency,
    };

#ifdef SCARS_RENDERER_SDL_RT64
    config.input_callbacks = ultramodern::input::callbacks_t{
        .poll_input = nullptr,
        .get_input = &scars::sdl_input_poll,
        .set_rumble = nullptr,
        .get_connected_device_info = &scars::sdl_device_info,
    };
#else
    config.input_callbacks = ultramodern::input::callbacks_t{
        .poll_input = nullptr,
        .get_input = &scars::input_poll,
        .set_rumble = nullptr,
        .get_connected_device_info = &scars::input_device_info,
    };
#endif

    config.events_callbacks = ultramodern::events::callbacks_t{
        .vi_callback = &scars::on_vi,
        .gfx_init_callback = &scars::on_gfx_init,
    };

    config.error_handling_callbacks = ultramodern::error_handling::callbacks_t{
        .message_box = &scars::message_box,
    };

    config.threads_callbacks = ultramodern::threads::callbacks_t{
        .get_game_thread_name = &scars::game_thread_name,
    };

    recomp::start_game(game.game_id, "");
    recomp::start(config);
#else
    (void) argc;
    (void) argv;
    std::puts("Runtime scaffold only: configure with -DSCARS_LINK_GENERATED=ON to link the corpus.");
#endif
    return 0;
}
