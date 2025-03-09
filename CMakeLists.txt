cmake_minimum_required(VERSION 3.19)
project(jsonrpc-cpp-lib VERSION 2.0.0 LANGUAGES CXX)

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
add_library(jsonrpc-cpp-lib ${SOURCES})

# Include directories for the library
target_include_directories(jsonrpc-cpp-lib PUBLIC
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
    target_link_libraries(jsonrpc-cpp-lib PUBLIC
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
        GIT_TAG v1.14.1
    )
    FetchContent_MakeAvailable(spdlog)

    FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-28-2
    )
    FetchContent_MakeAvailable(asio)

    # Ensure asio include directory is added
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE
        ${asio_SOURCE_DIR}/asio/include
    )

    # Link dependencies
    target_link_libraries(jsonrpc-cpp-lib PUBLIC
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
        TARGET jsonrpc-cpp-lib PRE_BUILD
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
include(CMakePackageConfigHelpers)

# Make the library installable
install(
    TARGETS jsonrpc-cpp-lib
    EXPORT jsonrpc-cpp-lib-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install header files
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Create a simple config file that includes the targets file
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc-cpp-libConfig.cmake" "
# jsonrpc-cpp-lib CMake configuration file
include(CMakeFindDependencyMacro)

# Find dependencies
find_dependency(nlohmann_json REQUIRED)
find_dependency(spdlog REQUIRED)

# Include targets file
include(\"\${CMAKE_CURRENT_LIST_DIR}/jsonrpc-cpp-lib-targets.cmake\")
")

# Generate the version file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc-cpp-libConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install CMake config files
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc-cpp-libConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc-cpp-libConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jsonrpc-cpp-lib
)

# Export targets
install(
    EXPORT jsonrpc-cpp-lib-targets
    FILE jsonrpc-cpp-lib-targets.cmake
    NAMESPACE jsonrpc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jsonrpc-cpp-lib
)

# Generate export targets for build tree
export(
    EXPORT jsonrpc-cpp-lib-targets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc-cpp-lib-targets.cmake"
    NAMESPACE jsonrpc::
)

# Configure and install pkg-config file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/jsonrpc-cpp-lib.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc-cpp-lib.pc"
    @ONLY
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/jsonrpc-cpp-lib.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)
