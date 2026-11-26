# devkitA64/libnx toolchain for the S.C.A.R.S Nintendo Switch target.
#
# Modelled on dkr-n64recomp's switch/cmake/SwitchToolchain.cmake, which is the
# proven setup for this stack:
#   - devkitPro's own CMake platform modules (Platform/NintendoSwitch.cmake);
#   - C++ exceptions and RTTI stay ENABLED (the runtime throws
#     thread_terminated and uses std::filesystem/RTTI; devkitPro's stock
#     templates pass -fno-exceptions/-fno-rtti - do not add them);
#   - SCARS_SWITCH is the marker the root CMakeLists consumes;
#   - devkitPro's aarch64 pkg-config is a bash wrapper on Windows, so the
#     Switch find modules read portlibs directly.
#
# Configure with:
#   cmake --preset switch
# from a shell where DEVKITPRO points at the devkitPro install prefix.

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR
        "The DEVKITPRO environment variable must point at the devkitPro "
        "install prefix (for example C:/devkitPro).")
endif()
file(TO_CMAKE_PATH "$ENV{DEVKITPRO}" DEVKITPRO)
# devkitPro's helpers concatenate this prefix into strings; forward slashes are
# required. dkp-initialize-path re-reads the environment variable.
string(REPLACE "\\" "/" DEVKITPRO "${DEVKITPRO}")
set(ENV{DEVKITPRO} "${DEVKITPRO}")

set(DEVKITA64 "${DEVKITPRO}/devkitA64")
set(NX_ROOT "${DEVKITPRO}/libnx")
set(SCARS_SWITCH_PORTLIBS "${DEVKITPRO}/portlibs/switch")

foreach(required IN ITEMS
        "${NX_ROOT}/switch.specs"
        "${NX_ROOT}/lib/libnx.a"
        "${DEVKITA64}/bin"
        "${SCARS_SWITCH_PORTLIBS}/lib")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR
            "devkitPro is missing ${required}. Update the Switch SDK with:\n"
            "  dkp-pacman -Syu --needed switch-dev switch-tools")
    endif()
endforeach()

set(CMAKE_SYSTEM_NAME NintendoSwitch)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# The host build tool must be resolved before devkitPro's toolchain turns off
# host-path search.
find_program(SCARS_HOST_NINJA NAMES ninja ninja-build HINTS ENV PATH)
if(SCARS_HOST_NINJA)
    set(CMAKE_MAKE_PROGRAM "${SCARS_HOST_NINJA}" CACHE FILEPATH "Host ninja" FORCE)
endif()

# Host-side tools the recompile pipeline still needs while cross-compiling.
find_program(SCARS_HOST_GIT NAMES git HINTS ENV PATH)
if(SCARS_HOST_GIT)
    set(GIT_EXECUTABLE "${SCARS_HOST_GIT}" CACHE FILEPATH "Host Git" FORCE)
endif()
find_program(SCARS_HOST_PYTHON NAMES python python3 HINTS ENV PATH)
if(SCARS_HOST_PYTHON)
    set(Python3_EXECUTABLE "${SCARS_HOST_PYTHON}" CACHE FILEPATH "Host Python 3" FORCE)
endif()

# devkitPro's official platform module resolves the compilers, find-root
# prefixes (portlibs + libnx + devkitA64 + tools), pkg-config and the
# elf2nro/nacptool family.
include("${DEVKITPRO}/cmake/Switch.cmake")

set(SCARS_SWITCH ON CACHE BOOL "Building the S.C.A.R.S Nintendo Switch target" FORCE)
set(ENV{DEVKITPRO} "${DEVKITPRO}")
set(SCARS_SWITCH_DEVKITPRO_ENV "${DEVKITPRO}" CACHE INTERNAL
    "DEVKITPRO prefix exported for switch.specs and elf2nro")
# Read by cmake/switch/FindSDL2.cmake and FindNXVK.cmake in subdirectory scopes.
set(SCARS_SWITCH_PORTLIBS "${SCARS_SWITCH_PORTLIBS}" CACHE INTERNAL
    "devkitPro Switch portlibs prefix")

find_program(SCARS_SWITCH_PKG_CONFIG NAMES aarch64-none-elf-pkg-config
    HINTS "${SCARS_SWITCH_PORTLIBS}/bin")
if(SCARS_SWITCH_PKG_CONFIG)
    set(PKG_CONFIG_EXECUTABLE "${SCARS_SWITCH_PKG_CONFIG}" CACHE FILEPATH
        "devkitPro target pkg-config")
endif()

# NACP display fields for the .nro.
set(SCARS_SWITCH_NRO_TITLE "S.C.A.R.S (recompiled)" CACHE STRING "NACP display name")
set(SCARS_SWITCH_NRO_AUTHOR "scars-n64recomp contributors" CACHE STRING "NACP author")
set(SCARS_SWITCH_NRO_TITLEID "0100000000005CA5" CACHE STRING "NACP title id (homebrew range)")
