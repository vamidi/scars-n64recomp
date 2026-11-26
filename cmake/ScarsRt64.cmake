# RT64 renderer dependency.
#
# RT64 is used unmodified (MIT licensed). The checkout lives in
# thirdparty/rt64 (gitignored, ~380 MB): fetch it with tools/fetch_rt64.ps1, or
# point SCARS_RT64_DIR at an existing tree. The same options the Diddy Kong
# Racing port uses apply:
#   RT64_STATIC            - link it into the executable, no rt64.dll
#   RT64_SDL_WINDOW_VULKAN - on non-Windows targets RT64 takes an SDL_Window*
#                            (Switch uses this with the NXVK Vulkan driver)
set(SCARS_RT64_DIR "${CMAKE_SOURCE_DIR}/thirdparty/rt64"
    CACHE PATH "RT64 checkout used by the RT64 renderer backend")

if(NOT EXISTS "${SCARS_RT64_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "RT64 was not found at ${SCARS_RT64_DIR}.\n"
        "Fetch it with: powershell -File tools/fetch_rt64.ps1\n"
        "or configure with -DSCARS_RT64_DIR=<path to an RT64 checkout>.")
endif()

set(RT64_STATIC TRUE CACHE BOOL "Build RT64 as a static library" FORCE)
if(NOT WIN32)
    set(RT64_SDL_WINDOW_VULKAN TRUE CACHE BOOL "RT64 expects an SDL window" FORCE)
endif()

if(SCARS_SWITCH)
    # RT64's CMakeLists only enables the SDL-window RenderWindow variant when
    # CMAKE_SYSTEM_NAME matches Linux. The console builds the same variant
    # (an SDL_Window* on top of nxvk's Vulkan), so set the same defines here;
    # they apply to the targets created below, RT64 included.
    add_compile_definitions(PLUME_SDL_VULKAN_ENABLED RT64_SDL_WINDOW_VULKAN)
endif()

message(STATUS "SCARS: RT64 from ${SCARS_RT64_DIR}")
add_subdirectory("${SCARS_RT64_DIR}" "${CMAKE_BINARY_DIR}/rt64")

# The Windows backend loads DXC beside the executable, and SDL2 comes from
# RT64's bundled mupen64plus deps.
if(WIN32)
    set(SCARS_RT64_DXC_DLLS
        "${SCARS_RT64_DIR}/src/contrib/dxc/bin/x64/dxcompiler.dll"
        "${SCARS_RT64_DIR}/src/contrib/dxc/bin/x64/dxil.dll")
    set(SCARS_RT64_SDL2_DIR "${SCARS_RT64_DIR}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
endif()
