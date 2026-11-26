// Phase 1 tracing backend.
//
// This is not a renderer. It exists so the boot harness can run the generated
// game code end to end and report what it asks for: RSP tasks, display lists,
// VI swaps, controller reads, audio buffers. Every hook logs and returns a
// benign value; nothing is drawn and no audio is produced.
//
// Phase 2 replaces create_trace_render_context() with RT64 and drops the RSP
// stub once the graphics/audio microcode is recompiled.
//
// The interface is intentionally narrow - one factory plus the callback
// functions - so swapping in a real backend touches this file and main.cpp
// only.

#ifndef SCARS_TRACE_BACKEND_H
#define SCARS_TRACE_BACKEND_H

#include <cstdint>

#include <librecomp/rsp.hpp>
#include <ultramodern/config.hpp>
#include <ultramodern/input.hpp>
#include <ultramodern/renderer_context.hpp>
#include <ultramodern/ultra64.h>

namespace scars {

// --- renderer -------------------------------------------------------------

// No window is created in Phase 1; RT64 supplies one in Phase 2.
ultramodern::renderer::WindowHandle create_no_window(void* gfx_data);

std::unique_ptr<ultramodern::renderer::RendererContext> create_trace_render_context(
    uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);

// --- RSP ------------------------------------------------------------------

// Logs the task and reports the RSP as complete without executing microcode.
RspUcodeFunc* get_trace_rsp_microcode(const OSTask* task);

// --- audio ----------------------------------------------------------------

void audio_queue_samples(int16_t* samples, size_t sample_count);
size_t audio_frames_remaining();
void audio_set_frequency(uint32_t frequency);

// --- input ----------------------------------------------------------------

bool input_poll(int controller_num, uint16_t* buttons, float* x, float* y);
ultramodern::input::connected_device_info_t input_device_info(int controller_num);

// --- events / errors / threads -------------------------------------------

void on_vi();
void on_gfx_init();
void message_box(const char* msg);
std::string game_thread_name(const OSThread* thread);

} // namespace scars

#endif // SCARS_TRACE_BACKEND_H
