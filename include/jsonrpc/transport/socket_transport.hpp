#pragma once

#include <array>
#include <asio.hpp>
#include <atomic>
#include <string>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::transport {

/**
 * @brief Transport implementation using TCP/IP sockets.
 *
 * This class provides transport functionality over TCP/IP sockets,
 * supporting both client and server modes for communication over a network.
 */
class SocketTransport : public Transport {
 public:
  /**
   * @brief Constructs a SocketTransport.
   * @param host The host address (IP or domain name).
   * @param port The port number.
   * @param isServer True if the transport acts as a server; false if it acts as
   * a client.
   * @param external_io_context Optional external io_context to use. If nullptr,
   * creates an internal io_context.
   */
  SocketTransport(
      std::string host, uint16_t port, bool is_server,
      asio::io_context *external_io_context = nullptr);

  ~SocketTransport() override;

  SocketTransport(const SocketTransport &) = delete;
  auto operator=(const SocketTransport &) -> SocketTransport & = delete;

  SocketTransport(SocketTransport &&) = delete;
  auto operator=(SocketTransport &&) -> SocketTransport & = delete;

  auto SendMessage(const std::string &message) -> std::future<void> override;
  auto ReceiveMessage() -> std::future<std::string> override;
  auto Close() -> std::future<void> override;

 protected:
  auto GetSocket() -> asio::ip::tcp::socket &;

  // Override the async methods from Transport to provide efficient
  // implementations
  void DoAsyncSendMessage(
      const std::string &message, SendHandler handler) override;
  void DoAsyncReceiveMessage(ReceiveHandler handler) override;
  void DoAsyncClose(CloseHandler handler) override;

 private:
  auto Connect() -> std::future<void>;
  auto BindAndListen() -> std::future<void>;

  asio::io_context::strand strand_;
  asio::ip::tcp::socket socket_;
  std::string host_;
  uint16_t port_;
  bool is_server_;
  std::atomic<bool> is_closed_{false};

  // Buffer for receiving data
  std::array<char, 1024> read_buffer_;
  std::string message_buffer_;
};

}  // namespace jsonrpc::transport
