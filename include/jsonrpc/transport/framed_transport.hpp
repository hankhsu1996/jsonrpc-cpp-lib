#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <unordered_map>

#include <asio.hpp>

namespace jsonrpc::transport {

class FramedTransportTest;

/**
 * @brief Base class for framed transport mechanisms.
 *
 * Provides modular functionality for sending and receiving framed messages.
 */
class FramedTransport {
 public:
  /// @brief A map of headers to their values.
  using HeaderMap = std::unordered_map<std::string, std::string>;

 protected:
  /// @brief The delimiter used to separate headers from the message content.
  static constexpr const char *kHeaderDelimiter = "\r\n\r\n";

  /**
   * @brief Constructs a framed message.
   *
   * Constructs a JSON message as a string with additional headers for
   * Content-Length and Content-Type, similar to HTTP.
   *
   * @param output The output stream to write the framed message.
   * @param message The message to be framed.
   */
  static void FrameMessage(std::ostream &output, const std::string &message);

  /**
   * @brief Reads headers from a buffer.
   * @param buffer The buffer containing the headers.
   * @return A map of header names to values.
   */
  static auto ReadHeadersFromBuffer(asio::streambuf &buffer) -> HeaderMap;

  /**
   * @brief Reads content length from headers.
   * @param headers The headers to read from.
   * @return The content length value.
   */
  static auto ReadContentLength(const HeaderMap &headers) -> int;

  /**
   * @brief Reads content from a buffer.
   * @param buffer The buffer to read from.
   * @param content_length The length of content to read.
   * @return The content as a string.
   */
  static auto ReadContent(asio::streambuf &buffer, std::size_t content_length)
      -> std::string;

  /**
   * @brief Parses a Content-Length header value to an integer.
   * @param header_value The header value to parse.
   * @return The parsed content length.
   * @throws std::runtime_error If the header value is invalid.
   */
  static auto ParseContentLength(const std::string &header_value) -> int;

 private:
  friend class FramedTransportTest;
};

}  // namespace jsonrpc::transport
