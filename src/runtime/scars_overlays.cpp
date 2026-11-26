// Wires the generated section table into the recomp runtime.
//
// N64Recomp writes the function table to `recomp_overlays.inl` but never
// registers it: that is the game project's job, and nothing did it, so
// `recomp::overlays::init_overlays()` saw zero sections and the first
// `load_overlays()` in the boot sequence dereferenced a null table.
//
// Including the .inl here also gives the linker the complete function table,
// which is the strongest possible pull on the generated corpus: every
// recompiled function is named exactly once.

#include <librecomp/overlays.hpp>

namespace {

#include "recomp_overlays.inl"

// Registration must happen before `recomp::start` runs the boot sequence.
// `init_overlays()` runs later, on the game thread, and consumes this data.
const bool registered = [] {
    recomp::overlays::register_overlays(
        recomp::overlays::overlay_section_table_data_t{
            .code_sections = section_table,
            .num_code_sections = num_sections,
            // Section indices are dense and start at zero for a single-image
            // game, so the address table has one entry per section.
            .total_num_sections = num_sections,
        },
        recomp::overlays::overlays_by_index_t{
            .table = overlay_sections_by_index,
            .len = ARRLEN(overlay_sections_by_index),
        });
    return true;
}();

} // namespace
