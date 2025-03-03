#include <memory>
#include <thread>

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

  SECTION("Method call with parameters") {
    auto endpoint =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // We know the endpoint's internal ID generator will generate the same
    // sequence
    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = {{"value", "test"}};
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetMessage(response.dump());

    // Make the call
    Json params = {{"param1", "value1"}, {"param2", 42}};
    auto response_received = endpoint->SendMethodCall("test_method", params);

    // Verify the sent request format
    REQUIRE(!transport_ptr->GetSentRequests().empty());
    auto sent_request = Json::parse(transport_ptr->GetSentRequests().back());
    REQUIRE(sent_request["jsonrpc"] == "2.0");
    REQUIRE(sent_request["method"] == "test_method");
    REQUIRE(sent_request["params"] == params);
    REQUIRE(sent_request["id"] == std::get<int64_t>(predicted_id));

    // Verify response matches exactly
    REQUIRE(response_received == response);
    REQUIRE_FALSE(endpoint->HasPendingRequests());

    // Clean shutdown
    endpoint->Shutdown();
  }

  SECTION("Method call without parameters") {
    auto endpoint =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // We know the endpoint's internal ID generator will generate the same
    // sequence
    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = "success";
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetMessage(response.dump());

    auto response_received = endpoint->SendMethodCall("test_method");

    REQUIRE(response_received["result"] == "success");
    REQUIRE(response_received["id"] == std::get<int64_t>(predicted_id));

    // Clean shutdown
    endpoint->Shutdown();
  }

  SECTION("Notification") {
    auto endpoint =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    Json params = {{"event", "update"}, {"value", 100}};
    endpoint->SendNotification("test_notification", params);

    // Verify notification was sent through transport
    REQUIRE(!transport_ptr->GetSentRequests().empty());

    // Parse and verify notification format
    auto sent_notification =
        Json::parse(transport_ptr->GetSentRequests().back());
    REQUIRE(sent_notification["jsonrpc"] == "2.0");
    REQUIRE(sent_notification["method"] == "test_notification");
    REQUIRE(sent_notification["params"] == params);
    REQUIRE_FALSE(
        sent_notification.contains("id"));  // Notifications must not have an ID

    // Clean shutdown
    endpoint->Shutdown();
  }
}

// Response Tests
TEST_CASE("Endpoint Response handling", "[Endpoint][Response]") {
  auto transport = std::make_unique<MockTransport>();
  auto* transport_ptr = transport.get();

  // Create a separate ID generator to predict IDs
  jsonrpc::endpoint::IncrementalIdGenerator test_id_gen;

  SECTION("Success response") {
    auto endpoint =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // We know the endpoint's internal ID generator will generate the same
    // sequence
    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = "success";
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetMessage(response.dump());

    auto response_received = endpoint->SendMethodCall("test_method");

    REQUIRE(response_received["result"] == "success");
    REQUIRE(response_received["id"] == std::get<int64_t>(predicted_id));

    endpoint->Shutdown();
  }

  SECTION("Error response") {
    auto endpoint =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // We know the endpoint's internal ID generator will generate the same
    // sequence
    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["error"] = {{"code", -32601}, {"message", "Method not found"}};
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetMessage(response.dump());

    auto response_received = endpoint->SendMethodCall("invalid_method");

    REQUIRE(response_received.contains("error"));
    REQUIRE(response_received["error"]["code"] == -32601);
    REQUIRE(response_received["error"]["message"] == "Method not found");
    REQUIRE(response_received["id"] == std::get<int64_t>(predicted_id));

    endpoint->Shutdown();
  }
}

// Configuration Tests
TEST_CASE("Endpoint configuration", "[Endpoint][Config]") {
  auto transport = std::make_unique<MockTransport>();
  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

  // No configuration tests needed since the methods were removed
}

