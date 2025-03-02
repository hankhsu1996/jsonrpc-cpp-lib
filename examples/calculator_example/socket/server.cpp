#include <memory>

#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/socket_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "../calculator.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::SocketTransport;
using Json = nlohmann::json;

auto main() -> int {
  auto logger = spdlog::basic_logger_mt("server", "logs/server.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  const std::string host = "0.0.0.0";
  const uint16_t port = 12345;

  auto transport = std::make_unique<SocketTransport>(host, port, true);
  RpcEndpoint server(std::move(transport));

  server.RegisterMethodCall("add", [](const std::optional<Json> &params) {
    return Calculator::Add(params.value());
  });

  server.RegisterMethodCall("divide", [](const std::optional<Json> &params) {
    return Calculator::Divide(params.value());
  });

  server.RegisterNotification(
      "stop", [&server](const std::optional<Json> &) { server.Stop(); });

  server.Start();
  return 0;
}
