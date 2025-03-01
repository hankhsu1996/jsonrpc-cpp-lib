#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "../common/mock_transport.hpp"
#include "jsonrpc/client/client.hpp"

using Json = nlohmann::json;

TEST_CASE("Client starts and stops correctly", "[Client]") {
  auto transport = std::make_unique<MockTransport>();
  jsonrpc::client::Client client(std::move(transport));

  client.Start();
  REQUIRE(client.IsRunning() == true);
  REQUIRE(client.HasPendingRequests() == false);

  client.Stop();
  REQUIRE(client.IsRunning() == false);
}

TEST_CASE("Client handles responses correctly", "[Client]") {
  auto transport = std::make_unique<MockTransport>();
  transport->SetResponse(R"({"jsonrpc":"2.0","result":"success","id":0})");

  jsonrpc::client::Client client(std::move(transport));
  client.Start();

  REQUIRE(client.HasPendingRequests() == false);
  auto response = client.SendMethodCall("test_method");
  REQUIRE(client.HasPendingRequests() == false);

  REQUIRE(response["result"] == "success");

  client.Stop();
}

TEST_CASE(
    "Client sends notification without expecting a response", "[Client]") {
  auto transport = std::make_unique<MockTransport>();
  MockTransport* transport_ptr = transport.get();

  jsonrpc::client::Client client(std::move(transport));
  client.Start();

  client.SendNotification(
      "notify_event", nlohmann::json({{"param1", "value1"}}));

  REQUIRE(client.HasPendingRequests() == false);
  REQUIRE(transport_ptr->sent_requests.size() == 1);
  REQUIRE(
      transport_ptr->sent_requests[0].find("notify_event") !=
      std::string::npos);

  client.Stop();
}

TEST_CASE("Client handles valid JSON-RPC response", "[Client]") {
  auto transport = std::make_unique<MockTransport>();
  MockTransport* transport_ptr = transport.get();

  transport_ptr->SetResponse(
      R"({"jsonrpc":"2.0","result":{"data":"success"},"id":0})");

  jsonrpc::client::Client client(std::move(transport));
  client.Start();

  REQUIRE(client.HasPendingRequests() == false);
  auto response = client.SendMethodCall("getData");
  REQUIRE(client.HasPendingRequests() == false);

  REQUIRE(response.contains("result"));
  REQUIRE(response["result"].contains("data"));
  REQUIRE(response["result"]["data"] == "success");

  client.Stop();
}

TEST_CASE("Client handles in-order responses correctly", "[Client][InOrder]") {
  auto transport = std::make_unique<MockTransport>();
  MockTransport* transport_ptr = transport.get();

  const int num_requests = 10;
  std::vector<std::string> responses;
  for (int i = 0; i < num_requests; ++i) {
    responses.push_back(fmt::format(
        R"({{"jsonrpc":"2.0","result":{{"data":"success_{}"}},"id":{}}})", i,
        i));
  }

  jsonrpc::client::Client client(std::move(transport));
  client.Start();

  for (int i = 0; i < num_requests; ++i) {
    transport_ptr->SetResponse(responses[i]);
    REQUIRE(client.HasPendingRequests() == false);
    auto response = client.SendMethodCall("getData");
    REQUIRE(client.HasPendingRequests() == false);
    REQUIRE(response["result"]["data"] == "success_" + std::to_string(i));
  }

  client.Stop();
}

TEST_CASE("Client handles async method calls correctly", "[Client][Async]") {
  auto transport = std::make_unique<MockTransport>();
  MockTransport* transport_ptr = transport.get();

  transport_ptr->SetResponse(
      R"({"jsonrpc":"2.0","result":"async_success","id":0})");

  jsonrpc::client::Client client(std::move(transport));
  client.Start();

  auto future_response = client.SendMethodCallAsync("async_test_method");

  REQUIRE(
      future_response.wait_for(std::chrono::seconds(1)) ==
      std::future_status::ready);

  nlohmann::json response = future_response.get();

  REQUIRE(client.HasPendingRequests() == false);
  REQUIRE(response["result"] == "async_success");

  client.Stop();
}

TEST_CASE(
    "Client handles multiple async method calls concurrently",
    "[Client][Async]") {
  auto transport = std::make_unique<MockTransport>();
  MockTransport* transport_ptr = transport.get();

  const int num_requests = 5;
  for (int i = 0; i < num_requests; ++i) {
    transport_ptr->SetResponse(fmt::format(
        R"({{"jsonrpc":"2.0","result":"success_{}","id":{}}})", i, i));
  }

  jsonrpc::client::Client client(std::move(transport));
  client.Start();

  std::vector<std::future<nlohmann::json>> futures;

  for (int i = 0; i < num_requests; ++i) {
    futures.push_back(
        client.SendMethodCallAsync(fmt::format("async_test_method_{}", i)));
  }

  for (int i = 0; i < num_requests; ++i) {
    REQUIRE(
        futures[i].wait_for(std::chrono::seconds(1)) ==
        std::future_status::ready);
    nlohmann::json response = futures[i].get();
    REQUIRE(response["result"] == fmt::format("success_{}", i));
  }

  REQUIRE(client.HasPendingRequests() == false);

  client.Stop();
}

