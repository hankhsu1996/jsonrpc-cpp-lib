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
   * @param external_io_context Optional external io_context to use. If nullptr,
   * creates an internal io_context.
   */
  explicit PipeTransport(
      std::string socket_path, bool is_server = false,
      asio::io_context *external_io_context = nullptr);

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

  // Override the async methods from Transport to provide efficient
  // implementations
  void DoAsyncSendMessage(
      const std::string &message, SendHandler handler) override;
  void DoAsyncReceiveMessage(ReceiveHandler handler) override;
  void DoAsyncClose(CloseHandler handler) override;

 private:
  asio::io_context::strand strand_;
  asio::local::stream_protocol::socket socket_;
  std::string socket_path_;
  bool is_server_;
  std::atomic<bool> is_closed_{false};

  // Buffer for receiving data
  std::array<char, 1024> read_buffer_;
  std::string message_buffer_;
};

}  // namespace jsonrpc::transport
