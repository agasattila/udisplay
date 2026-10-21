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
    test_udisplay_multi_instance.cpp
    test_version.cpp
    test_version_guard.cpp
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

# tx_ring is built into the library only with the NimBLE backend; the tests
# compile it directly.
target_sources(udisplay_test PRIVATE
    test_tx_ring.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tx_ring.c
)

# NimBLE backend, run against a fake NimBLE host (nimble_fake/). The backend
# source is compiled through nimble_fake/backend_under_test.c, which adds a
# per-test state reset.
add_executable(udisplay_nimble_test
    test_nimble_backend.cpp
    nimble_fake/fake_nimble.cpp
    nimble_fake/backend_under_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tx_ring.c
)
target_include_directories(udisplay_nimble_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/nimble_fake
    ${CMAKE_CURRENT_SOURCE_DIR}/nimble_fake/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/nimble
)
target_link_libraries(udisplay_nimble_test PRIVATE udisplay GTest::gtest_main)
target_compile_options(udisplay_nimble_test PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall -Wextra -Wpedantic>
)

include(GoogleTest)
gtest_discover_tests(udisplay_test)
gtest_discover_tests(udisplay_nimble_test)
