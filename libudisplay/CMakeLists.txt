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
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        ${UDISPLAY_VERSION_DEFINITIONS}
    )
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

    option(UDISPLAY_BUILD_TESTS "Build unit tests" ON)
    # The framework package ships libudisplay without its test/ directory.
    if(UDISPLAY_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt")
        enable_testing()
        add_subdirectory(test)
    endif()
endif()
