#include "jsonrpc/transport/framed_pipe_transport.hpp"

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

auto FramedPipeTransport::ReceiveMessage()
    -> asio::awaitable<std::expected<std::string, error::RpcError>> {
  while (true) {
    // Try to deframe from existing buffer
    auto result = framer_.TryDeframe(read_buffer_);
    if (result.complete) {
      read_buffer_.erase(0, result.consumed_bytes);
      co_return result.message;
    }

    if (!result.error.empty()) {
      spdlog::error("Framing error: {}", result.error);
      co_return error::CreateTransportError("Framing error: " + result.error);
    }

    // Get more data using base class receive
    auto chunk_result = co_await PipeTransport::ReceiveMessage();
    if (!chunk_result) {
      co_return std::unexpected(chunk_result.error());
    }

    read_buffer_ += *chunk_result;  // append new data
  }
}

}  // namespace jsonrpc::transport
