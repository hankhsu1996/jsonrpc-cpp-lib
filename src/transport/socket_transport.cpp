#include "jsonrpc/transport/socket_transport.hpp"

#include <stdexcept>

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
  spdlog::debug(
      "SocketTransport initialized ({}): {}:{}",
      is_server_ ? "server" : "client", address_, port_);
}

SocketTransport::~SocketTransport() {
  try {
    // If we haven't closed explicitly, do it now synchronously
    if (!is_closed_) {
      CloseNow();
    }
  } catch (const std::exception &e) {
    spdlog::error("Error in SocketTransport destructor: {}", e.what());
  }
}

auto SocketTransport::Start()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  try {
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

    // Set started flag before performing operations
    is_started_ = true;

    if (is_server_) {
      // For server, bind and listen for connections
      spdlog::info("Starting SocketTransport server at {}:{}", address_, port_);
      co_await BindAndListen();
    } else {
      // For client, fully connect immediately
      spdlog::info(
          "Connecting SocketTransport client to {}:{}", address_, port_);
      co_await Connect();
      spdlog::debug(
          "SocketTransport client connected to {}:{}", address_, port_);
    }

    co_return std::expected<void, error::RpcError>();
  } catch (const std::exception &e) {
    spdlog::error("Error in Start(): {}", e.what());
    is_started_ = false;
    throw;
  }
}

auto SocketTransport::Close()
    -> asio::awaitable<std::expected<void, error::RpcError>> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    if (is_closed_) {
      spdlog::debug("SocketTransport already closed");
      co_return std::expected<void, error::RpcError>();
    }

    is_closed_ = true;
    is_connected_ = false;

    spdlog::debug("Closing socket transport");

    // Cancel and close the socket safely
    if (socket_.is_open()) {
      spdlog::debug("Closing socket");
      socket_.cancel();
      asio::error_code ec;
      socket_.close(ec);
      if (ec) {
        spdlog::warn("Error closing socket: {}", ec.message());
      }
    }

    // Add an additional synchronization point to ensure all operations posted
    // to the strand complete
    co_await asio::post(GetStrand(), asio::use_awaitable);

    co_return std::expected<void, error::RpcError>();
  } catch (const std::exception &e) {
    spdlog::error("Error closing socket transport: {}", e.what());
    throw;
  }
}

void SocketTransport::CloseNow() {
  // Set closed flag to prevent concurrent operations
  is_closed_ = true;
  is_connected_ = false;

  try {
    // Cancel and close the socket synchronously
    if (socket_.is_open()) {
      spdlog::debug("Closing socket synchronously");
      socket_.cancel();
      asio::error_code ec;
      socket_.close(ec);
      if (ec) {
        spdlog::warn("Error closing socket: {}", ec.message());
      }
    }
  } catch (const std::exception &e) {
    spdlog::error("Error in synchronous close: {}", e.what());
  }
}

auto SocketTransport::GetSocket() -> asio::ip::tcp::socket & {
  return socket_;
}

auto SocketTransport::SendMessage(std::string message)
    -> asio::awaitable<void> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    if (is_closed_) {
      throw std::runtime_error("SendMessage() called on closed transport");
    }

    if (!is_started_) {
      throw std::runtime_error("Transport not started before sending message");
    }

    if (!socket_.is_open()) {
      throw std::runtime_error("Socket not open in SendMessage()");
    }

    co_await asio::async_write(
        socket_, asio::buffer(message), asio::use_awaitable);
  } catch (const std::exception &e) {
    spdlog::error("Exception in SendMessage(): {}", e.what());
    throw;
  }
}

auto SocketTransport::ReceiveMessage() -> asio::awaitable<std::string> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    if (is_closed_) {
      spdlog::warn("ReceiveMessage() called after transport was closed");
      co_return std::string();
    }

    if (!is_started_) {
      throw std::runtime_error(
          "Transport not started before receiving message");
    }

    if (!socket_.is_open()) {
      spdlog::warn("Socket not open in ReceiveMessage()");
      co_return std::string();
    }

    // Clear any existing message buffer
    message_buffer_.clear();

    // Read data from socket
    size_t bytes_read = co_await socket_.async_read_some(
        asio::buffer(read_buffer_), asio::use_awaitable);

    if (bytes_read == 0) {
      if (is_closed_) {
        co_return std::string();
      }
      throw std::runtime_error("Connection closed by peer");
    }

    message_buffer_.append(read_buffer_.data(), bytes_read);
    spdlog::debug("Received {} bytes", bytes_read);

    co_return std::move(message_buffer_);
  } catch (const asio::system_error &e) {
    // Handle ASIO-specific errors
    if (e.code() == asio::error::eof) {
      spdlog::debug("EOF received, connection closed by peer");
      is_connected_ = false;
    } else if (e.code() == asio::error::operation_aborted) {
      spdlog::debug("Read operation aborted");
    } else {
      spdlog::error("ASIO error in ReceiveMessage(): {}", e.what());
    }
    throw;
  } catch (const std::exception &e) {
    spdlog::error("Exception in ReceiveMessage(): {}", e.what());
    throw;
  }
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
    socket_.close();  // cleanup
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

  // Create and open acceptor
  asio::ip::tcp::acceptor acceptor(GetExecutor());

  acceptor.open(endpoint.protocol(), ec);
  if (ec) {
    spdlog::error("Error opening acceptor: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Open error: " + ec.message()));
  }

  acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
  if (ec) {
    spdlog::error("Error setting reuse_address: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Set option error: " + ec.message()));
  }

  acceptor.bind(endpoint, ec);
  if (ec) {
    spdlog::error("Error binding acceptor: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Bind error: " + ec.message()));
  }

  acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    spdlog::error("Error listening: {}", ec.message());
    co_return std::unexpected(
        error::CreateTransportError("Listen error: " + ec.message()));
  }

  spdlog::debug("Listening on {}:{}", address_, port_);

  // Accept a connection
  co_await acceptor.async_accept(
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
