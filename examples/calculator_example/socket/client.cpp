#include <memory>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/socket_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

/**
 * @brief Calculator Client Example using Socket Transport
 *
 * This is a simple demonstration of a JSON-RPC client using socket transport.
 * The example shows how to connect to a server, make method calls,
 * send notifications, and handle results and errors in a clean, structured way.
 */

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::SocketTransport;
using Json = nlohmann::json;

// All RPC operations in a separate coroutine function
auto RunClient(asio::io_context& io_context) -> asio::awaitable<void> {
  // Step 1: Initialize transport and create RPC client
  const std::string host = "127.0.0.1";
  const uint16_t port = 12345;
  spdlog::info("Connecting to server at {}:{}", host, port);

  auto transport =
      std::make_unique<SocketTransport>(io_context, host, port, false);
  auto client =
      co_await RpcEndpoint::CreateClient(io_context, std::move(transport));

  // Step 2: Make RPC method calls
  // Example 1: Call "add" method
  const int add_op1 = 10;
  const int add_op2 = 5;
  Json add_params = {{"a", add_op1}, {"b", add_op2}};
  Json add_result = co_await client->CallMethod("add", add_params);
  spdlog::info("Add result: {} + {} = {}", add_op1, add_op2, add_result.dump());

  // Example 2: Call "divide" method
  const int div_op1 = 10;
  const int div_op2 = 2;
  Json div_params = {{"a", div_op1}, {"b", div_op2}};
  Json div_result = co_await client->CallMethod("divide", div_params);
  spdlog::info("Div result: {} / {} = {}", div_op1, div_op2, div_result.dump());

  // Step 3: Send notifications
  spdlog::info("Sending 'stop' notification to server");
  co_await client->SendNotification("stop");

  // Step 4: Clean shutdown
  spdlog::info("Shutting down client");
  co_await client->Shutdown();
}

// Simple error handler function - keeps the main code clean
void HandleError(std::exception_ptr e) {
  if (!e) {
    return;
  }

  try {
    std::rethrow_exception(e);
  } catch (const std::exception& ex) {
    spdlog::error("RPC error: {}", ex.what());
  }
}

auto main() -> int {
  // Setup logging
  auto logger = spdlog::basic_logger_mt("client", "logs/client.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);
  spdlog::info("Starting JSON-RPC calculator client example");

  // Create ASIO io_context
  asio::io_context io_context;

  // Launch the RPC operations with our simple error handler
  asio::co_spawn(io_context, RunClient(io_context), HandleError);

  // Run the ASIO event loop
  spdlog::info("Running io_context");
  io_context.run();

  spdlog::info("Client shutdown complete");
  return 0;
}
