#pragma once

#include <array>
#include <atomic>
#include <string>

#include <asio.hpp>
#include <asio/local/stream_protocol.hpp>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::transport {

/**
 * @brief Unix domain socket transport implementation.
 *
 * This transport uses Unix domain sockets to communicate between endpoints.
 */
class PipeTransport : public Transport {
 public:
  /**
   * @brief Constructs a new Pipe Transport object.
   *
   * @param io_context The IO context to use.
   * @param socket_path The path to the socket file.
   * @param is_server Whether this endpoint is a server (true) or client
   * (false).
   */
  explicit PipeTransport(
      asio::io_context& io_context, std::string socket_path,
      bool is_server = false);

  /**
   * @brief Destroys the Pipe Transport object.
   */
  ~PipeTransport() override;

  // Delete copy and move constructors/assignments
  PipeTransport(const PipeTransport&) = delete;
  auto operator=(const PipeTransport&) -> PipeTransport& = delete;

  PipeTransport(PipeTransport&&) = delete;
  auto operator=(PipeTransport&&) -> PipeTransport& = delete;

  /**
   * @brief Gets the underlying socket.
   *
   * @return Reference to the socket
   */
  auto GetSocket() -> asio::local::stream_protocol::socket&;

  /**
   * @brief Start the transport
   *
   * For server: Sets up the socket and begins listening
   * For client: Connects to the server
   *
   * @return asio::awaitable<void>
   */
  auto Start() -> asio::awaitable<void> override;

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

 protected:
  /**
   * @brief Removes the socket file if it exists.
   *
   * This is called before binding to ensure that the socket file doesn't
   * already exist.
   */
  void RemoveExistingSocketFile();

  /**
   * @brief Connects to a server.
   *
   * @return asio::awaitable<void>
   */
  auto Connect() -> asio::awaitable<void>;

  /**
   * @brief Binds to a socket and starts listening.
   *
   * @return asio::awaitable<void>
   */
  auto BindAndListen() -> asio::awaitable<void>;

 private:
  asio::local::stream_protocol::socket socket_;
  std::shared_ptr<asio::local::stream_protocol::acceptor> acceptor_;
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
