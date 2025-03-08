#include <memory>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedPipeTransport;
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
  // Setup logging
  auto logger = spdlog::basic_logger_mt("client", "logs/client.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  try {
    // Create an io_context for asio operations
    asio::io_context io_context;

    // Run the client logic with proper awaiting
    asio::co_spawn(
        io_context,
        [&io_context]() -> asio::awaitable<void> {
          try {
            // Create the transport and fully initialized RPC client
            const std::string socket_path = "/tmp/calculator_framed_pipe";
            spdlog::info("Connecting to server on: {}", socket_path);

            auto transport = std::make_unique<FramedPipeTransport>(
                io_context, socket_path, false);
            auto client = co_await RpcEndpoint::CreateClient(
                io_context, std::move(transport));

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

    spdlog::info("Client shutdown complete");
    return 0;

  } catch (const std::exception& e) {
    spdlog::error("Fatal error: {}", e.what());
    return 1;
  }
}
