#include <memory>

#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "jsonrpc/transport/framed_socket_transport.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedSocketTransport;
using Json = nlohmann::json;

auto main() -> int {
  auto logger = spdlog::basic_logger_mt("client", "logs/client.log", true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  const std::string host = "127.0.0.1";
  const uint16_t port = 12345;
  auto transport = std::make_unique<FramedSocketTransport>(host, port, false);
  RpcEndpoint client(std::move(transport));
  client.Start();

  const int add_op1 = 10;
  const int add_op2 = 5;
  Json add_resp =
      client.SendMethodCall("add", Json({{"a", add_op1}, {"b", add_op2}}));
  spdlog::info("Add result: {}", add_resp.dump());

  const int div_op1 = 10;
  const int div_op2 = 2;
  Json div_resp =
      client.SendMethodCall("divide", Json({{"a", div_op1}, {"b", div_op2}}));
  spdlog::info("Divide result: {}", div_resp.dump());

  client.SendNotification("stop");

  client.Stop();
  return 0;
}
