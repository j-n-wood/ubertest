# tests/CMakeLists.txt
# GoogleTest configuration for unit tests

add_executable(run_tests
    sanity_test.cpp
)

target_link_libraries(run_tests PRIVATE
    GTest::gtest_main
)

target_include_directories(run_tests PRIVATE
    ${SHARED_INCLUDE_DIR}
)

include(GoogleTest)
gtest_discover_tests(run_tests)
