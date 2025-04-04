#include <string>

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/endpoint.hpp"
#include "jsonrpc/transport/pipe_transport.hpp"

/**
 * @brief Calculator Client Example using Pipe Transport
 *
 * This is a simple demonstration of a JSON-RPC client using pipe transport.
 * The example shows how to connect to a server, make method calls,
 * send notifications, and handle results and errors in a clean, structured way.
 */

using Json = nlohmann::json;
using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::PipeTransport;

// All RPC operations in a separate coroutine function
auto RunClient(asio::any_io_executor executor) -> asio::awaitable<void> {
  // Step 1: Initialize transport and create RPC client
  const std::string socket_path = "/tmp/calculator_pipe";
  spdlog::info("Connecting to server on: {}", socket_path);
  auto transport = std::make_unique<PipeTransport>(executor, socket_path);

  auto client_result =
      co_await RpcEndpoint::CreateClient(executor, std::move(transport));
  auto client = std::move(client_result.value());

  // Step 2: Make RPC method calls
  // Example 1: Call "add" method
  const int add_op1 = 10;
  const int add_op2 = 5;
  Json add_params = {{"a", add_op1}, {"b", add_op2}};
  auto add_result = co_await client->SendMethodCall("add", add_params);
  spdlog::info(
      "Add result: {} + {} = {}", add_op1, add_op2, add_result.value().dump());

  // Example 2: Call "divide" method
  const int div_op1 = 10;
  const int div_op2 = 2;
  Json div_params = {{"a", div_op1}, {"b", div_op2}};
  auto div_result = co_await client->SendMethodCall("divide", div_params);
  spdlog::info(
      "Div result: {} / {} = {}", div_op1, div_op2, div_result.value().dump());

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
  auto executor = io_context.get_executor();

  // Launch the RPC operations with our simple error handler
  asio::co_spawn(executor, RunClient(executor), HandleError);

  // Run the ASIO event loop
  spdlog::info("Running io_context");
  io_context.run();

  spdlog::info("Client shutdown complete");
  return 0;
}
