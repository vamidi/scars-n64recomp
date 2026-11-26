# SDL2 from the devkitPro switch portlibs (switch-sdl2).
#
# devkitPro's aarch64-none-elf-pkg-config is a bash wrapper script on Windows,
# so the libraries are resolved from the portlibs prefix directly instead of by
# executing pkg-config.
if(NOT SCARS_SWITCH)
    message(FATAL_ERROR "cmake/switch/FindSDL2.cmake is Switch-only; configure with the Switch toolchain.")
endif()

find_path(SDL2_INCLUDE_DIR SDL.h
    PATHS "${SCARS_SWITCH_PORTLIBS}/include/SDL2" NO_DEFAULT_PATH)
find_library(SDL2_MAIN_LIBRARY NAMES SDL2main
    PATHS "${SCARS_SWITCH_PORTLIBS}/lib" NO_DEFAULT_PATH)
find_library(SDL2_LIBRARY NAMES SDL2
    PATHS "${SCARS_SWITCH_PORTLIBS}/lib" NO_DEFAULT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2
    REQUIRED_VARS SDL2_LIBRARY SDL2_MAIN_LIBRARY SDL2_INCLUDE_DIR)

if(SDL2_FOUND)
    set(SDL2_LIBRARIES ${SDL2_MAIN_LIBRARY} ${SDL2_LIBRARY})
    set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
    # SDL2main provides main() and calls SDL_main, so it must precede SDL2.
    if(NOT TARGET SDL2::SDL2main)
        add_library(SDL2::SDL2main STATIC IMPORTED GLOBAL)
        set_target_properties(SDL2::SDL2main PROPERTIES
            IMPORTED_LOCATION "${SDL2_MAIN_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
    endif()
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 STATIC IMPORTED GLOBAL)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_LOCATION "${SDL2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "SDL2::SDL2main")
    endif()
endif()
mark_as_advanced(SDL2_INCLUDE_DIR SDL2_MAIN_LIBRARY SDL2_LIBRARY)
