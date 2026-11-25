# SCARS: Recompiled -- native static recompilation of S.C.A.R.S (Nintendo 64).
#
# Licensed under the MIT License (see LICENSE). No game code or assets are
# built into this tree; the recompiled corpus is produced locally by N64Recomp
# into build/generated/ (gitignored) from a user-supplied ELF/ROM.
#
# Layout mirrors the Tales of Legendia PS2 recomp: thirdparty/ holds the
# engine checkouts (here the N64Recomp fork and N64ModernRuntime) and this
# file wires them into one build.

cmake_minimum_required(VERSION 3.24)

# Regeneration rewrites the corpus; ccache reuses unchanged function bodies.
find_program(SCARS_CCACHE ccache)
if(SCARS_CCACHE)
    message(STATUS "SCARS: using ccache at ${SCARS_CCACHE}")
    set(CMAKE_C_COMPILER_LAUNCHER "${SCARS_CCACHE}" CACHE STRING "C compiler launcher")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCARS_CCACHE}" CACHE STRING "C++ compiler launcher")
endif()

# Version: `git describe` when available, otherwise a dev placeholder.
set(SCARS_VERSION "0.0.0-dev")
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                describe --tags --always --dirty
        OUTPUT_VARIABLE SCARS_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE SCARS_GIT_DESCRIBE_RESULT)
    if(SCARS_GIT_DESCRIBE_RESULT EQUAL 0 AND NOT SCARS_GIT_DESCRIBE STREQUAL "")
        set(SCARS_VERSION "${SCARS_GIT_DESCRIBE}")
    endif()
endif()

project(ScarsRecomp
    VERSION 0.1.0
    DESCRIPTION "S.C.A.R.S static recompilation"
    LANGUAGES C CXX)

message(STATUS "SCARS: version ${SCARS_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Keep every executable (N64Recomp, RSPRecomp, scars) in one place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(SCARS_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

# The recompiler. Point this at another checkout of the fork if needed.
set(SCARS_N64RECOMP_DIR "${SCARS_THIRDPARTY_DIR}/n64recomp"
    CACHE PATH "Checkout of the N64Recomp fork (built as the recompiler tools)")

# The runtime that recompiled code links against (librecomp + ultramodern).
set(SCARS_MODERN_RUNTIME_DIR "${SCARS_THIRDPARTY_DIR}/N64ModernRuntime"
    CACHE PATH "Checkout of N64ModernRuntime (N64 runtime libraries)")

# Where N64Recomp writes its .cpp output (see recomp/scars.toml).
set(SCARS_GENERATED_DIR "${CMAKE_SOURCE_DIR}/build/generated"
    CACHE PATH "Directory containing the N64Recomp-generated corpus")

option(SCARS_BUILD_RECOMPILER
    "Build the N64Recomp/RSPRecomp tools from SCARS_N64RECOMP_DIR."
    ON)

option(SCARS_BUILD_RUNTIME
    "Build the scars executable and the N64ModernRuntime libraries."
    ON)

option(SCARS_LINK_GENERATED
    "Compile and link the locally generated corpus from SCARS_GENERATED_DIR."
    OFF)

if(SCARS_LINK_GENERATED AND NOT SCARS_BUILD_RUNTIME)
    message(FATAL_ERROR
        "SCARS_LINK_GENERATED=ON requires SCARS_BUILD_RUNTIME=ON: the generated "
        "corpus is linked into the scars executable.")
endif()

# Generated code must not alias-optimize across guest memory widths, and guest
# arithmetic is expected to wrap.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl: keep the MSVC driver but pass clang's aliasing/wrapping flags.
    set(SCARS_GENERATED_OPTFLAGS
        "/O2 /clang:-fno-strict-aliasing /clang:-fwrapv" CACHE STRING
        "Space-separated flags for the generated corpus.")
elseif(MSVC)
    set(SCARS_GENERATED_OPTFLAGS "/O2" CACHE STRING
        "Space-separated flags for the generated corpus.")
else()
    set(SCARS_GENERATED_OPTFLAGS "-O2 -g0 -fno-strict-aliasing -fwrapv" CACHE STRING
        "Space-separated flags for the generated corpus.")
endif()

if(SCARS_BUILD_RECOMPILER)
    if(NOT EXISTS "${SCARS_N64RECOMP_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "N64Recomp was not found at ${SCARS_N64RECOMP_DIR}.\n"
            "Fetch the submodules first:\n"
            "  git submodule update --init --recursive")
    endif()
    add_subdirectory("${SCARS_N64RECOMP_DIR}" "${CMAKE_BINARY_DIR}/n64recomp")
endif()

if(SCARS_BUILD_RUNTIME)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScarsRuntime.cmake")
    add_subdirectory(src)
endif()

# Convenience target: run the recompiler over the tracked config.
if(TARGET N64RecompCLI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/recomp/scars.toml")
    add_custom_target(recompile
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SCARS_GENERATED_DIR}"
        COMMAND $<TARGET_FILE:N64RecompCLI> "${CMAKE_CURRENT_SOURCE_DIR}/recomp/scars.toml"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS N64RecompCLI
        COMMENT "N64Recomp: recomp/scars.toml -> ${SCARS_GENERATED_DIR}"
        USES_TERMINAL
        VERBATIM)
endif()

# Full AOT pipeline on Windows: Ghidra function export -> n64sym libultra
# names -> symbols TOML -> N64Recomp. Requires Ghidra and Python 3.
if(WIN32 AND TARGET N64RecompCLI)
    find_program(SCARS_POWERSHELL NAMES powershell pwsh)
    set(SCARS_GHIDRA_DIR "C:/ghidra_12.1.2_PUBLIC"
        CACHE PATH "Ghidra install used by the generate-aot target")
    if(SCARS_POWERSHELL)
        add_custom_target(generate-aot
            COMMAND "${SCARS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_aot.ps1"
                    -Ghidra "${SCARS_GHIDRA_DIR}"
                    -Recompiler $<TARGET_FILE:N64RecompCLI>
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS N64RecompCLI
            COMMENT "Ghidra + n64sym + N64Recomp (writes build/generated)"
            USES_TERMINAL
            VERBATIM)
    endif()
endif()

