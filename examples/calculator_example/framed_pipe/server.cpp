#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "../calculator.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedPipeTransport;
using Json = nlohmann::json;

/**
 * @brief Calculator Server Example using Framed Pipe Transport
 *
 * This is a simple demonstration of a JSON-RPC server using framed pipe
 * transport. While this example shows the full flexibility of the library,
 * production applications might benefit from helper functions to reduce
 * boilerplate.
 */

auto HandleStop(std::shared_ptr<RpcEndpoint> server) -> asio::awaitable<void> {
  if (server) {
    co_await server->Shutdown();
  }
  co_return;
}

// Main server logic encapsulated in a function
auto RunServer(asio::any_io_executor executor, std::string socket_path)
    -> asio::awaitable<void> {
  // Step 1: Create transport
  auto transport =
      std::make_unique<FramedPipeTransport>(executor, socket_path, true);

  // Step 2: Create RPC endpoint
  // RpcEndpoint server(executor, std::move(transport));
  auto server = std::make_shared<RpcEndpoint>(executor, std::move(transport));

  // Step 3: Register RPC methods
  server->RegisterMethodCall("add", Calculator::Add);
  server->RegisterMethodCall("divide", Calculator::Divide);

  // Step 4: Register stop notification
  server->RegisterNotification(
      "stop", [server](std::optional<Json>) { return HandleStop(server); });

  // Step 5: Start server
  co_await server->Start();

  // Step 6: Wait for server shutdown
  co_await server->WaitForShutdown();

  spdlog::info("Server shutdown complete");
  co_return;
}

// Helper function for error handling
auto HandleError(std::exception_ptr eptr) -> void {
  try {
    if (eptr) {
      std::rethrow_exception(eptr);
    }
  } catch (const std::exception& e) {
    spdlog::error("Server error: {}", e.what());
  }
}

auto main() -> int {
  // Setup logging
  auto logger = spdlog::basic_logger_mt("server", "logs/server.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  // Step 1: Set socket path and initialize
  const std::string socket_path = "/tmp/calculator_framed_pipe";
  spdlog::info("Starting server on socket: {}", socket_path);

  // Step 2: Create an io_context for asio operations
  asio::io_context io_context;
  auto executor = io_context.get_executor();

  // Step 3: Launch the server with error handling
  asio::co_spawn(executor, RunServer(executor, socket_path), HandleError);

  // Step 4: Run the io_context
  io_context.run();

  return 0;
}
