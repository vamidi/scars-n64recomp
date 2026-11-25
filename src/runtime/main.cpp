// SCARS: Recompiled runtime entry point (scaffold).
//
// This executable links the N64ModernRuntime libraries so the runtime part of
// the project always compiles. Once the recompiled corpus exists, replace the
// body with the usual flow:
//
//   recomp::Configuration cfg = { ... renderer/audio/input callbacks ... };
//   recomp::start(cfg);
//   recomp::register_game(...);
//   recomp::start_game(u8"SCARS", "");
//
// The renderer (RT64 or another backend) is not wired up yet.

#include <cstdio>

#include <recomp.h>

#ifndef SCARS_VERSION
#define SCARS_VERSION "unknown"
#endif

#ifdef SCARS_LINK_GENERATED
// Defined by the generated corpus. Referencing it is what forces the linker to
// pull every generated translation unit reachable from the boot entry point,
// and therefore to report the runtime symbols the corpus actually needs.
extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);

static void (*volatile const scars_entry_ref)(uint8_t*, recomp_context*) =
    &recomp_entrypoint;
#endif

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    std::printf("SCARS: Recompiled %s\n", SCARS_VERSION);
#ifdef SCARS_LINK_GENERATED
    // Consume the reference so no optimisation level can drop it.
    if (scars_entry_ref == nullptr) {
        std::puts("SCARS: internal error: the corpus entry point is null.");
        return 1;
    }
    std::puts("A recompiled corpus is linked, but the runtime entry point is not implemented yet.");
#else
    std::puts("Runtime scaffold only: configure with -DSCARS_LINK_GENERATED=ON to link the corpus.");
#endif
    return 0;
}