// Basic Lifecycle Tests
TEST_CASE("Endpoint lifecycle", "[Endpoint][Lifecycle]") {
  SECTION("Server lifecycle - Start/Wait pattern") {
    auto transport = std::make_unique<MockTransport>();
    auto* transport_ptr = transport.get();
    jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

    REQUIRE_FALSE(endpoint.IsRunning());

    // Start should be non-blocking
    endpoint.Start();
    REQUIRE(endpoint.IsRunning());

    // Send a test message to verify processing
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "test";
    transport_ptr->SetMessage(notification.dump());

    // Shutdown in separate thread since Wait() is blocking
    std::thread shutdown_thread([&endpoint]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      endpoint.Shutdown();
    });

    // Wait blocks until Shutdown completes
    endpoint.Wait();
    shutdown_thread.join();
    REQUIRE_FALSE(endpoint.IsRunning());
  }

  SECTION("Server lifecycle - Background thread pattern") {
    auto transport = std::make_unique<MockTransport>();
    auto* transport_ptr = transport.get();
    jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

    REQUIRE_FALSE(endpoint.IsRunning());

    // Start in background thread
    std::thread server_thread([&endpoint]() {
      endpoint.Start();
      endpoint.Wait();
    });

    // Give it time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(endpoint.IsRunning());

    // Test message processing
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "test";
    transport_ptr->SetMessage(notification.dump());

    // Shutdown is blocking
    endpoint.Shutdown();
    server_thread.join();
    REQUIRE_FALSE(endpoint.IsRunning());
  }

  SECTION("Double Start prevention") {
    auto transport = std::make_unique<MockTransport>();
    jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

    endpoint.Start();
    REQUIRE_THROWS_AS(endpoint.Start(), std::runtime_error);

    endpoint.Shutdown();
    REQUIRE_FALSE(endpoint.IsRunning());
  }

  SECTION("Multiple Shutdown calls are safe") {
    auto transport = std::make_unique<MockTransport>();
    jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

    endpoint.Start();
    endpoint.Shutdown();
    endpoint.Shutdown();  // Second call should be safe
    REQUIRE_FALSE(endpoint.IsRunning());
  }

  SECTION("Basic client lifecycle") {
    auto transport = std::make_unique<MockTransport>();
    auto endpoint =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));
    REQUIRE(endpoint->IsRunning());
    endpoint->Shutdown();
    REQUIRE_FALSE(endpoint->IsRunning());
  }
}

