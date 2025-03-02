#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/request.hpp"

using jsonrpc::endpoint::Request;
using jsonrpc::endpoint::RequestId;

TEST_CASE("Request construction and basic properties", "[Request]") {
  SECTION("Create request with all parameters") {
    RequestId id = 1;
    auto request =
        Request("test_method", nlohmann::json{{"param", "value"}}, id);

    REQUIRE(request.GetMethod() == "test_method");
    REQUIRE(request.GetParams().has_value());
    REQUIRE(request.GetParams()->contains("param"));
    REQUIRE(request.GetId() == id);
    REQUIRE_FALSE(request.IsNotification());
  }

  SECTION("Create notification (request without id)") {
    auto request = Request("test_method", nlohmann::json{{"param", "value"}});

    REQUIRE(request.GetMethod() == "test_method");
    REQUIRE(request.GetParams().has_value());
    REQUIRE(request.IsNotification());
  }

  SECTION("Create request without params") {
    RequestId id = "123";
    auto request = Request("test_method", std::nullopt, id);

    REQUIRE(request.GetMethod() == "test_method");
    REQUIRE_FALSE(request.GetParams().has_value());
    REQUIRE(request.GetId() == id);
    REQUIRE_FALSE(request.IsNotification());
  }
}

TEST_CASE("Request JSON serialization", "[Request]") {
  SECTION("Serialize request with numeric id") {
    RequestId id = 1;
    auto request =
        Request("test_method", nlohmann::json{{"param", "value"}}, id);
    auto json = request.ToJson();

    REQUIRE(json["jsonrpc"] == "2.0");
    REQUIRE(json["method"] == "test_method");
    REQUIRE(json["params"]["param"] == "value");
    REQUIRE(json["id"] == 1);
  }

  SECTION("Serialize request with string id") {
    RequestId id = "req1";
    auto request =
        Request("test_method", nlohmann::json{{"param", "value"}}, id);
    auto json = request.ToJson();

    REQUIRE(json["jsonrpc"] == "2.0");
    REQUIRE(json["method"] == "test_method");
    REQUIRE(json["params"]["param"] == "value");
    REQUIRE(json["id"] == "req1");
  }

  SECTION("Serialize notification") {
    auto request = Request("test_method", nlohmann::json{{"param", "value"}});
    auto json = request.ToJson();

    REQUIRE(json["jsonrpc"] == "2.0");
    REQUIRE(json["method"] == "test_method");
    REQUIRE(json["params"]["param"] == "value");
    REQUIRE_FALSE(json.contains("id"));
  }
}

TEST_CASE("Request JSON deserialization", "[Request]") {
  SECTION("Deserialize valid request") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"},
        {"method", "test_method"},
        {"params", {{"param", "value"}}},
        {"id", 1}};

    auto request = Request::FromJson(json);
    REQUIRE(request.GetMethod() == "test_method");
    REQUIRE(request.GetParams().has_value());
    REQUIRE(request.GetParams()->contains("param"));
    REQUIRE(std::get<int64_t>(request.GetId()) == 1);
  }

  SECTION("Deserialize valid notification") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"},
        {"method", "test_method"},
        {"params", {{"param", "value"}}}};

    auto request = Request::FromJson(json);
    REQUIRE(request.GetMethod() == "test_method");
    REQUIRE(request.GetParams().has_value());
    REQUIRE(request.IsNotification());
  }

  SECTION("Deserialize request with array params") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"},
        {"method", "test_method"},
        {"params", {1, 2, 3}},
        {"id", "req1"}};

    auto request = Request::FromJson(json);
    REQUIRE(request.GetMethod() == "test_method");
    REQUIRE(request.GetParams().has_value());
    REQUIRE(request.GetParams()->is_array());
    REQUIRE(std::get<std::string>(request.GetId()) == "req1");
  }
}

TEST_CASE("Request validation", "[Request]") {
  SECTION("Invalid JSON-RPC version") {
    nlohmann::json json = {
        {"jsonrpc", "1.0"}, {"method", "test_method"}, {"id", 1}};
    REQUIRE_THROWS_AS(Request::FromJson(json), std::invalid_argument);
  }

  SECTION("Missing method") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"}, {"params", {{"param", "value"}}}, {"id", 1}};
    REQUIRE_THROWS_AS(Request::FromJson(json), std::invalid_argument);
  }

  SECTION("Invalid method type") {
    nlohmann::json json = {{"jsonrpc", "2.0"}, {"method", 123}, {"id", 1}};
    REQUIRE_THROWS_AS(Request::FromJson(json), std::invalid_argument);
  }

  SECTION("Invalid params type") {
    nlohmann::json json = {
        {"jsonrpc", "2.0"},
        {"method", "test_method"},
        {"params", "invalid"},
        {"id", 1}};
    REQUIRE_THROWS_AS(Request::FromJson(json), std::invalid_argument);
  }
}
