add_executable(scars
    main.cpp
    ultra_stubs_scars.cpp)

target_compile_definitions(scars PRIVATE SCARS_VERSION="${SCARS_VERSION}")

target_link_libraries(scars PRIVATE librecomp ultramodern)

if(WIN32)
    # ultramodern's timer code uses the multimedia timer API.
    target_link_libraries(scars PRIVATE winmm)
endif()

if(SCARS_LINK_GENERATED)
    include("${CMAKE_SOURCE_DIR}/cmake/ScarsGeneratedPayload.cmake")
    scars_link_generated_payload(scars)
endif()
