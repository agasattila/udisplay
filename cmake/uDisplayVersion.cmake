if(NOT DEFINED UDISPLAY_VERSION)
    set(UDISPLAY_VERSION "0.0.0")
endif()

if(NOT DEFINED UDISPLAY_VERSION_FULL)
    set(UDISPLAY_VERSION_FULL "${UDISPLAY_VERSION}")
endif()

if(NOT UDISPLAY_VERSION MATCHES
        "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR
        "UDISPLAY_VERSION must have major.minor.patch format, "
        "got: '${UDISPLAY_VERSION}'"
    )
endif()

set(UDISPLAY_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(UDISPLAY_VERSION_MINOR "${CMAKE_MATCH_2}")
set(UDISPLAY_VERSION_PATCH "${CMAKE_MATCH_3}")

message(STATUS
    "uDisplay version: ${UDISPLAY_VERSION_FULL}"
)

# ── Git-derived display version (udisplay-client UI only) ──────────────────
# Distinct from UDISPLAY_VERSION_FULL (the numeric MAJOR.MINOR.PATCH consumed
# by libudisplay's firmware version.h) so this never leaks into the ESP-IDF
# build path. Exact tag match -> use the tag as-is. Otherwise ->
# v0.0.0-<short-hash>. git unavailable / not a repo -> v0.0.0-unknown.
#
# WORKING_DIRECTORY is explicit and required: execute_process() defaults to
# CMAKE_CURRENT_BINARY_DIR, not the source tree. Without it, any out-of-tree
# build (e.g. a Qt Creator shadow build, the default pattern for Qt/QML dev)
# would run git outside any git repository and silently fall back to
# "unknown" regardless of actual HEAD.
#
# TIMEOUT bounds each git call so a hung git (network-backed .git, a stuck
# credential-helper prompt, a PATH-shadowing shim) can't block configure
# indefinitely.
#
# Tag/hash output is validated against a safe character set before being
# embedded in the generated header — git tag names may legally contain a
# literal `"`, which would otherwise break out of the C string literal
# configure_file() embeds it in (@ONLY does no escaping). Mirrors the
# validate-before-embed pattern UDISPLAY_VERSION already uses above, but
# falls back to the safe default instead of FATAL_ERROR: an unusual existing
# tag shouldn't break everyone's build.
set(_udisplay_client_version_prefix "v0.0.0")
set(_udisplay_client_version_safe_pattern "^[A-Za-z0-9._-]+$")
find_package(Git QUIET)
set(UDISPLAY_CLIENT_DISPLAY_VERSION "${_udisplay_client_version_prefix}-unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _udisplay_git_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _udisplay_git_tag_result
        TIMEOUT 5
    )
    if(_udisplay_git_tag_result EQUAL 0 AND
       _udisplay_git_tag MATCHES "${_udisplay_client_version_safe_pattern}")
        set(UDISPLAY_CLIENT_DISPLAY_VERSION "${_udisplay_git_tag}")
    else()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _udisplay_git_hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _udisplay_git_hash_result
            TIMEOUT 5
        )
        if(_udisplay_git_hash_result EQUAL 0 AND
           _udisplay_git_hash MATCHES "${_udisplay_client_version_safe_pattern}")
            set(UDISPLAY_CLIENT_DISPLAY_VERSION
                "${_udisplay_client_version_prefix}-${_udisplay_git_hash}")
        endif()
    endif()
    unset(_udisplay_git_tag)
    unset(_udisplay_git_tag_result)
    unset(_udisplay_git_hash)
    unset(_udisplay_git_hash_result)
endif()
unset(_udisplay_client_version_prefix)
unset(_udisplay_client_version_safe_pattern)

message(STATUS
    "uDisplay client display version: ${UDISPLAY_CLIENT_DISPLAY_VERSION}"
)

