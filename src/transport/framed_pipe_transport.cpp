#include "jsonrpc/transport/framed_pipe_transport.hpp"

#include <array>
#include <string_view>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

FramedPipeTransport::FramedPipeTransport(
    asio::any_io_executor executor, const std::string& socket_path,
    bool is_server)
    : PipeTransport(std::move(executor), socket_path, is_server) {
}

auto FramedPipeTransport::SendMessage(std::string message)
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  auto framed_message = MessageFramer::Frame(message);
  co_return co_await PipeTransport::SendMessage(std::move(framed_message));
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
    std::array<char, 4096> buffer{};
    size_t n = co_await GetSocket().async_read_some(
        asio::buffer(buffer.data(), buffer.size()), asio::use_awaitable);

    if (n == 0) {
      throw std::runtime_error("Connection closed by peer");
    }

    read_buffer_.append(std::string_view(buffer.data(), n));
  }
}

}  // namespace jsonrpc::transport
