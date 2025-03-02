#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../common/mock_transport.hpp"
#include "jsonrpc/endpoint/endpoint.hpp"
#include "jsonrpc/endpoint/id_generator.hpp"

using Json = nlohmann::json;

// Request Tests
TEST_CASE("Endpoint Request handling", "[Endpoint][Request]") {
  auto transport = std::make_unique<MockTransport>();
  auto* transport_ptr = transport.get();

  // Create a separate ID generator to predict IDs
  jsonrpc::endpoint::IncrementalIdGenerator test_id_gen;

  // ID generator should be internal to endpoint
  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

  SECTION("Method call with parameters") {
    endpoint.Start();

    // We know the endpoint's internal ID generator will generate the same
    // sequence
    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = {{"value", "test"}};
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetResponse(response);

    // Make the call
    Json params = {{"param1", "value1"}, {"param2", 42}};
    auto response_received = endpoint.SendMethodCall("test_method", params);

    // Verify the sent request format
    REQUIRE(!transport_ptr->sent_requests.empty());
    auto sent_request = Json::parse(transport_ptr->sent_requests.back());
    REQUIRE(sent_request["jsonrpc"] == "2.0");
    REQUIRE(sent_request["method"] == "test_method");
    REQUIRE(sent_request["params"] == params);
    REQUIRE(sent_request["id"] == std::get<int64_t>(predicted_id));

    // Verify response matches exactly
    REQUIRE(response_received == response);

    REQUIRE_FALSE(endpoint.HasPendingRequests());
    endpoint.Stop();
  }

  SECTION("Method call without parameters") {
    endpoint.Start();

    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = "success";
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetResponse(response);

    auto response_received = endpoint.SendMethodCall("test_method");

    // Verify request and response
    auto sent_request = Json::parse(transport_ptr->sent_requests.back());
    REQUIRE(sent_request["id"] == std::get<int64_t>(predicted_id));
    REQUIRE(response_received == response);

    REQUIRE_FALSE(endpoint.HasPendingRequests());
    endpoint.Stop();
  }

  SECTION("Notification") {
    endpoint.Start();

    Json params = {{"event", "update"}, {"value", 100}};
    endpoint.SendNotification("test_notification", params);

    // Verify notification was sent through transport
    REQUIRE(!transport_ptr->sent_requests.empty());

    // Parse and verify notification format
    auto sent_notification = Json::parse(transport_ptr->sent_requests.back());
    REQUIRE(sent_notification["jsonrpc"] == "2.0");
    REQUIRE(sent_notification["method"] == "test_notification");
    REQUIRE(sent_notification["params"] == params);
    REQUIRE_FALSE(
        sent_notification.contains("id"));  // Notifications must not have an ID

    endpoint.Stop();
  }
}

// Response Tests
TEST_CASE("Endpoint Response handling", "[Endpoint][Response]") {
  auto transport = std::make_unique<MockTransport>();
  auto* transport_ptr = transport.get();

  // Create a separate ID generator to predict IDs
  jsonrpc::endpoint::IncrementalIdGenerator test_id_gen;

  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

  SECTION("Success response") {
    endpoint.Start();

    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = "success";
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetResponse(response);

    auto response_received = endpoint.SendMethodCall("test_method");

    REQUIRE(response_received["result"] == "success");
    REQUIRE(response_received["id"] == std::get<int64_t>(predicted_id));

    endpoint.Stop();
  }

  SECTION("Error response") {
    endpoint.Start();

    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["error"] = {{"code", -32601}, {"message", "Method not found"}};
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetResponse(response);

    auto response_received = endpoint.SendMethodCall("invalid_method");

    REQUIRE(response_received.contains("error"));
    REQUIRE(response_received["error"]["code"] == -32601);
    REQUIRE(response_received["error"]["message"] == "Method not found");
    REQUIRE(response_received["id"] == std::get<int64_t>(predicted_id));

    endpoint.Stop();
  }
}

