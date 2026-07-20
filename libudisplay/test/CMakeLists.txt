include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
# Prevent overriding the parent project's compiler/linker settings (Windows)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(udisplay_test
    test_protocol.cpp
    test_chunk_server.cpp
    test_framing.cpp
    test_udisplay.cpp
)

target_link_libraries(udisplay_test
    PRIVATE
        udisplay
        GTest::gtest_main
)

# Expose internal headers to tests
target_include_directories(udisplay_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

include(GoogleTest)
gtest_discover_tests(udisplay_test)