TEST_CASE("Client can handle server notifications", "[Client]") {
  SECTION("Client processes notifications without params") {
    auto mock_transport = std::make_unique<MockTransport>();
    auto* transport_ptr = mock_transport.get();
    jsonrpc::client::Client client(std::move(mock_transport));

    bool notification_received = false;
    client.RegisterNotification(
        "test_notification",
        [&notification_received](
            [[maybe_unused]] const std::optional<nlohmann::json>& params) {
          notification_received = true;
        });

    client.Start();

    // Simulate server sending a notification
    Json notification = {{"jsonrpc", "2.0"}, {"method", "test_notification"}};
    transport_ptr->SetResponse(notification.dump());

    // Give some time for the client to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.Stop();

    REQUIRE(notification_received);
  }

  SECTION("Client processes notifications with params") {
    auto mock_transport = std::make_unique<MockTransport>();
    auto* transport_ptr = mock_transport.get();
    jsonrpc::client::Client client(std::move(mock_transport));

    Json received_params;
    client.RegisterNotification(
        "test_notification",
        [&received_params](const std::optional<nlohmann::json>& params) {
          REQUIRE(params.has_value());
          received_params = params.value();
        });

    client.Start();

    // Simulate server sending a notification with params
    Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "test_notification"},
        {"params", {{"key1", "value1"}, {"key2", 42}}}};
    transport_ptr->SetResponse(notification.dump());

    // Give some time for the client to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.Stop();

    REQUIRE(received_params["key1"] == "value1");
    REQUIRE(received_params["key2"] == 42);
  }

  SECTION("Client handles unregistered notifications gracefully") {
    auto mock_transport = std::make_unique<MockTransport>();
    auto* transport_ptr = mock_transport.get();
    jsonrpc::client::Client client(std::move(mock_transport));

    client.Start();

    // Simulate server sending a notification for unregistered method
    Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "unknown_notification"},
        {"params", {{"data", "test"}}}};
    transport_ptr->SetResponse(notification.dump());

    // Should not crash
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.Stop();
  }

  SECTION("Client can handle notifications while processing method calls") {
    auto mock_transport = std::make_unique<MockTransport>();
    auto* transport_ptr = mock_transport.get();
    jsonrpc::client::Client client(std::move(mock_transport));

    bool notification_received = false;
    client.RegisterNotification(
        "test_notification",
        [&notification_received](
            [[maybe_unused]] const std::optional<nlohmann::json>& params) {
          notification_received = true;
        });

    client.Start();

    // Start an async method call
    auto future = client.SendMethodCallAsync("test_method", Json::object());

    // Simulate server sending a notification before the response
    Json notification = {{"jsonrpc", "2.0"}, {"method", "test_notification"}};
    transport_ptr->SetResponse(notification.dump());

    // Then simulate the method response
    Json response = {
        {"jsonrpc", "2.0"},
        {"id", 0},  // First request will always have ID 0
        {"result", "success"}};
    transport_ptr->SetResponse(response.dump());

    // Wait for both to be processed
    future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.Stop();

    REQUIRE(notification_received);
  }
}

TEST_CASE("Client handles request timeouts correctly", "[Client][Timeout]") {
  SECTION("Request times out when no response received") {
    auto transport = std::make_unique<MockTransport>();
    jsonrpc::client::Client client(std::move(transport));
    client.Start();

    REQUIRE_THROWS_WITH(
        client.SendMethodCall(
            "test_method", nlohmann::json::object(),
            std::chrono::milliseconds(100)),
        Catch::Matchers::ContainsSubstring("Request timed out after 100 ms"));

    client.Stop();
  }

  SECTION("Request succeeds when response received within timeout") {
    auto transport = std::make_unique<MockTransport>();
    auto* transport_ptr = transport.get();
    jsonrpc::client::Client client(std::move(transport));

    transport_ptr->SetResponse(
        R"({"jsonrpc":"2.0","result":"success","id":0})");
    client.Start();

    REQUIRE_NOTHROW(client.SendMethodCall(
        "test_method", nlohmann::json::object(),
        std::chrono::milliseconds(1000)));

    client.Stop();
  }

  SECTION("Timed out request is removed from pending requests") {
    auto transport = std::make_unique<MockTransport>();
    jsonrpc::client::Client client(std::move(transport));
    client.Start();

    try {
      client.SendMethodCall(
          "test_method", nlohmann::json::object(),
          std::chrono::milliseconds(100));
    } catch (const std::runtime_error&) {
      // Expected timeout
    }

    REQUIRE(client.HasPendingRequests() == false);
    client.Stop();
  }
}
