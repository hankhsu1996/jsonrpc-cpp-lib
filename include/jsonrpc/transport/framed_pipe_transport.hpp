#pragma once

#include <asio.hpp>
#include <asio/local/stream_protocol.hpp>
#include <string>

#include "jsonrpc/transport/framed_transport.hpp"
#include "jsonrpc/transport/pipe_transport.hpp"

namespace jsonrpc::transport {

/**
 * @brief Transport layer using Asio Unix domain sockets for JSON-RPC
 * communication with framing.
 */
class FramedPipeTransport : public PipeTransport, protected FramedTransport {
 public:
  /**
   * @brief Constructs a FramedPipeTransport.
   *
   * @param socket_path The path to the Unix domain socket.
   * @param is_server True if the transport acts as a server; false if it acts
   * as a client.
   * @param external_io_context Optional external io_context to use. If nullptr,
   * creates an internal io_context.
   */
  FramedPipeTransport(
      const std::string& socket_path, bool is_server,
      asio::io_context* external_io_context = nullptr);

  auto SendMessage(const std::string& message) -> std::future<void> override;
  auto ReceiveMessage() -> std::future<std::string> override;

 protected:
  /**
   * @brief Asynchronously sends a framed message.
   * @param message The message to send.
   * @param handler The callback to invoke after sending.
   */
  void DoAsyncSendMessage(
      const std::string& message, SendHandler handler) override;

  /**
   * @brief Asynchronously receives a framed message.
   * @param handler The callback to invoke after receiving.
   */
  void DoAsyncReceiveMessage(ReceiveHandler handler) override;

  /**
   * @brief Asynchronously closes the transport.
   * @param handler The callback to invoke after closing.
   */
  void DoAsyncClose(CloseHandler handler) override;

 private:
  // Helper methods to reduce cognitive complexity
  auto ReadFramedMessage(std::chrono::milliseconds timeout) -> std::string;
  void ReadHeaders(
      asio::streambuf& buffer, std::chrono::steady_clock::time_point deadline,
      asio::error_code& ec);
  void ReadContent(
      asio::streambuf& buffer, int content_length,
      std::chrono::steady_clock::time_point deadline, asio::error_code& ec);
};

}  // namespace jsonrpc::transport