// Thread Safety Tests
TEST_CASE("Endpoint thread safety", "[Endpoint][ThreadSafety]") {
  auto transport = std::make_unique<MockTransport>();
  auto* transport_ptr = transport.get();

  // Create a separate ID generator to predict IDs
  jsonrpc::endpoint::IncrementalIdGenerator test_id_gen;

  SECTION("Async method call with response") {
    auto endpoint =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // We know the endpoint's internal ID generator will generate the same
    // sequence
    auto predicted_id = test_id_gen.NextId();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = "async_success";
    response["id"] = std::get<int64_t>(predicted_id);
    transport_ptr->SetMessage(response.dump());

    // Make async call and verify we can get response
    auto future = endpoint->SendMethodCallAsync("async_method");
    REQUIRE(
        future.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

    auto result = future.get();
    REQUIRE(result["result"] == "async_success");
    REQUIRE(result["id"] == std::get<int64_t>(predicted_id));
  }

  SECTION("Multiple pending requests") {
    auto endpoint =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // Test that endpoint can handle multiple pending requests
    const int num_requests = 5;
    std::vector<std::future<nlohmann::json>> futures;
    std::vector<int64_t> expected_ids;

    spdlog::info(
        "Starting multiple requests test with {} requests", num_requests);

    // Set up all responses first
    for (int i = 0; i < num_requests; i++) {
      auto predicted_id = test_id_gen.NextId();
      expected_ids.push_back(std::get<int64_t>(predicted_id));

      nlohmann::json response;
      response["jsonrpc"] = "2.0";
      response["result"] = fmt::format("result_{}", i);
      response["id"] = std::get<int64_t>(predicted_id);
      transport_ptr->SetMessage(response.dump());
    }

    // Now send all requests
    for (int i = 0; i < num_requests; i++) {
      futures.push_back(
          endpoint->SendMethodCallAsync(fmt::format("method_{}", i)));
    }

    // Verify all requests complete with correct IDs
    for (size_t i = 0; i < futures.size(); i++) {
      REQUIRE(
          futures[i].wait_for(std::chrono::milliseconds(100)) ==
          std::future_status::ready);
      auto result = futures[i].get();
      REQUIRE(result["id"] == expected_ids[i]);
      REQUIRE(result["result"] == fmt::format("result_{}", i));
    }
    spdlog::info("All requests completed successfully");

    REQUIRE_FALSE(endpoint->HasPendingRequests());
  }
}

// Server-Side Tests
TEST_CASE("Endpoint server functionality", "[Endpoint][Server]") {
  SECTION("Method call handling") {
    auto transport = std::make_unique<MockTransport>();
    auto* transport_ptr = transport.get();
    jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

    // Define expected parameters and result
    nlohmann::json expected_params = {{"a", 10}, {"b", 5}};
    nlohmann::json expected_result = 15.0;

    int call_count = 0;

    // Register a method handler
    endpoint.RegisterMethodCall(
        "add", [&call_count,
                expected_params](const std::optional<nlohmann::json>& params) {
          call_count++;
          REQUIRE(params.has_value());
          REQUIRE(params.value() == expected_params);
          return 15.0;
        });

    // Start the endpoint
    endpoint.Start();

    // Simulate receiving a request
    nlohmann::json request;
    request["jsonrpc"] = "2.0";
    request["method"] = "add";
    request["params"] = expected_params;
    request["id"] = 0;

    transport_ptr->SetMessage(request.dump());

    // Give some time for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify the method was called and response was sent
    REQUIRE(call_count > 0);
    REQUIRE(!transport_ptr->GetSentRequests().empty());

    if (!transport_ptr->GetSentRequests().empty()) {
      auto sent_response =
          nlohmann::json::parse(transport_ptr->GetSentRequests().back());
      REQUIRE(sent_response["jsonrpc"] == "2.0");
      REQUIRE(sent_response["result"] == expected_result);
      REQUIRE(sent_response["id"] == 0);
    }

    endpoint.Shutdown();
  }

  SECTION("Notification handling") {
    // Test notification handling directly with the dispatcher
    jsonrpc::endpoint::Dispatcher dispatcher;

    int notification_count = 0;
    nlohmann::json expected_params = {{"event", "update"}};

    dispatcher.RegisterNotification(
        "update", [&notification_count, expected_params](
                      const std::optional<nlohmann::json>& params) {
          notification_count++;
          REQUIRE(params.has_value());
          REQUIRE(params.value() == expected_params);
        });

    // Create a notification message
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "update";
    notification["params"] = expected_params;

    // Dispatch the notification directly
    auto response = dispatcher.DispatchRequest(notification.dump());

    // Verify the notification was handled
    REQUIRE(notification_count > 0);

    // No response should be sent for notifications
    REQUIRE(!response.has_value());
  }

  SECTION("Method not found") {
    auto transport = std::make_unique<MockTransport>();
    auto* transport_ptr = transport.get();
    jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

    // Start the endpoint
    endpoint.Start();

    // Simulate receiving a request for a non-existent method
    nlohmann::json request;
    request["jsonrpc"] = "2.0";
    request["method"] = "unknown_method";
    request["id"] = 1;

    transport_ptr->SetMessage(request.dump());

    // Give some time for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify an error response was sent
    REQUIRE(!transport_ptr->GetSentRequests().empty());

    if (!transport_ptr->GetSentRequests().empty()) {
      auto sent_response =
          nlohmann::json::parse(transport_ptr->GetSentRequests().back());
      REQUIRE(sent_response["jsonrpc"] == "2.0");
      REQUIRE(sent_response["error"]["code"] == -32601);  // Method not found
      REQUIRE(sent_response["id"] == 1);
    }

    endpoint.Shutdown();
  }
}

TEST_CASE("Endpoint client functionality", "[Endpoint][Client]") {
  SECTION("Client lifecycle") {
    auto transport = std::make_unique<MockTransport>();
    auto* transport_ptr = transport.get();

    // Create a response for the client to receive
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = "success";
    response["id"] = 1;

    transport_ptr->SetMessage(response.dump());

    // Create a client endpoint
    auto client =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // Verify the client is running
    REQUIRE(client->IsRunning());

    // Clean shutdown
    client->Shutdown();
    REQUIRE(!client->IsRunning());
  }

  SECTION("Client notification") {
    auto transport = std::make_unique<MockTransport>();
    auto* transport_ptr = transport.get();

    // Create a client endpoint
    auto client =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // Send a notification
    client->SendNotification("test", nlohmann::json::object());

    // Verify the notification was sent
    REQUIRE(!transport_ptr->GetSentRequests().empty());
    auto sent_notification =
        nlohmann::json::parse(transport_ptr->GetSentRequests().back());
    REQUIRE(sent_notification["jsonrpc"] == "2.0");
    REQUIRE(sent_notification["method"] == "test");
    REQUIRE(!sent_notification.contains("id"));

    // Clean shutdown
    client->Shutdown();
  }

  SECTION("Client request with response") {
    auto transport = std::make_unique<MockTransport>();
    auto* transport_ptr = transport.get();

    // Create a client endpoint
    auto client =
        jsonrpc::endpoint::RpcEndpoint::CreateClient(std::move(transport));

    // Send a request
    auto future = std::async(std::launch::async, [&client]() {
      return client->SendMethodCall("test", nlohmann::json::object());
    });

    // Wait a bit for the request to be sent
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify the request was sent
    REQUIRE(!transport_ptr->GetSentRequests().empty());
    auto sent_request =
        nlohmann::json::parse(transport_ptr->GetSentRequests().back());
    REQUIRE(sent_request["jsonrpc"] == "2.0");
    REQUIRE(sent_request["method"] == "test");
    REQUIRE(sent_request.contains("id"));

    // Create a response
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = "success";
    response["id"] = sent_request["id"];

    // Send the response
    transport_ptr->SetMessage(response.dump());

    // Wait for the response
    auto result = future.get();

    // Verify the response
    REQUIRE(result["result"] == "success");

    // Clean shutdown
    client->Shutdown();
  }
}
