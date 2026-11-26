#include "rt64_renderer.h"
#include "scars_log.h"

#include <array>
#include <cstdio>

#ifdef _WIN32
// dxcapi.h (pulled in by RT64's headers) derives from IUnknown; with
// WIN32_LEAN_AND_MEAN those declarations are not in Windows.h. RT64's own
// Windows build relies on the full Windows.h.
#include <Unknwn.h>
#include <oaidl.h>
#endif

#include <hle/rt64_application.h>
#include <hle/rt64_interpreter.h>
#include <common/rt64_user_configuration.h>
#include <ultramodern/ultramodern.hpp>

namespace scars {
namespace {

// RT64 wants live pointers to the machine's registers and memories. These are
// process-static, like the emulator-side equivalents.
std::array<uint8_t, 0x40> g_rom_header{};
std::array<uint8_t, 0x1000> g_dmem{};
std::array<uint8_t, 0x1000> g_imem{};
uint32_t g_mi_interrupt = 0;
std::array<uint32_t, 8> g_dpc_registers{};

void check_interrupts() {}

RT64::UserConfiguration::GraphicsAPI to_rt64(ultramodern::renderer::GraphicsApi api) {
    using RT = RT64::UserConfiguration::GraphicsAPI;
    switch (api) {
    case ultramodern::renderer::GraphicsApi::D3D12:
        return RT::D3D12;
    case ultramodern::renderer::GraphicsApi::Vulkan:
        return RT::Vulkan;
    case ultramodern::renderer::GraphicsApi::Metal:
        return RT::Metal;
    default:
        return RT::Automatic;
    }
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing value) {
    using RT = RT64::UserConfiguration::Antialiasing;
    switch (value) {
    case ultramodern::renderer::Antialiasing::MSAA2X:
        return RT::MSAA2X;
    case ultramodern::renderer::Antialiasing::MSAA4X:
        return RT::MSAA4X;
    case ultramodern::renderer::Antialiasing::MSAA8X:
        return RT::MSAA8X;
    default:
        return RT::None;
    }
}

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio value) {
    using RT = RT64::UserConfiguration::AspectRatio;
    switch (value) {
    case ultramodern::renderer::AspectRatio::Expand:
        return RT::Expand;
    case ultramodern::renderer::AspectRatio::Manual:
        return RT::Manual;
    default:
        return RT::Original;
    }
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult result) {
    using RT = RT64::Application::SetupResult;
    using UM = ultramodern::renderer::SetupResult;
    switch (result) {
    case RT::Success:
        return UM::Success;
    case RT::DynamicLibrariesNotFound:
        return UM::DynamicLibrariesNotFound;
    case RT::InvalidGraphicsAPI:
        return UM::InvalidGraphicsAPI;
    case RT::GraphicsAPINotFound:
        return UM::GraphicsAPINotFound;
    case RT::GraphicsDeviceNotFound:
    default:
        return UM::GraphicsDeviceNotFound;
    }
}

ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    using RT = RT64::UserConfiguration::GraphicsAPI;
    using UM = ultramodern::renderer::GraphicsApi;
    switch (api) {
    case RT::D3D12:
        return UM::D3D12;
    case RT::Vulkan:
        return UM::Vulkan;
    case RT::Metal:
        return UM::Metal;
    default:
        return UM::Auto;
    }
}

// Maps ultramodern's presentation settings onto RT64's user configuration.
void apply_config(RT64::Application& application, const ultramodern::renderer::GraphicsConfig& config) {
    application.userConfig.graphicsAPI = to_rt64(config.api_option);
    application.userConfig.antialiasing = to_rt64(config.msaa_option);
    application.userConfig.aspectRatio = to_rt64(config.ar_option);
    application.userConfig.resolution = config.res_option == ultramodern::renderer::Resolution::Auto
                                            ? RT64::UserConfiguration::Resolution::WindowIntegerScale
                                            : RT64::UserConfiguration::Resolution::Manual;
    application.userConfig.resolutionMultiplier = config.res_option == ultramodern::renderer::Resolution::Original2x
                                                      ? 2.0 * (config.ds_option > 0 ? config.ds_option : 1)
                                                      : static_cast<double>(config.ds_option > 0 ? config.ds_option : 1);
    application.userConfig.downsampleMultiplier = config.ds_option > 0 ? config.ds_option : 1;
    application.userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
    application.userConfig.internalColorFormat =
        config.hpfb_option == ultramodern::renderer::HighPrecisionFramebuffer::On
            ? RT64::UserConfiguration::InternalColorFormat::High
        : config.hpfb_option == ultramodern::renderer::HighPrecisionFramebuffer::Off
            ? RT64::UserConfiguration::InternalColorFormat::Standard
            : RT64::UserConfiguration::InternalColorFormat::Automatic;

    // The game runs at a fixed PAL cadence; presenting at the authored rate
    // keeps timing decisions out of the renderer for now (Phase 5.6 revisits
    // frame pacing and interpolation).
    application.userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
    application.userConfig.refreshRateTarget = 50;
}

