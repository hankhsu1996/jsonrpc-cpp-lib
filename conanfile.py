from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps


class JsonRpcRecipe(ConanFile):
    """ Conan recipe for jsonrpc-cpp-lib """
    name = "jsonrpc-cpp-lib"
    version = "2.0.1"
    license = "MIT"
    author = "Shou-Li Hsu <hank850503@gmail.com>"
    url = "https://github.com/hankhsu1996/jsonrpc-cpp-lib"
    description = (
        "Welcome to the JSON-RPC 2.0 Modern C++ Library! "
        "This library provides a lightweight, modern C++ implementation of "
        "JSON-RPC 2.0 servers and clients. It is designed to be flexible, allowing "
        "integration with various transport layers. This library makes it easy to "
        "register methods and notifications, binding them to client logic efficiently."
    )
    topics = ("json-rpc", "rpc", "json", "modern-c++", "networking")

    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"

    # Declare package dependencies
    requires = [
        "nlohmann_json/3.11.3",
        "spdlog/1.14.1",
        "asio/1.28.2"
    ]

    tool_requires = [
        "ninja/1.12.1",
        "ccache/4.10"
    ]

    test_requires = [
        "catch2/3.6.0"
    ]

    # Define options for building examples and tests
    options = {
        "build_examples": [True, False],
        "build_tests": [True, False]
    }
    default_options = {
        "build_examples": False,
        "build_tests": False
    }

    exports_sources = "CMakeLists.txt", "src/*", "include/*", "LICENSE", "README.md"

    def layout(self):
        """ Define the layout of the project """
        cmake_layout(self)

    def generate(self):
        """ Generate the CMake toolchain and dependencies files """
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.user_presets_path = 'ConanPresets.json'
        tc.cache_variables["USE_CONAN"] = "ON"
        tc.cache_variables["BUILD_EXAMPLES"] = self.options.build_examples
        tc.cache_variables["BUILD_TESTS"] = self.options.build_tests
        tc.generator = "Ninja"
        tc.generate()

    def build(self):
        """ Build the project """
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        """ Package the project """
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        """ Define package information for consumers """
        self.cpp_info.libs = ["jsonrpc-cpp-lib"]
