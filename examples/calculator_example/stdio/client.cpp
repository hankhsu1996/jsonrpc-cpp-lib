#include <memory>

#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/stdio_transport.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::StdioTransport;
using Json = nlohmann::json;

auto main() -> int {
  auto logger = spdlog::basic_logger_mt("client", "logs/client.log");
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  auto transport = std::make_unique<StdioTransport>();
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
