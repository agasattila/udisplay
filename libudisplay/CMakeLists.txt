cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/uDisplayVersion.cmake")

# The configured version reaches libudisplay and everything that links it
# as compile definitions, overriding the defaults in
# include/libudisplay/udisplay.h (see the Version section there).
set(UDISPLAY_VERSION_DEFINITIONS
    "UDISPLAY_VERSION_MAJOR=${UDISPLAY_VERSION_MAJOR}"
    "UDISPLAY_VERSION_MINOR=${UDISPLAY_VERSION_MINOR}"
    "UDISPLAY_VERSION_PATCH=${UDISPLAY_VERSION_PATCH}"
    "UDISPLAY_VERSION_STRING=\"${UDISPLAY_VERSION_FULL}\""
)

if(DEFINED IDF_VERSION_MAJOR)
    # ── ESP-IDF component build ───────────────────────────────────────────
    # The NimBLE backend is opt-in (Kconfig UDISPLAY_ENABLE_NIMBLE): without
    # it, no NimBLE source is compiled and nothing links the bt component.
    set(udisplay_nimble_srcs "")
    if(CONFIG_UDISPLAY_ENABLE_NIMBLE)
        list(APPEND udisplay_nimble_srcs
            "src/tx_ring.c"
            "src/nimble/udisplay_nimble.c"
            "src/nimble/udisplay_nimble_port_esp_idf.c"
        )
    endif()

    idf_component_register(
        SRCS
            "src/protocol.c"
            "src/chunk_server.c"
            "src/framing.c"
            "src/udisplay.c"
            ${udisplay_nimble_srcs}
        INCLUDE_DIRS
            "include"
        PRIV_INCLUDE_DIRS
            "src"
    )
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        ${UDISPLAY_VERSION_DEFINITIONS}
    )
    if(CONFIG_UDISPLAY_ENABLE_NIMBLE)
        # Not a PRIV_REQUIRES entry: ESP-IDF resolves those in an early pass
        # that can run before sdkconfig exists, so a Kconfig-conditional
        # requirement gets lost on a fresh build.
        idf_component_optional_requires(PRIVATE bt)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            "UDISPLAY_NIMBLE_TX_QUEUE_BYTES=${CONFIG_UDISPLAY_NIMBLE_TX_QUEUE_BYTES}u"
        )
    endif()
else()
    # ── Standalone build (tests / host development) ───────────────────────
    project(udisplay VERSION ${UDISPLAY_VERSION} LANGUAGES C CXX)

    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    add_library(udisplay STATIC
        src/protocol.c
        src/chunk_server.c
        src/framing.c
        src/udisplay.c
    )
    target_include_directories(udisplay
        PUBLIC  
            include
        PRIVATE 
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(udisplay PUBLIC
        ${UDISPLAY_VERSION_DEFINITIONS}
    )
    target_compile_options(udisplay PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    # Optional NimBLE backend. Off by default; when ON, the caller provides
    # NimBLE: UDISPLAY_NIMBLE_TARGET names a CMake target that carries the
    # NimBLE host include directories and libraries plus an implementation
    # of src/nimble/udisplay_nimble_port.h for the platform. ESP-IDF builds
    # use the Kconfig option instead (see above) and ship their own glue.
    option(UDISPLAY_ENABLE_NIMBLE "Build the NimBLE BLE transport backend" OFF)
    set(UDISPLAY_NIMBLE_TARGET "" CACHE STRING
        "Target providing NimBLE headers/libraries and the port glue (UDISPLAY_ENABLE_NIMBLE=ON)")
    if(UDISPLAY_ENABLE_NIMBLE)
        if(NOT UDISPLAY_NIMBLE_TARGET OR NOT TARGET ${UDISPLAY_NIMBLE_TARGET})
            message(FATAL_ERROR
                "UDISPLAY_ENABLE_NIMBLE=ON needs UDISPLAY_NIMBLE_TARGET set to an "
                "existing target providing NimBLE and the port glue")
        endif()
        target_sources(udisplay PRIVATE
            src/tx_ring.c
            src/nimble/udisplay_nimble.c
        )
        target_link_libraries(udisplay PUBLIC ${UDISPLAY_NIMBLE_TARGET})
    endif()

    option(UDISPLAY_BUILD_TESTS "Build unit tests" ON)
    # The framework package ships libudisplay without its test/ directory.
    if(UDISPLAY_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt")
        enable_testing()
        add_subdirectory(test)
    endif()
endif()
