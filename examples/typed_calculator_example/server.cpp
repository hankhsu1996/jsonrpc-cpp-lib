#include <iostream>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "typed_calculator.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::PipeTransport;

/**
 * @brief Simple typed calculator server example
 *
 * This example demonstrates registering methods with typed parameters and
 * results.
 */

// Handler for stop notification
auto HandleStop(std::shared_ptr<RpcEndpoint> server) -> asio::awaitable<void> {
  if (server) {
    co_await server->Shutdown();
  }
  co_return;
}

// Main server logic
auto RunServer(asio::any_io_executor executor, std::string socket_path)
    -> asio::awaitable<void> {
  // Create transport
  auto transport = std::make_unique<PipeTransport>(executor, socket_path, true);

  // Create RPC endpoint
  auto server = std::make_shared<RpcEndpoint>(executor, std::move(transport));

  // Register TYPED method calls using template methods
  // Note how we specify the parameter and result types in the template
  // arguments
  server->RegisterMethodCall<AddParams, Result>(
      "add", [](AddParams params) -> asio::awaitable<Result> {
        return TypedCalculator::Add(std::move(params));
      });

  server->RegisterMethodCall<DivideParams, Result>(
      "divide", [](DivideParams params) -> asio::awaitable<Result> {
        return TypedCalculator::Divide(std::move(params));
      });

  // Register stop notification with untyped (JSON) parameter
  server->RegisterNotification("stop", [server](std::optional<nlohmann::json>) {
    return HandleStop(server);
  });

  // Start server
  co_await server->Start();
  spdlog::info("Server started. Waiting for requests...");

  // Wait for server shutdown
  co_await server->WaitForShutdown();
  spdlog::info("Server shutdown complete");
  co_return;
}

// Error handler function
auto HandleError(std::exception_ptr eptr) -> void {
  try {
    if (eptr) {
      std::rethrow_exception(eptr);
    }
  } catch (const std::runtime_error& e) {
    spdlog::error("Runtime error: {}", e.what());
  } catch (const std::exception& e) {
    spdlog::error("Server error: {}", e.what());
  }
}

auto main() -> int {
  // Setup logging
  try {
    auto logger =
        spdlog::basic_logger_mt("typed_server", "logs/typed_server.log", true);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::debug);
  } catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "Log initialization failed: " << ex.what() << std::endl;
  }

  // Set socket path and initialize
  const std::string socket_path = "/tmp/typed_calculator_pipe";
  spdlog::info("Starting typed calculator server on socket: {}", socket_path);

  // Create an io_context for asio operations
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  // Launch the server with error handling
  asio::co_spawn(executor, RunServer(executor, socket_path), HandleError);

  // Run the io_context
  io_context.run();

  return 0;
}
