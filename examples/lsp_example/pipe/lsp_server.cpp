#include <string>

#include <jsonrpc/endpoint/endpoint.hpp>
#include <jsonrpc/transport/framed_pipe_transport.hpp>
#include <spdlog/spdlog.h>

#include "../utils.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedPipeTransport;

auto main(int argc, char* argv[]) -> int {
  try {
    std::vector<std::string> args(argv, argv + argc);
    std::string pipe_name = ParsePipeArguments(args);
    SetupLogger();

    auto transport = std::make_unique<FramedPipeTransport>(pipe_name, false);
    RpcEndpoint server(std::move(transport));

    RegisterLSPHandlers(server);

    spdlog::info("Starting LSP server...");
    server.Start();

  } catch (const std::exception& ex) {
    spdlog::error("Exception: {}", ex.what());
    return 1;
  }

  return 0;
}
