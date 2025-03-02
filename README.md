# JSON-RPC 2.0 Modern C++ Library

[![CI](https://github.com/hankhsu1996/jsonrpc-cpp-lib/actions/workflows/ci.yml/badge.svg?event=push)](https://github.com/hankhsu1996/jsonrpc-cpp-lib/actions/workflows/ci.yml)
![GitHub Release](https://img.shields.io/github/v/release/hankhsu1996/jsonrpc-cpp-lib)
![GitHub License](https://img.shields.io/github/license/hankhsu1996/jsonrpc-cpp-lib)

Welcome to the **JSON-RPC 2.0 Modern C++ Library**! This library provides a lightweight, modern C++ implementation of [JSON-RPC 2.0](https://www.jsonrpc.org/specification) servers and clients. It is designed to be flexible, allowing integration with various transport layers. This library makes it easy to register methods and notifications, binding them to client logic efficiently.

## ✨ Features

- **Fully Compliant with JSON-RPC 2.0**: Supports method calls, notifications, comprehensive error handling, and batch requests.
- **Modern and Lightweight**: Leverages C++20 features with minimal dependencies, focusing solely on the JSON-RPC protocol.
- **Unified Endpoint Design**: Single `RpcEndpoint` class that can act as both client and server, following JSON-RPC 2.0's symmetric design.
- **Transport-Agnostic**: Abstract transport layer allows use of provided implementations or custom ones.
- **Simple JSON Integration**: Uses [nlohmann/json](https://github.com/nlohmann/json) for easy JSON object interaction, requiring no learning curve.
- **Flexible Handler Registration**: Register handlers using `std::function`, supporting lambdas, function pointers, and other callable objects.

## 🚀 Getting Started

### Prerequisites

- **Compiler**: Any compiler with C++20 support.
- **Bazel**: Version 7.0+ (for Bzlmod support).
- **CMake**: Version 3.19+ (for CMake preset support, optional).
- **Conan**: Version 2.0+ (optional).

### Using Bazel

To include this library in your project with Bazel, ensure you are using Bazel 7.0 or later, as Bzlmod is enabled by default. Add the following to your `MODULE.bazel` file:

```bazel
http_archive = use_repo_rule("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
http_archive(
  name = "jsonrpc",
  urls = ["https://github.com/hankhsu1996/jsonrpc-cpp-lib/archive/refs/tags/v1.0.0.tar.gz"],
  strip_prefix = "jsonrpc-cpp-lib-1.0.0",
  sha256 = "a381dc02ab95c31902077793e926adbb0d8f67eadbc386b03451e952d50f0615",
)
```

### Optional: Using Conan 2

For projects using Conan, create a `conanfile.txt` in your project directory with the following content:

```ini
[requires]
jsonrpc-cpp-lib/1.0.0

[generators]
CMakeDeps
CMakeToolchain
```

### Optional: Using CMake FetchContent

If you prefer using CMake, add the library to your project with the following in your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
  jsonrpc-cpp-lib
  GIT_REPOSITORY https://github.com/hankhsu1996/jsonrpc-cpp-lib.git
  GIT_TAG v1.0.0
)
FetchContent_MakeAvailable(jsonrpc-cpp-lib)
```

## 📖 Usage and Examples

### Creating a JSON-RPC Endpoint

Here's how to create a simple JSON-RPC endpoint that both sends and receives method calls:

```cpp
using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::StdioTransport;
using Json = nlohmann::json;

// Create an endpoint with stdio transport
auto endpoint = RpcEndpoint(std::make_unique<StdioTransport>());

// Register a method named "add" that adds two numbers
endpoint.RegisterMethodCall("add", [](const std::optional<Json> &params) {
  int result = params.value()["a"] + params.value()["b"];
  return Json{{"result", result}};
});

// Register a notification named "stop" to stop the endpoint
endpoint.RegisterNotification("stop", [&endpoint](const std::optional<Json> &) {
  endpoint.Stop();
});

// Start the endpoint
endpoint.Start();

// Send a method call to another endpoint
auto response = endpoint.SendMethodCall("add", Json({{"a", 10}, {"b", 5}}));
spdlog::info("Add result: {}", response.dump());

// Send a notification
endpoint.SendNotification("stop");
```

Each endpoint can both register methods to handle incoming calls and send outgoing calls to other endpoints. The transport layer (e.g., stdio, socket, pipe) determines how endpoints connect to each other.

These examples demonstrate the basic usage of JSON-RPC endpoints. For more examples including different transport types and complete applications, please refer to the [examples folder](./examples/).

## 🛠️ Developer Guide

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

## 🤝 Contributing

We welcome contributions! If you have suggestions or find any issues, feel free to open an issue or pull request.

## 📄 License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE) file for more details.
