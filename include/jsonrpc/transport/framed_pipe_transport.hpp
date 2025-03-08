#pragma once

#include <string>

#include <asio.hpp>

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
   * @param io_context The io_context to use for async operations.
   * @param socket_path The path to the Unix domain socket.
   * @param is_server True if the transport acts as a server; false if it acts
   * as a client.
   */
  FramedPipeTransport(
      asio::io_context& io_context, const std::string& socket_path,
      bool is_server);

  // Delete copy and move constructors/assignments
  FramedPipeTransport(const FramedPipeTransport&) = delete;
  auto operator=(const FramedPipeTransport&) -> FramedPipeTransport& = delete;
  FramedPipeTransport(FramedPipeTransport&&) = delete;
  auto operator=(FramedPipeTransport&&) -> FramedPipeTransport& = delete;

  ~FramedPipeTransport() override;

  /**
   * @brief Start the transport
   *
   * Calls PipeTransport::Start() to set up socket connections
   *
   * @return asio::awaitable<void>
   */
  auto Start() -> asio::awaitable<void> override;

  // Override virtual methods from PipeTransport
  auto SendMessage(const std::string& message)
      -> asio::awaitable<void> override;
  auto ReceiveMessage() -> asio::awaitable<std::string> override;
  auto Close() -> asio::awaitable<void> override;

  /**
   * @brief Close the transport synchronously.
   *
   * Safe to use in destructors. Delegates to the parent class.
   */
  void CloseNow() override;

 private:
  /**
   * @brief Reads headers from the transport.
   *
   * @param buffer The buffer to read from.
   * @return Awaitable that completes when headers are read.
   */
  auto ReadHeaders(asio::streambuf& buffer) -> asio::awaitable<void>;

  /**
   * @brief Reads content from the transport.
   *
   * @param buffer The buffer to read from.
   * @param content_length The length of the content to read.
   * @return Awaitable that completes with the content.
   */
  auto ReadContent(asio::streambuf& buffer, int content_length)
      -> asio::awaitable<std::string>;

  /**
   * @brief Reads headers from a buffer.
   *
   * @param buffer The buffer to read from.
   * @return Header map.
   */
  static auto ReadHeadersFromBuffer(asio::streambuf& buffer)
      -> FramedTransport::HeaderMap;

  /**
   * @brief Reads content length from headers.
   *
   * @param headers The headers to read from.
   * @return Content length.
   */
  static auto ReadContentLength(const FramedTransport::HeaderMap& headers)
      -> int;
};

}  // namespace jsonrpc::transport
