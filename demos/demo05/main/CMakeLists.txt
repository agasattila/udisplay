set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

# led_strip is only needed for WS2812-type boards (see Kconfig.projbuild
# DEMO05_LED_IS_WS2812) - conditionally required so a plain-GPIO board build
# never links the led_strip/RMT driver at all. CONFIG_DEMO05_LED_IS_WS2812 is
# available here because sdkconfig is processed before component
# registration.
set(demo05_led_requires "")
if(CONFIG_DEMO05_LED_IS_WS2812)
    list(APPEND demo05_led_requires led_strip)
endif()

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
        ${demo05_led_requires}
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
