#pragma once

#include <asio.hpp>
#include <asio/local/stream_protocol.hpp>
#include <string>

#include "jsonrpc/transport/framed_transport.hpp"
#include "jsonrpc/transport/pipe_transport.hpp"

namespace jsonrpc::transport {

/**
 * @brief Transport layer using Asio Unix domain sockets for JSON-RPC
 * communication with framing.
 */
class FramedPipeTransport : public PipeTransport, protected FramedTransport {
 public:
  FramedPipeTransport(const std::string& socket_path, bool is_server);

  auto SendMessage(const std::string& message) -> std::future<void> override;
  auto ReceiveMessage() -> std::future<std::string> override;

 private:
  // Helper methods to reduce cognitive complexity
  auto ReadFramedMessage(std::chrono::milliseconds timeout) -> std::string;
  void ReadHeaders(
      asio::streambuf& buffer, std::chrono::steady_clock::time_point deadline,
      asio::error_code& ec);
  void ReadContent(
      asio::streambuf& buffer, int content_length,
      std::chrono::steady_clock::time_point deadline, asio::error_code& ec);
};

}  // namespace jsonrpc::transport
