#include <future>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/transport/framed_socket_transport.hpp"

// Helper function to get a free port
auto GetFreePort() -> uint16_t {
  static uint16_t port = 9000;
  return port++;
}

TEST_CASE(
    "FramedSocketTransport can send and receive a single message",
    "[FramedSocketTransport]") {
  std::string host = "127.0.0.1";
  uint16_t port = GetFreePort();
  spdlog::info(
      "Test: FramedSocketTransport single message - using {}:{}", host, port);

  // Create server in a separate thread
  std::thread server_thread([host, port]() {
    try {
      spdlog::info("Server: Creating transport on {}:{}", host, port);
      auto server_transport =
          std::make_unique<jsonrpc::transport::FramedSocketTransport>(
              host, port, true);
      spdlog::info("Server: Transport created successfully");

      // Receive message with a timeout
      spdlog::info("Server: Waiting to receive message...");
      auto future = server_transport->ReceiveMessage();
      auto status = future.wait_for(std::chrono::seconds(10));

      if (status == std::future_status::ready) {
        std::string received = future.get();
        spdlog::info("Server: Message received: {}", received);
        REQUIRE(
            received ==
            R"({"jsonrpc":"2.0","method":"testMethod","params":{},"id":1})");
      } else {
        spdlog::error("Server: Timeout waiting for message");
        FAIL("Server timed out waiting for message");
      }
    } catch (const std::exception& e) {
      spdlog::error("Server exception: {}", e.what());
      FAIL("Server failed to receive message: " + std::string(e.what()));
    }
  });

  // Give the server time to start
  spdlog::info("Main: Waiting for server to start...");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Create client and send message
  try {
    spdlog::info("Client: Creating transport to connect to {}:{}", host, port);
    auto client_transport =
        std::make_unique<jsonrpc::transport::FramedSocketTransport>(
            host, port, false);
    spdlog::info("Client: Transport created successfully");

    std::string test_message =
        R"({"jsonrpc":"2.0","method":"testMethod","params":{},"id":1})";
    spdlog::info("Client: Sending message: {}", test_message);

    auto send_future = client_transport->SendMessage(test_message);
    auto status = send_future.wait_for(std::chrono::seconds(10));

    if (status == std::future_status::ready) {
      spdlog::info("Client: Message sent successfully");
      REQUIRE(status == std::future_status::ready);
    } else {
      spdlog::error("Client: Timeout sending message");
      FAIL("Client timed out sending message");
    }
  } catch (const std::exception& e) {
    spdlog::error("Client exception: {}", e.what());
    FAIL("Client failed to send message: " + std::string(e.what()));
  }

  // Wait for server thread to complete
  spdlog::info("Main: Waiting for server thread to complete...");
  if (server_thread.joinable()) {
    server_thread.join();
  }
  spdlog::info("Test completed successfully");
}

TEST_CASE(
    "FramedSocketTransport can handle bidirectional communication",
    "[FramedSocketTransport]") {
  std::string host = "127.0.0.1";
  uint16_t port = GetFreePort();
  spdlog::info(
      "Test: FramedSocketTransport bidirectional - using {}:{}", host, port);

  std::string request =
      R"({"jsonrpc":"2.0","method":"echo","params":{"message":"Hello"},"id":1})";
  std::string response =
      R"({"jsonrpc":"2.0","result":{"message":"Hello"},"id":1})";
  bool server_received = false;
  bool client_received = false;

  // Create server in a separate thread
  std::thread server_thread([&]() {
    try {
      spdlog::info("Server: Creating transport on {}:{}", host, port);
      auto server_transport =
          std::make_unique<jsonrpc::transport::FramedSocketTransport>(
              host, port, true);
      spdlog::info("Server: Transport created successfully");

      // Receive request
      spdlog::info("Server: Waiting to receive request...");
      auto receive_future = server_transport->ReceiveMessage();
      auto status = receive_future.wait_for(std::chrono::seconds(10));

      if (status == std::future_status::ready) {
        std::string received_request = receive_future.get();
        spdlog::info("Server: Request received: {}", received_request);
        server_received = (received_request == request);

        // Send response
        spdlog::info("Server: Sending response: {}", response);
        auto send_future = server_transport->SendMessage(response);
        auto send_status = send_future.wait_for(std::chrono::seconds(10));

        if (send_status != std::future_status::ready) {
          spdlog::error("Server: Timeout sending response");
        } else {
          spdlog::info("Server: Response sent successfully");
        }
      } else {
        spdlog::error("Server: Timeout waiting for request");
      }
    } catch (const std::exception& e) {
      spdlog::error("Server exception: {}", e.what());
    }
  });

  // Give the server time to start
  spdlog::info("Main: Waiting for server to start...");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Create client and handle communication
  std::thread client_thread([&]() {
    try {
      spdlog::info(
          "Client: Creating transport to connect to {}:{}", host, port);
      auto client_transport =
          std::make_unique<jsonrpc::transport::FramedSocketTransport>(
              host, port, false);
      spdlog::info("Client: Transport created successfully");

      // Send request
      spdlog::info("Client: Sending request: {}", request);
      auto send_future = client_transport->SendMessage(request);
      auto send_status = send_future.wait_for(std::chrono::seconds(10));

      if (send_status != std::future_status::ready) {
        spdlog::error("Client: Timeout sending request");
      } else {
        spdlog::info("Client: Request sent successfully");

        // Receive response
        spdlog::info("Client: Waiting to receive response...");
        auto receive_future = client_transport->ReceiveMessage();
        auto status = receive_future.wait_for(std::chrono::seconds(10));

        if (status == std::future_status::ready) {
          std::string received_response = receive_future.get();
          spdlog::info("Client: Response received: {}", received_response);
          client_received = (received_response == response);
        } else {
          spdlog::error("Client: Timeout waiting for response");
        }
      }
    } catch (const std::exception& e) {
      spdlog::error("Client exception: {}", e.what());
    }
  });

  // Wait for threads to complete
  spdlog::info("Main: Waiting for threads to complete...");
  if (server_thread.joinable()) {
    server_thread.join();
  }
  if (client_thread.joinable()) {
    client_thread.join();
  }

  // Verify communication was successful
  spdlog::info("Server received correct request: {}", server_received);
  spdlog::info("Client received correct response: {}", client_received);
  REQUIRE(server_received);
  REQUIRE(client_received);
  spdlog::info("Test completed successfully");
}

