# Install-time helper for the top-level framework install: copies
# libudisplay's main header to DESTINATION with the #ifndef-guarded version
# defaults (0.0.0 in git) replaced by the framework version, so a consumer
# that only includes "libudisplay/udisplay.h" (no CMake compile definitions)
# still sees the release version.
#
# Runs from install(CODE) with SOURCE, DESTINATION, MAJOR, MINOR, PATCH and
# FULL set. Patching at install time keeps the header out of the top-level
# configure dependencies.

file(READ "${SOURCE}" _content)
foreach(_pair "MAJOR=${MAJOR}" "MINOR=${MINOR}" "PATCH=${PATCH}"
              "STRING=\"${FULL}\"")
    string(REGEX MATCH "^([A-Z]+)=(.*)$" _ "${_pair}")
    set(_name "UDISPLAY_VERSION_${CMAKE_MATCH_1}")
    set(_value "${CMAKE_MATCH_2}")
    set(_regex "\n#define ${_name} [^\n]*\n")
    string(REGEX MATCHALL "${_regex}" _matches "${_content}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one '#define ${_name} ...' line in "
            "${SOURCE} to patch, found ${_count}")
    endif()
    string(REGEX REPLACE "${_regex}" "\n#define ${_name} ${_value}\n"
        _content "${_content}")
endforeach()

message(STATUS "Installing: ${DESTINATION} (version ${FULL})")
file(WRITE "${DESTINATION}" "${_content}")
list(APPEND CMAKE_INSTALL_MANIFEST_FILES "${DESTINATION}")
