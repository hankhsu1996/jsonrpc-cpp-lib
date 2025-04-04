#include "jsonrpc/transport/socket_transport.hpp"

#include <asio.hpp>
#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

SocketTransport::SocketTransport(
    asio::any_io_executor executor, std::string address, uint16_t port,
    bool is_server)
    : Transport(std::move(executor)),
      socket_(GetExecutor()),
      address_(std::move(address)),
      port_(port),
      is_server_(is_server),
      read_buffer_() {
}

SocketTransport::~SocketTransport() {
  if (!is_closed_) {
    spdlog::debug("SocketTransport destructor triggering CloseNow()");
    try {
      CloseNow();
    } catch (const std::exception &e) {
      spdlog::error("Error in SocketTransport destructor: {}", e.what());
    }
  }
}

auto SocketTransport::Start()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_started_) {
    spdlog::debug("SocketTransport already started");
    co_return std::unexpected(
        error::CreateTransportError("SocketTransport already started"));
  }

  if (is_closed_) {
    spdlog::error("Cannot start a closed transport");
    co_return std::unexpected(
        error::CreateTransportError("Cannot start a closed transport"));
  }

  std::expected<void, error::RpcError> result;

  if (is_server_) {
    spdlog::debug("Starting SocketTransport server at {}:{}", address_, port_);
    result = co_await BindAndListen();
    if (!result) {
      spdlog::error(
          "Error starting SocketTransport server: {}", result.error().message);
      co_return std::unexpected(result.error());
    }
  } else {
    spdlog::debug(
        "Connecting SocketTransport client to {}:{}", address_, port_);
    result = co_await Connect();
    if (!result) {
      spdlog::error(
          "Error connecting SocketTransport client: {}",
          result.error().message);
      co_return std::unexpected(result.error());
    }
    spdlog::debug("SocketTransport client connected to {}:{}", address_, port_);
  }

  is_started_ = true;
  spdlog::debug("SocketTransport successfully started");
  co_return std::expected<void, error::RpcError>{};
}

auto SocketTransport::Close()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    spdlog::debug("SocketTransport already closed");
    co_return std::expected<void, error::RpcError>{};
  }

  is_closed_ = true;
  is_connected_ = false;

  spdlog::debug("Closing socket transport");

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

  // Clean up acceptor if this is a server
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

  spdlog::debug("SocketTransport closed");
  co_return std::expected<void, error::RpcError>{};
}

void SocketTransport::CloseNow() {
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

  try {
    try_close_socket();
    try_close_acceptor();
  } catch (const std::exception &e) {
    spdlog::error("Unexpected exception during CloseNow(): {}", e.what());
  }
}

auto SocketTransport::GetSocket() -> asio::ip::tcp::socket & {
  return socket_;
}

auto SocketTransport::SendMessage(std::string message)
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    co_return std::unexpected(error::CreateTransportError(
        "SendMessage() called on closed transport"));
  }

  if (!is_started_) {
    co_return std::unexpected(error::CreateTransportError(
        "Transport not started before sending message"));
  }

  if (!socket_.is_open()) {
    co_return std::unexpected(
        error::CreateTransportError("Socket not open in SendMessage()"));
  }

  std::error_code ec;
  co_await asio::async_write(
      socket_, asio::buffer(message),
      asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    spdlog::error("SendMessage failed: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Error sending message: " + ec.message()));
  }

  co_return std::expected<void, error::RpcError>{};
}

