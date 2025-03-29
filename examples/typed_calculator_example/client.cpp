#include <string>

#include <asio.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/endpoint.hpp"
#include "jsonrpc/transport/pipe_transport.hpp"
#include "typed_calculator.hpp"

/**
 * @brief Simple typed calculator client example
 *
 * This example demonstrates calling methods with typed parameters and results.
 */

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::PipeTransport;

// All RPC operations in a separate coroutine function
auto RunClient(asio::any_io_executor executor) -> asio::awaitable<void> {
  // Initialize transport and create RPC client
  const std::string socket_path = "/tmp/typed_calculator_pipe";
  auto transport = std::make_unique<PipeTransport>(executor, socket_path);

  auto client =
      co_await RpcEndpoint::CreateClient(executor, std::move(transport));

  // Call "add" method with typed params and result
  AddParams add_params{.a = 10.0, .b = 5.0};

  // Send the method call with typed params and receive typed result
  // Note how we specify the parameter and result types in the template
  // arguments
  Result add_result =
      co_await client->SendMethodCall<AddParams, Result>("add", add_params);

  // Access the result using the typed struct
  spdlog::info(
      "Add result: {} + {} = {}", add_params.a, add_params.b, add_result.value);

  // Call "divide" method with typed params and result
  DivideParams div_params{.a = 10.0, .b = 2.0};
  Result div_result = co_await client->SendMethodCall<DivideParams, Result>(
      "divide", div_params);
  spdlog::info(
      "Divide result: {} / {} = {}", div_params.a, div_params.b,
      div_result.value);

  // Send notification to shut down the server
  co_await client->SendNotification("stop");

  // Clean shutdown
  co_await client->Shutdown();
}

// Simple error handler
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
  auto logger =
      spdlog::basic_logger_mt("typed_client", "logs/typed_client.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  // Create ASIO io_context
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  // Launch the RPC operations with error handler
  asio::co_spawn(executor, RunClient(executor), HandleError);

  // Run the ASIO event loop
  io_context.run();

  spdlog::info("Client shutdown complete");
  return 0;
}
