# Nintendo Switch platform adapters, at the project boundary.
#
# Nothing here patches RT64, N64ModernRuntime or N64Recomp: RT64's own
# RT64_SDL_WINDOW_VULKAN option plus the devkitPro portlibs are enough. This
# file only checks the toolchain assumptions and points CMake at the Switch
# find modules.
if(NOT SCARS_SWITCH)
    return()
endif()

# The runtime throws and catches (ultramodern::thread_terminated) and uses
# std::filesystem/RTTI, so exceptions and RTTI must actually work. Stock Switch
# homebrew templates disable both; assert rather than trust the toolchain.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_QUIET TRUE)
check_cxx_source_compiles("
struct Boom { };
int main(void) {
    try { throw Boom(); } catch (const Boom &) { return 0; }
    return 1;
}" SCARS_SWITCH_HAS_EXCEPTIONS)
check_cxx_source_compiles("
#include <typeinfo>
struct Poly { virtual ~Poly() { } };
int main(void) {
    Poly value;
    return typeid(value).name() != nullptr ? 0 : 1;
}" SCARS_SWITCH_HAS_RTTI)
unset(CMAKE_REQUIRED_QUIET)
if(NOT SCARS_SWITCH_HAS_EXCEPTIONS)
    message(FATAL_ERROR "The Switch toolchain must compile C++ exceptions (do not pass -fno-exceptions).")
endif()
if(NOT SCARS_SWITCH_HAS_RTTI)
    message(FATAL_ERROR "The Switch toolchain must preserve C++ RTTI (do not pass -fno-rtti).")
endif()

# newlib hides POSIX/GNU declarations under strict -std=c++20 unless _GNU_SOURCE
# is set; N64ModernRuntime's vendored fmt/miniz call fileno/fdopen/putc_unlocked.
add_compile_definitions(_GNU_SOURCE)

# Switch find modules (SDL2, nxvk), mirroring dkr-n64recomp.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/switch")

find_package(SDL2 REQUIRED)
find_package(NXVK REQUIRED)

# RT64's Vulkan backend is loaded through volk at runtime; nxvk supplies the
# console's libvulkan. Linking the interface target keeps the import library and
# any required link options in the executable.
set(SCARS_SWITCH_LINK_LIBS nxvk::nxvk CACHE INTERNAL "Switch-specific link libraries")

# The desktop launcher/config UI is not part of this target.
set(SCARS_RENDERER_SDL_RT64 ON CACHE BOOL "SDL2 + RT64 renderer" FORCE)
