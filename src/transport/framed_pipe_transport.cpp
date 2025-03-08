#include "jsonrpc/transport/framed_pipe_transport.hpp"

#include <unistd.h>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

FramedPipeTransport::FramedPipeTransport(
    asio::io_context& io_context, const std::string& socket_path,
    bool is_server)
    : PipeTransport(io_context, socket_path, is_server) {
  spdlog::debug(
      "FramedPipeTransport initialized with socket path: {}", socket_path);
}

FramedPipeTransport::~FramedPipeTransport() {
  try {
    // Always call CloseNow() - it's safe to call multiple times
    CloseNow();
  } catch (const std::exception& e) {
    spdlog::error("Error in FramedPipeTransport destructor: {}", e.what());
  }
}

void FramedPipeTransport::CloseNow() {
  // Just delegate to the parent class implementation
  PipeTransport::CloseNow();
}

auto FramedPipeTransport::Start() -> asio::awaitable<void> {
  try {
    spdlog::debug("Starting FramedPipeTransport");

    // Call the parent (PipeTransport) Start() method to handle socket setup
    co_await PipeTransport::Start();

    spdlog::debug("FramedPipeTransport started successfully");
    co_return;
  } catch (const std::exception& e) {
    spdlog::error("Error starting FramedPipeTransport: {}", e.what());
    throw;
  }
}

auto FramedPipeTransport::SendMessage(const std::string& message)
    -> asio::awaitable<void> {
  try {
    // Create a framed message using a temporary stream
    asio::streambuf message_buf;
    std::ostream message_stream(&message_buf);
    FrameMessage(message_stream, message);

    // Convert the streambuf to a string
    std::string framed_message(
        asio::buffer_cast<const char*>(message_buf.data()), message_buf.size());

    // Send the framed message using the parent class
    co_await PipeTransport::SendMessage(framed_message);
  } catch (const std::exception& e) {
    spdlog::error("Error framing/sending message: {}", e.what());
    throw;
  }
}

auto FramedPipeTransport::ReceiveMessage() -> asio::awaitable<std::string> {
  try {
    // Create a buffer to receive data
    asio::streambuf buffer;

    // Read headers
    co_await ReadHeaders(buffer);

    // Parse headers
    auto headers = ReadHeadersFromBuffer(buffer);

    // Get content length
    int content_length = ReadContentLength(headers);
    if (content_length <= 0) {
      throw std::runtime_error("Invalid content length");
    }

    // Read content
    std::string content = co_await ReadContent(buffer, content_length);

    co_return content;
  } catch (const std::exception& e) {
    spdlog::error("Error receiving framed message: {}", e.what());
    throw;
  }
}

auto FramedPipeTransport::Close() -> asio::awaitable<void> {
  try {
    // Call the parent class Close method
    co_await PipeTransport::Close();

    // Add our own synchronization point for extra safety
    co_await asio::post(GetStrand(), asio::use_awaitable);

    co_return;
  } catch (const std::exception& e) {
    spdlog::error("Error closing FramedPipeTransport: {}", e.what());
    throw;
  }
}

auto FramedPipeTransport::ReadHeaders(asio::streambuf& buffer)
    -> asio::awaitable<void> {
  // Read until we hit the double newline (end of headers)
  std::string delimiter = "\r\n\r\n";
  auto& socket = GetSocket();

  co_await asio::async_read_until(
      socket, buffer, delimiter, asio::use_awaitable);
}

auto FramedPipeTransport::ReadContent(
    asio::streambuf& buffer,
    int content_length) -> asio::awaitable<std::string> {
  auto& socket = GetSocket();

  // Check if we already have enough data in the buffer
  if (buffer.size() < static_cast<std::size_t>(content_length)) {
    // Read more data to get the full content
    co_await asio::async_read(
        socket, buffer, asio::transfer_exactly(content_length - buffer.size()),
        asio::use_awaitable);
  }

  // Extract the content from the buffer
  std::string content(
      asio::buffer_cast<const char*>(buffer.data()) + buffer.size() -
          content_length,
      content_length);

  // Consume the content from the buffer
  buffer.consume(content_length);

  co_return content;
}

auto FramedPipeTransport::ReadHeadersFromBuffer(asio::streambuf& buffer)
    -> FramedTransport::HeaderMap {
  // Create a header map
  FramedTransport::HeaderMap headers;

  // Convert buffer to a string for parsing
  std::string header_str(
      asio::buffer_cast<const char*>(buffer.data()), buffer.size());

  // Find the end of headers marker
  size_t pos = header_str.find("\r\n\r\n");
  if (pos == std::string::npos) {
    return headers;  // No valid headers
  }

  // Parse each header line
  std::istringstream header_stream(header_str.substr(0, pos));
  std::string line;
  while (std::getline(header_stream, line) && !line.empty() && line != "\r") {
    // Remove trailing \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Find the colon separator
    size_t colon_pos = line.find(':');
    if (colon_pos != std::string::npos) {
      std::string key = line.substr(0, colon_pos);
      std::string value = line.substr(colon_pos + 1);

      // Trim whitespace
      value.erase(0, value.find_first_not_of(" \t"));

      headers[key] = value;
    }
  }

  // Consume the headers from the buffer, leaving only the content
  buffer.consume(pos + 4);  // +4 for the \r\n\r\n delimiter

  return headers;
}

auto FramedPipeTransport::ReadContentLength(
    const FramedTransport::HeaderMap& headers) -> int {
  // Look for Content-Length header
  auto it = headers.find("Content-Length");
  if (it == headers.end()) {
    return 0;  // No Content-Length header
  }

  // Parse the content length
  try {
    return std::stoi(it->second);
  } catch (const std::exception&) {
    return 0;  // Invalid Content-Length
  }
}

}  // namespace jsonrpc::transport