auto SocketTransport::ReceiveMessage()
    -> asio::awaitable<std::expected<std::string, error::RpcError>> {
  co_await asio::post(GetStrand(), asio::use_awaitable);

  if (is_closed_) {
    spdlog::warn("ReceiveMessage() called after transport was closed");
    co_return std::unexpected(error::CreateTransportError(
        "ReceiveMessage() called after transport was closed"));
  }

  if (!is_started_) {
    co_return std::unexpected(error::CreateTransportError(
        "Transport not started before receiving message"));
  }

  if (!socket_.is_open()) {
    spdlog::warn("Socket not open in ReceiveMessage()");
    co_return std::unexpected(
        error::CreateTransportError("Socket not open in ReceiveMessage()"));
  }

  message_buffer_.clear();

  std::error_code ec;
  size_t bytes_read = co_await socket_.async_read_some(
      asio::buffer(read_buffer_),
      asio::redirect_error(asio::use_awaitable, ec));

  if (ec) {
    if (ec == asio::error::eof) {
      spdlog::debug("EOF received, connection closed by peer");
      is_connected_ = false;
      co_return std::unexpected(
          error::CreateTransportError("Connection closed by peer"));
    } else if (ec == asio::error::operation_aborted) {
      spdlog::debug("Read operation aborted");
      co_return std::unexpected(
          error::CreateTransportError("Receive operation aborted"));
    } else {
      spdlog::error("ASIO error in ReceiveMessage(): {}", ec.message());
      co_return std::unexpected(
          error::CreateTransportError("Receive error: " + ec.message()));
    }
  }

  if (bytes_read == 0) {
    co_return std::unexpected(
        error::CreateTransportError("Connection closed by peer (no data)"));
  }

  message_buffer_.append(read_buffer_.data(), bytes_read);
  spdlog::debug("Received {} bytes", bytes_read);
  co_return std::move(message_buffer_);
}

auto SocketTransport::Connect()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  spdlog::debug("Connecting to {}:{}", address_, port_);

  if (is_connected_) {
    co_return std::expected<void, error::RpcError>{};
  }

  if (is_closed_) {
    co_return std::unexpected(
        error::CreateTransportError("Cannot connect a closed transport"));
  }

  // Close any existing socket
  asio::error_code ec;
  if (socket_.is_open()) {
    socket_.close(ec);
    if (ec) {
      spdlog::warn("Error closing socket before reconnect: {}", ec.message());
    }
  }

  // Create new socket if needed
  if (!socket_.is_open()) {
    socket_ = asio::ip::tcp::socket(GetExecutor());
  }

  // Resolve address
  asio::ip::tcp::resolver resolver(GetExecutor());
  auto endpoints = co_await resolver.async_resolve(
      address_, std::to_string(port_),
      asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    spdlog::error("Error resolving {}:{}: {}", address_, port_, ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Resolve error: " + ec.message()));
  }

  // Connect
  co_await asio::async_connect(
      socket_, endpoints, asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    spdlog::error(
        "Error connecting to {}:{}: {}", address_, port_, ec.message());
    if (socket_.is_open()) {
      socket_.close();
    }
    co_return std::unexpected(
        error::CreateTransportError("Connect error: " + ec.message()));
  }

  is_connected_ = true;
  spdlog::debug("Connected to {}:{}", address_, port_);
  co_return std::expected<void, error::RpcError>{};
}

auto SocketTransport::BindAndListen()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  spdlog::debug("Binding to {}:{}", address_, port_);

  asio::error_code ec;

  // Create the endpoint
  asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port_);

  // Resolve specific address if not 0.0.0.0 / ::
  if (address_ != "0.0.0.0" && address_ != "::") {
    asio::ip::tcp::resolver resolver(GetExecutor());
    auto results = co_await resolver.async_resolve(
        address_, std::to_string(port_),
        asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
      spdlog::error("Error resolving {}:{}: {}", address_, port_, ec.message());
      co_return std::unexpected(
          error::CreateTransportError("Resolve error: " + ec.message()));
    }
    endpoint = *results.begin();
  }

  // Lazily construct the acceptor if not already created
  if (!acceptor_) {
    acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(GetExecutor());
  }

  // Create and open acceptor
  acceptor_->open(endpoint.protocol(), ec);
  if (ec) {
    spdlog::error("Error opening acceptor: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Open error: " + ec.message()));
  }

  acceptor_->set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
  if (ec) {
    spdlog::error("Error setting reuse_address: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Set option error: " + ec.message()));
  }

  acceptor_->bind(endpoint, ec);
  if (ec) {
    spdlog::error("Error binding acceptor: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Bind error: " + ec.message()));
  }

  acceptor_->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    spdlog::error("Error listening: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Listen error: " + ec.message()));
  }

  spdlog::debug("Listening on {}:{}", address_, port_);

  // Accept a connection
  co_await acceptor_->async_accept(
      socket_, asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    spdlog::error("Error accepting connection: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Accept error: " + ec.message()));
  }

  is_connected_ = true;
  spdlog::debug("Accepted connection on {}:{}", address_, port_);

  co_return std::expected<void, error::RpcError>{};
}

}  // namespace jsonrpc::transport
