#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "jsonrpc/transport/pipe_transport.hpp"

TEST_CASE(
    "PipeTransport starts server and client communication", "[PipeTransport]") {
  std::string socket_path = "/tmp/test_socket";

  // Start the server in a separate thread
  std::thread server_thread([&]() {
    jsonrpc::transport::PipeTransport server_transport(socket_path, true);

    // Wait for a message from the client
    std::string received_message = server_transport.ReceiveMessage().get();
    REQUIRE(received_message == "Hello, Server!");

    // Send a response back to the client
    server_transport.SendMessage("Hello, Client!").get();
  });

  // Give the server some time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Start the client and connect to the server
  jsonrpc::transport::PipeTransport client_transport(socket_path, false);

  // Send a message to the server
  client_transport.SendMessage("Hello, Server!").get();

  // Wait for a response from the server
  std::string response = client_transport.ReceiveMessage().get();
  REQUIRE(response == "Hello, Client!");

  server_thread.join();
}

TEST_CASE("PipeTransport handles empty message correctly", "[PipeTransport]") {
  std::string socket_path = "/tmp/test_socket_empty";

  // Start the server in a separate thread
  std::thread server_thread([&]() {
    jsonrpc::transport::PipeTransport server_transport(socket_path, true);

    // Send an empty message to the client
    server_transport.SendMessage("").get();
  });

  // Give the server some time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Start the client and connect to the server
  jsonrpc::transport::PipeTransport client_transport(socket_path, false);

  // Wait for the empty response from the server
  std::string response = client_transport.ReceiveMessage().get();
  REQUIRE(response.empty());

  server_thread.join();
}

TEST_CASE("PipeTransport throws on invalid socket path", "[PipeTransport]") {
  REQUIRE_THROWS_WITH(
      jsonrpc::transport::PipeTransport("/tmp/non_existent_socket", false),
      "Error connecting to socket");
}

TEST_CASE(
    "PipeTransport operations complete within timeout", "[PipeTransport]") {
  std::string socket_path = "/tmp/test_socket_timeout";

  // Start the server in a separate thread
  std::thread server_thread([&]() {
    jsonrpc::transport::PipeTransport server_transport(socket_path, true);

    // Simulate slow processing
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Send a response
    server_transport.SendMessage("Response after delay").get();
  });

  // Give the server some time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Start the client
  jsonrpc::transport::PipeTransport client_transport(socket_path, false);

  // Record start time
  auto start = std::chrono::steady_clock::now();

  // Receive message
  std::string response = client_transport.ReceiveMessage().get();

  // Record end time
  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

  // Verify operation completed within timeout
  REQUIRE(duration.count() < 5);
  REQUIRE(response == "Response after delay");

  server_thread.join();
}

TEST_CASE(
    "PipeTransport handles rapid close during operation", "[PipeTransport]") {
  std::string socket_path = "/tmp/test_socket_close";

  // Start the server in a separate thread
  std::thread server_thread([&]() {
    jsonrpc::transport::PipeTransport server_transport(socket_path, true);

    // Close immediately after client connects
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    server_transport.Close().get();
  });

  // Give the server some time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Start the client
  jsonrpc::transport::PipeTransport client_transport(socket_path, false);

  // Record start time
  auto start = std::chrono::steady_clock::now();

  // Attempt to receive message (should throw due to closed transport)
  REQUIRE_THROWS_AS(
      client_transport.ReceiveMessage().get(), std::runtime_error);

  // Record end time
  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

  // Verify operation completed within timeout
  REQUIRE(duration.count() < 5);

  server_thread.join();
}
