#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/dispatcher.hpp"
#include "jsonrpc/endpoint/types.hpp"

using jsonrpc::endpoint::Dispatcher;
using jsonrpc::endpoint::ErrorCode;

TEST_CASE("Dispatcher initialization", "[Dispatcher]") {
  SECTION("Single-threaded initialization") {
    Dispatcher dispatcher(false);
    REQUIRE_NOTHROW(dispatcher.DispatchRequest("{}"));
  }

  SECTION("Multi-threaded initialization") {
    Dispatcher dispatcher(true, 4);
    REQUIRE_NOTHROW(dispatcher.DispatchRequest("{}"));
  }
}

TEST_CASE("Method registration and handling", "[Dispatcher]") {
  Dispatcher dispatcher(false);

  SECTION("Register and call method") {
    dispatcher.RegisterMethodCall(
        "sum",
        [](const std::optional<nlohmann::json>& params) -> nlohmann::json {
          int result = 0;
          if (params && params->is_array()) {
            for (const auto& value : *params) {
              result += value.get<int>();
            }
          }
          return result;
        });

    auto response = dispatcher.DispatchRequest(
        R"({"jsonrpc":"2.0","method":"sum","params":[1,2,3],"id":1})");
    REQUIRE(response.has_value());
    auto json = nlohmann::json::parse(*response);
    REQUIRE(json["result"] == 6);
  }

  SECTION("Register and call notification") {
    bool notification_called = false;
    dispatcher.RegisterNotification(
        "update",
        [&notification_called](const std::optional<nlohmann::json>& params) {
          notification_called = true;
          REQUIRE(params.has_value());
          REQUIRE((*params)["value"] == 42);
        });

    auto response = dispatcher.DispatchRequest(
        R"({"jsonrpc":"2.0","method":"update","params":{"value":42}})");
    REQUIRE_FALSE(response.has_value());
    REQUIRE(notification_called);
  }
}

TEST_CASE("Batch request handling", "[Dispatcher]") {
  Dispatcher dispatcher(false);

  SECTION("Valid batch request") {
    dispatcher.RegisterMethodCall(
        "sum",
        [](const std::optional<nlohmann::json>& params) -> nlohmann::json {
          return params->at(0).get<int>() + params->at(1).get<int>();
        });

    dispatcher.RegisterNotification(
        "notify", [](const std::optional<nlohmann::json>&) {
          // Do nothing
        });

    std::string batch_request = R"([
            {"jsonrpc":"2.0","method":"sum","params":[1,2],"id":"1"},
            {"jsonrpc":"2.0","method":"notify","params":[7]},
            {"jsonrpc":"2.0","method":"sum","params":[3,4],"id":"2"}
        ])";

    auto response = dispatcher.DispatchRequest(batch_request);
    REQUIRE(response.has_value());
    auto json = nlohmann::json::parse(*response);
    REQUIRE(json.is_array());
    REQUIRE(json.size() == 2);  // Notification doesn't produce response
    REQUIRE(json[0]["result"] == 3);
    REQUIRE(json[1]["result"] == 7);
  }

  SECTION("Empty batch") {
    auto response = dispatcher.DispatchRequest("[]");
    REQUIRE(response.has_value());
    auto json = nlohmann::json::parse(*response);
    REQUIRE(
        json["error"]["code"] == static_cast<int>(ErrorCode::kInvalidRequest));
  }

  SECTION("Invalid batch request") {
    auto response = dispatcher.DispatchRequest("[1]");
    REQUIRE(response.has_value());
    auto json = nlohmann::json::parse(*response);
    REQUIRE(json.is_array());
    REQUIRE(
        json[0]["error"]["code"] ==
        static_cast<int>(ErrorCode::kInvalidRequest));
  }
}

TEST_CASE("Error handling", "[Dispatcher]") {
  Dispatcher dispatcher(false);

  SECTION("Method not found") {
    auto response = dispatcher.DispatchRequest(
        R"({"jsonrpc":"2.0","method":"unknown","id":1})");
    REQUIRE(response.has_value());
    auto json = nlohmann::json::parse(*response);
    REQUIRE(
        json["error"]["code"] == static_cast<int>(ErrorCode::kMethodNotFound));
  }

  SECTION("Invalid request") {
    auto response = dispatcher.DispatchRequest(
        R"({"method":"test"})");  // Missing jsonrpc version
    REQUIRE(response.has_value());
    auto json = nlohmann::json::parse(*response);
    REQUIRE(
        json["error"]["code"] == static_cast<int>(ErrorCode::kInvalidRequest));
  }

  SECTION("Parse error") {
    auto response = dispatcher.DispatchRequest("invalid json");
    REQUIRE(response.has_value());
    auto json = nlohmann::json::parse(*response);
    REQUIRE(json["error"]["code"] == static_cast<int>(ErrorCode::kParseError));
  }

  SECTION("Internal error") {
    dispatcher.RegisterMethodCall(
        "fail", [](const std::optional<nlohmann::json>&) -> nlohmann::json {
          throw std::runtime_error("Intentional failure");
        });

    auto response = dispatcher.DispatchRequest(
        R"({"jsonrpc":"2.0","method":"fail","id":1})");
    REQUIRE(response.has_value());
    auto json = nlohmann::json::parse(*response);
    REQUIRE(
        json["error"]["code"] == static_cast<int>(ErrorCode::kInternalError));
  }
}

TEST_CASE("Thread safety in multi-threaded mode", "[Dispatcher]") {
  Dispatcher dispatcher(true, 4);
  std::atomic<int> sum{0};

  dispatcher.RegisterMethodCall(
      "increment",
      [&sum](const std::optional<nlohmann::json>& params) -> nlohmann::json {
        int value = params->at(0).get<int>();
        sum += value;
        return sum.load();
      });

  std::vector<std::string> requests;
  for (int i = 0; i < 10; ++i) {
    requests.push_back(fmt::format(
        R"({{"jsonrpc":"2.0","method":"increment","params":[{}],"id":{}}})",
        i + 1, i));
  }

  std::string batch_request =
      "[" +
      std::accumulate(
          std::next(requests.begin()), requests.end(), requests[0],
          [](const std::string& a, const std::string& b) {
            return a + "," + b;
          }) +
      "]";

  auto response = dispatcher.DispatchRequest(batch_request);
  REQUIRE(response.has_value());
  auto json = nlohmann::json::parse(*response);
  REQUIRE(json.is_array());
  REQUIRE(json.size() == 10);

  // Sum should be 55 (1+2+3+...+10)
  REQUIRE(sum == 55);
}
