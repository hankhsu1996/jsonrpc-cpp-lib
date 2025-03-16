cmake_minimum_required(VERSION 3.19)
project(jsonrpc VERSION 2.0.2 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Export compile_commands.json for tools like clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optionally enable ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
else()
    message(STATUS "ccache not found, proceeding without it.")
endif()

# Source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Add the library target before linking
add_library(jsonrpc ${SOURCES})

# Include directories for the library
target_include_directories(jsonrpc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Check for Conan usage
if(USE_CONAN)
    # Find and link dependencies via Conan
    find_package(nlohmann_json REQUIRED)
    find_package(spdlog REQUIRED)
    find_package(asio REQUIRED)

    # Link dependencies
    target_link_libraries(jsonrpc PUBLIC
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        asio::asio
    )
else()
    include(FetchContent)

    # Fetch dependencies using FetchContent
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)

    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.1
    )
    FetchContent_MakeAvailable(spdlog)

    FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-32-0
    )
    FetchContent_MakeAvailable(asio)

    # Ensure asio include directory is added
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE
        ${asio_SOURCE_DIR}/asio/include
    )

    # Link dependencies
    target_link_libraries(jsonrpc PUBLIC
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        asio
    )
endif()

# Option to build examples
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Conditionally copy compile_commands.json if it exists
if(EXISTS ${CMAKE_BINARY_DIR}/compile_commands.json)
    add_custom_command(
        TARGET jsonrpc PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Copying compile_commands.json to source directory"
        VERBATIM
    )
endif()

# Option to build tests
option(BUILD_TESTS "Build tests" OFF)

# Set BUILD_TESTS to ON if this is the main project
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(BUILD_TESTS ON)
endif()

# Include tests only if requested and if the tests directory exists
if(BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests")
    enable_testing()

    # Conditional Catch2 handling for testing
    if(USE_CONAN)
        find_package(Catch2 REQUIRED)
    else()
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.6.0
        )
        FetchContent_MakeAvailable(Catch2)

        # Update CMAKE_MODULE_PATH to include Catch2 extras
        list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    endif()

    add_subdirectory(tests)
endif()

# ----------------------------------------------------------------
# Installation and export configuration
# ----------------------------------------------------------------

include(GNUInstallDirs)

# Install header files
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install the library
install(
    TARGETS jsonrpc
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Configure and install pkg-config file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/jsonrpc.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc.pc"
    @ONLY
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)
