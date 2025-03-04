#include <asio.hpp>
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

    // Create a shared io_context for all components
    asio::io_context io_context;

    // The 'false' argument indicates that the transport is acting as a client.
    // In this setup, VS Code creates and owns the pipe, and the LSP server
    // (this process) connects to the pipe as a client.
    auto transport =
        std::make_unique<FramedPipeTransport>(pipe_name, false, &io_context);

    RpcEndpoint server(std::move(transport));

    RegisterLSPHandlers(server);
    server.Start();

    // Run the io_context in the main thread
    asio::executor_work_guard<asio::io_context::executor_type> work_guard(
        io_context.get_executor());
    io_context.run();

    server.Wait();

    return 0;

  } catch (const std::exception& ex) {
    spdlog::error("Fatal error: {}", ex.what());
    return 1;
  }
}
