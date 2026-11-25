# Compiles the N64Recomp output directory into a static library and links it
# into the game executable.
#
# N64Recomp emits one translation unit per group of functions (plus overlay and
# patch units). The corpus is generated output: it is not tracked, it is not
# distributed, and it must be rebuilt with the matching ROM/ELF.

function(scars_link_generated_payload target)
    if(NOT IS_DIRECTORY "${SCARS_GENERATED_DIR}")
        message(FATAL_ERROR
            "SCARS_LINK_GENERATED=ON but ${SCARS_GENERATED_DIR} does not exist. "
            "Run the recompile target first (cmake --build <dir> --target recompile).")
    endif()

    file(GLOB_RECURSE _scars_generated CONFIGURE_DEPENDS
        "${SCARS_GENERATED_DIR}/*.cpp"
        "${SCARS_GENERATED_DIR}/*.c")
    list(LENGTH _scars_generated _scars_generated_count)
    if(_scars_generated_count EQUAL 0)
        message(FATAL_ERROR
            "SCARS_LINK_GENERATED=ON but no .cpp/.c files were found under "
            "${SCARS_GENERATED_DIR}. Run the recompile target first.")
    endif()
    message(STATUS "scars: linking ${_scars_generated_count} recompiled translation units from ${SCARS_GENERATED_DIR}")

    add_library(scars_generated STATIC ${_scars_generated})
    target_include_directories(scars_generated PRIVATE
        "${SCARS_GENERATED_DIR}"
        ${SCARS_RUNTIME_INCLUDE_DIRS})

    separate_arguments(_scars_generated_flags UNIX_COMMAND "${SCARS_GENERATED_OPTFLAGS}")
    target_compile_options(scars_generated PRIVATE ${_scars_generated_flags})

    if(MSVC)
        # Generated code casts guest memory widths constantly, has unreachable
        # handlers and set-but-unused locals; none of it is actionable here.
        target_compile_options(scars_generated PRIVATE
            /bigobj /wd4068 /wd4101 /wd4189 /wd4244 /wd4267 /wd4702)
    endif()

    target_link_libraries(${target} PRIVATE scars_generated)
    target_compile_definitions(${target} PRIVATE SCARS_LINK_GENERATED=1)
endfunction()
