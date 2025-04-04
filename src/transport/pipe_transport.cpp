#include "jsonrpc/transport/pipe_transport.hpp"

#include <filesystem>
#include <string>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

PipeTransport::PipeTransport(
    asio::any_io_executor executor, std::string socket_path, bool is_server)
    : Transport(std::move(executor)),
      socket_(GetExecutor()),
      socket_path_(std::move(socket_path)),
      is_server_(is_server),
      read_buffer_() {
}

PipeTransport::~PipeTransport() {
  if (!is_closed_) {
    spdlog::debug("PipeTransport destructor triggering CloseNow()");
    try {
      CloseNow();
    } catch (const std::exception &e) {
      spdlog::error("Error in PipeTransport destructor: {}", e.what());
    }
  }
}

auto PipeTransport::Start()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_started_) {
    spdlog::debug("PipeTransport already started");
    co_return std::unexpected(
        error::CreateTransportError("PipeTransport already started"));
  }

  if (is_closed_) {
    spdlog::error("Cannot start a closed transport");
    co_return std::unexpected(
        error::CreateTransportError("Cannot start a closed transport"));
  }

  if (is_server_) {
    // For server, bind and listen for connections
    spdlog::debug("Starting PipeTransport server at {}", socket_path_);
    auto result = co_await BindAndListen();
    if (!result) {
      spdlog::error(
          "Error starting PipeTransport server: {}", result.error().message);
      co_return std::unexpected(result.error());
    }
  } else {
    // For client, connect to the server
    spdlog::debug("Connecting PipeTransport client to {}", socket_path_);
    auto result = co_await Connect();
    if (!result) {
      spdlog::error(
          "Error connecting PipeTransport client: {}", result.error().message);
      co_return std::unexpected(result.error());
    }
    spdlog::debug("PipeTransport client connected to {}", socket_path_);
  }

  // Set started flag before performing operations
  is_started_ = true;
  spdlog::debug("PipeTransport successfully started");
  co_return std::expected<void, error::RpcError>();
}

auto PipeTransport::Close()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    spdlog::debug("PipeTransport already closed");
    co_return std::expected<void, error::RpcError>();
  }

  is_closed_ = true;
  is_connected_ = false;

  // Cancel and close the socket safely
  std::error_code ec;
  if (socket_.is_open()) {
    socket_.cancel(ec);
    if (ec) {
      spdlog::warn("Error canceling socket: {}", ec.message());
    }
    socket_.close(ec);
    if (ec) {
      spdlog::warn("Error closing socket: {}", ec.message());
    }
  }

  // clean up acceptor if this is a server
  if (is_server_ && acceptor_) {
    acceptor_->cancel(ec);
    if (ec) {
      spdlog::warn("Error canceling acceptor: {}", ec.message());
    }
    acceptor_->close(ec);
    if (ec) {
      spdlog::warn("Error closing acceptor: {}", ec.message());
    }
  }

  // Clean up the socket file if this is a server
  if (is_server_ && !socket_path_.empty()) {
    auto result = RemoveExistingSocketFile();
    if (!result) {
      spdlog::warn("Error removing socket file: {}", result.error().message);
    }
  }

  spdlog::debug("PipeTransport closed");
  co_return std::expected<void, error::RpcError>();
}

void PipeTransport::CloseNow() {
  is_closed_ = true;
  is_connected_ = false;

  auto try_close_socket = [&]() {
    if (!socket_.is_open()) {
      return;
    }
    spdlog::debug("Closing socket synchronously");

    std::error_code ec;
    socket_.cancel();
    socket_.close(ec);
    if (ec) {
      spdlog::warn("Error closing socket: {}", ec.message());
    }
  };

  auto try_close_acceptor = [&]() {
    if (!is_server_ || !acceptor_ || !acceptor_->is_open()) {
      return;
    }
    spdlog::debug("Closing acceptor synchronously");

    std::error_code ec;
    acceptor_->cancel();
    acceptor_->close(ec);
    if (ec) {
      spdlog::warn("Error closing acceptor: {}", ec.message());
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
        spdlog::warn("Error removing socket file: {}", ec.message());
      } else {
        spdlog::debug("Removed socket file: {}", socket_path_);
      }
    } else if (ec) {
      spdlog::warn("Error checking socket file existence: {}", ec.message());
    }
  };

  try {
    try_close_socket();
    try_close_acceptor();
    try_remove_socket_file();
  } catch (const std::exception &e) {
    spdlog::error("Unexpected exception during CloseNow(): {}", e.what());
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
      return std::unexpected(error::CreateTransportError(
          "Error checking if socket file exists: " + ec.message()));
    }
    std::filesystem::remove(socket_path_, ec);
    if (ec) {
      return std::unexpected(error::CreateTransportError(
          "Error removing socket file: " + ec.message()));
    }
    spdlog::debug("Removed existing socket file: {}", socket_path_);
  } else {
    spdlog::debug("No existing socket file to remove: {}", socket_path_);
  }
  return {};
}

