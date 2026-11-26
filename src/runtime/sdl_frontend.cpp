#include "sdl_frontend.h"
#include "scars_log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <SDL_syswm.h>
#endif

namespace scars {
namespace {

SDL_Window* g_window = nullptr;
SDL_GameController* g_controllers[4] = {nullptr, nullptr, nullptr, nullptr};
bool g_keyboard_claimed = false;

// N64 button bits, as osContPad uses them.
constexpr uint16_t kA = 0x8000;
constexpr uint16_t kB = 0x4000;
constexpr uint16_t kZ = 0x2000;
constexpr uint16_t kStart = 0x1000;
constexpr uint16_t kUp = 0x0800;
constexpr uint16_t kDown = 0x0400;
constexpr uint16_t kLeft = 0x0200;
constexpr uint16_t kRight = 0x0100;
constexpr uint16_t kL = 0x0020;
constexpr uint16_t kR = 0x0010;
constexpr uint16_t kCUp = 0x0008;
constexpr uint16_t kCDown = 0x0004;
constexpr uint16_t kCLeft = 0x0002;
constexpr uint16_t kCRight = 0x0001;

void open_controller(int index) {
    if (g_controllers[index] != nullptr) {
        return;
    }
    if (SDL_IsGameController(index)) {
        g_controllers[index] = SDL_GameControllerOpen(index);
        if (g_controllers[index] != nullptr) {
            logf("controller %d: %s", index, SDL_GameControllerName(g_controllers[index]));
        }
    }
}

void open_available_controllers() {
    for (int i = 0; i < 4; i++) {
        open_controller(i);
    }
}

} // namespace

bool sdl_init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        logf("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // Allow the process to keep running when the window is closed; the game
    // quits through ultramodern, not by terminating on SDL_QUIT alone.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    open_available_controllers();
    logf("SDL %d.%d.%d ready", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    return true;
}

void sdl_shutdown() {
    for (SDL_GameController*& controller : g_controllers) {
        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
            controller = nullptr;
        }
    }
    if (g_window != nullptr) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
}

ultramodern::renderer::WindowHandle sdl_create_window(void* gfx_data) {
    (void) gfx_data;
    if (g_window == nullptr) {
        // 4:3 at 3x the N64's 320x240; RT64 scales the VI image to fit.
        constexpr int width = 960;
        constexpr int height = 720;
        g_window = SDL_CreateWindow("S.C.A.R.S (recompiled)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
                                    height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (g_window == nullptr) {
            logf("SDL_CreateWindow failed: %s", SDL_GetError());
            return {};
        }
        logf("window created (%dx%d), SDL window id %u", width, height, SDL_GetWindowID(g_window));
    }

    ultramodern::renderer::WindowHandle handle{};
#ifdef _WIN32
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(g_window, &info) == SDL_TRUE) {
        handle.window = info.info.win.window;
    } else {
        logf("SDL_GetWindowWMInfo failed: %s", SDL_GetError());
    }
    // RT64 filters window messages for the thread that owns the window.
    handle.thread_id = GetCurrentThreadId();
#else
    handle = g_window;
#endif
    return handle;
}

void sdl_pump_events(void* gfx_data) {
    (void) gfx_data;
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        switch (event.type) {
        case SDL_QUIT:
            // Closing the window ends the process. ultramodern has no public
            // "quit" that also unwinds the guest, so exiting here is honest.
            logf("SDL_QUIT received (window closed); exiting");
            sdl_shutdown();
            std::exit(0);
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE || event.window.event == SDL_WINDOWEVENT_SHOWN ||
                event.window.event == SDL_WINDOWEVENT_HIDDEN || event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                logf("window event %d on window %u", static_cast<int>(event.window.event), event.window.windowID);
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            open_controller(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            for (SDL_GameController*& controller : g_controllers) {
                if (controller != nullptr &&
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                }
            }
            break;
        default:
            break;
        }
    }
}

SDL_Window* sdl_window() {
    return g_window;
}

bool sdl_input_poll(int controller, uint16_t* buttons, float* x, float* y) {
    if (buttons == nullptr || x == nullptr || y == nullptr || controller < 0 || controller >= 4) {
        return false;
    }

    *buttons = 0;
    *x = 0.0f;
    *y = 0.0f;

    SDL_GameController* pad = g_controllers[controller];
    if (pad != nullptr) {
        const auto pressed = [pad](SDL_GameControllerButton button) {
            return SDL_GameControllerGetButton(pad, button) != 0;
        };

        if (pressed(SDL_CONTROLLER_BUTTON_A)) { *buttons |= kA; }
        if (pressed(SDL_CONTROLLER_BUTTON_B)) { *buttons |= kB; }
        if (pressed(SDL_CONTROLLER_BUTTON_X)) { *buttons |= kB; }
        if (pressed(SDL_CONTROLLER_BUTTON_Y)) { *buttons |= kCUp; }
        if (pressed(SDL_CONTROLLER_BUTTON_START)) { *buttons |= kStart; }
        if (pressed(SDL_CONTROLLER_BUTTON_DPAD_UP)) { *buttons |= kUp; }
        if (pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) { *buttons |= kDown; }
        if (pressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) { *buttons |= kLeft; }
        if (pressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) { *buttons |= kRight; }
        if (pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) { *buttons |= kL; }
        if (pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) { *buttons |= kR; }
        if (pressed(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) { *buttons |= kZ; }

        const float axis_x = static_cast<float>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX)) / 32767.0f;
        const float axis_y = static_cast<float>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY)) / 32767.0f;
        constexpr float deadzone = 0.20f;
        const float magnitude = std::sqrt(axis_x * axis_x + axis_y * axis_y);
        if (magnitude > deadzone) {
            const float scale = (std::min)(1.0f, (magnitude - deadzone) / (1.0f - deadzone)) / magnitude;
            *x = axis_x * scale;
            *y = -axis_y * scale;
        }

        // Right stick as the C buttons, so menus are usable without the D-pad.
        const float c_x = static_cast<float>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX)) / 32767.0f;
        const float c_y = static_cast<float>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY)) / 32767.0f;
        if (c_x < -0.5f) { *buttons |= kCLeft; }
        if (c_x > 0.5f) { *buttons |= kCRight; }
        if (c_y < -0.5f) { *buttons |= kCUp; }
        if (c_y > 0.5f) { *buttons |= kCDown; }
        return true;
    }

    // Test hook: SCARS_AUTO_CONFIRM makes port 1 pulse A once a second, so the
    // boot sequence can be driven past menus without a keyboard or focus.
    if (static const bool auto_confirm = std::getenv("SCARS_AUTO_CONFIRM") != nullptr; auto_confirm && controller == 0) {
        using clock = std::chrono::steady_clock;
        static const clock::time_point started = clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - started).count();
        if ((elapsed % 1000) < 120) {
            *buttons |= kA;
        }
    }

    if (controller != 0) {
        return false;
    }

    // Keyboard fallback for port 1.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr) {
        return false;
    }
    g_keyboard_claimed = true;

    if (keys[SDL_SCANCODE_Z]) { *buttons |= kA; }
    if (keys[SDL_SCANCODE_X]) { *buttons |= kB; }
    if (keys[SDL_SCANCODE_C]) { *buttons |= kZ; }
    if (keys[SDL_SCANCODE_RETURN]) { *buttons |= kStart; }
    if (keys[SDL_SCANCODE_UP]) { *buttons |= kUp; }
    if (keys[SDL_SCANCODE_DOWN]) { *buttons |= kDown; }
    if (keys[SDL_SCANCODE_LEFT]) { *buttons |= kLeft; }
    if (keys[SDL_SCANCODE_RIGHT]) { *buttons |= kRight; }
    if (keys[SDL_SCANCODE_Q]) { *buttons |= kL; }
    if (keys[SDL_SCANCODE_E]) { *buttons |= kR; }
    if (keys[SDL_SCANCODE_I]) { *buttons |= kCUp; }
    if (keys[SDL_SCANCODE_K]) { *buttons |= kCDown; }
    if (keys[SDL_SCANCODE_J]) { *buttons |= kCLeft; }
    if (keys[SDL_SCANCODE_L]) { *buttons |= kCRight; }

    // WASD is the analogue stick; arrows stay on the D-pad.
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    if (keys[SDL_SCANCODE_A]) { stick_x -= 1.0f; }
    if (keys[SDL_SCANCODE_D]) { stick_x += 1.0f; }
    if (keys[SDL_SCANCODE_W]) { stick_y += 1.0f; }
    if (keys[SDL_SCANCODE_S]) { stick_y -= 1.0f; }
    *x = stick_x;
    *y = stick_y;
    return true;
}

ultramodern::input::connected_device_info_t sdl_device_info(int controller) {
    if (controller < 0 || controller >= 4) {
        return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
    }
    if (g_controllers[controller] != nullptr || controller == 0) {
        return {ultramodern::input::Device::Controller, ultramodern::input::Pak::None};
    }
    return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
}

} // namespace scars
