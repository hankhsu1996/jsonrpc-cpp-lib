#pragma once

#include <cstddef>
#include <istream>
#include <ostream>
#include <string>
#include <unordered_map>

namespace jsonrpc::transport {

class FramedTransportTest;

/**
 * @brief Base class for framed transport mechanisms.
 *
 * Provides modular functionality for sending and receiving framed messages.
 */
class FramedTransport {
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

  static auto ReadHeadersFromStream(std::istream &input) -> HeaderMap;
  static auto ReadContentLengthFromStream(std::istream &input) -> int;

  /**
   * @brief Reads content from the input stream based on the content length.
   *
   * @param input The input stream to read the content from.
   * @param content_length The length of the content to be read.
   * @return The content as a string.
   */
  static auto ReadContent(std::istream &input, std::size_t content_length)
      -> std::string;

  /**
   * @brief Receives a framed message.
   *
   * Reads headers to determine the content length, then reads the message
   * content based on that length.
   *
   * @param input The input stream to read the framed message.
   * @return The received message content.
   */
  static auto ReceiveFramedMessage(std::istream &input) -> std::string;

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
