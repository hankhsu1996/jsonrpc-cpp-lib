#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/dispatcher.hpp"
#include "jsonrpc/endpoint/task_executor.hpp"
#include "jsonrpc/endpoint/types.hpp"

using jsonrpc::endpoint::Dispatcher;
using jsonrpc::endpoint::ErrorCode;
using jsonrpc::endpoint::TaskExecutor;

TEST_CASE("Dispatcher initialization", "[Dispatcher]") {
  asio::io_context io_ctx;
  auto executor =
      std::make_shared<TaskExecutor>(1);  // Use real TaskExecutor with 1 thread
  Dispatcher dispatcher(executor);

  bool test_passed = false;

  // Create a coroutine that awaits the dispatcher result
  asio::co_spawn(
      io_ctx,
      [&]() -> asio::awaitable<void> {
        auto response = co_await dispatcher.DispatchRequest("{}");
        test_passed = response.has_value();
        co_return;
      },
      asio::detached);

  // Run the io_context to execute the coroutine
  io_ctx.run();

  // Check our test result
  REQUIRE(test_passed);
}

TEST_CASE("Method registration and handling", "[Dispatcher]") {
  asio::io_context io_ctx;
  auto executor =
      std::make_shared<TaskExecutor>(1);  // Use real TaskExecutor with 1 thread
  Dispatcher dispatcher(executor);

  SECTION("Register and call method") {
    dispatcher.RegisterMethodCall(
        "sum",
        [](const std::optional<nlohmann::json>& params)
            -> asio::awaitable<nlohmann::json> {
          int result = 0;
          if (params && params->is_array()) {
            for (const auto& value : *params) {
              result += value.get<int>();
            }
          }
          co_return result;
        });

    nlohmann::json result_json;
    bool test_passed = false;

    // Use coroutine approach
    asio::co_spawn(
        io_ctx,
        [&]() -> asio::awaitable<void> {
          auto response = co_await dispatcher.DispatchRequest(
              R"({"jsonrpc":"2.0","method":"sum","params":[1,2,3],"id":1})");

          test_passed = response.has_value();
          if (test_passed) {
            result_json = nlohmann::json::parse(*response);
          }
          co_return;
        },
        asio::detached);

    // Run the io_context to execute the coroutine
    io_ctx.run();

    REQUIRE(test_passed);
    REQUIRE(result_json["result"] == 6);
  }
}

TEST_CASE("Batch request handling", "[Dispatcher]") {
  asio::io_context io_ctx;
  auto executor = std::make_shared<TaskExecutor>(1);  // Use real TaskExecutor
  Dispatcher dispatcher(executor);

  SECTION("Valid batch request") {
    dispatcher.RegisterMethodCall(
        "sum",
        [](const std::optional<nlohmann::json>& params)
            -> asio::awaitable<nlohmann::json> {
          co_return params->at(0).get<int>() + params->at(1).get<int>();
        });

    dispatcher.RegisterNotification(
        "notify",
        [](const std::optional<nlohmann::json>&) -> asio::awaitable<void> {
          co_return;
        });

    std::string batch_request = R"([
            {"jsonrpc":"2.0","method":"sum","params":[1,2],"id":"1"},
            {"jsonrpc":"2.0","method":"notify","params":[7]},
            {"jsonrpc":"2.0","method":"sum","params":[3,4],"id":"2"}
        ])";

    nlohmann::json result_json;
    bool test_passed = false;

    asio::co_spawn(
        io_ctx,
        [&]() -> asio::awaitable<void> {
          auto response = co_await dispatcher.DispatchRequest(batch_request);

          test_passed = response.has_value();
          if (test_passed) {
            result_json = nlohmann::json::parse(*response);
          }
          co_return;
        },
        asio::detached);

    io_ctx.run();

    REQUIRE(test_passed);
    REQUIRE(result_json.is_array());
    REQUIRE(result_json.size() == 2);  // Notification doesn't produce response
    REQUIRE(result_json[0]["result"] == 3);
    REQUIRE(result_json[1]["result"] == 7);
  }

  SECTION("Empty batch") {
    bool test_passed = false;
    nlohmann::json result_json;

    asio::co_spawn(
        io_ctx,
        [&]() -> asio::awaitable<void> {
          auto response = co_await dispatcher.DispatchRequest("[]");

          test_passed = response.has_value();
          if (test_passed) {
            result_json = nlohmann::json::parse(*response);
          }
          co_return;
        },
        asio::detached);

    io_ctx.run();

    REQUIRE(test_passed);
    REQUIRE(
        result_json["error"]["code"] ==
        static_cast<int>(ErrorCode::kInvalidRequest));
  }

  SECTION("Invalid batch request") {
    bool test_passed = false;
    nlohmann::json result_json;

    asio::co_spawn(
        io_ctx,
        [&]() -> asio::awaitable<void> {
          auto response = co_await dispatcher.DispatchRequest("[1]");

          test_passed = response.has_value();
          if (test_passed) {
            result_json = nlohmann::json::parse(*response);
          }
          co_return;
        },
        asio::detached);

    io_ctx.run();

    REQUIRE(test_passed);
    REQUIRE(result_json.is_array());
    REQUIRE(
        result_json[0]["error"]["code"] ==
        static_cast<int>(ErrorCode::kInvalidRequest));
  }
}

