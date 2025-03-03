#include "jsonrpc/transport/framed_pipe_transport.hpp"

#include <future>
#include <stdexcept>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

FramedPipeTransport::FramedPipeTransport(
    const std::string& socket_path, bool is_server)
    : PipeTransport(socket_path, is_server) {
  spdlog::info(
      "FramedPipeTransport initialized with socket path: {}", socket_path);
}

auto FramedPipeTransport::SendMessage(const std::string& message)
    -> std::future<void> {
  return std::async(std::launch::async, [this, message]() {
    try {
      asio::streambuf message_buf;
      std::ostream message_stream(&message_buf);
      FrameMessage(message_stream, message);

      asio::error_code ec;
      std::size_t bytes_written =
          asio::write(GetSocket(), message_buf.data(), ec);

      if (ec) {
        throw std::runtime_error("Error sending message: " + ec.message());
      }

      spdlog::info(
          "FramedPipeTransport sent message with {} bytes", bytes_written);
    } catch (const std::exception& e) {
      spdlog::error("FramedPipeTransport failed to send message: {}", e.what());
      throw;
    }
  });
}

auto FramedPipeTransport::ReceiveMessage() -> std::future<std::string> {
  return std::async(std::launch::async, [this]() {
    // Default timeout of 30 seconds
    std::chrono::milliseconds timeout(30000);
    return ReadFramedMessage(timeout);
  });
}

auto FramedPipeTransport::ReadFramedMessage(std::chrono::milliseconds timeout)
    -> std::string {
  asio::streambuf buffer;
  asio::error_code ec;
  GetSocket().non_blocking(true);

  auto deadline = std::chrono::steady_clock::now() + timeout;

  // Read headers until \r\n\r\n delimiter
  ReadHeaders(buffer, deadline, ec);

  // Use FramedTransport to process the message
  std::istream stream(&buffer);

  // Try to use ReceiveFramedMessage directly if possible
  try {
    // Extract content length from the headers
    int content_length = ReadContentLengthFromStream(stream);

    // Read remaining content
    ReadContent(buffer, content_length, deadline, ec);

    // Extract the message content
    std::string content(
        asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
    return content;
  } catch (const std::exception& e) {
    throw std::runtime_error(
        std::string("Failed to read framed message: ") + e.what());
  }
}

void FramedPipeTransport::ReadHeaders(
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

void FramedPipeTransport::ReadContent(
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
