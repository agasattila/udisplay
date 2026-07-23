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

