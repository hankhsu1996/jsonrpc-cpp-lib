#pragma once

#include <array>
#include <asio.hpp>
#include <asio/local/stream_protocol.hpp>
#include <atomic>
#include <string>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::transport {

/**
 * @brief Transport implementation using Unix domain sockets.
 *
 * This class provides transport functionality over Unix domain sockets,
 * supporting local communication between processes on the same machine.
 */
class PipeTransport : public Transport {
 public:
  /**
   * @brief Constructs a PipeTransport.
   *
   * @param socket_path The path to the Unix domain socket.
   * @param is_server True if the transport acts as a server; false if it acts
   * as a client.
   */
  explicit PipeTransport(std::string socket_path, bool is_server = false);

  ~PipeTransport() override;

  PipeTransport(const PipeTransport &) = delete;
  auto operator=(const PipeTransport &) -> PipeTransport & = delete;

  PipeTransport(PipeTransport &&) = delete;
  auto operator=(PipeTransport &&) -> PipeTransport & = delete;

  auto SendMessage(const std::string &message) -> std::future<void> override;
  auto ReceiveMessage() -> std::future<std::string> override;
  auto Close() -> std::future<void> override;

  /**
   * @brief Gets the underlying socket.
   * @return A reference to the socket.
   */
  auto GetSocket() -> asio::local::stream_protocol::socket &;

 protected:
  void RemoveExistingSocketFile();
  auto Connect() -> std::future<void>;
  auto BindAndListen() -> std::future<void>;

 private:
  asio::io_context io_context_;
  asio::local::stream_protocol::socket socket_;
  std::string socket_path_;
  bool is_server_;
  std::atomic<bool> is_closed_{false};

  // Buffer for receiving data
  std::array<char, 1024> read_buffer_;
  std::string message_buffer_;
};

}  // namespace jsonrpc::transport
