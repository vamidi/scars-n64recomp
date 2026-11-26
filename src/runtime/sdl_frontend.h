// SDL2 frontend: window, event pump and controller input.
//
// The window belongs to the thread that calls recomp::start (the process main
// thread); `recomp::start` calls gfx_callbacks.update_gfx on that same thread,
// so SDL_PollEvent stays on the thread that created the window. RT64 is handed
// the native handle plus that thread's id.

#ifndef SCARS_SDL_FRONTEND_H
#define SCARS_SDL_FRONTEND_H

#include <cstdint>

#include <ultramodern/input.hpp>
#include <ultramodern/renderer_context.hpp>

struct SDL_Window;

namespace scars {

// Returns false (and logs) if SDL cannot start; callers fall back to the
// headless trace renderer.
bool sdl_init();
void sdl_shutdown();

ultramodern::renderer::WindowHandle sdl_create_window(void* gfx_data);
void sdl_pump_events(void* gfx_data);

SDL_Window* sdl_window();

// N64 controller mapping for `controller` (0-based port). Returns false when
// that port has no device, which makes osContGetReadData report "no response".
bool sdl_input_poll(int controller, uint16_t* buttons, float* x, float* y);
ultramodern::input::connected_device_info_t sdl_device_info(int controller);

} // namespace scars

#endif // SCARS_SDL_FRONTEND_H
