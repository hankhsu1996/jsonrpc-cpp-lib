#include "jsonrpc/transport/framed_pipe_transport.hpp"

#include <unistd.h>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

FramedPipeTransport::FramedPipeTransport(
    asio::io_context& io_context, const std::string& socket_path,
    bool is_server)
    : PipeTransport(io_context, socket_path, is_server) {
}

auto FramedPipeTransport::SendMessage(const std::string& message)
    -> asio::awaitable<void> {
  auto framed_message = MessageFramer::Frame(message);
  co_await asio::async_write(
      GetSocket(), asio::buffer(framed_message), asio::use_awaitable);
}

auto FramedPipeTransport::ReceiveMessage() -> asio::awaitable<std::string> {
  while (true) {
    // Try to deframe from existing buffer
    auto result = framer_.TryDeframe(read_buffer_);

    if (result.complete) {
      read_buffer_.erase(0, result.consumed_bytes);
      co_return result.message;
    }

    if (!result.error.empty()) {
      spdlog::error("Framing error: {}", result.error);
      throw std::runtime_error(result.error);
    }

    // Need more data
    char buf[4096];
    size_t n = co_await GetSocket().async_read_some(
        asio::buffer(buf), asio::use_awaitable);

    if (n == 0) {
      throw std::runtime_error("Connection closed by peer");
    }

    read_buffer_.append(buf, n);
  }
}

}  // namespace jsonrpc::transport
