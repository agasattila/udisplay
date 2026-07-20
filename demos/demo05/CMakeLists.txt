cmake_minimum_required(VERSION 3.16)

# libudisplay is an IDF-compatible component — add its parent directory
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../libudisplay")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(demo05)
