# Add test files
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

# Create a test executable
add_executable(tests ${TEST_SOURCES})

# Link with the main project and Catch2
target_link_libraries(tests PRIVATE jsonrpc Catch2::Catch2WithMain)

# Register the tests
include(Catch)
catch_discover_tests(tests)
