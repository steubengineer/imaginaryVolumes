# First-party tests (ADR-0002). Built with the strict warning set and linked
# against Catch2's built-in main (from the vendored amalgamated .cpp).
add_executable(iv_tests
    test_error.cpp
    test_assert.cpp)
target_link_libraries(iv_tests PRIVATE iv iv_warnings catch2_amalgamated)

add_test(NAME iv_tests COMMAND iv_tests)
