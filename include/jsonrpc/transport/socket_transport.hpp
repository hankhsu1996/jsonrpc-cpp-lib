#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <string>

#include <asio.hpp>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::transport {

class SocketTransport : public Transport {
 public:
  SocketTransport(
      asio::any_io_executor executor, std::string address, uint16_t port,
      bool is_server, std::shared_ptr<spdlog::logger> logger = nullptr);

  ~SocketTransport() override;

  SocketTransport(const SocketTransport&) = delete;
  auto operator=(const SocketTransport&) -> SocketTransport& = delete;

  SocketTransport(SocketTransport&&) = delete;
  auto operator=(SocketTransport&&) -> SocketTransport& = delete;

  auto Start()
      -> asio::awaitable<std::expected<void, error::RpcError>> override;

  auto Close()
      -> asio::awaitable<std::expected<void, error::RpcError>> override;

  auto CloseNow() -> void override;

  auto SendMessage(std::string message)
      -> asio::awaitable<std::expected<void, error::RpcError>> override;

  auto ReceiveMessage()
      -> asio::awaitable<std::expected<std::string, error::RpcError>> override;

 private:
  auto GetSocket() -> asio::ip::tcp::socket&;

  auto Connect() -> asio::awaitable<std::expected<void, error::RpcError>>;

  auto BindAndListen() -> asio::awaitable<std::expected<void, error::RpcError>>;

  // Sends messages from the queue in sequence
  auto SendMessageLoop() -> asio::awaitable<void>;

  asio::ip::tcp::socket socket_;
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
  std::string address_;
  uint16_t port_;
  bool is_server_;
  std::atomic<bool> is_closed_{false};
  std::atomic<bool> is_started_{false};
  std::atomic<bool> is_connected_{false};

  // Message sending queue and state
  std::deque<std::string> send_queue_;
  std::atomic<bool> sending_{false};

  // Buffer for reading data
  std::array<char, 1024> read_buffer_;
  std::string message_buffer_;
};

}  // namespace jsonrpc::transport