TEST_CASE(
    "FramedSocketTransport server can send multiple messages to client",
    "[FramedSocketTransport]") {
  std::string host = "127.0.0.1";
  uint16_t port = GetFreePort();
  spdlog::info(
      "Test: FramedSocketTransport server multiple messages - using {}:{}",
      host, port);

  // Define test messages to send from server to client
  std::vector<std::string> server_messages = {
      R"({"jsonrpc":"2.0","method":"notification1","params":{},"id":null})",
      R"({"jsonrpc":"2.0","method":"notification2","params":{"data":"test"},"id":null})",
      R"({"jsonrpc":"2.0","method":"notification3","params":[1,2,3],"id":null})"};

  // Track received messages
  std::vector<std::string> received_messages;
  bool test_completed = false;

  // Create server in a separate thread
  std::thread server_thread([host, port, &server_messages]() {
    try {
      spdlog::info("Server: Creating transport on {}:{}", host, port);
      auto server_transport =
          std::make_unique<jsonrpc::transport::FramedSocketTransport>(
              host, port, true);
      spdlog::info("Server: Transport created successfully");

      // Wait for client to connect (receive initial message)
      spdlog::info("Server: Waiting for client connection message...");
      auto future = server_transport->ReceiveMessage();
      auto status = future.wait_for(std::chrono::seconds(10));

      if (status == std::future_status::ready) {
        std::string received = future.get();
        spdlog::info("Server: Connection message received: {}", received);

        // Send multiple messages to client
        spdlog::info(
            "Server: Sending {} messages to client", server_messages.size());
        for (size_t i = 0; i < server_messages.size(); ++i) {
          spdlog::info(
              "Server: Sending message {}: {}", i + 1, server_messages[i]);
          auto send_future = server_transport->SendMessage(server_messages[i]);
          auto send_status = send_future.wait_for(std::chrono::seconds(10));

          if (send_status != std::future_status::ready) {
            spdlog::error("Server: Timeout sending message {}", i + 1);
            FAIL("Server timed out sending message " + std::to_string(i + 1));
          } else {
            spdlog::info("Server: Message {} sent successfully", i + 1);
          }

          // Small delay between messages to ensure ordering
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      } else {
        spdlog::error("Server: Timeout waiting for client connection");
        FAIL("Server timed out waiting for client connection");
      }
    } catch (const std::exception& e) {
      spdlog::error("Server exception: {}", e.what());
      FAIL("Server failed: " + std::string(e.what()));
    }
    spdlog::info("Server: Completed sending all messages");
  });

  // Give the server time to start
  spdlog::info("Main: Waiting for server to start...");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Create client and receive messages
  std::thread client_thread([host, port, &server_messages, &received_messages,
                             &test_completed]() {
    try {
      spdlog::info(
          "Client: Creating transport to connect to {}:{}", host, port);
      auto client_transport =
          std::make_unique<jsonrpc::transport::FramedSocketTransport>(
              host, port, false);
      spdlog::info("Client: Transport created successfully");

      // Send initial connection message
      std::string connection_message =
          R"({"jsonrpc":"2.0","method":"connect","params":{},"id":0})";
      spdlog::info(
          "Client: Sending connection message: {}", connection_message);
      auto send_future = client_transport->SendMessage(connection_message);
      auto send_status = send_future.wait_for(std::chrono::seconds(10));

      if (send_status != std::future_status::ready) {
        spdlog::error("Client: Timeout sending connection message");
        FAIL("Client timed out sending connection message");
      }

      spdlog::info(
          "Client: Connection message sent, waiting for server messages...");

      // Receive multiple messages from server
      for (size_t i = 0; i < server_messages.size(); ++i) {
        spdlog::info("Client: Waiting for message {}", i + 1);
        auto receive_future = client_transport->ReceiveMessage();
        auto status = receive_future.wait_for(std::chrono::seconds(10));

        if (status == std::future_status::ready) {
          std::string received = receive_future.get();
          spdlog::info("Client: Message {} received: {}", i + 1, received);
          received_messages.push_back(received);
        } else {
          spdlog::error("Client: Timeout waiting for message {}", i + 1);
          FAIL("Client timed out waiting for message " + std::to_string(i + 1));
        }
      }

      spdlog::info("Client: Received all expected messages");
    } catch (const std::exception& e) {
      spdlog::error("Client exception: {}", e.what());
      FAIL("Client failed: " + std::string(e.what()));
    }
    test_completed = true;
  });

  // Wait for threads to complete
  spdlog::info("Main: Waiting for threads to complete...");
  if (server_thread.joinable()) {
    server_thread.join();
  }
  if (client_thread.joinable()) {
    client_thread.join();
  }

  // Verify all messages were received correctly
  REQUIRE(received_messages.size() == server_messages.size());
  for (size_t i = 0; i < server_messages.size(); ++i) {
    spdlog::info("Verifying message {}", i + 1);
    REQUIRE(received_messages[i] == server_messages[i]);
  }

  REQUIRE(test_completed);
  spdlog::info("Test completed successfully");
}
