# Top-level build: every desktop component, their tests, and the uDisplay
# framework package (libudisplay + udisplay-gen + demos as sources).
#
#   cmake -B build -DUDISPLAY_VERSION=1.2.3
#   cmake --build build --parallel
#   ctest --test-dir build --output-on-failure
#   cmake --install build --prefix stage/udisplay-framework-1.2.3
#   cpack --config build/CPackConfig.cmake   # -> udisplay-framework-1.2.3.tar.gz
#
# Each subproject still builds standalone exactly as before (see
# docs/building.md). The ESP-IDF demo (demo05) and the Android client are
# cross builds and stay out of this desktop build.

cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/uDisplayVersion.cmake")
project(udisplay-framework VERSION ${UDISPLAY_VERSION} LANGUAGES C CXX)

option(UDISPLAY_BUILD_CLIENT "Build udisplay-client (requires Qt 6)" ON)
option(UDISPLAY_BUILD_DEMOS  "Build the desktop TCP demos (demo01-demo03)" ON)
# Same cache variable libudisplay's standalone build uses for its own tests.
option(UDISPLAY_BUILD_TESTS  "Build and register all unit tests with CTest" ON)

if(UDISPLAY_BUILD_TESTS)
    enable_testing()
endif()

if(UDISPLAY_BUILD_DEMOS OR UDISPLAY_BUILD_TESTS)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
endif()

# Fails configure with an install hint instead of failing mid-build.
function(udisplay_require_python_modules what)
    string(REPLACE ";" ", " _modules "${ARGN}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import ${_modules}"
        RESULT_VARIABLE _result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR
            "${what} needs the Python modules ${_modules} for "
            "${Python3_EXECUTABLE}. Install them with\n"
            "  pip install -e \"${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen[dev]\"\n"
            "(ideally in a venv, then pass -DPython3_EXECUTABLE=<venv>/bin/python)."
        )
    endif()
endfunction()

# ── libudisplay ──────────────────────────────────────────────────────────
# Never install googletest (fetched by the libudisplay tests) into the
# framework tree.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(libudisplay)

