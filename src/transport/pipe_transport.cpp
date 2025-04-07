#include "jsonrpc/transport/pipe_transport.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <jsonrpc/utils/string_utils.hpp>
#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

using error::Ok;
using error::RpcError;
using error::RpcErrorCode;

PipeTransport::PipeTransport(
    asio::any_io_executor executor, std::string socket_path, bool is_server,
    std::shared_ptr<spdlog::logger> logger)
    : Transport(std::move(executor), logger),
      socket_(GetExecutor()),
      socket_path_(std::move(socket_path)),
      is_server_(is_server),
      read_buffer_() {
}

PipeTransport::~PipeTransport() {
  if (!is_closed_) {
    Logger()->debug("PipeTransport destructor triggering CloseNow()");
    try {
      CloseNow();
    } catch (const std::exception &e) {
      Logger()->error("PipeTransport destructor error: {}", e.what());
    }
  }
}

auto PipeTransport::Start()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  Logger()->debug("PipeTransport starting");
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_started_) {
    Logger()->debug("PipeTransport already started");
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError, "PipeTransport already started");
  }

  if (is_closed_) {
    Logger()->error("PipeTransport cannot start a closed transport");
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError, "Cannot start a closed transport");
  }

  if (is_server_) {
    // For server, bind and listen for connections
    Logger()->debug("PipeTransport starting server at {}", socket_path_);
    auto result = co_await BindAndListen();
    if (!result) {
      Logger()->error(
          "PipeTransport server error starting at {}: {}", socket_path_,
          result.error().Message());
      co_return result;
    }
  } else {
    // For client, connect to the server
    Logger()->debug("PipeTransport connecting client to {}", socket_path_);
    auto result = co_await Connect();
    if (!result) {
      Logger()->error(
          "PipeTransport client error connecting to {}: {}", socket_path_,
          result.error().Message());
      co_return result;
    }
  }
  Logger()->debug("PipeTransport client connected to {}", socket_path_);

  // Set started flag before performing operations
  is_started_ = true;
  Logger()->debug("PipeTransport successfully started");
  co_return Ok();
}

auto PipeTransport::Close()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  Logger()->debug("PipeTransport closing");
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    Logger()->debug("PipeTransport already closed");
    co_return Ok();
  }

  is_closed_ = true;
  is_connected_ = false;

  // Clear the message queue
  send_queue_.clear();

  // Cancel and close the socket safely
  std::error_code ec;
  if (socket_.is_open()) {
    socket_.cancel(ec);
    if (ec) {
      Logger()->warn("PipeTransport error canceling socket: {}", ec.message());
    }
    socket_.close(ec);
    if (ec) {
      Logger()->warn("PipeTransport error closing socket: {}", ec.message());
    }
  }

  // clean up acceptor if this is a server
  if (is_server_ && acceptor_) {
    acceptor_->cancel(ec);
    if (ec) {
      Logger()->warn(
          "PipeTransport error canceling acceptor: {}", ec.message());
    }
    acceptor_->close(ec);
    if (ec) {
      Logger()->warn("PipeTransport error closing acceptor: {}", ec.message());
    }
  }

  // Clean up the socket file if this is a server
  if (is_server_ && !socket_path_.empty()) {
    auto result = RemoveExistingSocketFile();
    if (!result) {
      Logger()->warn(
          "PipeTransport error removing socket file: {}",
          result.error().Message());
    }
  }

  Logger()->debug("PipeTransport closed");
  co_return Ok();
}

