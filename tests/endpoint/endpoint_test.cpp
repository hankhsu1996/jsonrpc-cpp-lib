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
      co_await endpoint->Start();
      co_await endpoint->Shutdown();
    });
  }

  SECTION("Double Start prevention") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      // Create transport and endpoint
      auto transport = std::make_unique<MockTransport>(executor);
      auto endpoint =
          std::make_unique<RpcEndpoint>(executor, std::move(transport));

      // First start should succeed
      co_await endpoint->Start();

      // Second start should throw
      REQUIRE_THROWS_AS(co_await endpoint->Start(), std::runtime_error);

      // Shutdown should work
      co_await endpoint->Shutdown();

      // Verify endpoint is no longer running
      bool is_running = endpoint->IsRunning();
      REQUIRE_FALSE(is_running);
    });
  }
}

// // Basic Request/Response Format Tests
// TEST_CASE("RpcEndpoint - Message format", "[endpoint]") {
//   asio::io_context io_ctx;

//   SECTION("Notification format") {
//     RunTest([&]() -> asio::awaitable<void> {
//       auto transport = std::make_unique<MockTransport>(io_ctx);
//       auto* transport_ptr = transport.get();
//       auto endpoint = std::make_unique<RpcEndpoint>(std::move(transport));

//       co_await endpoint->Start();

//       // Send a notification
//       Json params = {{"event", "update"}, {"value", 100}};
//       co_await endpoint->SendNotification("test_notification", params);

//       // Verify notification format
//       REQUIRE(!transport_ptr->GetSentRequests().empty());
//       auto sent_notification =
//           Json::parse(transport_ptr->GetSentRequests().back());
//       REQUIRE(sent_notification["jsonrpc"] == "2.0");
//       REQUIRE(sent_notification["method"] == "test_notification");
//       REQUIRE(sent_notification["params"] == params);
//       REQUIRE_FALSE(sent_notification.contains("id"));

//       co_await endpoint->Shutdown();
//     });
//   }
// }

// // Method Registration Tests
// TEST_CASE("RpcEndpoint - Method registration", "[endpoint]") {
//   asio::io_context io_ctx;

//   SECTION("Method registration") {
//     RunTest([&]() -> asio::awaitable<void> {
//       auto transport = std::make_unique<MockTransport>(io_ctx);
//       auto endpoint = std::make_unique<RpcEndpoint>(std::move(transport));

//       co_await endpoint->Start();

//       // Register a method
//       endpoint->RegisterMethodCall(
//           "test_method",
//           [](const std::optional<nlohmann::json>& params)
//               -> asio::awaitable<nlohmann::json> {
//             co_return nlohmann::json::object({{"result", "success"}});
//           });

//       co_await endpoint->Shutdown();
//     });
//   }

//   SECTION("Notification registration") {
//     RunTest([&]() -> asio::awaitable<void> {
//       auto transport = std::make_unique<MockTransport>(io_ctx);
//       auto endpoint = std::make_unique<RpcEndpoint>(std::move(transport));

//       co_await endpoint->Start();

//       // Register a notification handler
//       endpoint->RegisterNotification(
//           "test_notification",
//           [](const std::optional<nlohmann::json>& params)
//               -> asio::awaitable<void> { co_return; });

//       co_await endpoint->Shutdown();
//     });
//   }
// }
