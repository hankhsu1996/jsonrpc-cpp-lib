# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System and Commands

This project supports dual build systems with Bazel being preferred:

### Bazel (Preferred)

- **Build**: `bazel build //...`
- **Test**: `bazel test //...`
- **Generate compile_commands.json**: `bazel run @hedron_compile_commands//:refresh_all`

### CMake with Conan

- **Install dependencies**: `conan install . --build=missing`
- **Configure**: `cmake --preset release` (or `debug`)
- **Build**: `cmake --build --preset release`
- **Test**: `ctest --preset release`

### CMake without Conan

- **Configure**: `cmake -S . -B build`
- **Build**: `cmake --build build`
- **Test**: `ctest --test-dir build`

## Architecture Overview

This is a **JSON-RPC 2.0 Modern C++ Library** implementing both client and server functionality in a unified design:

### Core Components

- **RpcEndpoint** (`include/jsonrpc/endpoint/endpoint.hpp`): Unified class that acts as both client and server following JSON-RPC 2.0's symmetric design
- **Transport Layer** (`include/jsonrpc/transport/`): Abstract transport with implementations for pipes, sockets, and framed pipes
- **Dispatcher** (`include/jsonrpc/endpoint/dispatcher.hpp`): Routes method calls and notifications to registered handlers
- **Request/Response** (`include/jsonrpc/endpoint/request.hpp`, `response.hpp`): JSON-RPC message structures

### Transport Implementations

- **PipeTransport**: Unix domain sockets/named pipes
- **SocketTransport**: TCP sockets
- **FramedPipeTransport**: Length-prefixed message framing over pipes

### Key Dependencies

- **nlohmann/json**: JSON parsing and serialization
- **asio**: Async I/O and coroutines (C++23 `awaitable`)
- **spdlog**: Logging

## Development Guidelines

### Programming Model

- Uses **C++23 features** extensively
- **ASIO coroutines** (`asio::awaitable`) for async operations - always include `<asio.hpp>`
- **Thread pools** for CPU-bound handlers, single-threaded for endpoint logic
- Use **strands** for synchronization instead of mutexes

### Code Style

- Follows **Google naming conventions** (enforced by clang-tidy)
- **CamelCase** for classes, functions, enums
- **lower_case** for variables, parameters, members
- **Private members** end with `_`
- **Constants** prefixed with `k`

### API Evolution Philosophy

- This is an **early-stage library** - APIs can change freely
- **No backward compatibility** priority at this stage
- **Remove deprecated code** instead of marking as deprecated
- **Critical evaluation** over blind instruction following

### Avoid These Patterns

- Raw pointers (use smart pointers, containers, `std::optional`)
- Global state and singletons
- `std::future`, `std::promise`, `std::lock`, `std::mutex` (usually indicates design issues)

## Git Workflow Rules

### Branch Naming
- Use prefixes: `feature/`, `chore/`, or `bugfix/`
- Use kebab-case after prefix: `feature/add-new-transport`
- Examples: `chore/update-dependencies`, `bugfix/fix-memory-leak`

### Commit Messages
- Keep commit messages **concise and simple**
- Use **imperative mood** starting with a verb: "Remove", "Add", "Fix"
- **No attribution** in commit messages (save for PR descriptions)
- **No colons** in commit messages
- For simple changes, use single-line messages
- Only use bullet points for complex multi-part changes

Examples:
- **Good**: "Remove global spdlog debug calls from endpoint templates"
- **Good**: "Add message queue support to transport layer"
- **Bad**: "Remove global spdlog debug calls: fix logging issue"
- **Bad**: "Remove global spdlog debug calls and add CLAUDE.md with project architecture and development guidelines"

### Special Characters
- **Avoid special Unicode characters** (checkmarks, crosses, emojis) in code and documentation
- Use plain text alternatives: "Good/Bad", "Yes/No", "Pass/Fail"

## Project Structure

- `src/endpoint/`: Core RPC endpoint implementation
- `src/transport/`: Transport layer implementations
- `include/jsonrpc/`: Public API headers
- `examples/`: Usage examples including LSP client/server
- `tests/`: Unit and integration tests
