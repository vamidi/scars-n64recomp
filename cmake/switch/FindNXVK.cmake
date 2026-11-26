# nxvk: the Mesa NVK driver ported to the Nintendo Switch.
#
# RT64 renders through Vulkan; on the console that Vulkan is nxvk. The portlib
# is not part of devkitPro and must be built and installed into
# ${DEVKITPRO}/portlibs/switch (see docs/SWITCH.md). devkitPro's
# aarch64-none-elf-pkg-config is a bash wrapper on Windows, so nxvk.pc is
# parsed as text instead of executing it.
if(NOT SCARS_SWITCH)
    message(FATAL_ERROR "cmake/switch/FindNXVK.cmake is Switch-only; configure with the Switch toolchain.")
endif()

set(_nxvk_pc "${SCARS_SWITCH_PORTLIBS}/lib/pkgconfig/nxvk.pc")
if(NOT EXISTS "${_nxvk_pc}")
    message(FATAL_ERROR
        "The nxvk portlib is not installed under ${SCARS_SWITCH_PORTLIBS}.\n"
        "RT64 needs it for Vulkan on the console. See docs/SWITCH.md "
        "(build nxvk, then install its .pc, headers and libvulkan stub).")
endif()
file(READ "${_nxvk_pc}" _nxvk_pc_text)

# Expand .pc variable references (two passes cover ${exec_prefix}=${prefix}).
foreach(var IN ITEMS prefix exec_prefix libdir includedir)
    if(_nxvk_pc_text MATCHES "(^|\n)${var}=([^\n\r]*)")
        set(_nxvk_raw_${var} "${CMAKE_MATCH_2}")
    else()
        set(_nxvk_raw_${var} "")
    endif()
endforeach()
foreach(pass RANGE 2)
    foreach(ref IN ITEMS prefix exec_prefix libdir includedir)
        foreach(target IN ITEMS prefix exec_prefix libdir includedir)
            string(REPLACE "\${${ref}}" "${_nxvk_raw_${ref}}"
                _nxvk_raw_${target} "${_nxvk_raw_${target}}")
        endforeach()
    endforeach()
endforeach()

set(_nxvk_libs "")
set(_nxvk_cflags "")
if(_nxvk_pc_text MATCHES "(^|\n)Libs:([^\n\r]*)")
    set(_nxvk_libs "${CMAKE_MATCH_2}")
endif()
if(_nxvk_pc_text MATCHES "(^|\n)Cflags:([^\n\r]*)")
    set(_nxvk_cflags "${CMAKE_MATCH_2}")
endif()
string(REPLACE "\${libdir}" "${_nxvk_raw_libdir}" _nxvk_libs "${_nxvk_libs}")
string(REPLACE "\${includedir}" "${_nxvk_raw_includedir}" _nxvk_cflags "${_nxvk_cflags}")
separate_arguments(_nxvk_libs UNIX_COMMAND "${_nxvk_libs}")
separate_arguments(_nxvk_cflags UNIX_COMMAND "${_nxvk_cflags}")

set(_nxvk_link_dirs "")
set(_nxvk_link_opts "")
set(_nxvk_link_libs "")
set(_nxvk_includes "")
set(_nxvk_defines "")
set(_nxvk_opts "")
foreach(token IN LISTS _nxvk_libs)
    if(token MATCHES "^-L(.*)")
        list(APPEND _nxvk_link_dirs "${CMAKE_MATCH_1}")
    elseif(token MATCHES "^-l(.*)")
        list(APPEND _nxvk_link_libs "${CMAKE_MATCH_1}")
    else()
        list(APPEND _nxvk_link_opts "${token}")
    endif()
endforeach()
foreach(token IN LISTS _nxvk_cflags)
    if(token MATCHES "^-I(.*)")
        list(APPEND _nxvk_includes "${CMAKE_MATCH_1}")
    elseif(token MATCHES "^-D(.*)")
        list(APPEND _nxvk_defines "${CMAKE_MATCH_1}")
    else()
        list(APPEND _nxvk_opts "${token}")
    endif()
endforeach()

if(NOT TARGET nxvk::nxvk)
    add_library(nxvk::nxvk INTERFACE IMPORTED GLOBAL)
    target_include_directories(nxvk::nxvk INTERFACE ${_nxvk_includes})
    target_compile_definitions(nxvk::nxvk INTERFACE ${_nxvk_defines})
    target_compile_options(nxvk::nxvk INTERFACE ${_nxvk_opts})
    if(_nxvk_link_dirs)
        target_link_directories(nxvk::nxvk INTERFACE ${_nxvk_link_dirs})
    endif()
    target_link_options(nxvk::nxvk INTERFACE ${_nxvk_link_opts})
    target_link_libraries(nxvk::nxvk INTERFACE ${_nxvk_link_libs})
endif()
set(NXVK_FOUND TRUE)
