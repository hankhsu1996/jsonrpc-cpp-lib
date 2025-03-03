#include <memory>

#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/pipe_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "../calculator.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::PipeTransport;
using Json = nlohmann::json;

auto main() -> int {
  auto logger = spdlog::basic_logger_mt("server", "logs/server.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  const std::string socket_path = "/tmp/calculator_pipe";
  spdlog::info("Starting server on socket: {}", socket_path);

  auto transport = std::make_unique<PipeTransport>(socket_path, true);
  RpcEndpoint server(std::move(transport));

  server.RegisterMethodCall("add", [](const std::optional<Json>& params) {
    spdlog::debug(
        "Handling 'add' method call with params: {}",
        params.has_value() ? params.value().dump() : "null");
    auto result = Calculator::Add(params.value());
    spdlog::debug("Add result: {}", result.dump());
    return result;
  });

  server.RegisterMethodCall("divide", [](const std::optional<Json>& params) {
    spdlog::debug(
        "Handling 'divide' method call with params: {}",
        params.has_value() ? params.value().dump() : "null");
    auto result = Calculator::Divide(params.value());
    spdlog::debug("Divide result: {}", result.dump());
    return result;
  });

  server.RegisterNotification("stop", [&server](const std::optional<Json>&) {
    spdlog::info("Received stop notification, shutting down");
    server.Shutdown().get();
  });

  // Server pattern: Start() + Wait()
  spdlog::info("Server started, waiting for requests...");
  server.Start();

  // Wait for server to finish
  spdlog::info("Waiting for server to finish...");
  server.Wait();  // Blocks until Shutdown() called

  spdlog::info("Server shutdown complete");
  return 0;
}
