#include "jsonrpc/endpoint/dispatcher.hpp"

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using jsonrpc::endpoint::Dispatcher;
using jsonrpc::error::RpcErrorCode;

// Helper function for running dispatcher tests
template <typename TestFunc>
auto RunTest(TestFunc&& test_func) {
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

TEST_CASE("Dispatcher initialization", "[Dispatcher]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Dispatcher dispatcher(executor);
    auto response = co_await dispatcher.DispatchRequest("{}");
    REQUIRE(response.has_value());
    co_return;
  });
}

TEST_CASE("Method registration and handling", "[Dispatcher]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    Dispatcher dispatcher(executor);
    dispatcher.RegisterMethodCall(
        "sum",
        [](std::optional<nlohmann::json> params)
            -> asio::awaitable<nlohmann::json> {
          int result = 0;
          if (params && params->is_array()) {
            for (const auto& value : *params) {
              result += value.get<int>();
            }
          }
          co_return result;
        });
    auto response = co_await dispatcher.DispatchRequest(
        R"({"jsonrpc":"2.0","method":"sum","params":[1,2,3],"id":1})");
    REQUIRE(response.has_value());
    auto result = nlohmann::json::parse(response.value());
    REQUIRE(result["result"] == 6);
    co_return;
  });
}

TEST_CASE("Batch request handling", "[Dispatcher]") {
  asio::io_context io_ctx;
  auto executor = io_ctx.get_executor();
  Dispatcher dispatcher(executor);

  SECTION("Valid batch request") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      Dispatcher dispatcher(executor);
      dispatcher.RegisterMethodCall(
          "sum",
          [](std::optional<nlohmann::json> params)
              -> asio::awaitable<nlohmann::json> {
            co_return params->at(0).get<int>() + params->at(1).get<int>();
          });
      dispatcher.RegisterNotification(
          "notify",
          [](std::optional<nlohmann::json> params) -> asio::awaitable<void> {
            co_return;
          });
      std::string batch_request = R"([
          {"jsonrpc":"2.0","method":"sum","params":[1,2],"id":"1"},
          {"jsonrpc":"2.0","method":"notify","params":[7]},
          {"jsonrpc":"2.0","method":"sum","params":[3,4],"id":"2"}
      ])";
      auto response = co_await dispatcher.DispatchRequest(batch_request);
      REQUIRE(response.has_value());
      auto result = nlohmann::json::parse(response.value());
      REQUIRE(result.is_array());
      REQUIRE(result.size() == 2);
      REQUIRE(result[0]["result"] == 3);
      REQUIRE(result[1]["result"] == 7);
      co_return;
    });
  }

  SECTION("Empty batch") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      Dispatcher dispatcher(executor);
      auto response = co_await dispatcher.DispatchRequest("[]");
      REQUIRE(response.has_value());
      auto result = nlohmann::json::parse(*response);
      REQUIRE(
          result["error"]["code"] ==
          static_cast<int>(RpcErrorCode::kInvalidRequest));
      co_return;
    });
  }

  SECTION("Invalid batch request") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      Dispatcher dispatcher(executor);
      auto response = co_await dispatcher.DispatchRequest("[1]");
      REQUIRE(response.has_value());
      auto result = nlohmann::json::parse(*response);
      REQUIRE(result.is_array());
      REQUIRE(result.size() == 1);
      REQUIRE(
          result[0]["error"]["code"] ==
          static_cast<int>(RpcErrorCode::kInvalidRequest));
      co_return;
    });
  }
}

TEST_CASE("Error handling", "[Dispatcher]") {
  asio::io_context io_ctx;
  auto executor = io_ctx.get_executor();
  Dispatcher dispatcher(executor);

  SECTION("Method not found") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      Dispatcher dispatcher(executor);
      auto response = co_await dispatcher.DispatchRequest(
          R"({"jsonrpc":"2.0","method":"unknown","id":1})");
      REQUIRE(response.has_value());
      auto result = nlohmann::json::parse(*response);
      REQUIRE(
          result["error"]["code"] ==
          static_cast<int>(RpcErrorCode::kMethodNotFound));
      co_return;
    });
  }

  SECTION("Invalid request") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      Dispatcher dispatcher(executor);
      auto response =
          co_await dispatcher.DispatchRequest(R"({"method":"test"})");
      REQUIRE(response.has_value());
      auto result = nlohmann::json::parse(*response);
      REQUIRE(
          result["error"]["code"] ==
          static_cast<int>(RpcErrorCode::kInvalidRequest));
    });
  }

  SECTION("Parse error") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      Dispatcher dispatcher(executor);
      auto response = co_await dispatcher.DispatchRequest("invalid json");
      REQUIRE(response.has_value());
      auto result = nlohmann::json::parse(*response);
      REQUIRE(
          result["error"]["code"] ==
          static_cast<int>(RpcErrorCode::kParseError));
    });
  }

  SECTION("Parse error") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      Dispatcher dispatcher(executor);
      auto response = co_await dispatcher.DispatchRequest("invalid json");
      REQUIRE(response.has_value());
      auto result = nlohmann::json::parse(*response);
      REQUIRE(
          result["error"]["code"] ==
          static_cast<int>(RpcErrorCode::kParseError));
    });
  }

  SECTION("Internal error") {
    RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
      Dispatcher dispatcher(executor);
      dispatcher.RegisterMethodCall(
          "fail",
          [](const std::optional<nlohmann::json>&)
              -> asio::awaitable<nlohmann::json> {
            throw std::runtime_error("Intentional failure");
          });
      auto response = co_await dispatcher.DispatchRequest(
          R"({"jsonrpc":"2.0","method":"fail","id":1})");
      REQUIRE(response.has_value());
      auto result = nlohmann::json::parse(*response);
      REQUIRE(
          result["error"]["code"] ==
          static_cast<int>(RpcErrorCode::kInternalError));
    });
  }
}
