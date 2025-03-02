#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/response.hpp"
#include "jsonrpc/endpoint/types.hpp"

using jsonrpc::endpoint::ErrorCode;
using jsonrpc::endpoint::RequestId;
using jsonrpc::endpoint::Response;

TEST_CASE("Response creation and basic properties", "[Response]") {
  SECTION("Create success response with result") {
    nlohmann::json result = {{"data", "test_value"}};
    RequestId id = 1;
    auto response = Response::CreateResult(result, id);

    REQUIRE(response.IsSuccess());
    REQUIRE(response.GetResult() == result);
    REQUIRE(response.GetId() == id);
  }

  SECTION("Create error response") {
    RequestId id = "req1";
    auto response = Response::CreateLibError(ErrorCode::kMethodNotFound, id);

    REQUIRE_FALSE(response.IsSuccess());
    REQUIRE(
        response.GetError()["code"] ==
        static_cast<int>(ErrorCode::kMethodNotFound));
    REQUIRE(response.GetId() == id);
  }

  SECTION("Create response without id") {
    nlohmann::json result = "test";
    auto response = Response::CreateResult(result, std::nullopt);

    REQUIRE(response.IsSuccess());
    REQUIRE(response.GetResult() == result);
    REQUIRE_FALSE(response.GetId().has_value());
  }

  SECTION("Create success response") {
    nlohmann::json result = {{"key", "value"}};
    auto response = Response::CreateResult(result, std::nullopt);

    REQUIRE(response.IsSuccess());
    REQUIRE(response.GetResult() == result);
    REQUIRE_FALSE(response.GetId().has_value());
  }

  SECTION("Create success response with ID") {
    nlohmann::json result = {{"key", "value"}};
    RequestId id = "req1";
    auto response = Response::CreateResult(result, id);

    REQUIRE(response.IsSuccess());
    REQUIRE(response.GetResult() == result);
    auto response_id = response.GetId();
    REQUIRE(response_id.has_value());
    REQUIRE(std::get<std::string>(*response_id) == "req1");
  }

  SECTION("Create error response") {
    auto response = Response::CreateLibError(ErrorCode::kMethodNotFound);

    REQUIRE_FALSE(response.IsSuccess());
    REQUIRE(response.GetError()["code"] == -32601);
    REQUIRE(response.GetError()["message"] == "Method not found");
    REQUIRE_FALSE(response.GetId().has_value());
  }
}

TEST_CASE("Response JSON serialization", "[Response]") {
  SECTION("Serialize success response") {
    nlohmann::json result = {{"key", "value"}};
    RequestId id = 1;
    auto response = Response::CreateResult(result, id);
    auto json = response.ToJson();

    REQUIRE(json["jsonrpc"] == "2.0");
    REQUIRE(json["result"] == result);
    REQUIRE(json["id"] == 1);
    REQUIRE_FALSE(json.contains("error"));
  }

  SECTION("Serialize error response") {
    RequestId id = "req1";
    auto response = Response::CreateLibError(ErrorCode::kInvalidRequest, id);
    auto json = response.ToJson();

    REQUIRE(json["jsonrpc"] == "2.0");
    REQUIRE(
        json["error"]["code"] == static_cast<int>(ErrorCode::kInvalidRequest));
    REQUIRE_FALSE(json.contains("result"));
    REQUIRE(json["id"] == "req1");
  }

  SECTION("Serialize response without id") {
    auto response = Response::CreateLibError(ErrorCode::kParseError);
    auto json = response.ToJson();

    REQUIRE(json["jsonrpc"] == "2.0");
    REQUIRE(json["error"]["code"] == static_cast<int>(ErrorCode::kParseError));
    REQUIRE(json["id"] == nullptr);
  }
}

TEST_CASE("Response deserialization", "[Response]") {
  SECTION("Deserialize success response") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"}, {"result", {{"key", "value"}}}, {"id", 1}};

    auto response = Response::FromJson(json);
    REQUIRE(response.IsSuccess());
    REQUIRE(response.GetResult()["key"] == "value");
    auto id = response.GetId();
    REQUIRE(id.has_value());
    REQUIRE(std::get<int64_t>(*id) == 1);
  }

  SECTION("Deserialize error response") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"},
        {"error", {{"code", -32601}, {"message", "Method not found"}}},
        {"id", "req1"}};

    auto response = Response::FromJson(json);
    REQUIRE_FALSE(response.IsSuccess());
    REQUIRE(response.GetError()["code"] == -32601);
    REQUIRE(response.GetError()["message"] == "Method not found");
    auto id = response.GetId();
    REQUIRE(id.has_value());
    REQUIRE(std::get<std::string>(*id) == "req1");
  }
}

TEST_CASE("Response validation", "[Response]") {
  SECTION("Invalid JSON-RPC version") {
    nlohmann::json json = {{"jsonrpc", "1.0"}, {"result", "test"}, {"id", 1}};
    REQUIRE_THROWS_AS(Response::FromJson(json), std::invalid_argument);
  }

  SECTION("Missing both result and error") {
    nlohmann::json json = {{"jsonrpc", "2.0"}, {"id", 1}};
    REQUIRE_THROWS_AS(Response::FromJson(json), std::invalid_argument);
  }

  SECTION("Both result and error present") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"},
        {"result", "test"},
        {"error", {{"code", -32601}, {"message", "Method not found"}}},
        {"id", 1}};
    REQUIRE_THROWS_AS(Response::FromJson(json), std::invalid_argument);
  }

  SECTION("Invalid error object") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"},
        {"error", {{"message", "Method not found"}}},  // Missing code
        {"id", 1}};
    REQUIRE_THROWS_AS(Response::FromJson(json), std::invalid_argument);
  }
}