TEST_CASE("Error handling", "[Dispatcher]") {
  asio::io_context io_ctx;
  Dispatcher dispatcher(std::make_shared<TaskExecutor>(1));
  SECTION("Method not found") {
    asio::co_spawn(
        io_ctx,
        [&dispatcher]() -> asio::awaitable<void> {
          auto response = co_await dispatcher.DispatchRequest(
              R"({"jsonrpc":"2.0","method":"unknown","id":1})");
          REQUIRE(response.has_value());
          auto json = nlohmann::json::parse(*response);
          REQUIRE(
              json["error"]["code"] ==
              static_cast<int>(ErrorCode::kMethodNotFound));
          co_return;
        },
        asio::detached);
    io_ctx.run();
  }

  SECTION("Invalid request") {
    asio::co_spawn(
        io_ctx,
        [&dispatcher]() -> asio::awaitable<void> {
          auto response =
              co_await dispatcher.DispatchRequest(R"({"method":"test"})");
          REQUIRE(response.has_value());
          auto json = nlohmann::json::parse(*response);
          REQUIRE(
              json["error"]["code"] ==
              static_cast<int>(ErrorCode::kInvalidRequest));
        },
        asio::detached);
  }

  // SECTION("Parse error") {
  //   auto response = asio::co_spawn(
  //                       io_ctx, dispatcher.DispatchRequest("invalid json"),
  //                       asio::use_future)
  //                       .get();
  //   REQUIRE(response.has_value());
  //   auto json = nlohmann::json::parse(*response);
  //   REQUIRE(json["error"]["code"] ==
  //   static_cast<int>(ErrorCode::kParseError));
  // }

  SECTION("Parse error") {
    asio::co_spawn(
        io_ctx,
        [&dispatcher]() -> asio::awaitable<void> {
          auto response = co_await dispatcher.DispatchRequest("invalid json");
          REQUIRE(response.has_value());
          auto json = nlohmann::json::parse(*response);
          REQUIRE(
              json["error"]["code"] ==
              static_cast<int>(ErrorCode::kParseError));
        },
        asio::detached);
  }

  // SECTION("Internal error") {
  //   dispatcher.RegisterMethodCall(
  //       "fail",
  //       [](const std::optional<nlohmann::json>&)
  //           -> asio::awaitable<nlohmann::json> {
  //         throw std::runtime_error("Intentional failure");
  //         co_return nlohmann::json();
  //       });

  //   auto response = asio::co_spawn(
  //                       io_ctx,
  //                       dispatcher.DispatchRequest(
  //                           R"({"jsonrpc":"2.0","method":"fail","id":1})"),
  //                       asio::use_future)
  //                       .get();
  //   REQUIRE(response.has_value());
  //   auto json = nlohmann::json::parse(*response);
  //   REQUIRE(
  //       json["error"]["code"] ==
  //       static_cast<int>(ErrorCode::kInternalError));
  // }

  SECTION("Internal error") {
    dispatcher.RegisterMethodCall(
        "fail",
        [](const std::optional<nlohmann::json>&)
            -> asio::awaitable<nlohmann::json> {
          throw std::runtime_error("Intentional failure");
          co_return nlohmann::json();
        });
    asio::co_spawn(
        io_ctx,
        [&dispatcher]() -> asio::awaitable<void> {
          auto response = co_await dispatcher.DispatchRequest(
              R"({"jsonrpc":"2.0","method":"fail","id":1})");
          REQUIRE(response.has_value());
          auto json = nlohmann::json::parse(*response);
          REQUIRE(
              json["error"]["code"] ==
              static_cast<int>(ErrorCode::kInternalError));
        },
        asio::detached);
  }
}
