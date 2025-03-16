# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.1.0] - 2025-03-16

### Changed

- Renamed project from "jsonrpc-cpp-lib" to simply "jsonrpc" for cleaner integration
- Simplified CMake installation process for better reliability

## [2.0.2] - 2025-03-13

### Changed

- Update Catch2 from 3.6.0 to 3.8.0
- Update spdlog from 1.14.1 to 1.15.1
- Update asio from 1.28.2 to 1.32.0
- Update bazel_skylib from 1.5.0 to 1.7.1
- Modify CMakePresets.json to use Clang compiler by default, resolving batch processing memory allocation issues experienced with GCC

## [2.0.1] - 2025-03-09

### Changed

- Restructured Bazel build system for better organization

## [2.0.0] - 2025-03-09

### Changed

- **BREAKING**: Complete migration from blocking calls to Asio coroutines with awaitable interfaces
- **BREAKING**: Method signatures now return `asio::awaitable<>` instead of direct values
- **BREAKING**: Client code must use `co_await` with coroutine functions
- **BREAKING**: Method handlers must be implemented as coroutines
- Refactored transport layer to use asynchronous operations throughout
- Redesigned RPC endpoint to leverage coroutines for all operations
- Transitioned from thread pools to IO context-based execution
- Removed manual thread management in favor of asio's task model
- Improved error handling with exception propagation in coroutines

### Added

- Support for proper cancellation and timeouts via Asio facilities
- New static factory method `CreateClient` for client initialization
- Better documentation and examples for the coroutine-based approach
- LSP client example using TypeScript
- CMake export configuration for easier integration

### Removed

- Synchronous blocking API methods
- Thread pool-based execution model
- Stdio transport (poor fit for asynchronous model)

## [1.0.0] - 2024-08-16

### Added

- Initial release of JSON-RPC 2.0 C++ implementation.
- Implemented core JSON-RPC functionality:
  - Methods and notifications with named and positional parameters.
  - Abstract transport layer support with stdio, HTTP, and ASIO socket connectors.
  - Support for JSON-RPC batch mode.
- Client and server implementation with example files.
- Bazel build system and Conan package management support.
- Doxygen documentation with enhanced branding.
- Unit tests for key components: client, server, request, response, and transport layers.
- ASIO socket and Unix domain socket transport implementations.
- Support for async method calls and non-blocking request handling.
- Google C++ Style alignment and modern C++ practices.