void PipeTransport::CloseNow() {
  is_closed_ = true;
  is_connected_ = false;

  // Clear the message queue
  send_queue_.clear();

  auto try_close_socket = [&]() {
    if (!socket_.is_open()) {
      return;
    }
    Logger()->debug("PipeTransport closing socket synchronously");

    std::error_code ec;
    socket_.cancel();
    socket_.close(ec);
    if (ec) {
      Logger()->warn("PipeTransport error closing socket: {}", ec.message());
    }
  };

  auto try_close_acceptor = [&]() {
    if (!is_server_ || !acceptor_ || !acceptor_->is_open()) {
      return;
    }
    Logger()->debug("PipeTransport closing acceptor synchronously");

    std::error_code ec;
    acceptor_->cancel();
    acceptor_->close(ec);
    if (ec) {
      Logger()->warn("PipeTransport error closing acceptor: {}", ec.message());
    }
  };

  auto try_remove_socket_file = [&]() {
    if (!is_server_ || socket_path_.empty()) {
      return;
    }

    std::error_code ec;
    if (std::filesystem::exists(socket_path_, ec) && !ec) {
      std::filesystem::remove(socket_path_, ec);
      if (ec) {
        Logger()->warn(
            "PipeTransport error removing socket file: {}", ec.message());
      } else {
        Logger()->debug("PipeTransport removed socket file: {}", socket_path_);
      }
    } else if (ec) {
      Logger()->warn(
          "PipeTransport error checking socket file existence: {}",
          ec.message());
    }
  };

  try {
    try_close_socket();
    try_close_acceptor();
    try_remove_socket_file();
  } catch (const std::exception &e) {
    Logger()->error("PipeTransport error during CloseNow(): {}", e.what());
  }
}

auto PipeTransport::GetSocket() -> asio::local::stream_protocol::socket & {
  return socket_;
}

auto PipeTransport::RemoveExistingSocketFile()
    -> std::expected<void, error::RpcError> {
  std::error_code ec;
  if (std::filesystem::exists(socket_path_, ec)) {
    if (ec) {
      return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError,
          "Error checking if socket file exists: " + ec.message());
    }
    std::filesystem::remove(socket_path_, ec);
    if (ec) {
      return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError,
          "Error removing socket file: " + ec.message());
    }
    Logger()->debug(
        "PipeTransport removed existing socket file: {}", socket_path_);
  } else {
    Logger()->debug(
        "PipeTransport no existing socket file to remove: {}", socket_path_);
  }
  return {};
}

auto PipeTransport::SendMessage(std::string message)
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "Attempt to send message on closed transport");
  }

  if (!is_started_) {
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "Transport not started before sending message");
  }

  if (!socket_.is_open()) {
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError, "Socket not open");
  }

  Logger()->debug("Queuing {} bytes to send to pipe", message.size());
  send_queue_.push_back(std::move(message));

  // If there's no active sending task, start one
  if (!sending_.exchange(true)) {
    asio::co_spawn(GetStrand(), SendMessageLoop(), asio::detached);
  }

  co_return Ok();
}

auto PipeTransport::SendMessageLoop() -> asio::awaitable<void> {
  while (!send_queue_.empty()) {
    std::string message = std::move(send_queue_.front());
    send_queue_.pop_front();

    Logger()->debug("Sending {} bytes to pipe", message.size());
    std::size_t bytes_sent = 0;
    const std::size_t chunk_size =
        32 * 1024;  // 32KB chunks to be safe for WSL's 64KB limit

    while (bytes_sent < message.size()) {
      auto remaining = message.size() - bytes_sent;
      auto current_chunk_size = std::min(remaining, chunk_size);

      // Use string_view to avoid copying data
      std::string_view chunk =
          std::string_view(message).substr(bytes_sent, current_chunk_size);

      // Write to the socket with error redirection
      std::error_code ec;
      auto chunk_sent = co_await asio::async_write(
          socket_, asio::buffer(chunk),
          asio::redirect_error(asio::use_awaitable, ec));

      if (ec) {
        Logger()->error(
            "PipeTransport error sending message: {}", ec.message());
        break;  // Exit but continue processing other messages
      }

      bytes_sent += chunk_sent;
      Logger()->debug(
          "Sent {} bytes to pipe, total {}/{}", chunk_sent, bytes_sent,
          message.size());
    }
  }

  // Mark sending as complete
  sending_ = false;
}

auto PipeTransport::Flush()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  Logger()->debug("Flushing message queue");
  while (true) {
    co_await asio::post(GetStrand(), asio::use_awaitable);
    if (send_queue_.empty() && !sending_) {
      break;
    }
    co_await asio::steady_timer(GetExecutor(), std::chrono::milliseconds(10))
        .async_wait(asio::use_awaitable);
  }
  co_return Ok();
}

