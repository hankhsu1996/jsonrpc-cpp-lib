#pragma once

#include <array>
#include <atomic>
#include <string>

#include <asio.hpp>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::transport {

class SocketTransport : public Transport {
 public:
  SocketTransport(
      asio::any_io_executor executor, std::string address, uint16_t port,
      bool is_server);

  ~SocketTransport() override;

  SocketTransport(const SocketTransport&) = delete;
  auto operator=(const SocketTransport&) -> SocketTransport& = delete;

  SocketTransport(SocketTransport&&) = delete;
  auto operator=(SocketTransport&&) -> SocketTransport& = delete;

  auto Start()
      -> asio::awaitable<std::expected<void, error::RpcError>> override;

  auto SendMessage(std::string message) -> asio::awaitable<void> override;

  auto ReceiveMessage() -> asio::awaitable<std::string> override;

  auto Close() -> asio::awaitable<void> override;

  void CloseNow() override;

  auto GetSocket() -> asio::ip::tcp::socket&;

 private:
  auto Connect() -> asio::awaitable<void>;

  auto BindAndListen() -> asio::awaitable<void>;

  asio::ip::tcp::socket socket_;
  std::string address_;
  uint16_t port_;
  bool is_server_;
  std::atomic<bool> is_closed_{false};
  std::atomic<bool> is_started_{false};
  std::atomic<bool> is_connected_{false};

  // Buffer for reading data
  std::array<char, 1024> read_buffer_;
  std::string message_buffer_;
};

}  // namespace jsonrpc::transport
