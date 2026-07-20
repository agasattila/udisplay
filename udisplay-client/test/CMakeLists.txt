find_package(Qt6 6.4 REQUIRED COMPONENTS Core Test)

# yaml-cpp is already fetched/found by the parent CMakeLists.txt
# The yaml-cpp target is available here via the parent scope.

# ── QML layout regression tests ─────────────────────────────────────────────
# Run via the `qml` CLI interpreter (part of qt6-declarative-dev) rather than
# QTest — these assert on actual Quick Layout geometry (x/width after a real
# layout pass), which C++ unit tests can't observe. Optional: skipped if the
# `qml` binary isn't found, so it never blocks a build lacking it.
# Prefer the versioned Qt6 binary over a bare `qml` on PATH — on Debian/Ubuntu
# `qml` is a qtchooser shim that needs QT_SELECT/extra env setup to resolve a
# Qt installation; /usr/lib/qt6/bin/qml (or the equivalent under
# QT_INSTALL_BINS) is the real Qt6 binary and works standalone.
find_program(QML_EXECUTABLE NAMES qml6 qml
             HINTS "${Qt6_DIR}/../../../bin" /usr/lib/qt6/bin
                   /usr/lib/x86_64-linux-gnu/qt6/bin)
if(QML_EXECUTABLE)
    add_test(NAME test_qml_nested_row_layout
             COMMAND ${QML_EXECUTABLE} -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/qml/test_nested_row_layout.qml")
    set_tests_properties(test_qml_nested_row_layout PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10)

    add_test(NAME test_qml_row_flex_align
             COMMAND ${QML_EXECUTABLE} -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/qml/test_row_flex_align.qml")
    set_tests_properties(test_qml_row_flex_align PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10)

    add_test(NAME test_qml_grid_flex_align
             COMMAND ${QML_EXECUTABLE} -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/qml/test_grid_flex_align.qml")
    set_tests_properties(test_qml_grid_flex_align PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10)

    add_test(NAME test_qml_row_nested_mixed_flex
             COMMAND ${QML_EXECUTABLE} -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/qml/test_row_nested_mixed_flex.qml")
    set_tests_properties(test_qml_row_nested_mixed_flex PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 15)

    add_test(NAME test_qml_device_screen_style
             COMMAND ${QML_EXECUTABLE} -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/qml/test_device_screen_style.qml")
    set_tests_properties(test_qml_device_screen_style PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10)

    add_test(NAME test_qml_button_face_children
             COMMAND ${QML_EXECUTABLE} -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/qml/test_button_face_children.qml")
    set_tests_properties(test_qml_button_face_children PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
        # Regression guard: ButtonWidget.qml's facesRowLoader anchors must never
        # do arithmetic directly on an AnchorLine (e.g. `parent.top + 6`) — that
        # silently fails to assign and spams this warning on every binding
        # re-evaluation. Use topMargin/bottomMargin instead. A passing exit code
        # alone doesn't catch this since the warning isn't fatal.
        FAIL_REGULAR_EXPRESSION "Unable to assign .* to QQuickAnchorLine")

    add_test(NAME test_qml_button_face_nested
             COMMAND ${QML_EXECUTABLE} -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/qml/test_button_face_nested.qml")
    set_tests_properties(test_qml_button_face_nested PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
        FAIL_REGULAR_EXPRESSION "Unable to assign .* to QQuickAnchorLine")

    add_test(NAME test_qml_button_face_vertical_center
             COMMAND ${QML_EXECUTABLE} -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/qml/test_button_face_vertical_center.qml")
    set_tests_properties(test_qml_button_face_vertical_center PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
        FAIL_REGULAR_EXPRESSION "Unable to assign .* to QQuickAnchorLine")
else()
    message(STATUS "qml executable not found — skipping QML layout regression tests")
endif()

# ── test_protocol ────────────────────────────────────────────────────────
add_executable(test_protocol
    test_protocol.cpp
    ../src/Protocol.cpp
)
target_include_directories(test_protocol PRIVATE ../src)
target_link_libraries(test_protocol PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_protocol COMMAND test_protocol -v2)

# ── test_bootstrap ───────────────────────────────────────────────────────
add_executable(test_bootstrap
    test_bootstrap.cpp
    ../src/MockTransport.h   # listed so AUTOMOC generates moc_MockTransport.cpp
    ../src/Protocol.cpp
    ../src/Transport.cpp
    ../src/BootstrapManager.cpp
)
target_include_directories(test_bootstrap PRIVATE ../src)
target_link_libraries(test_bootstrap PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_bootstrap COMMAND test_bootstrap -v2)

# ── test_yaml_parser ──────────────────────────────────────────────────────
add_executable(test_yaml_parser
    test_yaml_parser.cpp
    ../src/YamlParser.cpp
)
target_include_directories(test_yaml_parser PRIVATE ../src)
target_link_libraries(test_yaml_parser PRIVATE Qt6::Core Qt6::Test yaml-cpp)
add_test(NAME test_yaml_parser COMMAND test_yaml_parser -v2)

# ── test_widget_model ──────────────────────────────────────────────────────
add_executable(test_widget_model
    test_widget_model.cpp
    ../src/WidgetModel.cpp
    ../src/Protocol.cpp
)
target_include_directories(test_widget_model PRIVATE ../src)
target_link_libraries(test_widget_model PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_widget_model COMMAND test_widget_model -v2)

# ── test_widget_dump ─────────────────────────────────────────────────────
add_executable(test_widget_dump
    test_widget_dump.cpp
    ../src/WidgetDump.cpp
)
target_include_directories(test_widget_dump PRIVATE ../src)
target_link_libraries(test_widget_dump PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_widget_dump COMMAND test_widget_dump -v2)

# ── test_discovery_model ─────────────────────────────────────────────────
add_executable(test_discovery_model
    test_discovery_model.cpp
    ../src/IScanner.cpp
    ../src/BleScanner.cpp
    ../src/MdnsScanner.cpp
    ../src/DiscoveryModel.cpp
    ../src/Protocol.cpp
)
target_include_directories(test_discovery_model PRIVATE ../src)
target_link_libraries(test_discovery_model PRIVATE Qt6::Core Qt6::Test)
if(HAVE_MDNS)
    target_compile_definitions(test_discovery_model PRIVATE HAVE_QZEROCONF)
    target_link_libraries(test_discovery_model PRIVATE QtZeroConf)
    if(AVAHI_CLIENT_FOUND)
        target_link_libraries(test_discovery_model PRIVATE ${AVAHI_CLIENT_LIBRARIES})
    endif()
endif()
if(HAVE_BLE)
    target_compile_definitions(test_discovery_model PRIVATE HAVE_BLE)
    target_link_libraries(test_discovery_model PRIVATE Qt6::Bluetooth)
endif()
add_test(NAME test_discovery_model COMMAND test_discovery_model -v2)

# ── test_device_controller ────────────────────────────────────────────────
find_package(ZLIB REQUIRED)
set(_DC_TEST_SOURCES
    test_device_controller.cpp
    ../src/DeviceController.cpp
    ../src/YamlParser.cpp
    ../src/WidgetModel.cpp
    ../src/WidgetDump.cpp
    ../src/Protocol.cpp
    ../src/Transport.cpp
    ../src/BootstrapManager.cpp
    ../src/TcpTransport.cpp
    ../src/MockTransport.h   # listed so AUTOMOC generates moc_MockTransport.cpp
)
if(HAVE_BLE)
    list(APPEND _DC_TEST_SOURCES ../src/BleTransport.cpp)
endif()
add_executable(test_device_controller ${_DC_TEST_SOURCES})
target_include_directories(test_device_controller PRIVATE ../src)
target_link_libraries(test_device_controller
    PRIVATE Qt6::Core Qt6::Test Qt6::Network Qt6::Sql yaml-cpp ZLIB::ZLIB
)
if(HAVE_BLE)
    target_compile_definitions(test_device_controller PRIVATE HAVE_BLE)
    target_link_libraries(test_device_controller PRIVATE Qt6::Bluetooth)
endif()
add_test(NAME test_device_controller COMMAND test_device_controller -v2)

# ── test_design_mode ──────────────────────────────────────────────────────
add_executable(test_design_mode
    test_design_mode.cpp
    ../src/DeviceController.cpp
    ../src/YamlParser.cpp
    ../src/WidgetModel.cpp
    ../src/WidgetDump.cpp
    ../src/Protocol.cpp
    ../src/Transport.cpp
    ../src/BootstrapManager.cpp
    ../src/TcpTransport.cpp
    ../src/MockTransport.h   # listed so AUTOMOC generates moc_MockTransport.cpp
)
target_include_directories(test_design_mode PRIVATE ../src)
target_link_libraries(test_design_mode
    PRIVATE Qt6::Core Qt6::Test Qt6::Network Qt6::Sql yaml-cpp ZLIB::ZLIB
)
add_test(NAME test_design_mode COMMAND test_design_mode -v2)
