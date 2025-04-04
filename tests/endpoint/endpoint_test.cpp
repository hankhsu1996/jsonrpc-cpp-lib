#include "jsonrpc/endpoint/endpoint.hpp"

#include <memory>

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "../common/mock_transport.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::test::MockTransport;
using Json = nlohmann::json;

namespace {

// Simple ASIO test runner that handles coroutines and proper completion
template <typename TestFunc>
auto RunTest(TestFunc&& test_func) {
  auto cout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("server_logger", cout_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);

  // Create io_context and get executor
  asio::io_context io_ctx;
  auto executor = io_ctx.get_executor();

  // Use a non-coroutine lambda to launch the test function
  asio::co_spawn(
      io_ctx,
      [f = std::forward<TestFunc>(test_func), executor]() {
        // Return the awaitable directly - not a coroutine lambda
        return f(executor);
      },
      asio::detached);

  // Run the io_context
  io_ctx.run();
}

}  // namespace

// Basic Lifecycle Tests
TEST_CASE("RpcEndpoint - Basic lifecycle", "[endpoint]") {
  SECTION("Start and shutdown") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      // Create transport and endpoint
      auto transport = std::make_unique<MockTransport>(executor);
      auto endpoint =
          std::make_unique<RpcEndpoint>(executor, std::move(transport));

      // Start and shutdown
      auto start_result = co_await endpoint->Start();
      REQUIRE(start_result);
      auto shutdown_result = co_await endpoint->Shutdown();
      REQUIRE(shutdown_result);
    });
  }

  SECTION("Double Start prevention") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      // Create transport and endpoint
      auto transport = std::make_unique<MockTransport>(executor);
      auto endpoint =
          std::make_unique<RpcEndpoint>(executor, std::move(transport));

      // First start should succeed
      auto start_result = co_await endpoint->Start();
      REQUIRE(start_result);

      // Second start should throw
      auto start_result2 = co_await endpoint->Start();
      REQUIRE_FALSE(start_result2);
      REQUIRE(
          start_result2.error().message == "RPC endpoint is already running");

      // Shutdown should work
      auto shutdown_result = co_await endpoint->Shutdown();
      REQUIRE(shutdown_result);

      // Verify endpoint is no longer running
      bool is_running = endpoint->IsRunning();
      REQUIRE_FALSE(is_running);
    });
  }
}
