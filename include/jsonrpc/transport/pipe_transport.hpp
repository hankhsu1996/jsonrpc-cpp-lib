#pragma once

#include <array>
#include <atomic>
#include <expected>
#include <string>

#include <asio.hpp>
#include <asio/local/stream_protocol.hpp>

#include "jsonrpc/error/error.hpp"
#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::transport {

class PipeTransport : public Transport {
 public:
  explicit PipeTransport(
      asio::any_io_executor executor, std::string socket_path,
      bool is_server = false, std::shared_ptr<spdlog::logger> logger = nullptr);

  ~PipeTransport() override;

  PipeTransport(const PipeTransport&) = delete;
  auto operator=(const PipeTransport&) -> PipeTransport& = delete;

  PipeTransport(PipeTransport&&) = delete;
  auto operator=(PipeTransport&&) -> PipeTransport& = delete;

  auto Start()
      -> asio::awaitable<std::expected<void, error::RpcError>> override;

  auto Close()
      -> asio::awaitable<std::expected<void, error::RpcError>> override;

  void CloseNow() override;

  auto SendMessage(std::string message)
      -> asio::awaitable<std::expected<void, error::RpcError>> override;

  auto ReceiveMessage()
      -> asio::awaitable<std::expected<std::string, error::RpcError>> override;

 protected:
  auto GetSocket() -> asio::local::stream_protocol::socket&;

  auto RemoveExistingSocketFile() -> std::expected<void, error::RpcError>;

  auto Connect() -> asio::awaitable<std::expected<void, error::RpcError>>;

  auto BindAndListen() -> asio::awaitable<std::expected<void, error::RpcError>>;

 private:
  asio::local::stream_protocol::socket socket_;
  std::unique_ptr<asio::local::stream_protocol::acceptor> acceptor_;
  std::string socket_path_;
  bool is_server_;
  std::atomic<bool> is_closed_{false};
  std::atomic<bool> is_started_{false};
  std::atomic<bool> is_connected_{false};

  // Buffer for reading data
  std::array<char, 1024> read_buffer_;
  std::string message_buffer_;
};

}  // namespace jsonrpc::transport
