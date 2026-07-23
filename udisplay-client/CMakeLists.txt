cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/uDisplayVersion.cmake")
project(udisplay-client VERSION ${UDISPLAY_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Qml Quick QuickControls2 Network Sql)
find_package(ZLIB REQUIRED)


# ── QZeroConf for mDNS discovery ─────────────────────────────────────────
# Linux: requires libavahi-client-dev (apt install libavahi-client-dev)
# macOS/iOS: uses Bonjour (system), no extra packages needed
# Android: uses NSD via JNI, no extra packages needed
# Without avahi-client on Linux, MdnsScanner compiles as a stub that emits
# a scanError — the build still succeeds and manual TCP always works.
find_package(PkgConfig QUIET)
set(HAVE_MDNS OFF)
if(ANDROID)
    # QZeroConf uses Android NSD via JNI — never link host Avahi libraries
    set(HAVE_MDNS ON)
elseif(APPLE)
    # Bonjour always available on Apple platforms
    set(HAVE_MDNS ON)
elseif(PkgConfig_FOUND)
    # Skip on Android above: cross-compiling from Linux leaks host pkg-config
    # which finds the host's avahi-client and produces bogus Android link flags.
    pkg_check_modules(AVAHI_CLIENT avahi-client QUIET)
    if(AVAHI_CLIENT_FOUND)
        set(HAVE_MDNS ON)
    endif()
endif()

if(HAVE_MDNS)
    include(FetchContent)
    FetchContent_Declare(
        qzeroconf
        GIT_REPOSITORY https://github.com/jbagg/QtZeroConf.git
        GIT_TAG        81701d6
    )
    # Work around find_package(QT NAMES Qt6 Qt5 ...) resolving to Qt5 on
    # Debian/Ubuntu with both Qt versions installed.  Patch QZeroConf's
    # CMakeLists.txt to replace the NAMES search with a direct Qt6 declaration
    # before running it as a subdirectory.  The patch is idempotent so it is
    # applied every configure run (handles stale build directories).
    FetchContent_GetProperties(qzeroconf)
    if(NOT qzeroconf_POPULATED)
        FetchContent_Populate(qzeroconf)
    endif()
    if(EXISTS "${qzeroconf_SOURCE_DIR}/CMakeLists.txt")
        file(READ "${qzeroconf_SOURCE_DIR}/CMakeLists.txt" _qzc)
        string(REPLACE
            "find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network)"
            "set(QT_VERSION_MAJOR 6)  # patched by udisplay-client to force Qt6"
            _qzc_patched "${_qzc}")
        if(NOT "${_qzc_patched}" STREQUAL "${_qzc}")
            file(WRITE "${qzeroconf_SOURCE_DIR}/CMakeLists.txt" "${_qzc_patched}")
            message(STATUS "QZeroConf: patched CMakeLists.txt to force Qt6")
        endif()
    endif()
    add_subdirectory(${qzeroconf_SOURCE_DIR} ${qzeroconf_BINARY_DIR} EXCLUDE_FROM_ALL)
    if(ANDROID)
        message(STATUS "mDNS: QZeroConf enabled (Android NSD/JNI)")
    elseif(APPLE)
        message(STATUS "mDNS: QZeroConf enabled (Bonjour)")
    else()
        message(STATUS "mDNS: QZeroConf enabled (libavahi-client found)")
    endif()
else()
    message(STATUS "mDNS: QZeroConf disabled — MdnsScanner will emit stub error")
    message(STATUS "      Install libavahi-client-dev to enable mDNS discovery")
endif()

# ── Qt Bluetooth for BLE discovery + transport ────────────────────────────
# Android: Qt6::Bluetooth always available in the Qt Android build.
# Apple: Bluetooth framework always present (no extra packages needed).
# Linux desktop: requires Qt6 Bluetooth module (qt6-connectivity-dev).
# Gate pkg-config detection behind NOT ANDROID to avoid host library leaking
# into cross-compile sysroot (same pattern as avahi-client above).
set(HAVE_BLE OFF)
if(ANDROID)
    set(HAVE_BLE ON)
else()
    find_package(Qt6 COMPONENTS Bluetooth QUIET)
    if(Qt6Bluetooth_FOUND)
        set(HAVE_BLE ON)
    endif()
endif()

if(HAVE_BLE)
    message(STATUS "BLE: Qt6::Bluetooth found — BleTransport enabled")
else()
    message(STATUS "BLE: Qt6::Bluetooth not found — BleTransport disabled (BLE scanning will emit stub error)")
endif()

# yaml-cpp: prefer system install, fall back to FetchContent
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
    )
    set(YAML_CPP_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS  OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL      OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# ── Schema enum code generation ──────────────────────────────────────────
# Output goes into src/ so tests already on the ../src include path find it.
set(UDISPLAY_SCHEMA_ENUMS_H "${CMAKE_CURRENT_SOURCE_DIR}/src/udisplay_schema_enums.h")
add_custom_command(
    OUTPUT  "${UDISPLAY_SCHEMA_ENUMS_H}"
    COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_schema_enums.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../udisplay.schema.json"
            "${UDISPLAY_SCHEMA_ENUMS_H}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_schema_enums.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../udisplay.schema.json"
    COMMENT "Generating udisplay_schema_enums.h from schema"
)
add_custom_target(gen_schema_enums DEPENDS "${UDISPLAY_SCHEMA_ENUMS_H}")

# ── App icon generation ────────────────────────────────────────────────
# Linux: requires rsvg-convert (apt install librsvg2-bin, see docs/building.md)
set(UDISPLAY_ICON_SVG "${CMAKE_CURRENT_SOURCE_DIR}/udisplay_icon.svg")
set(UDISPLAY_ICON_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-icons")
set(UDISPLAY_DESKTOP_ICON_PNG "${UDISPLAY_ICON_GEN_DIR}/icons/app.png")
set(UDISPLAY_ANDROID_MIPMAP_PNGS
    "${UDISPLAY_ICON_GEN_DIR}/android/mipmap-mdpi/ic_launcher.png"
    "${UDISPLAY_ICON_GEN_DIR}/android/mipmap-hdpi/ic_launcher.png"
    "${UDISPLAY_ICON_GEN_DIR}/android/mipmap-xhdpi/ic_launcher.png"
    "${UDISPLAY_ICON_GEN_DIR}/android/mipmap-xxhdpi/ic_launcher.png"
    "${UDISPLAY_ICON_GEN_DIR}/android/mipmap-xxxhdpi/ic_launcher.png"
)
add_custom_command(
    OUTPUT  "${UDISPLAY_DESKTOP_ICON_PNG}" ${UDISPLAY_ANDROID_MIPMAP_PNGS}
    COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_app_icons.py"
            "${UDISPLAY_ICON_SVG}"
            "${UDISPLAY_ICON_GEN_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_app_icons.py"
            "${UDISPLAY_ICON_SVG}"
    COMMENT "Generating app icons from udisplay_icon.svg (requires rsvg-convert)"
)
add_custom_target(gen_app_icons
    DEPENDS "${UDISPLAY_DESKTOP_ICON_PNG}" ${UDISPLAY_ANDROID_MIPMAP_PNGS})

set(CLIENT_SOURCES
    src/Protocol.cpp
    src/Transport.cpp
    src/TcpTransport.cpp
    src/BootstrapManager.cpp
    src/YamlParser.cpp
    src/WidgetModel.cpp
    src/WidgetDump.cpp
    src/DeviceController.cpp
    src/IScanner.cpp
    src/BleScanner.cpp
    src/MdnsScanner.cpp
    src/DiscoveryModel.cpp
    src/main.cpp
)
if(HAVE_BLE)
    list(APPEND CLIENT_SOURCES src/BleTransport.cpp)
endif()

qt_add_executable(udisplay-client ${CLIENT_SOURCES})
add_dependencies(udisplay-client gen_schema_enums gen_app_icons)

# Embed the desktop/QML icon as a Qt resource (":/icons/app.png",
# "qrc:/icons/app.png" from QML) so it stays valid wherever the binary is
# packaged/installed — the add_dependencies above orders gen_app_icons
# before RCC compiles this resource.
qt_add_resources(udisplay-client "app_icons"
    PREFIX "/icons"
    BASE "${UDISPLAY_ICON_GEN_DIR}/icons"
    FILES "${UDISPLAY_DESKTOP_ICON_PNG}"
)

qt_add_qml_module(udisplay-client
    URI UDisplay
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/DiscoveryScreen.qml
        qml/DeviceScreen.qml
        qml/widgets/DisplayWidget.qml
        qml/widgets/LedWidget.qml
        qml/widgets/RgbLedWidget.qml
        qml/widgets/ButtonWidget.qml
        qml/widgets/ButtonGroupWidget.qml
        qml/widgets/SliderWidget.qml
        qml/widgets/ToggleWidget.qml
        qml/widgets/TextWidget.qml
        qml/widgets/DropdownWidget.qml
        qml/widgets/LabelWidget.qml
        qml/widgets/SeparatorWidget.qml
        qml/widgets/SectionWidget.qml
        qml/widgets/RowWidget.qml
        qml/widgets/GridWidget.qml
        qml/widgets/WidgetDelegate.qml
)

target_include_directories(udisplay-client PRIVATE src)

target_link_libraries(udisplay-client PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Sql
    ZLIB::ZLIB
    yaml-cpp
)

if(HAVE_MDNS)
    target_compile_definitions(udisplay-client PRIVATE HAVE_QZEROCONF)
    target_link_libraries(udisplay-client PRIVATE QtZeroConf)
    if(AVAHI_CLIENT_FOUND)
        target_link_libraries(udisplay-client PRIVATE ${AVAHI_CLIENT_LIBRARIES})
    endif()
endif()

if(HAVE_BLE)
    target_compile_definitions(udisplay-client PRIVATE HAVE_BLE)
    target_link_libraries(udisplay-client PRIVATE Qt6::Bluetooth)
endif()

if(ANDROID AND (HAVE_MDNS OR HAVE_BLE))
    # QT_ANDROID_PACKAGE_SOURCE_DIR is a single directory that androiddeployqt
    # merges into the APK.  Both MDNS (Java class) and BLE (AndroidManifest.xml)
    # need to land here, so we build the directory unconditionally and populate
    # it based on which features are enabled.
    set(_QZC_PKG_DIR "${CMAKE_CURRENT_BINARY_DIR}/android-pkg-extras")
    file(MAKE_DIRECTORY "${_QZC_PKG_DIR}")

    if(HAVE_MDNS)
        # androiddeployqt won't find QZeroConf's Java NSD class unless it's in
        # a package-source-dir with the correct package directory structure.
        # Without this, androidnsd.cpp's JNI bootstrap throws ClassNotFoundException
        # for qtzeroconf.QZeroConfNsdManager at runtime.
        file(MAKE_DIRECTORY "${_QZC_PKG_DIR}/src/qtzeroconf")
        file(COPY "${qzeroconf_SOURCE_DIR}/QZeroConfNsdManager.java"
             DESTINATION "${_QZC_PKG_DIR}/src/qtzeroconf")
    endif()

    if(HAVE_BLE)
        # Copy the manifest into the package source dir. androiddeployqt does
        # NOT merge this with Qt's base manifest — it overwrites the whole
        # file, so android/AndroidManifest.xml must be the complete manifest
        # (permissions + <application> + launcher <activity>).
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidManifest.xml"
             DESTINATION "${_QZC_PKG_DIR}")
    endif()

    # App icon mipmaps: unlike the manifest copy above (configure-time
    # file(COPY), fine since the manifest is a static source file), the
    # mipmap PNGs don't exist until gen_app_icons runs at build time — so
    # this has to be a build-time copy with an explicit dependency, not a
    # configure-time one.
    set(_ANDROID_MIPMAP_OUTPUTS
        "${_QZC_PKG_DIR}/res/mipmap-mdpi/ic_launcher.png"
        "${_QZC_PKG_DIR}/res/mipmap-hdpi/ic_launcher.png"
        "${_QZC_PKG_DIR}/res/mipmap-xhdpi/ic_launcher.png"
        "${_QZC_PKG_DIR}/res/mipmap-xxhdpi/ic_launcher.png"
        "${_QZC_PKG_DIR}/res/mipmap-xxxhdpi/ic_launcher.png"
    )
    add_custom_command(
        OUTPUT ${_ANDROID_MIPMAP_OUTPUTS}
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${UDISPLAY_ICON_GEN_DIR}/android" "${_QZC_PKG_DIR}/res"
        DEPENDS gen_app_icons
        COMMENT "Copying app icon mipmaps into Android package source dir"
    )
    add_custom_target(copy_app_icon_mipmaps DEPENDS ${_ANDROID_MIPMAP_OUTPUTS})
    add_dependencies(udisplay-client copy_app_icon_mipmaps)

    set_target_properties(udisplay-client PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${_QZC_PKG_DIR}"
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher")
endif()

include(GNUInstallDirs)

install(
    TARGETS udisplay-client
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Tests
option(UDISPLAY_CLIENT_BUILD_TESTS "Build unit tests" ON)
if(UDISPLAY_CLIENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
