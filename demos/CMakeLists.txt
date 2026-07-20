cmake_minimum_required(VERSION 3.16)
project(udisplay_demos LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add libudisplay once; each demo's CMakeLists.txt guards against re-adding it.
set(UDISPLAY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../libudisplay
    ${CMAKE_CURRENT_BINARY_DIR}/libudisplay
)

add_subdirectory(demo01)
add_subdirectory(demo02)
add_subdirectory(demo03)
