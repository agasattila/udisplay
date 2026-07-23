cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/uDisplayVersion.cmake")

set(UDISPLAY_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/include/version.h.in"
    "${UDISPLAY_GENERATED_DIR}/udisplay/version.h"
    @ONLY
)


if(DEFINED IDF_VERSION_MAJOR)
    # ── ESP-IDF component build ───────────────────────────────────────────
    idf_component_register(
        SRCS
            "src/protocol.c"
            "src/chunk_server.c"
            "src/framing.c"
            "src/udisplay.c"
        INCLUDE_DIRS
            "include"
            "${UDISPLAY_GENERATED_DIR}"
        PRIV_INCLUDE_DIRS
            "src"
    )
else()
    # ── Standalone build (tests / host development) ───────────────────────
    project(udisplay VERSION ${UDISPLAY_VERSION} LANGUAGES C CXX)

    # configure_file(
    #     "${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in"
    #     "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h"
    #     @ONLY
    # )


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
            "${UDISPLAY_GENERATED_DIR}"
        PRIVATE 
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_options(udisplay PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    option(UDISPLAY_BUILD_TESTS "Build unit tests" ON)
    if(UDISPLAY_BUILD_TESTS)
        enable_testing()
        add_subdirectory(test)
    endif()
endif()