# ── Demos (demo01-demo03) ────────────────────────────────────────────────
# Generate code with the in-tree udisplay-gen, not whatever `udisplay-gen`
# happens to be on PATH, so the demos always match the sources being built.
if(UDISPLAY_BUILD_DEMOS)
    udisplay_require_python_modules("udisplay-gen (demo code generation)"
        click yaml jsonschema)
    set(UDISPLAY_GEN_COMMAND
        "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen"
            "${Python3_EXECUTABLE}" -m udisplay_gen
    )
    file(GLOB_RECURSE UDISPLAY_GEN_DEPENDS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen/udisplay_gen/*.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen/udisplay_gen/*.json"
    )
    add_subdirectory(demos)

    if(UDISPLAY_BUILD_TESTS)
        # Self-contained regression test for demos/shared/demo_tcp.c; it
        # uses assert(), so keep NDEBUG off even in Release builds.
        add_executable(test_demo_tcp demos/shared/test_demo_tcp.c)
        target_include_directories(test_demo_tcp PRIVATE demos/shared)
        target_link_libraries(test_demo_tcp PRIVATE udisplay)
        target_compile_options(test_demo_tcp PRIVATE -UNDEBUG)
        add_test(NAME test_demo_tcp COMMAND test_demo_tcp)
    endif()
endif()

# ── udisplay-gen tests ───────────────────────────────────────────────────
if(UDISPLAY_BUILD_TESTS)
    udisplay_require_python_modules("udisplay-gen tests"
        click yaml jsonschema pytest)
    add_test(
        NAME udisplay-gen-pytest
        COMMAND "${Python3_EXECUTABLE}" -m pytest -q -p no:cacheprovider tests
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen"
    )
endif()

# ── Framework install: version-stamped libudisplay header ────────────────
if(UDISPLAY_BUILD_TESTS)
    add_test(
        NAME framework-version-header
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-framework-version-header"
            "-DC_COMPILER=${CMAKE_C_COMPILER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/framework_version_header.cmake"
    )
endif()

# ── udisplay-client ──────────────────────────────────────────────────────
# Built and tested here, but not part of the framework package: its install
# rules are excluded from a plain `cmake --install` and only run with
# `--component client` (see udisplay-client/CMakeLists.txt).
if(UDISPLAY_BUILD_CLIENT)
    set(UDISPLAY_CLIENT_BUILD_TESTS ${UDISPLAY_BUILD_TESTS})
    add_subdirectory(udisplay-client)
endif()

# ── Framework install tree ───────────────────────────────────────────────
# `cmake --install` stages the framework with the same relative layout as
# the repository, so libudisplay's `../cmake/` include and the demos'
# `../../libudisplay` paths keep working inside the package:
#
#   <prefix>/
#   ├── VERSION, README.md, udisplay.schema.json, LICENSES/
#   ├── cmake/          uDisplayVersion.cmake + pinned version defaults
#   ├── libudisplay/    sources (ESP-IDF component / host CMake project)
#   ├── udisplay-gen/   Python package, version patched in
#   └── demos/
#
# Everything is COMPONENT framework. Point --prefix at
# udisplay-framework-<version>/, or let CPack build the tarball.

set(UDISPLAY_FRAMEWORK_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/framework")

# Writes only when the content changed, so reconfiguring doesn't touch
# timestamps (and re-trigger installs) for nothing.
function(udisplay_write_if_changed path content)
    if(EXISTS "${path}")
        file(READ "${path}" _old)
        if(_old STREQUAL content)
            return()
        endif()
    endif()
    file(WRITE "${path}" "${content}")
endfunction()

# Pinned version for cmake/uDisplayVersion.cmake inside the package.
udisplay_write_if_changed(
    "${UDISPLAY_FRAMEWORK_STAGE_DIR}/cmake/uDisplayVersionDefaults.cmake"
"# Generated by the uDisplay top-level build: the version this framework
# package was built with. Values passed with -D take precedence.
if(NOT DEFINED UDISPLAY_VERSION)
    set(UDISPLAY_VERSION \"${UDISPLAY_VERSION}\")
endif()
if(NOT DEFINED UDISPLAY_VERSION_FULL)
    set(UDISPLAY_VERSION_FULL \"${UDISPLAY_VERSION_FULL}\")
endif()
")

udisplay_write_if_changed(
    "${UDISPLAY_FRAMEWORK_STAGE_DIR}/VERSION"
    "${UDISPLAY_VERSION_FULL}\n")

# udisplay-gen's single version source is __version__ in __init__.py
# (pyproject.toml reads it dynamically); stamp the framework version in.
set(_gen_init "${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen/udisplay_gen/__init__.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gen_init}")
file(READ "${_gen_init}" _gen_init_content)
set(_gen_version_regex "\n__version__ = \"[^\"\n]*\"\n")
if(NOT _gen_init_content MATCHES "${_gen_version_regex}")
    message(FATAL_ERROR
        "No '__version__ = \"...\"' line found in ${_gen_init} to patch")
endif()
string(REGEX REPLACE "${_gen_version_regex}"
    "\n__version__ = \"${UDISPLAY_VERSION}\"\n"
    _gen_init_content "${_gen_init_content}")
udisplay_write_if_changed(
    "${UDISPLAY_FRAMEWORK_STAGE_DIR}/udisplay-gen/udisplay_gen/__init__.py"
    "${_gen_init_content}")

# Local build/editor/cache leftovers never belong in the package, even when
# installing from a working tree rather than a clean checkout.
set(_framework_source_excludes
    PATTERN ".*"                  EXCLUDE   # .gitignore, .venv, .pytest_cache, ...
    PATTERN "build"               EXCLUDE
    PATTERN "build-*"             EXCLUDE
    PATTERN "__pycache__"         EXCLUDE
    PATTERN "*.pyc"               EXCLUDE
    PATTERN "*.egg-info"          EXCLUDE
    PATTERN "CMakeLists.txt.user" EXCLUDE
    # ESP-IDF (demo05) local state
    PATTERN "managed_components"  EXCLUDE
    PATTERN "sdkconfig"           EXCLUDE
    PATTERN "sdkconfig.old"       EXCLUDE
    PATTERN "dependencies.lock"   EXCLUDE
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/udisplay.schema.json"
        "${UDISPLAY_FRAMEWORK_STAGE_DIR}/VERSION"
    DESTINATION "."
    COMPONENT framework
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES/"
    DESTINATION "LICENSES"
    COMPONENT framework
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uDisplayVersion.cmake"
        "${UDISPLAY_FRAMEWORK_STAGE_DIR}/cmake/uDisplayVersionDefaults.cmake"
    DESTINATION "cmake"
    COMPONENT framework
)

# libudisplay without its gtest suite (its CMakeLists.txt skips test/ when
# the directory is absent), with the version-stamped main header.
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/libudisplay/"
    DESTINATION "libudisplay"
    COMPONENT framework
    ${_framework_source_excludes}
    REGEX "/libudisplay/test$" EXCLUDE
    REGEX "/libudisplay/include/libudisplay/udisplay\\.h$" EXCLUDE
)
install(
    CODE "
        set(SOURCE \"${CMAKE_CURRENT_SOURCE_DIR}/libudisplay/include/libudisplay/udisplay.h\")
        set(DESTINATION \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/libudisplay/include/libudisplay/udisplay.h\")
        set(MAJOR ${UDISPLAY_VERSION_MAJOR})
        set(MINOR ${UDISPLAY_VERSION_MINOR})
        set(PATCH ${UDISPLAY_VERSION_PATCH})
        set(FULL ${UDISPLAY_VERSION_FULL})
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/uDisplayStampVersionHeader.cmake\")
    "
    COMPONENT framework
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen/pyproject.toml"
        "${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen/LICENSE"
    DESTINATION "udisplay-gen"
    COMPONENT framework
)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/udisplay-gen/udisplay_gen"
    DESTINATION "udisplay-gen"
    COMPONENT framework
    ${_framework_source_excludes}
    REGEX "/udisplay_gen/__init__\\.py$" EXCLUDE
    # A symlink to the repository-root schema; replaced by a real copy below.
    REGEX "/udisplay_gen/schema/udisplay\\.schema\\.json$" EXCLUDE
)
install(
    FILES "${UDISPLAY_FRAMEWORK_STAGE_DIR}/udisplay-gen/udisplay_gen/__init__.py"
    DESTINATION "udisplay-gen/udisplay_gen"
    COMPONENT framework
)
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/udisplay.schema.json"
    DESTINATION "udisplay-gen/udisplay_gen/schema"
    COMPONENT framework
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/demos/"
    DESTINATION "demos"
    COMPONENT framework
    ${_framework_source_excludes}
    # demo04's generated files (udisplay-gen --lang micropython output)
    REGEX "/demo04/(ui|udisplay_runtime)\\.py$" EXCLUDE
)

# ── Packaging: udisplay-framework-<version>.tar.gz ───────────────────────
set(CPACK_PACKAGE_NAME "udisplay-framework")
set(CPACK_PACKAGE_VENDOR "uDisplay")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "uDisplay framework: libudisplay, udisplay-gen and demos")
set(CPACK_PACKAGE_VERSION "${UDISPLAY_VERSION_FULL}")
set(CPACK_PACKAGE_FILE_NAME "udisplay-framework-${UDISPLAY_VERSION_FULL}")
set(CPACK_GENERATOR "TGZ")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
# One archive containing only the framework component (never the client).
set(CPACK_COMPONENTS_ALL framework)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package")
set(CPACK_SOURCE_GENERATOR "")
include(CPack)
