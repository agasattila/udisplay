# Configures a scratch top-level build with a non-default version, installs
# its framework component, and checks that the installed
# libudisplay/udisplay.h carries that version on its own: a consumer compiled
# against the installed include directory, without any version compile
# definitions, must see it. The source header still says 0.0.0, so this
# fails if the install copies it unpatched.
#
#   cmake -DSOURCE_DIR=<repo> -DWORK_DIR=<scratch> -DC_COMPILER=<cc>
#         -P framework_version_header.cmake

foreach(_var SOURCE_DIR WORK_DIR C_COMPILER)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "${_var} not set")
    endif()
endforeach()

set(_version "12.34.56")
set(_full "12.34.56-rc1")
set(_build "${WORK_DIR}/build")
set(_prefix "${WORK_DIR}/udisplay-framework-${_full}")

function(run what)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _output
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${what} failed (${_result}):\n${_output}")
    endif()
    set(RUN_OUTPUT "${_output}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
# Nothing is built: the framework install only copies sources.
run("configure" "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${_build}"
    "-DUDISPLAY_VERSION=${_version}"
    "-DUDISPLAY_VERSION_FULL=${_full}"
    -DUDISPLAY_BUILD_CLIENT=OFF
    -DUDISPLAY_BUILD_DEMOS=OFF
    -DUDISPLAY_BUILD_TESTS=OFF
)
run("install" "${CMAKE_COMMAND}" --install "${_build}"
    --component framework --prefix "${_prefix}")

set(_include "${_prefix}/libudisplay/include")
if(NOT EXISTS "${_include}/libudisplay/udisplay.h")
    message(FATAL_ERROR "libudisplay/udisplay.h was not installed")
endif()

# No separate version header ships alongside it.
file(GLOB_RECURSE _version_headers "${_prefix}/libudisplay/*version*.h*")
if(_version_headers)
    message(FATAL_ERROR "Unexpected version header(s): ${_version_headers}")
endif()

file(WRITE "${WORK_DIR}/consumer.c" "
#include \"libudisplay/udisplay.h\"
#include <stdio.h>

#if UDISPLAY_VERSION != UDISPLAY_VERSION_ENCODE(12, 34, 56)
#error \"installed header does not carry version 12.34.56\"
#endif

int main(void)
{
    printf(\"%d.%d.%d %s\\n\", UDISPLAY_VERSION_MAJOR, UDISPLAY_VERSION_MINOR,
           UDISPLAY_VERSION_PATCH, UDISPLAY_VERSION_STRING);
    return 0;
}
")
run("compiling a consumer of the installed header" "${C_COMPILER}"
    -std=c11 -I "${_include}"
    "${WORK_DIR}/consumer.c" -o "${WORK_DIR}/consumer")
run("running the consumer" "${WORK_DIR}/consumer")
if(NOT RUN_OUTPUT STREQUAL "${_version} ${_full}\n")
    message(FATAL_ERROR
        "Consumer printed '${RUN_OUTPUT}', expected '${_version} ${_full}'")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "Installed udisplay.h carries version ${_full}")
