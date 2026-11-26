add_executable(scars
    main.cpp
    audio_hle.cpp
    crash_handler.cpp
    guest_hw.cpp
    guest_pak.cpp
    guest_mmio.cpp
    scars_game.cpp
    scars_log.cpp
    scars_overlays.cpp
    trace_backend.cpp
    ultra_stubs_scars.cpp)

target_compile_definitions(scars PRIVATE
    SCARS_VERSION="${SCARS_VERSION}"
    # RT64's hlsl++ breaks if Windows.h's min/max macros are visible.
    NOMINMAX)

target_link_libraries(scars PRIVATE librecomp ultramodern)

if(WIN32)
    # ultramodern's timer code uses the multimedia timer API.
    target_link_libraries(scars PRIVATE winmm)
    # crash_handler.cpp symbolizes stacks with DbgHelp.
    target_link_libraries(scars PRIVATE dbghelp)
endif()

if(SCARS_RENDERER_SDL_RT64)
    target_sources(scars PRIVATE
        sdl_frontend.cpp
        rt64_renderer.cpp)
    # RT64 sets these at directory scope for its own sources, so a consumer that
    # includes its headers must set them too: HLSL_CPU selects hlsl++'s CPU
    # implementation, which the API headers depend on.
    target_compile_definitions(scars PRIVATE
        SCARS_RENDERER_SDL_RT64=1
        SDL_MAIN_HANDLED
        HLSL_CPU
        FFX_GCC
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
    # RT64 uses directory-scoped include_directories() for its own contrib tree,
    # so consumers have to repeat them; the plume/plume-contrib paths arrive via
    # the rt64 link, these are the rest (hlsl++ is what rt64_hlslpp.h needs).
    target_include_directories(scars PRIVATE
        "${SCARS_RT64_DIR}/src"
        "${SCARS_RT64_DIR}/src/contrib"
        "${SCARS_RT64_DIR}/src/contrib/plume"
        "${SCARS_RT64_DIR}/src/contrib/imgui"
        "${SCARS_RT64_DIR}/src/contrib/hlslpp/include"
        "${SCARS_RT64_DIR}/src/contrib/mupen64plus-core/src/api"
        "${SCARS_RT64_DIR}/src/contrib/nativefiledialog-extended/src/include"
        "${SCARS_RT64_SDL2_DIR}/include")
    target_link_libraries(scars PRIVATE rt64)
    if(WIN32)
        target_link_directories(scars PRIVATE "${SCARS_RT64_SDL2_DIR}/lib/x64")
        # SDL2main is deliberately not linked: this is a console executable
        # with its own main(), and SDL_MAIN_HANDLED disables the rename.
        target_link_libraries(scars PRIVATE SDL2)
        add_custom_command(TARGET scars POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SCARS_RT64_SDL2_DIR}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:scars>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SCARS_RT64_DXC_DLLS} "$<TARGET_FILE_DIR:scars>"
            COMMENT "Staging SDL2.dll and the DXC shader compiler next to scars.exe")
    else()
        target_link_libraries(scars PRIVATE SDL2::SDL2)
        if(SCARS_SWITCH AND TARGET nxvk::nxvk)
            # nxvk supplies the console's libvulkan for RT64's backend.
            target_link_libraries(scars PRIVATE nxvk::nxvk)
        endif()
    endif()
endif()

# Recompiled RSP microcode (build/generated_rsp via the generate-rsp target).
# Without it the runtime falls back to the tracing RSP stub.
set(SCARS_GENERATED_RSP "${CMAKE_SOURCE_DIR}/build/generated_rsp/aspMain.cpp")
if(EXISTS "${SCARS_GENERATED_RSP}")
    target_sources(scars PRIVATE "${SCARS_GENERATED_RSP}")
    target_compile_definitions(scars PRIVATE SCARS_HAVE_RECOMP_ASPMAIN=1)
    message(STATUS "scars: compiling the recompiled audio microcode")
else()
    message(STATUS "scars: no recompiled RSP microcode (run the generate-rsp target)")
endif()

if(SCARS_LINK_GENERATED)
    include("${CMAKE_SOURCE_DIR}/cmake/ScarsGeneratedPayload.cmake")
    scars_link_generated_payload(scars)
    # scars_overlays.cpp includes the generated recomp_overlays.inl.
    target_include_directories(scars PRIVATE "${SCARS_GENERATED_DIR}")
endif()
