#include "jsonrpc/transport/pipe_transport.hpp"

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {

template <typename F>
void RunTest(F&& test_fn) {
  // set logger to stdout
  auto cout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("server_logger", cout_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);
  asio::io_context io_ctx;
  asio::co_spawn(
      io_ctx, std::forward<F>(test_fn)(io_ctx.get_executor()), asio::detached);
  io_ctx.run();
}

}  // namespace

// Simple test to check if we can create and destroy a transport
TEST_CASE("PipeTransport basic creation test", "[PipeTransport]") {
  asio::io_context io_context;

  jsonrpc::transport::PipeTransport server_transport(
      io_context.get_executor(), "/tmp/test_socket_basic", true);

  asio::co_spawn(io_context, server_transport.Close(), asio::detached);

  io_context.run();  // Ensures async tasks complete before exiting
}

// Test proper closing of transport
TEST_CASE("PipeTransport can be properly closed", "[PipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string socket_path = "/tmp/test_socket_close_test";

    // Create a server transport
    spdlog::info("Creating server transport");
    jsonrpc::transport::PipeTransport server_transport(
        executor, socket_path, true);
    spdlog::info("Server transport created");

    // Close it properly
    co_await server_transport.Close();
  });
}

// Test basic client-server connection
TEST_CASE("PipeTransport basic client-server connection", "[PipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string socket_path = "/tmp/test_socket_connection";

    // Create a server transport
    spdlog::info("Creating server transport");
    jsonrpc::transport::PipeTransport server_transport(
        executor, socket_path, true);
    spdlog::info("Server transport created");
    // Create a client transport that connects to the server
    spdlog::info("Creating client transport");
    jsonrpc::transport::PipeTransport client_transport(
        executor, socket_path, false);

    // Just wait a bit to ensure connection is established
    co_await asio::steady_timer(executor, std::chrono::milliseconds(100))
        .async_wait(asio::use_awaitable);

    // Close both transports
    co_await client_transport.Close();
    co_await server_transport.Close();
  });
}

TEST_CASE(
    "PipeTransport starts server and client communication", "[PipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string socket_path = "/tmp/test_socket";
    auto strand = asio::make_strand(executor);

    // Start server thread
    asio::co_spawn(
        strand,
        [executor, socket_path]() -> asio::awaitable<void> {
          jsonrpc::transport::PipeTransport server_transport(
              executor, socket_path, true);
          auto start_result = co_await server_transport.Start();
          REQUIRE(start_result);

          auto received_message = co_await server_transport.ReceiveMessage();
          REQUIRE(received_message == "Hello, Server!");

          auto close_result = co_await server_transport.Close();
          REQUIRE(close_result);
        },
        asio::detached);

    // Start the client thread
    asio::co_spawn(
        strand,
        [executor, socket_path]() -> asio::awaitable<void> {
          jsonrpc::transport::PipeTransport client_transport(
              executor, socket_path, false);
          auto start_result = co_await client_transport.Start();
          REQUIRE(start_result);

          co_await client_transport.SendMessage("Hello, Server!");

          auto close_result = co_await client_transport.Close();
          REQUIRE(close_result);
        },
        asio::detached);

    // Wait for both threads to complete
    co_await asio::steady_timer(executor, std::chrono::seconds(1))
        .async_wait(asio::use_awaitable);
  });
}

TEST_CASE("PipeTransport throws on invalid socket path", "[PipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    try {
      jsonrpc::transport::PipeTransport transport(
          executor, "/tmp/non_existent_socket", false);
      co_await transport.Close();
    } catch (const std::exception& e) {
      REQUIRE(std::string(e.what()) == "Error during connect");
    }
  });
}

TEST_CASE("PipeTransport handles multiple messages", "[PipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    std::string socket_path = "/tmp/test_socket_multi";
    auto strand = asio::make_strand(executor);

    // Start server thread
    asio::co_spawn(
        strand,
        [executor, socket_path]() -> asio::awaitable<void> {
          jsonrpc::transport::PipeTransport server_transport(
              executor, socket_path, true);
          auto start_result = co_await server_transport.Start();
          REQUIRE(start_result);

          for (int i = 0; i < 10; ++i) {
            auto received_message = co_await server_transport.ReceiveMessage();
            REQUIRE(received_message == "Hello, Server! " + std::to_string(i));
          }

          auto close_result = co_await server_transport.Close();
          REQUIRE(close_result);
        },
        asio::detached);

    // Start the client thread
    asio::co_spawn(
        strand,
        [executor, socket_path]() -> asio::awaitable<void> {
          jsonrpc::transport::PipeTransport client_transport(
              executor, socket_path, false);
          auto start_result = co_await client_transport.Start();
          REQUIRE(start_result);

          for (int i = 0; i < 10; ++i) {
            co_await client_transport.SendMessage(
                "Hello, Server! " + std::to_string(i));
          }

          auto close_result = co_await client_transport.Close();
          REQUIRE(close_result);
        },
        asio::detached);

    // Wait for both threads to complete
    co_await asio::steady_timer(executor, std::chrono::seconds(1))
        .async_wait(asio::use_awaitable);
  });
}
