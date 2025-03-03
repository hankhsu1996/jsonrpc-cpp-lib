#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "jsonrpc/transport/socket_transport.hpp"

TEST_CASE(
    "SocketTransport starts server and client communication",
    "[SocketTransport]") {
  std::string host = "127.0.0.1";
  uint16_t port = 12345;

  // Start the server in a separate thread
  std::thread server_thread([&]() {
    jsonrpc::transport::SocketTransport server_transport(host, port, true);

    // Wait for a message from the client
    std::string received_message = server_transport.ReceiveMessage().get();
    REQUIRE(received_message == "Hello, Server!");

    // Send a response back to the client
    server_transport.SendMessage("Hello, Client!").get();
  });

  // Give the server some time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Start the client and connect to the server
  jsonrpc::transport::SocketTransport client_transport(host, port, false);

  // Send a message to the server
  client_transport.SendMessage("Hello, Server!").get();

  // Wait for a response from the server
  std::string response = client_transport.ReceiveMessage().get();
  REQUIRE(response == "Hello, Client!");

  server_thread.join();
}

TEST_CASE(
    "SocketTransport handles empty message correctly", "[SocketTransport]") {
  std::string host = "127.0.0.1";
  uint16_t port = 12346;

  // Start the server in a separate thread
  std::thread server_thread([&]() {
    jsonrpc::transport::SocketTransport server_transport(host, port, true);

    // Send an empty message to the client
    server_transport.SendMessage("").get();
  });

  // Give the server some time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Start the client and connect to the server
  jsonrpc::transport::SocketTransport client_transport(host, port, false);

  // Wait for the empty response from the server
  std::string response = client_transport.ReceiveMessage().get();
  REQUIRE(response.empty());

  server_thread.join();
}
