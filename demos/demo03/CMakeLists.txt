cmake_minimum_required(VERSION 3.16)
project(demo03 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── libudisplay (skipped when built from a parent that already added it) ──────
if (NOT TARGET udisplay)
    set(UDISPLAY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../libudisplay
        ${CMAKE_CURRENT_BINARY_DIR}/libudisplay
    )
endif()

# ── Code generation: udisplay-gen build demo03.yaml --lang cpp --modern ───────
set(UDISPLAY_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${UDISPLAY_GEN_DIR})

add_custom_command(
    OUTPUT
        ${UDISPLAY_GEN_DIR}/udisplay_ui.hpp
        ${UDISPLAY_GEN_DIR}/udisplay_ui.bin
    COMMAND
        udisplay-gen build ${CMAKE_CURRENT_SOURCE_DIR}/demo03.yaml
                           --lang cpp --modern
                           -o ${UDISPLAY_GEN_DIR}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/demo03.yaml
    COMMENT
        "Generating udisplay_ui.hpp from demo03.yaml (--lang cpp --modern)"
    VERBATIM
)
add_custom_target(demo03_ui_gen
    DEPENDS
        ${UDISPLAY_GEN_DIR}/udisplay_ui.hpp
        ${UDISPLAY_GEN_DIR}/udisplay_ui.bin
)

# ── demo03 executable ─────────────────────────────────────────────────────────
add_executable(demo03
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/demo_tcp.c
)
add_dependencies(demo03 demo03_ui_gen)
target_include_directories(demo03 PRIVATE
    ${UDISPLAY_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
target_link_libraries(demo03 PRIVATE udisplay m pthread)
target_compile_options(demo03 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
