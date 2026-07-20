cmake_minimum_required(VERSION 3.16)
project(demo01 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── libudisplay (skipped when built from a parent that already added it) ──────
if (NOT TARGET udisplay)
    set(UDISPLAY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../libudisplay
        ${CMAKE_CURRENT_BINARY_DIR}/libudisplay
    )
endif()

# ── Code generation: udisplay-gen build demo01.yaml ───────────────────────────
set(UDISPLAY_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${UDISPLAY_GEN_DIR})

add_custom_command(
    OUTPUT
        ${UDISPLAY_GEN_DIR}/udisplay_ui.h
        ${UDISPLAY_GEN_DIR}/udisplay_ui.c
    COMMAND
        udisplay-gen build ${CMAKE_CURRENT_SOURCE_DIR}/demo01.yaml
                           -o ${UDISPLAY_GEN_DIR}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/demo01.yaml
    COMMENT
        "Generating udisplay_ui.h / udisplay_ui.c from demo01.yaml"
    VERBATIM
)
add_custom_target(demo01_ui_gen
    DEPENDS
        ${UDISPLAY_GEN_DIR}/udisplay_ui.h
        ${UDISPLAY_GEN_DIR}/udisplay_ui.c
)

# ── demo01 executable ─────────────────────────────────────────────────────────
add_executable(demo01
    main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/demo_tcp.c
    ${UDISPLAY_GEN_DIR}/udisplay_ui.c
)
add_dependencies(demo01 demo01_ui_gen)
target_include_directories(demo01 PRIVATE
    ${UDISPLAY_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
target_link_libraries(demo01 PRIVATE udisplay m pthread)
target_compile_options(demo01 PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
