#include <memory>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/socket_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::SocketTransport;
using Json = nlohmann::json;

// Separate function to handle RPC calls using coroutines
auto RunRpcCalls(RpcEndpoint* client) -> asio::awaitable<void> {
  try {
    const int add_op1 = 10;
    const int add_op2 = 5;
    Json add_resp = co_await client->CallMethod(
        "add", Json({{"a", add_op1}, {"b", add_op2}}));
    spdlog::info("Add result: {}", add_resp.dump());

    const int div_op1 = 10;
    const int div_op2 = 2;
    Json div_resp = co_await client->CallMethod(
        "divide", Json({{"a", div_op1}, {"b", div_op2}}));
    spdlog::info("Divide result: {}", div_resp.dump());

    // Tell server to stop
    co_await client->SendNotification("stop");

    // Clean shutdown of client
    co_await client->Shutdown();
  } catch (const std::exception& e) {
    spdlog::error("RPC error: {}", e.what());
    throw;
  }
}

auto main() -> int {
  auto logger = spdlog::basic_logger_mt("client", "logs/client.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  try {
    asio::io_context io_context;

    // Run the client logic, properly awaiting client creation first
    asio::co_spawn(
        io_context,
        [&io_context]() -> asio::awaitable<void> {
          try {
            const std::string host = "127.0.0.1";
            const uint16_t port = 12345;

            // Create and fully initialize the client
            auto client = co_await RpcEndpoint::CreateClient(
                io_context, std::make_unique<SocketTransport>(
                                io_context, host, port, false));

            // Now that client is fully initialized, run RPC calls
            co_await RunRpcCalls(client.get());
          } catch (const std::exception& e) {
            spdlog::error("Error in client coroutine: {}", e.what());
          }
        },
        [](std::exception_ptr e) {
          if (e) {
            try {
              std::rethrow_exception(e);
            } catch (const std::exception& ex) {
              spdlog::error("Client error: {}", ex.what());
            }
          }
        });

    // Run the io_context
    io_context.run();
    return 0;

  } catch (const std::exception& e) {
    spdlog::error("Error: {}", e.what());
    return 1;
  }
}
