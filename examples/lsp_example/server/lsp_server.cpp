#include <string>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <spdlog/spdlog.h>

#include "../utils.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedPipeTransport;

/**
 * @brief LSP Server Example using Framed Pipe Transport
 *
 * This is a simple demonstration of a JSON-RPC server using framed pipe
 * transport. While this example shows the full flexibility of the library,
 * production applications might benefit from helper functions to reduce
 * boilerplate.
 */

auto main(int argc, char* argv[]) -> int {
  // Setup logging
  SetupLogger();

  // Check command line arguments
  const std::vector<std::string> args(argv, argv + argc);
  const std::string pipe_name = ParsePipeArguments(args);
  spdlog::info("Starting LSP server on pipe: {}", pipe_name);

  try {
    // Create an io_context for asio operations
    asio::io_context io_context;

    // Create the transport and RPC endpoint
    auto transport =
        std::make_unique<FramedPipeTransport>(io_context, pipe_name, false);
    RpcEndpoint server(io_context, std::move(transport));

    // Register LSP method handlers
    RegisterLSPHandlers(server);

    // Start server asynchronously
    asio::co_spawn(io_context, server.Start(), asio::detached);

    // Wait for server shutdown
    asio::co_spawn(
        io_context,
        [&server]() -> asio::awaitable<void> {
          co_await server.WaitForShutdown();
          spdlog::info("Server shutdown monitoring complete");
          co_return;
        }(),
        asio::detached);

    // Run the io_context
    io_context.run();

    spdlog::info("Server shutdown complete");
    return 0;

  } catch (const std::exception& ex) {
    spdlog::error("Fatal error: {}", ex.what());
    return 1;
  }
}
