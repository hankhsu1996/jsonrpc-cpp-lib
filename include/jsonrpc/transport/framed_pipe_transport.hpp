#pragma once

#include <string>

#include <asio.hpp>

#include "jsonrpc/transport/message_framer.hpp"
#include "jsonrpc/transport/pipe_transport.hpp"

namespace jsonrpc::transport {

class FramedPipeTransport : public PipeTransport {
 public:
  FramedPipeTransport(
      asio::any_io_executor executor, const std::string& socket_path,
      bool is_server, std::shared_ptr<spdlog::logger> logger = nullptr);

  auto SendMessage(std::string message)
      -> asio::awaitable<std::expected<void, error::RpcError>> override;

  auto ReceiveMessage()
      -> asio::awaitable<std::expected<std::string, error::RpcError>> override;

 private:
  std::string read_buffer_;
  MessageFramer framer_;
};

}  // namespace jsonrpc::transport
