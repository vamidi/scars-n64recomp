# N64ModernRuntime wiring.
#
# N64ModernRuntime provides two libraries:
#   ultramodern -- the libultra replacement (threads, timers, audio, VI, ...)
#   librecomp   -- recomp runtime (sections, overlays, mods, patches, RSP)
#
# librecomp's own CMakeLists.txt unconditionally adds the N64Recomp checkout
# nested inside N64ModernRuntime. This project builds the vamidi fork from
# thirdparty/n64recomp instead, so librecomp is compiled here against those
# targets. Its sources are globbed so new upstream files are picked up.

set(SCARS_RUNTIME_DIR "${SCARS_MODERN_RUNTIME_DIR}")

foreach(_scars_required
        "librecomp/src"
        "ultramodern/include"
        "thirdparty/miniz"
        "thirdparty/o1heap/o1heap")
    if(NOT EXISTS "${SCARS_RUNTIME_DIR}/${_scars_required}")
        message(FATAL_ERROR
            "N64ModernRuntime was not found at ${SCARS_RUNTIME_DIR} "
            "(missing ${_scars_required}).\n"
            "Fetch the submodules first:\n"
            "  git submodule update --init --recursive")
    endif()
endforeach()

if(NOT TARGET N64Recomp OR NOT TARGET LiveRecomp)
    message(FATAL_ERROR
        "librecomp is linked against N64Recomp, which is not configured. "
        "Enable SCARS_BUILD_RECOMPILER (or provide the targets yourself).")
endif()

# miniz is self-contained and can be used as its own subproject.
add_subdirectory("${SCARS_RUNTIME_DIR}/thirdparty/miniz"
                 "${CMAKE_BINARY_DIR}/thirdparty/miniz"
                 EXCLUDE_FROM_ALL)

# ultramodern has no N64Recomp dependency, so its CMakeLists is used as-is.
add_subdirectory("${SCARS_RUNTIME_DIR}/ultramodern"
                 "${CMAKE_BINARY_DIR}/thirdparty/ultramodern")

# Upstream passes -Wno-unused-parameter unconditionally. clang-cl accepts it,
# cl.exe rejects it as an unknown /W option (D8021). Strip it for MSVC.
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    get_target_property(_scars_ultramodern_opts ultramodern COMPILE_OPTIONS)
    if(_scars_ultramodern_opts)
        list(REMOVE_ITEM _scars_ultramodern_opts -Wno-unused-parameter)
        set_property(TARGET ultramodern PROPERTY COMPILE_OPTIONS "${_scars_ultramodern_opts}")
    endif()
    unset(_scars_ultramodern_opts)
endif()

file(GLOB_RECURSE SCARS_LIBRECOMP_SOURCES CONFIGURE_DEPENDS
    "${SCARS_RUNTIME_DIR}/librecomp/src/*.cpp")

add_library(librecomp STATIC
    ${SCARS_LIBRECOMP_SOURCES}
    "${SCARS_RUNTIME_DIR}/thirdparty/o1heap/o1heap/o1heap.c")

target_include_directories(librecomp PUBLIC
    "${SCARS_RUNTIME_DIR}/librecomp/include"
    "${SCARS_RUNTIME_DIR}/librecomp/src"
    "${SCARS_RUNTIME_DIR}/ultramodern/include"
    "${SCARS_RUNTIME_DIR}/thirdparty"
    "${SCARS_RUNTIME_DIR}/thirdparty/concurrentqueue"
    "${SCARS_RUNTIME_DIR}/thirdparty/o1heap")
target_include_directories(librecomp PRIVATE
    "${SCARS_RUNTIME_DIR}/librecomp/include/librecomp")

# GNU-style warning flags are accepted by clang-cl but not by cl.exe.
if(NOT MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(librecomp PRIVATE -Wno-unused-parameter)
endif()
if(WIN32)
    target_compile_definitions(librecomp PRIVATE NOMINMAX)
endif()

# Public: consumers of librecomp headers need recomp.h from N64Recomp.
target_link_libraries(librecomp PUBLIC ultramodern miniz N64Recomp PRIVATE LiveRecomp)

# Include paths a generated translation unit needs.
set(SCARS_RUNTIME_INCLUDE_DIRS
    "${SCARS_RUNTIME_DIR}/librecomp/include"
    "${SCARS_RUNTIME_DIR}/ultramodern/include"
    "${SCARS_N64RECOMP_DIR}/include"
    CACHE STRING "Include directories for the recompiled corpus")

