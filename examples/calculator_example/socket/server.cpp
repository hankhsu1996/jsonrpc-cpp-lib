#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/socket_transport.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "../calculator.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::SocketTransport;
using Json = nlohmann::json;

/**
 * @brief Calculator Server Example
 *
 * This is a simple demonstration of a JSON-RPC server using socket transport.
 * While this example shows the full flexibility of the library, production
 * applications might benefit from helper functions to reduce boilerplate.
 */

auto main() -> int {
  // Setup logging
  auto logger = spdlog::basic_logger_mt("server", "logs/server.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  const std::string host = "127.0.0.1";
  const int port = 12345;
  spdlog::info("Starting server on: {}:{}", host, port);

  try {
    // Create an io_context for asio operations
    asio::io_context io_context;

    // Create the transport and RPC endpoint
    auto transport =
        std::make_unique<SocketTransport>(io_context, host, port, true);
    RpcEndpoint server(io_context, std::move(transport));

    // Register RPC methods
    server.RegisterMethodCall("add", Calculator::Add);
    server.RegisterMethodCall("divide", Calculator::Divide);

    // Register stop notification
    server.RegisterNotification(
        "stop", [&server](const std::optional<Json>&) -> asio::awaitable<void> {
          co_await server.Shutdown();
          co_return;
        });

    // Start server asynchronously
    asio::co_spawn(io_context, server.Start(), asio::detached);

    // Wait for server shutdown
    asio::co_spawn(
        io_context,
        [&server]() -> asio::awaitable<void> {
          co_await server.WaitForShutdown();
          co_return;
        }(),
        asio::detached);

    // Run the io_context
    io_context.run();

    return 0;

  } catch (const std::exception& e) {
    spdlog::error("Error: {}", e.what());
    return 1;
  }
}
