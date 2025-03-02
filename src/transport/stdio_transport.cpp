#include "jsonrpc/transport/stdio_transport.hpp"

#include <iostream>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

void StdioTransport::SendMessage(const std::string &message) {
  if (is_closed_) {
    throw std::runtime_error("Transport is closed");
  }
  spdlog::debug("StdioTransport sending message: {}", message);
  std::cout << message << std::endl;
}

auto StdioTransport::ReceiveMessage() -> std::string {
  if (is_closed_) {
    throw std::runtime_error("Transport is closed");
  }
  std::string response;
  if (!std::getline(std::cin, response)) {
    throw std::runtime_error("Failed to receive message");
  }
  spdlog::debug("StdioTransport received response: {}", response);
  return response;
}

void StdioTransport::Close() {
  is_closed_ = true;
  spdlog::info("Stdio transport closed");
}

}  // namespace jsonrpc::transport