class Rt64Renderer final : public ultramodern::renderer::RendererContext {
public:
    Rt64Renderer(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode)
        : rdram_(rdram) {
        g_rom_header.fill(0);
        g_dmem.fill(0);
        g_imem.fill(0);
        g_mi_interrupt = 0;
        g_dpc_registers.fill(0);

        RT64::Application::Core core{};
#ifdef _WIN32
        core.window = window_handle.window;
#else
        core.window = window_handle;
#endif
        core.checkInterrupts = &check_interrupts;
        core.HEADER = g_rom_header.data();
        core.RDRAM = rdram;
        core.DMEM = g_dmem.data();
        core.IMEM = g_imem.data();
        core.MI_INTR_REG = &g_mi_interrupt;
        core.DPC_START_REG = &g_dpc_registers[0];
        core.DPC_END_REG = &g_dpc_registers[1];
        core.DPC_CURRENT_REG = &g_dpc_registers[2];
        core.DPC_STATUS_REG = &g_dpc_registers[3];
        core.DPC_CLOCK_REG = &g_dpc_registers[4];
        core.DPC_BUFBUSY_REG = &g_dpc_registers[5];
        core.DPC_PIPEBUSY_REG = &g_dpc_registers[6];
        core.DPC_TMEM_REG = &g_dpc_registers[7];

        auto* vi = ultramodern::renderer::get_vi_regs();
        core.VI_STATUS_REG = &vi->VI_STATUS_REG;
        core.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG;
        core.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
        core.VI_INTR_REG = &vi->VI_INTR_REG;
        core.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG;
        core.VI_TIMING_REG = &vi->VI_TIMING_REG;
        core.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG;
        core.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG;
        core.VI_LEAP_REG = &vi->VI_LEAP_REG;
        core.VI_H_START_REG = &vi->VI_H_START_REG;
        core.VI_V_START_REG = &vi->VI_V_START_REG;
        core.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
        core.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG;
        core.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;

        RT64::ApplicationConfiguration app_config{};
        app_config.appId = "scars-port";
        // No launcher yet: keep RT64 from reading/writing a user config file and
        // use the compiled-in defaults.
        app_config.useConfigurationFile = false;

        application_ = std::make_unique<RT64::Application>(core, app_config);
        const auto config = ultramodern::renderer::get_graphics_config();
        apply_config(*application_, config);
        application_->userConfig.developerMode = developer_mode;
        // Trim the inferred VI guard rows so the 320x200-ish PAL image fills the
        // window without a border.
        application_->enhancementConfig.presentation.removeBlackBorders = true;

        uint32_t thread_id = 0;
#ifdef _WIN32
        thread_id = window_handle.thread_id;
#endif
        setup_result = map_setup_result(application_->setup(thread_id));
        if (setup_result != ultramodern::renderer::SetupResult::Success) {
            logf("RT64 setup failed (result %d)", static_cast<int>(setup_result));
            application_.reset();
            return;
        }

        chosen_api = map_graphics_api(application_->chosenGraphicsAPI);
        logf("RT64 ready (graphics API %d)", static_cast<int>(chosen_api));
    }

    ~Rt64Renderer() override {
        shutdown();
    }

    bool valid() override {
        return application_ != nullptr;
    }

    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override {
        if (application_ == nullptr || old_config == new_config) {
            return false;
        }
        apply_config(*application_, new_config);
        if (old_config.wm_option != new_config.wm_option) {
            application_->setFullScreen(new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
        }
        return true;
    }

    void enable_instant_present() override {}

    void send_dl(const OSTask* task) override {
        if (application_ == nullptr || task == nullptr) {
            return;
        }

        // RT64 indexes RDRAM by physical offset; the guest hands us a kseg0
        // virtual address (0x803314F0), whose physical form is the low 29 bits.
        const uint32_t data_ptr = static_cast<uint32_t>(task->t.data_ptr) & 0x1FFFFFFFu;
        if (data_ptr == 0) {
            return;
        }

        // Pick the GBI from the microcode in RDRAM. RT64 hashes the text and
        // data blocks; the result is logged once because an unrecognised ucode
        // means the task cannot be decoded and Phase 3 needs RSPRecomp.
        if (application_->interpreter) {
            application_->interpreter->loadUCodeGBI(static_cast<uint32_t>(task->t.ucode) & 0x1FFFFFFFu,
                                                    static_cast<uint32_t>(task->t.ucode_data) & 0x1FFFFFFFu, true);
            const bool recognised = application_->interpreter->hleGBI != nullptr;
            if (!ucode_reported_) {
                ucode_reported_ = true;
                if (recognised) {
                    // GBI carries the decoded ucode family; the human-readable
                    // variant name lives on the matching GBIInstance.
                    logf("RT64 HLE microcode recognised: family %d (ucode=0x%08X)",
                         static_cast<int>(application_->interpreter->hleGBI->ucode),
                         static_cast<uint32_t>(task->t.ucode));
                } else {
                    logf("RT64 does not recognise the graphics ucode at 0x%08X; "
                         "this task cannot be decoded by HLE (RSPRecomp needed)",
                         static_cast<uint32_t>(task->t.ucode));
                }
            }
            if (!recognised) {
                return;
            }
        }

        log_throttled("rt64-dl", 4, "RT64 decoding display list 0x%08X (size 0x%X)", data_ptr,
                      task->t.data_size);
        application_->processDisplayLists(rdram_, data_ptr, data_ptr + task->t.data_size, true);
    }

    void send_dummy_workload(uint32_t fb_address) override {
        (void) fb_address;
        if (application_ != nullptr) {
            application_->updateScreen();
        }
    }

    void update_screen() override {
        if (application_ != nullptr) {
            application_->updateScreen();
        }
    }

    void shutdown() override {
        if (application_ != nullptr) {
            application_->end();
            application_.reset();
        }
    }

    uint32_t get_display_framerate() const override {
        // PAL. The runtime's VI thread still paces at 60 Hz (see the state
        // document); this only reports the authored rate to the renderer.
        return 50;
    }

    float get_resolution_scale() const override {
        return 1.0f;
    }

private:
    uint8_t* rdram_ = nullptr;
    std::unique_ptr<RT64::Application> application_;
    bool ucode_reported_ = false;
};

} // namespace

std::unique_ptr<ultramodern::renderer::RendererContext> create_rt64_renderer(
    uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    return std::make_unique<Rt64Renderer>(rdram, window_handle, developer_mode);
}

} // namespace scars
