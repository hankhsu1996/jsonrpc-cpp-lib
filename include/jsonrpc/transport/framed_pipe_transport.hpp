#pragma once

#include <string>

#include <asio.hpp>

#include "jsonrpc/transport/message_framer.hpp"
#include "jsonrpc/transport/pipe_transport.hpp"

namespace jsonrpc::transport {

/**
 * @brief Transport layer using Asio Unix domain sockets for JSON-RPC
 * communication with framing.
 */
class FramedPipeTransport : public PipeTransport {
 public:
  /**
   * @brief Constructs a FramedPipeTransport.
   *
   * @param executor The executor to use for async operations.
   * @param socket_path The path to the Unix domain socket.
   * @param is_server True if the transport acts as a server; false if it acts
   * as a client.
   */
  FramedPipeTransport(
      asio::any_io_executor executor, const std::string& socket_path,
      bool is_server);

  auto SendMessage(std::string message) -> asio::awaitable<void> override;

  auto ReceiveMessage() -> asio::awaitable<std::string> override;

 private:
  std::string read_buffer_;
  MessageFramer framer_;
};

}  // namespace jsonrpc::transport
