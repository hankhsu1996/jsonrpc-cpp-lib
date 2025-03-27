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

auto SocketTransport::Start() -> asio::awaitable<void> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    if (is_started_) {
      spdlog::debug("SocketTransport already started");
      co_return;
    }

    if (is_closed_) {
      spdlog::error("Cannot start a closed transport");
      throw std::runtime_error("Cannot start a closed transport");
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

    co_return;
  } catch (const std::exception &e) {
    spdlog::error("Error in Start(): {}", e.what());
    is_started_ = false;
    throw;
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

auto SocketTransport::Close() -> asio::awaitable<void> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    if (is_closed_) {
      co_return;  // Already closed
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

    co_return;
  } catch (const std::exception &e) {
    spdlog::error("Error closing socket transport: {}", e.what());
    throw;
  }
}

auto SocketTransport::Connect() -> asio::awaitable<void> {
  spdlog::debug("Connecting to {}:{}", address_, port_);

  // Make sure we're not already connected
  if (is_connected_) {
    co_return;
  }

  try {
    // Check if we're closed
    if (is_closed_) {
      throw std::runtime_error("Cannot connect a closed transport");
    }

    // Close any existing socket
    if (socket_.is_open()) {
      asio::error_code ec;
      socket_.close(ec);
      if (ec) {
        spdlog::warn("Error closing socket before reconnect: {}", ec.message());
      }
    }

    // Create a new socket if needed
    if (!socket_.is_open()) {
      socket_ = asio::ip::tcp::socket(GetExecutor());
    }

    // Resolve the endpoint
    asio::ip::tcp::resolver resolver(GetExecutor());
    auto endpoints = co_await resolver.async_resolve(
        address_, std::to_string(port_), asio::use_awaitable);

    // Connect to the first endpoint
    co_await asio::async_connect(socket_, endpoints, asio::use_awaitable);

    // Set connected flag only after successful connection
    is_connected_ = true;
    spdlog::debug("Connected to {}:{}", address_, port_);
  } catch (const std::exception &e) {
    spdlog::error("Error connecting to {}:{}: {}", address_, port_, e.what());

    // Reset socket to clean state
    try {
      if (socket_.is_open()) {
        asio::error_code ec;
        socket_.close(ec);
      }
    } catch (...) {
      // Ignore errors during cleanup
    }

    throw;
  }
}

auto SocketTransport::BindAndListen() -> asio::awaitable<void> {
  spdlog::debug("Binding to {}:{}", address_, port_);

  try {
    // Create the endpoint
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port_);

    // For specific address binding
    if (address_ != "0.0.0.0" && address_ != "::") {
      asio::ip::tcp::resolver resolver(GetExecutor());
      auto results = co_await resolver.async_resolve(
          address_, std::to_string(port_), asio::use_awaitable);
      endpoint = *results.begin();
    }

    // Create an acceptor
    asio::ip::tcp::acceptor acceptor(GetExecutor(), endpoint);
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));

    spdlog::debug("Listening on {}:{}", address_, port_);

    // Accept a connection
    co_await acceptor.async_accept(socket_, asio::use_awaitable);
    is_connected_ = true;

    spdlog::debug("Accepted connection on {}:{}", address_, port_);
  } catch (const std::exception &e) {
    spdlog::error(
        "Error binding/listening on {}:{}: {}", address_, port_, e.what());
    throw;
  }
}

}  // namespace jsonrpc::transport