auto PipeTransport::ReceiveMessage()
    -> asio::awaitable<std::expected<std::string, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    Logger()->warn(
        "PipeTransport ReceiveMessage called after transport was closed");
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "ReceiveMessage called after transport was closed");
  }

  if (!is_started_) {
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "Transport not started before receiving message");
  }

  if (!socket_.is_open()) {
    Logger()->warn("PipeTransport ReceiveMessage called on a closed socket");
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "ReceiveMessage called on a closed socket");
  }

  message_buffer_.clear();

  std::error_code ec;
  std::size_t bytes_read = co_await socket_.async_read_some(
      asio::buffer(read_buffer_),
      asio::redirect_error(asio::use_awaitable, ec));

  if (ec) {
    if (ec == asio::error::eof) {
      Logger()->debug("PipeTransport connection closed by peer (EOF)");
      is_connected_ = false;
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError, "Connection closed by peer");
    } else if (ec == asio::error::operation_aborted) {
      Logger()->debug("PipeTransport Receive operation aborted");
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError, "Receive aborted");
    } else {
      Logger()->error(
          "PipeTransport error receiving message: {}", ec.message());
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError, "Receive error: " + ec.message());
    }
  }

  if (bytes_read == 0) {
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError, "No data received");
  }

  message_buffer_.append(read_buffer_.data(), bytes_read);
  auto log_message = message_buffer_;
  if (log_message.size() > 70) {
    log_message = log_message.substr(0, 70) + "...";
  }
  std::ranges::replace(log_message, '\n', ' ');
  std::ranges::replace(log_message, '\r', ' ');
  Logger()->debug("PipeTransport received message: {}", log_message);
  co_return std::move(message_buffer_);
}

auto PipeTransport::Connect()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  Logger()->debug("PipeTransport connecting to {}", socket_path_);

  // Make sure we're not already connected
  if (is_connected_) {
    co_return Ok();
  }

  // Check if we're closed
  if (is_closed_) {
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError, "Cannot connect a closed transport");
  }

  // Close any existing socket
  std::error_code ec;
  if (socket_.is_open()) {
    socket_.close(ec);
    if (ec) {
      Logger()->warn(
          "PipeTransport error closing socket before reconnect: {}",
          ec.message());
    }
  }

  // Create a new socket
  socket_ = asio::local::stream_protocol::socket(GetExecutor());

  // Create the endpoint and connect
  asio::local::stream_protocol::endpoint endpoint(socket_path_);
  co_await socket_.async_connect(
      endpoint, asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    Logger()->error(
        "PipeTransport error connecting to {}: {}", socket_path_, ec.message());
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError, "Error connecting to: " + ec.message());
  }

  is_connected_ = true;
  Logger()->debug("PipeTransport connected to {}", socket_path_);

  co_return Ok();
}

auto PipeTransport::BindAndListen()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  Logger()->debug("PipeTransport binding to {}", socket_path_);

  auto result = RemoveExistingSocketFile();
  if (!result) {
    Logger()->error(
        "PipeTransport error removing existing socket file: {}",
        result.error().Message());
    co_return result;
  }

  // Lazily construct the acceptor if not already created
  if (!acceptor_) {
    acceptor_ =
        std::make_unique<asio::local::stream_protocol::acceptor>(GetExecutor());
  }

  // Create the endpoint
  asio::local::stream_protocol::endpoint endpoint(socket_path_);

  // Open and bind the acceptor
  std::error_code ec;
  acceptor_->open(endpoint.protocol(), ec);
  if (ec) {
    Logger()->error("PipeTransport error opening acceptor: {}", ec.message());
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "Error opening acceptor: " + ec.message());
  }
  acceptor_->bind(endpoint, ec);
  if (ec) {
    Logger()->error("PipeTransport error binding acceptor: {}", ec.message());
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "Error binding acceptor: " + ec.message());
  }
  acceptor_->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    Logger()->error(
        "PipeTransport error listening on acceptor: {}", ec.message());
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "Error listening on acceptor: " + ec.message());
  }

  // Accept a connection
  Logger()->debug("PipeTransport waiting for connection on {}", socket_path_);
  co_await acceptor_->async_accept(
      socket_, asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    Logger()->error(
        "PipeTransport error accepting connection: {}", ec.message());
    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "Error accepting connection: " + ec.message());
  }
  is_connected_ = true;
  Logger()->debug("PipeTransport accepted connection on {}", socket_path_);

  co_return Ok();
}

}  // namespace jsonrpc::transport
