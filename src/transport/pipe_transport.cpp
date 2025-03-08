#include "jsonrpc/transport/pipe_transport.hpp"

#include <filesystem>
#include <string>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

PipeTransport::PipeTransport(
    asio::io_context &io_context, std::string socket_path, bool is_server)
    : Transport(io_context),
      socket_(io_context),
      socket_path_(std::move(socket_path)),
      is_server_(is_server),
      read_buffer_() {
  spdlog::debug(
      "PipeTransport initialized ({}): {}", is_server_ ? "server" : "client",
      socket_path_);

  // Create acceptor in constructor if this is a server, but don't bind yet
  if (is_server_) {
    acceptor_ = std::make_shared<asio::local::stream_protocol::acceptor>(
        GetIoContext());
  }
  // Connections will be established in the Start() method
}

PipeTransport::~PipeTransport() {
  try {
    // If we haven't closed explicitly, do it now synchronously
    if (!is_closed_) {
      CloseNow();
    }
  } catch (const std::exception &e) {
    spdlog::error("Error in PipeTransport destructor: {}", e.what());
  }
}

void PipeTransport::CloseNow() {
  // Set the closed flag to prevent concurrent operations
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

    // Cancel and close the acceptor safely
    if (is_server_ && acceptor_ && acceptor_->is_open()) {
      spdlog::debug("Closing acceptor synchronously");
      acceptor_->cancel();
      asio::error_code ec;
      acceptor_->close(ec);
      if (ec) {
        spdlog::warn("Error closing acceptor: {}", ec.message());
      }
    }

    // Clean up the socket file
    if (is_server_ && !socket_path_.empty() &&
        std::filesystem::exists(socket_path_)) {
      try {
        std::filesystem::remove(socket_path_);
        spdlog::debug("Removed socket file: {}", socket_path_);
      } catch (const std::exception &e) {
        spdlog::warn("Error removing socket file: {}", e.what());
      }
    }
  } catch (const std::exception &e) {
    spdlog::error("Error in synchronous close: {}", e.what());
  }
}

auto PipeTransport::Start() -> asio::awaitable<void> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    if (is_started_) {
      spdlog::debug("PipeTransport already started");
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
      spdlog::info("Starting PipeTransport server at {}", socket_path_);
      co_await BindAndListen();
    } else {
      // For client, connect to the server
      spdlog::info("Connecting PipeTransport client to {}", socket_path_);
      co_await Connect();
      spdlog::debug("PipeTransport client connected to {}", socket_path_);
    }

    co_return;
  } catch (const std::exception &e) {
    spdlog::error("Error in PipeTransport::Start(): {}", e.what());
    is_started_ = false;
    throw;
  }
}

auto PipeTransport::GetSocket() -> asio::local::stream_protocol::socket & {
  return socket_;
}

void PipeTransport::RemoveExistingSocketFile() {
  try {
    // Check if the socket file exists and remove it if it does
    if (std::filesystem::exists(socket_path_)) {
      std::filesystem::remove(socket_path_);
      spdlog::debug("Removed existing socket file: {}", socket_path_);
    }
  } catch (const std::exception &e) {
    spdlog::error("Error removing socket file: {}", e.what());
    throw;
  }
}

auto PipeTransport::SendMessage(const std::string &message)
    -> asio::awaitable<void> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    if (is_closed_) {
      throw std::runtime_error("Attempt to send message on closed transport");
    }

    // Ensure transport is started
    if (!is_started_) {
      throw std::runtime_error("Transport not started before sending message");
    }

    // No lazy connection - Start() should have connected if needed
    if (!socket_.is_open()) {
      throw std::runtime_error("Socket not open");
    }

    // Send the message
    co_await asio::async_write(
        socket_, asio::buffer(message), asio::use_awaitable);
  } catch (const std::exception &e) {
    spdlog::error("Error sending message: {}", e.what());
    throw;
  }
}

