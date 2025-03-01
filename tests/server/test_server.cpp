#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "../common/mock_transport.hpp"
#include "jsonrpc/server/server.hpp"

using Json = nlohmann::json;

TEST_CASE("Server initializes correctly", "[Server]") {
  jsonrpc::server::Server server(std::make_unique<MockTransport>());
}

TEST_CASE("Server stops and exits running state", "[Server]") {
  auto mock_transport = std::make_unique<MockTransport>();
  jsonrpc::server::Server server(std::move(mock_transport));

  std::thread server_thread([&server]() { server.Start(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  server.Stop();

  if (server_thread.joinable()) {
    server_thread.join();
  }

  REQUIRE(server.IsRunning() == false);
}

TEST_CASE("Server can send notifications", "[Server]") {
  auto mock_transport = std::make_unique<MockTransport>();
  auto* transport_ptr = mock_transport.get();
  jsonrpc::server::Server server(std::move(mock_transport));

  SECTION("Server can send notification without params") {
    std::thread server_thread([&server]() { server.Start(); });
    std::this_thread::sleep_for(
        std::chrono::milliseconds(100));  // Wait for server to start

    server.SendNotification("test_method");

    server.Stop();
    if (server_thread.joinable()) {
      server_thread.join();
    }

    // Verify the sent message
    auto sent_message = Json::parse(transport_ptr->GetLastSentMessage());
    REQUIRE(sent_message["jsonrpc"] == "2.0");
    REQUIRE(sent_message["method"] == "test_method");
    REQUIRE(sent_message.contains("params") == false);
  }

  SECTION("Server can send notification with params") {
    std::thread server_thread([&server]() { server.Start(); });
    std::this_thread::sleep_for(
        std::chrono::milliseconds(100));  // Wait for server to start

    Json params = {{"key1", "value1"}, {"key2", 42}};
    server.SendNotification("test_method", params);

    server.Stop();
    if (server_thread.joinable()) {
      server_thread.join();
    }

    // Verify the sent message
    auto sent_message = Json::parse(transport_ptr->GetLastSentMessage());
    REQUIRE(sent_message["jsonrpc"] == "2.0");
    REQUIRE(sent_message["method"] == "test_method");
    REQUIRE(sent_message["params"]["key1"] == "value1");
    REQUIRE(sent_message["params"]["key2"] == 42);
  }

  SECTION("Server throws when sending notification while not running") {
    REQUIRE_THROWS_AS(
        server.SendNotification("test_method"), std::runtime_error);
  }
}

TEST_CASE("Server handles concurrent transport access safely", "[Server]") {
  auto mock_transport = std::make_unique<MockTransport>();
  auto* transport_ptr = mock_transport.get();
  jsonrpc::server::Server server(std::move(mock_transport));

  SECTION("Multiple threads can send notifications concurrently") {
    std::thread server_thread([&server]() { server.Start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int num_threads = 10;
    std::vector<std::thread> notification_threads;

    // Launch multiple threads to send notifications
    for (int i = 0; i < num_threads; ++i) {
      notification_threads.emplace_back([&server, i]() {
        Json params = {{"thread_id", i}};
        server.SendNotification("test_method", params);
      });
    }

    // Wait for all notifications to complete
    for (auto& thread : notification_threads) {
      thread.join();
    }

    server.Stop();
    server_thread.join();

    // Verify all notifications were sent
    const auto& sent_messages = transport_ptr->sent_requests;
    REQUIRE(sent_messages.size() == num_threads);

    // Verify each message is valid JSON-RPC 2.0 notification
    for (const auto& msg : sent_messages) {
      auto json = Json::parse(msg);
      REQUIRE(json["jsonrpc"] == "2.0");
      REQUIRE(json["method"] == "test_method");
      REQUIRE(json.contains("params"));
      REQUIRE(json["params"]["thread_id"].is_number());
    }
  }

  SECTION("Server can handle concurrent requests and notifications") {
    // Register a method that takes some time to process
    server.RegisterMethodCall(
        "slow_method",
        []([[maybe_unused]] const std::optional<nlohmann::json>& params)
            -> nlohmann::json {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          return Json{"processed"};
        });

    std::thread server_thread([&server]() { server.Start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send a mix of notifications and requests concurrently
    std::vector<std::thread> mixed_threads;
    const int num_threads = 5;

    for (int i = 0; i < num_threads; ++i) {
      // Thread for notification
      mixed_threads.emplace_back([&server, i]() {
        Json params = {{"notification_id", i}};
        server.SendNotification("test_notification", params);
      });

      // Thread for method call (simulated from transport)
      mixed_threads.emplace_back([transport_ptr, i]() {
        Json request = {
            {"jsonrpc", "2.0"},
            {"method", "slow_method"},
            {"id", i},
            {"params", {{"request_id", i}}}};
        transport_ptr->SetResponse(request.dump());
      });
    }

    for (auto& thread : mixed_threads) {
      thread.join();
    }

    server.Stop();
    server_thread.join();

    // Verify messages were processed
    REQUIRE(transport_ptr->sent_requests.size() > 0);
  }
}