auto PipeTransport::SendMessage(std::string message)
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    co_return std::unexpected(error::CreateTransportError(
        "Attempt to send message on closed transport"));
  }

  if (!is_started_) {
    co_return std::unexpected(error::CreateTransportError(
        "Transport not started before sending message"));
  }

  if (!socket_.is_open()) {
    co_return std::unexpected(error::CreateTransportError("Socket not open"));
  }

  // Write to the socket with error redirection
  std::error_code ec;
  co_await asio::async_write(
      socket_, asio::buffer(message),
      asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    spdlog::error("Error sending message: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Error sending message: " + ec.message()));
  }

  co_return std::expected<void, error::RpcError>{};
}

auto PipeTransport::ReceiveMessage()
    -> asio::awaitable<std::expected<std::string, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    spdlog::warn("ReceiveMessage called after transport was closed");
    co_return std::unexpected(error::CreateTransportError(
        "ReceiveMessage called after transport was closed"));
  }

  if (!is_started_) {
    co_return std::unexpected(error::CreateTransportError(
        "Transport not started before receiving message"));
  }

  if (!socket_.is_open()) {
    spdlog::warn("ReceiveMessage called on a closed socket");
    co_return std::unexpected(error::CreateTransportError(
        "ReceiveMessage called on a closed socket"));
  }

  message_buffer_.clear();

  std::error_code ec;
  std::size_t bytes_read = co_await socket_.async_read_some(
      asio::buffer(read_buffer_),
      asio::redirect_error(asio::use_awaitable, ec));

  if (ec) {
    if (ec == asio::error::eof) {
      spdlog::debug("Connection closed by peer (EOF)");
      is_connected_ = false;
      co_return std::unexpected(
          error::CreateTransportError("Connection closed by peer"));
    } else if (ec == asio::error::operation_aborted) {
      spdlog::debug("Receive operation aborted");
      co_return std::unexpected(error::CreateTransportError("Receive aborted"));
    } else {
      spdlog::error("Error receiving message: {}", ec.message());
      co_return std::unexpected(
          error::CreateTransportError("Receive error: " + ec.message()));
    }
  }

  if (bytes_read == 0) {
    co_return std::unexpected(error::CreateTransportError("No data received"));
  }

  message_buffer_.append(read_buffer_.data(), bytes_read);
  spdlog::debug("Received message: {}", message_buffer_);
  co_return std::move(message_buffer_);
}

auto PipeTransport::Connect()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  spdlog::debug("Connecting to {}", socket_path_);

  // Make sure we're not already connected
  if (is_connected_) {
    co_return std::expected<void, error::RpcError>();
  }

  // Check if we're closed
  if (is_closed_) {
    co_return std::unexpected(
        error::CreateTransportError("Cannot connect a closed transport"));
  }

  // Close any existing socket
  std::error_code ec;
  if (socket_.is_open()) {
    socket_.close(ec);
    if (ec) {
      spdlog::warn("Error closing socket before reconnect: {}", ec.message());
    }
  }

  // Create a new socket
  socket_ = asio::local::stream_protocol::socket(GetExecutor());

  // Create the endpoint and connect
  asio::local::stream_protocol::endpoint endpoint(socket_path_);
  co_await socket_.async_connect(
      endpoint, asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    spdlog::error("Error connecting to {}: {}", socket_path_, ec.message());
    co_return std::unexpected(error::CreateTransportError(ec.message()));
  }

  is_connected_ = true;
  spdlog::debug("Connected to {}", socket_path_);

  co_return std::expected<void, error::RpcError>();
}

auto PipeTransport::BindAndListen()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  spdlog::debug("Binding to {}", socket_path_);

  auto result = RemoveExistingSocketFile();
  if (!result) {
    spdlog::error(
        "Error removing existing socket file: {}", result.error().message);
    co_return std::unexpected(result.error());
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
    spdlog::error("Error opening acceptor: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Error opening acceptor: " + ec.message()));
  }
  acceptor_->bind(endpoint, ec);
  if (ec) {
    spdlog::error("Error binding acceptor: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Error binding acceptor: " + ec.message()));
  }
  acceptor_->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    spdlog::error("Error listening on acceptor: {}", ec.message());
    co_return std::unexpected(error::CreateTransportError(
        "Error listening on acceptor: " + ec.message()));
  }

  // Accept a connection
  spdlog::debug("Waiting for connection on {}", socket_path_);
  co_await acceptor_->async_accept(
      socket_, asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    spdlog::error("Error accepting connection: {}", ec.message());
    co_return std::unexpected(error::CreateTransportError(ec.message()));
  }
  is_connected_ = true;
  spdlog::debug("Accepted connection on {}", socket_path_);

  co_return std::expected<void, error::RpcError>();
}

}  // namespace jsonrpc::transport
