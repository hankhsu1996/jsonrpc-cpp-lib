#include <jsonrpc/transport/framed_socket_transport.hpp>
#include <spdlog/spdlog.h>

#include "../utils.hpp"

using jsonrpc::endpoint::RpcEndpoint;
using jsonrpc::transport::FramedSocketTransport;

auto main() -> int {
  try {
    SetupLogger();

    // The 'false' argument indicates that the transport is acting as a client.
    // In this setup, VS Code creates and owns the socket, and the LSP server
    // (this process) connects to the socket as a client.
    auto transport =
        std::make_unique<FramedSocketTransport>("localhost", 2087, false);
    RpcEndpoint server(std::move(transport));

    RegisterLSPHandlers(server);
    spdlog::info("Starting LSP server...");
    server.Start();
    server.Wait();

    return 0;

  } catch (const std::exception& ex) {
    spdlog::error("Fatal error: {}", ex.what());
    return 1;
  }
}