auto PipeTransport::ReceiveMessage() -> asio::awaitable<std::string> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    if (is_closed_) {
      spdlog::warn("ReceiveMessage called after transport was closed");
      co_return std::string();
    }

    if (!is_started_) {
      throw std::runtime_error(
          "Transport not started before receiving message");
    }

    if (!socket_.is_open()) {
      spdlog::warn("ReceiveMessage called on a closed socket");
      co_return std::string();
    }

    // Clear any existing message buffer
    message_buffer_.clear();

    // Read data from socket
    std::size_t bytes_read = co_await socket_.async_read_some(
        asio::buffer(read_buffer_), asio::use_awaitable);

    if (bytes_read == 0) {
      if (is_closed_) {
        co_return std::string();
      }
      throw std::runtime_error("Connection closed by peer");
    }

    message_buffer_.append(read_buffer_.data(), bytes_read);
    spdlog::debug("Received message: {}", message_buffer_);
    co_return std::move(message_buffer_);
  } catch (const asio::system_error &e) {
    // Handle ASIO-specific errors
    if (e.code() == asio::error::eof) {
      spdlog::debug("Connection closed by peer (EOF)");
      is_connected_ = false;
    } else if (e.code() == asio::error::operation_aborted) {
      spdlog::debug("Receive operation aborted");
    } else {
      spdlog::error("ASIO error receiving message: {}", e.what());
    }
    throw;
  } catch (const std::exception &e) {
    spdlog::error("Error receiving message: {}", e.what());
    throw;
  }
}

auto PipeTransport::Close() -> asio::awaitable<void> {
  try {
    co_await asio::post(GetStrand(), asio::use_awaitable);

    spdlog::debug("Closing pipe transport");

    if (is_closed_) {
      co_return;  // Already closed
    }

    is_closed_ = true;
    is_connected_ = false;

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

    // Cancel and close the acceptor safely
    if (is_server_ && acceptor_ && acceptor_->is_open()) {
      spdlog::debug("Closing acceptor");
      acceptor_->cancel();
      asio::error_code ec;
      acceptor_->close(ec);
      if (ec) {
        spdlog::warn("Error closing acceptor: {}", ec.message());
      }
    }

    // Ensure the socket file is removed only after closing the socket
    if (is_server_ && !socket_path_.empty() &&
        std::filesystem::exists(socket_path_)) {
      try {
        std::filesystem::remove(socket_path_);
        spdlog::debug("Removed socket file: {}", socket_path_);
      } catch (const std::exception &e) {
        spdlog::warn("Error removing socket file: {}", e.what());
      }
    }

    // Add an additional synchronization point to ensure all operations posted
    // to the strand complete
    co_await asio::post(GetStrand(), asio::use_awaitable);

    co_return;
  } catch (const std::exception &e) {
    spdlog::error("Error closing pipe transport: {}", e.what());
    throw;
  }
}

auto PipeTransport::Connect() -> asio::awaitable<void> {
  spdlog::debug("Connecting to {}", socket_path_);

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
      socket_ = asio::local::stream_protocol::socket(GetIoContext());
    }

    // Create the endpoint and connect
    asio::local::stream_protocol::endpoint endpoint(socket_path_);
    co_await socket_.async_connect(endpoint, asio::use_awaitable);

    // Set connected flag only after successful connection
    is_connected_ = true;
    spdlog::debug("Connected to {}", socket_path_);
  } catch (const std::exception &e) {
    spdlog::error("Error connecting to {}: {}", socket_path_, e.what());

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

auto PipeTransport::BindAndListen() -> asio::awaitable<void> {
  spdlog::debug("Binding to {}", socket_path_);

  try {
    // Remove any existing socket file
    RemoveExistingSocketFile();

    // Create the endpoint
    asio::local::stream_protocol::endpoint endpoint(socket_path_);

    // Open and bind the acceptor
    acceptor_->open();
    acceptor_->bind(endpoint);
    acceptor_->listen();

    spdlog::debug("Listening on {}", socket_path_);

    // Accept a connection
    co_await acceptor_->async_accept(socket_, asio::use_awaitable);
    is_connected_ = true;
    spdlog::debug("Accepted connection on {}", socket_path_);
  } catch (const std::exception &e) {
    spdlog::error("Error binding/listening on {}: {}", socket_path_, e.what());
    throw;
  }
}

}  // namespace jsonrpc::transport
