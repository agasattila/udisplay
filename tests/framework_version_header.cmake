# Installs the framework component of a configured top-level build into a
# scratch prefix and checks that libudisplay/udisplay.h carries the build's
# version in place of the 0.0.0 defaults.
#
#   cmake -DBUILD_DIR=... -DPREFIX=... -DMAJOR=1 -DMINOR=2 -DPATCH=3
#         -DFULL=1.2.3-rc1 -P framework_version_header.cmake

foreach(_var BUILD_DIR PREFIX MAJOR MINOR PATCH FULL)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "${_var} not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${PREFIX}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
            --component framework --prefix "${PREFIX}"
    RESULT_VARIABLE _result
    OUTPUT_QUIET
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed: ${_result}")
endif()

set(_header "${PREFIX}/libudisplay/include/libudisplay/udisplay.h")
if(NOT EXISTS "${_header}")
    message(FATAL_ERROR "${_header} was not installed")
endif()
file(READ "${_header}" _content)

foreach(_pair "MAJOR=${MAJOR}" "MINOR=${MINOR}" "PATCH=${PATCH}"
              "STRING=\"${FULL}\"")
    string(REGEX MATCH "^([A-Z]+)=(.*)$" _ "${_pair}")
    set(_line "#define UDISPLAY_VERSION_${CMAKE_MATCH_1} ${CMAKE_MATCH_2}")
    string(FIND "${_content}" "\n${_line}\n" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "'${_line}' not found in ${_header}")
    endif()
endforeach()

# No separate version header ships alongside it.
file(GLOB_RECURSE _version_headers "${PREFIX}/libudisplay/*version*.h*")
if(_version_headers)
    message(FATAL_ERROR "Unexpected version header(s): ${_version_headers}")
endif()

file(REMOVE_RECURSE "${PREFIX}")
message(STATUS "Installed udisplay.h carries version ${FULL}")
