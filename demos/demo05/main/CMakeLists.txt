set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "."
        "${GEN_DIR}"
    PRIV_REQUIRES
        libudisplay
        nvs_flash
        bt
)

# Generate udisplay_ui.hpp (header-only) from demo05.yaml before compiling
add_custom_command(
    OUTPUT
        "${GEN_DIR}/udisplay_ui.hpp"
        "${GEN_DIR}/udisplay_ui.bin"
    COMMAND
        udisplay-gen build "${CMAKE_CURRENT_SOURCE_DIR}/demo05.yaml"
                           --lang cpp --modern
                           -o "${GEN_DIR}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/demo05.yaml"
    COMMENT "Generating udisplay_ui.hpp from demo05.yaml (--lang cpp --modern)"
    VERBATIM
)
add_custom_target(demo05_ui_gen
    DEPENDS
        "${GEN_DIR}/udisplay_ui.hpp"
        "${GEN_DIR}/udisplay_ui.bin"
)
add_dependencies(${COMPONENT_LIB} demo05_ui_gen)
