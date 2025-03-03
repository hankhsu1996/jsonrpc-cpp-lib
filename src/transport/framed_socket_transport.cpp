#include "jsonrpc/transport/framed_socket_transport.hpp"

#include <future>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

FramedSocketTransport::FramedSocketTransport(
    const std::string& host, uint16_t port, bool is_server)
    : SocketTransport(host, port, is_server), FramedTransport() {
  spdlog::info(
      "FramedSocketTransport initialized with host: {} and port: {}", host,
      port);
}

auto FramedSocketTransport::SendMessage(const std::string& message)
    -> std::future<void> {
  return std::async(std::launch::async, [this, message]() {
    try {
      // Use the common framing functionality from FramedTransport
      std::ostringstream message_stream;
      FrameMessage(message_stream, message);
      std::string framed_message = message_stream.str();

      asio::error_code ec;
      std::size_t bytes_written =
          asio::write(GetSocket(), asio::buffer(framed_message), ec);

      if (ec) {
        throw std::runtime_error("Error sending message: " + ec.message());
      }

      spdlog::info(
          "FramedSocketTransport sent message with {} bytes", bytes_written);
    } catch (const std::exception& e) {
      spdlog::error(
          "FramedSocketTransport failed to send message: {}", e.what());
      throw;
    }
  });
}

auto FramedSocketTransport::ReceiveMessage() -> std::future<std::string> {
  return std::async(std::launch::async, [this]() {
    // Default timeout of 30 seconds
    std::chrono::milliseconds timeout(30000);
    return ReadFramedMessage(timeout);
  });
}

auto FramedSocketTransport::ReadFramedMessage(std::chrono::milliseconds timeout)
    -> std::string {
  asio::streambuf buffer;
  asio::error_code ec;
  GetSocket().non_blocking(true);

  auto deadline = std::chrono::steady_clock::now() + timeout;

  // Read headers until \r\n\r\n delimiter
  ReadHeaders(buffer, deadline, ec);

  // Use FramedTransport to process the message
  std::istream stream(&buffer);

  try {
    // Extract content length from the headers
    int content_length = ReadContentLengthFromStream(stream);

    // Read remaining content
    ReadRemainingContent(buffer, content_length, deadline, ec);

    // Extract the message content
    std::string content;
    content.reserve(content_length);

    std::istream content_stream(&buffer);
    content.assign(
        std::istreambuf_iterator<char>(content_stream),
        std::istreambuf_iterator<char>());

    return content;
  } catch (const std::exception& e) {
    throw std::runtime_error(
        std::string("Failed to read framed message: ") + e.what());
  }
}

void FramedSocketTransport::ReadHeaders(
    asio::streambuf& buffer, std::chrono::steady_clock::time_point deadline,
    asio::error_code& ec) {
  while (std::chrono::steady_clock::now() < deadline) {
    asio::read_until(GetSocket(), buffer, kHeaderDelimiter, ec);
    if (ec == asio::error::would_block) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (ec) {
      throw std::runtime_error(
          "Failed to read message headers: " + ec.message());
    }
    return;
  }
  throw std::runtime_error("Timeout waiting for response");
}

void FramedSocketTransport::ReadRemainingContent(
    asio::streambuf& buffer, int content_length,
    std::chrono::steady_clock::time_point deadline, asio::error_code& ec) {
  // Calculate how much more content we need to read
  std::size_t remaining_content_length = content_length - buffer.size();

  // Read any remaining content directly into the buffer
  if (remaining_content_length > 0) {
    while (std::chrono::steady_clock::now() < deadline) {
      asio::read(GetSocket(), buffer.prepare(remaining_content_length), ec);
      if (ec == asio::error::would_block) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      if (ec && ec != asio::error::eof) {
        throw std::runtime_error(
            "Failed to read message content: " + ec.message());
      }
      buffer.commit(remaining_content_length);
      return;
    }
    throw std::runtime_error("Timeout waiting for response");
  }
}

}  // namespace jsonrpc::transport
