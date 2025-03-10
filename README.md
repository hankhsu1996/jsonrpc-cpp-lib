# JSON-RPC 2.0 Modern C++ Library

[![CI](https://github.com/hankhsu1996/jsonrpc-cpp-lib/actions/workflows/ci.yml/badge.svg?event=push)](https://github.com/hankhsu1996/jsonrpc-cpp-lib/actions/workflows/ci.yml)
![GitHub Release](https://img.shields.io/github/v/release/hankhsu1996/jsonrpc-cpp-lib)
![GitHub License](https://img.shields.io/github/license/hankhsu1996/jsonrpc-cpp-lib)

Welcome to the **JSON-RPC 2.0 Modern C++ Library**! This library provides a lightweight, modern C++ implementation of [JSON-RPC 2.0](https://www.jsonrpc.org/specification) servers and clients. It is designed to be flexible, allowing integration with various transport layers. This library makes it easy to register methods and notifications, binding them to client logic efficiently.

## Features

- **Fully Compliant with JSON-RPC 2.0**: Supports method calls, notifications, comprehensive error handling, and batch requests.
- **Modern and Lightweight**: Leverages C++20 features with minimal dependencies, focusing solely on the JSON-RPC protocol.
- **Unified Endpoint Design**: Single `RpcEndpoint` class that can act as both client and server, following JSON-RPC 2.0's symmetric design.
- **Transport-Agnostic**: Abstract transport layer allows use of provided implementations or custom ones.
- **Simple JSON Integration**: Uses [nlohmann/json](https://github.com/nlohmann/json) for easy JSON object interaction, requiring no learning curve.
- **Flexible Handler Registration**: Register handlers using `std::function`, supporting lambdas, function pointers, and other callable objects.

## Getting Started

### Prerequisites

- **Compiler**: Any compiler with C++20 support.
- **Build System**: Either Bazel 7.0+ (preferred) or CMake 3.19+ (alternative).
- **Optional**: Conan 2.0+ for dependency management with CMake.

### Adding to Your Project

There are several ways to include this library in your project:

#### Option 1: Using Bazel (Recommended)

Bazel provides a streamlined dependency management experience. To include this library in your project with Bazel, ensure you are using Bazel 7.0 or later, as Bzlmod is enabled by default.

##### A. Modern Approach: Using Bzlmod with Git Override (Recommended)

This is the modern, recommended approach that automatically handles all dependencies:

```bazel
# In your MODULE.bazel file
bazel_dep(name = "jsonrpc_cpp_lib", version = "0.0.0")
git_override(
    module_name = "jsonrpc_cpp_lib",
    remote = "https://github.com/hankhsu1996/jsonrpc-cpp-lib.git",
    tag = "v2.0.0"
)
```

With this approach, all of this library's dependencies (nlohmann_json, spdlog, asio, etc.) will be automatically pulled in and version-managed by Bazel.

##### B. Traditional Approach: Using HTTP Archive

If you prefer the traditional approach, add the following to your `MODULE.bazel` or `WORKSPACE.bazel` file:

```bazel
http_archive = use_repo_rule("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
http_archive(
  name = "jsonrpc",
  urls = ["https://github.com/hankhsu1996/jsonrpc-cpp-lib/archive/refs/tags/v2.0.0.tar.gz"],
  strip_prefix = "jsonrpc-cpp-lib-2.0.0",
  sha256 = "12ce2e8d539e01f0f226ba4be409115aff88e48dbe4c6d7178fdcbdd7fb54244",
)
```

Note: With this approach, you'll need to manually add all dependencies (nlohmann_json, spdlog, asio, etc.) to your build files.

#### Option 2: Using CMake

CMake offers two main approaches for including this library:

##### A. As a Build-Time Dependency (FetchContent)

This approach downloads and builds the library as part of your project. It's ideal for development workflows where you want everything self-contained:

```cmake
include(FetchContent)
FetchContent_Declare(
  jsonrpc-cpp-lib
  GIT_REPOSITORY https://github.com/hankhsu1996/jsonrpc-cpp-lib.git
  GIT_TAG v2.0.0
)
FetchContent_MakeAvailable(jsonrpc-cpp-lib)

# Link your target with the library
target_link_libraries(your_app PRIVATE jsonrpc::jsonrpc-cpp-lib)
```

##### B. As a System-Wide Installation (find_package)

This approach uses a pre-installed version of the library. It's better for production environments and system-wide installations:

1. First, install the library:

   ```bash
   # Configure the project
   cmake -S . -B build

   # Build the project
   cmake --build build

   # Install the library (may require sudo)
   cmake --install build
   ```

   You can specify a custom installation prefix with `-DCMAKE_INSTALL_PREFIX=/your/custom/path`

2. Then use it in your project:

   ```cmake
   # Find the package
   find_package(jsonrpc-cpp-lib REQUIRED)

   # Create your executable
   add_executable(your_app main.cpp)

   # Link against the library
   target_link_libraries(your_app PRIVATE jsonrpc::jsonrpc-cpp-lib)
   ```

#### Option 3: Using Conan with CMake

For projects using Conan for dependency management, create a `conanfile.txt` in your project directory:

```ini
[requires]
jsonrpc-cpp-lib/2.0.0

[generators]
CMakeDeps
CMakeToolchain
```

Then use Conan to install dependencies and generate CMake files:

```bash
conan install . --build=missing
cmake -DUSE_CONAN=ON -B build .
cmake --build build
```

## Usage and Examples

### Creating a JSON-RPC Client and Server

Here's a simplified example of how to create a client and server using the library:

**Server Example**

```cpp
#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::PipeTransport;
using Json = nlohmann::json;

// Calculator functions
auto Add(const std::optional<Json>& params) -> asio::awaitable<Json> {
  const auto& p = params.value_or(Json::object());
  double a = p["a"];
  double b = p["b"];
  co_return Json{{"result", a + b}};
}

// Main server function
auto RunServer(asio::io_context& io_context, const std::string& socket_path)
    -> asio::awaitable<void> {
  // Create transport and RPC endpoint
  auto transport = std::make_unique<PipeTransport>(io_context, socket_path, true);
  RpcEndpoint server(io_context, std::move(transport));

  // Register methods
  server.RegisterMethodCall("add", Add);

  // Register shutdown notification
  server.RegisterNotification(
    "stop", [&server](const std::optional<Json>&) -> asio::awaitable<void> {
      co_await server.Shutdown();
      co_return;
    });

  // Start server and wait for shutdown
  co_await server.Start();
  co_await server.WaitForShutdown();
  co_return;
}

int main() {
  asio::io_context io_context;
  const std::string socket_path = "/tmp/calculator_pipe";

  // Run server
  asio::co_spawn(io_context, RunServer(io_context, socket_path),
    [](std::exception_ptr e) {
      if (e) {
        try { std::rethrow_exception(e); }
        catch (const std::exception& ex) {
          std::cerr << "Error: " << ex.what() << std::endl;
        }
      }
    });

  io_context.run();
  return 0;
}
```

**Client Example**

```cpp
#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::PipeTransport;
using Json = nlohmann::json;

// Main client function
auto RunClient(asio::io_context& io_context) -> asio::awaitable<void> {
  // Create transport and RPC client
  const std::string socket_path = "/tmp/calculator_pipe";
  auto transport = std::make_unique<PipeTransport>(io_context, socket_path);
  auto client = co_await RpcEndpoint::CreateClient(io_context, std::move(transport));

  // Call "add" method
  Json params = {{"a", 10}, {"b", 5}};
  Json result = co_await client->CallMethod("add", params);
  std::cout << "Result: " << result.dump() << std::endl;

  // Send shutdown notification
  co_await client->SendNotification("stop");

  // Clean shutdown
  co_await client->Shutdown();
  co_return;
}

int main() {
  asio::io_context io_context;

  // Run client
  asio::co_spawn(io_context, RunClient(io_context),
    [](std::exception_ptr e) {
      if (e) {
        try { std::rethrow_exception(e); }
        catch (const std::exception& ex) {
          std::cerr << "Error: " << ex.what() << std::endl;
        }
      }
    });

  io_context.run();
  return 0;
}
```

For more examples including different transport types and complete applications, please refer to the [examples folder](./examples/).

## Developer Guide

Follow these steps to build, test, and set up your development environment. Bazel is the preferred method.

### Option 1: Using Bazel (Preferred)

**Step 1: Build the Project**

```
bazel build //...
```

**Step 2: Run Tests**

```
bazel test //...
```

### Option 2: Using CMake and Conan

**Step 1: Install Dependencies**

```
conan profile detect --force
conan install . --build=missing
conan install . -s build_type=Debug --build=missing
```

**Step 2: Configure and Build**

```
cmake --preset release
cmake --build --preset release
```

**Step 3: Run Tests**

```
ctest --preset release
```

### Optional: Debug Configuration

For Debug configuration:

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

### Optional: CMake without Conan

If you prefer not to use Conan, you can build the project with CMake directly:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

### Compilation Database

Generate the `compile_commands.json` file for tools like `clang-tidy` and `clangd`:

- **Bazel**: Run `bazel run @hedron_compile_commands//:refresh_all`.
- **CMake**: Simply build the project. The database will be generated automatically.

In both cases, the `compile_commands.json` file will be placed in the root directory.

## Contributing

We welcome contributions! If you have suggestions or find any issues, feel free to open an issue or pull request.

## License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE) file for more details.