// Configuration Tests
TEST_CASE("Endpoint configuration", "[Endpoint][Config]") {
  auto transport = std::make_unique<MockTransport>();
  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

  SECTION("Request timeout configuration") {
    const auto new_timeout = std::chrono::milliseconds(5000);
    endpoint.SetRequestTimeout(new_timeout);
    REQUIRE(endpoint.GetRequestTimeout() == new_timeout);
  }

  SECTION("Max batch size configuration") {
    const size_t new_batch_size = 50;
    endpoint.SetMaxBatchSize(new_batch_size);
    REQUIRE(endpoint.GetMaxBatchSize() == new_batch_size);
  }
}

// Basic Lifecycle Tests
TEST_CASE("Endpoint lifecycle", "[Endpoint][Lifecycle]") {
  auto transport = std::make_unique<MockTransport>();
  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

  SECTION("Basic start and stop") {
    REQUIRE_FALSE(endpoint.IsRunning());
    endpoint.Start();
    REQUIRE(endpoint.IsRunning());
    endpoint.Stop();
    REQUIRE_FALSE(endpoint.IsRunning());
  }

  SECTION("Start-stop with pending notification") {
    endpoint.Start();
    REQUIRE(endpoint.IsRunning());

    // Send notification which should not block
    Json params = {{"event", "shutdown"}, {"reason", "test"}};
    endpoint.SendNotification("test_notification", params);

    // Should be able to stop immediately
    endpoint.Stop();
    REQUIRE_FALSE(endpoint.IsRunning());
  }

  SECTION("Multiple start-stop cycles") {
    for (int i = 0; i < 3; ++i) {
      REQUIRE_FALSE(endpoint.IsRunning());
      endpoint.Start();
      REQUIRE(endpoint.IsRunning());
      endpoint.Stop();
      REQUIRE_FALSE(endpoint.IsRunning());
    }
  }
}

// Thread Safety Tests
TEST_CASE("Endpoint thread safety", "[Endpoint][ThreadSafety]") {
  auto transport = std::make_unique<MockTransport>();
  auto* transport_ptr = transport.get();
  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

  // Create a separate ID generator to predict IDs
  jsonrpc::endpoint::IncrementalIdGenerator test_id_gen;

  SECTION("Async method call with response") {
    endpoint.Start();

    // We know the endpoint's internal ID generator will generate the same
    // sequence
    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = "async_success";
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetResponse(response);

    // Make async call and verify we can get response
    auto future = endpoint.SendMethodCallAsync("async_method");
    REQUIRE(
        future.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

    auto result = future.get();
    REQUIRE(result["result"] == "async_success");
    REQUIRE(result["id"] == std::get<int64_t>(predicted_id));

    endpoint.Stop();
  }

  SECTION("Multiple pending requests") {
    endpoint.Start();

    // Test that endpoint can handle multiple pending requests
    const int num_requests = 5;
    std::vector<std::future<nlohmann::json>> futures;
    std::vector<int64_t> expected_ids;

    spdlog::info(
        "Starting multiple requests test with {} requests", num_requests);
    // Send requests first
    for (int i = 0; i < num_requests; i++) {
      auto predicted_id = test_id_gen.NextId();
      expected_ids.push_back(std::get<int64_t>(predicted_id));
      futures.push_back(
          endpoint.SendMethodCallAsync(fmt::format("method_{}", i)));
    }

    REQUIRE(endpoint.HasPendingRequests());
    spdlog::info("All requests sent, setting up responses");

    // Then set up responses with predicted IDs
    for (int i = 0; i < num_requests; i++) {
      nlohmann::json response;
      response["jsonrpc"] = "2.0";
      response["result"] = fmt::format("result_{}", i);
      response["id"] = expected_ids[i];
      transport_ptr->SetResponse(response);
    }

    // Verify all requests complete with correct IDs
    for (size_t i = 0; i < futures.size(); i++) {
      REQUIRE(
          futures[i].wait_for(std::chrono::seconds(1)) ==
          std::future_status::ready);
      auto result = futures[i].get();
      REQUIRE(result["id"] == expected_ids[i]);
      REQUIRE(result["result"] == fmt::format("result_{}", i));
    }
    spdlog::info("All requests completed successfully");

    REQUIRE_FALSE(endpoint.HasPendingRequests());
    endpoint.Stop();
  }
}
