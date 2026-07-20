cmake_minimum_required(VERSION 3.16)

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
        PRIV_INCLUDE_DIRS
            "src"
    )
else()
    # ── Standalone build (tests / host development) ───────────────────────
    project(udisplay VERSION 0.2.1 LANGUAGES C CXX)

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
        PUBLIC  include
        PRIVATE src
    )
    target_compile_options(udisplay PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    option(UDISPLAY_BUILD_TESTS "Build unit tests" ON)
    if(UDISPLAY_BUILD_TESTS)
        enable_testing()
        add_subdirectory(test)
    endif()
endif()
