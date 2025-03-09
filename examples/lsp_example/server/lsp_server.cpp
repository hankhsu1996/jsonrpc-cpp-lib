#include <iostream>
#include <memory>
#include <string>

#include <asio.hpp>
#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

using Json = nlohmann::json;
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

auto ParsePipeArguments(const std::vector<std::string>& args) -> std::string {
  const std::string pipe_prefix = "--pipe=";
  if (args.size() < 2 || args[1].rfind(pipe_prefix, 0) != 0) {
    throw std::invalid_argument("Usage: <executable> --pipe=<pipe name>");
  }
  return args[1].substr(pipe_prefix.length());
}

void RegisterLSPHandlers(jsonrpc::endpoint::RpcEndpoint& server) {
  server.RegisterMethodCall(
      "initialize", [](const std::optional<Json>&) -> asio::awaitable<Json> {
        spdlog::info("LSP Server initialized");
        Json response = {
            {"capabilities",
             {{"positionEncoding", "utf-16"},
              {"textDocumentSync",
               {{"openClose", true},
                {"change", 1},
                {"save", {{"includeText", false}}}}},
              {"completionProvider",
               {{"resolveProvider", false}, {"triggerCharacters", {" "}}}}}},
            {"serverInfo",
             {{"name", "LSP Example Server"}, {"version", "1.0"}}}};
        co_return response;
      });

  server.RegisterNotification(
      "initialized", [](const std::optional<Json>&) -> asio::awaitable<void> {
        spdlog::info("Client initialized");
        co_return;
      });

  server.RegisterMethodCall(
      "textDocument/completion",
      [](const std::optional<Json>& params) -> asio::awaitable<Json> {
        Json response;
        if (params && params->contains("textDocument") &&
            params->contains("position")) {
          response = Json::array(
              {{{"label", "world"}, {"kind", 1}, {"insertText", "world"}}});
        } else {
          response = Json::array();
        }
        co_return response;
      });

  server.RegisterMethodCall(
      "shutdown", [](const std::optional<Json>&) -> asio::awaitable<Json> {
        spdlog::info("Server shutting down");
        co_return Json::object();
      });

  server.RegisterNotification(
      "exit", [&server](const std::optional<Json>&) -> asio::awaitable<void> {
        spdlog::info("Server exiting");
        co_await server.Shutdown();
      });
}

// Main server logic encapsulated in a function
auto RunLSPServer(asio::io_context& io_context, const std::string& pipe_name)
    -> asio::awaitable<void> {
  // Step 1: Create transport
  auto transport =
      std::make_unique<FramedPipeTransport>(io_context, pipe_name, false);

  // Step 2: Create RPC endpoint
  RpcEndpoint server(io_context, std::move(transport));

  // Step 3: Register LSP method handlers
  RegisterLSPHandlers(server);

  // Step 4: Start server
  co_await server.Start();

  // Step 5: Wait for server shutdown
  co_await server.WaitForShutdown();

  spdlog::info("Server shutdown monitoring complete");
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

auto main(int argc, char* argv[]) -> int {
  // Step 1: Setup logging
  auto cout_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(std::cout);
  auto logger = std::make_shared<spdlog::logger>("server_logger", cout_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  // Step 2: Parse command line arguments
  const std::vector<std::string> args(argv, argv + argc);
  const std::string pipe_name = ParsePipeArguments(args);
  spdlog::info("Starting LSP server on pipe: {}", pipe_name);

  // Step 3: Create an io_context for asio operations
  asio::io_context io_context;

  // Step 4: Launch the server with error handling
  asio::co_spawn(io_context, RunLSPServer(io_context, pipe_name), HandleError);

  // Step 5: Run the io_context
  io_context.run();

  spdlog::info("Server shutdown complete");
  return 0;
}
