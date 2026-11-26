// RT64 renderer backend for the PC build.
//
// Follows the wiring the Diddy Kong Racing port uses:
//   - RT64 owns the graphics device; we hand it the SDL window's native handle
//     plus the id of the thread that created that window;
//   - `RT64::Application::Core` points at the guest's VI/DPC/MI register
//     storage, so RT64's VI renderer reads the same values the guest wrote;
//   - a graphics task is decoded by RT64's own HLE GBI interpreter, selected by
//     hashing the microcode text/data in RDRAM. Graphics microcode therefore
//     needs no RSP translation unit. If the ucode is not in RT64's database the
//     interpreter has no GBI and the task cannot be decoded; that case is
//     logged so Phase 3 can fall back to RSPRecomp.
//
// RT64 is used unmodified: only its public configuration structs are touched.

#ifndef SCARS_RT64_RENDERER_H
#define SCARS_RT64_RENDERER_H

#include <cstdint>
#include <memory>

#include <ultramodern/renderer_context.hpp>

namespace RT64 {
struct Application;
}

namespace scars {

// Factory for recomp::Configuration::renderer_callbacks.
std::unique_ptr<ultramodern::renderer::RendererContext> create_rt64_renderer(uint8_t* rdram,
                                                                             ultramodern::renderer::WindowHandle window_handle,
                                                                             bool developer_mode);

} // namespace scars

#endif // SCARS_RT64_RENDERER_H
