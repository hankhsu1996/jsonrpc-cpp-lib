#pragma once

#include <array>
#include <atomic>
#include <string>

#include <asio.hpp>

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
   * @param io_context The io_context to use for async operations.
   * @param address The host address (IP or domain name).
   * @param port The port number.
   * @param is_server True if the transport acts as a server; false if it acts
   * as a client.
   */
  SocketTransport(
      asio::io_context& io_context, std::string address, uint16_t port,
      bool is_server);

  /**
   * @brief Destroys the Socket Transport object.
   */
  ~SocketTransport() override;

  SocketTransport(const SocketTransport&) = delete;
  auto operator=(const SocketTransport&) -> SocketTransport& = delete;

  SocketTransport(SocketTransport&&) = delete;
  auto operator=(SocketTransport&&) -> SocketTransport& = delete;

  /**
   * @brief Start the transport
   *
   * For server: Binds to the specified address/port and starts listening
   * For client: Connects to the specified server address/port
   *
   * @return asio::awaitable<void>
   */
  auto Start() -> asio::awaitable<void> override;

  // Implement pure virtual functions from Transport
  auto SendMessage(const std::string& message)
      -> asio::awaitable<void> override;

  auto ReceiveMessage() -> asio::awaitable<std::string> override;

  auto Close() -> asio::awaitable<void> override;

  /**
   * @brief Close the transport synchronously.
   *
   * Safe to use in destructors. Immediately cancels operations and closes
   * socket connections.
   */
  void CloseNow() override;

  /**
   * @brief Gets access to the underlying socket.
   * @return A reference to the socket.
   */
  auto GetSocket() -> asio::ip::tcp::socket&;

 private:
  /**
   * @brief Connects to a remote endpoint if in client mode.
   * @return Awaitable that completes when connected.
   */
  auto Connect() -> asio::awaitable<void>;

  /**
   * @brief Binds to a local endpoint and listens if in server mode.
   * @return Awaitable that completes when a client connects.
   */
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
