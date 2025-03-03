#include "jsonrpc/transport/pipe_transport.hpp"

#include <filesystem>
#include <stdexcept>
#include <thread>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

PipeTransport::PipeTransport(std::string socket_path, bool is_server)
    : socket_(io_context_),
      socket_path_(std::move(socket_path)),
      is_server_(is_server),
      read_buffer_() {
  try {
    if (is_server_) {
      RemoveExistingSocketFile();
      BindAndListen().get();
    } else {
      Connect().get();
    }
  } catch (const std::exception &e) {
    spdlog::error("Failed to initialize pipe transport: {}", e.what());
    throw;
  }
}

PipeTransport::~PipeTransport() {
  if (!is_closed_) {
    try {
      Close().get();
    } catch (const std::exception &e) {
      spdlog::error("Error during transport close: {}", e.what());
    }
  }
}

auto PipeTransport::SendMessage(const std::string &message)
    -> std::future<void> {
  if (is_closed_) {
    spdlog::error("Cannot send message - transport is closed");
    throw std::runtime_error("Transport is closed");
  }

  spdlog::debug("Sending message of length {}: {}", message.length(), message);
  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    // For now, we'll use synchronous write and wrap it in a promise
    // In a future version, we could use asio's async_write
    asio::write(socket_, asio::buffer(message + "\n"));
    spdlog::debug("Message sent successfully");
    promise.set_value();
  } catch (const std::exception &e) {
    spdlog::error("Error sending message: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

auto PipeTransport::ReceiveMessage() -> std::future<std::string> {
  if (is_closed_) {
    spdlog::error("Cannot receive message - transport is closed");
    throw std::runtime_error("Transport is closed");
  }

  std::promise<std::string> promise;
  auto future = promise.get_future();

  try {
    socket_.non_blocking(true);

    // Create a thread to handle the asynchronous read
    std::thread([this, p = std::move(promise)]() mutable {
      try {
        while (!is_closed_) {
          std::error_code ec;
          size_t bytes = socket_.read_some(asio::buffer(read_buffer_), ec);

          if (ec == asio::error::would_block) {
            // No data available yet, sleep briefly and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
          }

          if (ec == asio::error::eof || ec == asio::error::connection_reset) {
            spdlog::info("Connection closed by peer");
            socket_.close();
            is_closed_ = true;
            p.set_exception(std::make_exception_ptr(
                std::runtime_error("Transport was closed")));
            return;
          }

          if (ec) {
            spdlog::error("Socket error: {}", ec.message());
            throw std::runtime_error("Socket error: " + ec.message());
          }

          // Process the received data
          std::string_view chunk(read_buffer_.data(), bytes);
          message_buffer_.append(chunk);

          // Check if we have a complete message (ending with newline)
          auto newline_pos = message_buffer_.find('\n');
          if (newline_pos != std::string::npos) {
            std::string message = message_buffer_.substr(0, newline_pos);
            message_buffer_ = message_buffer_.substr(newline_pos + 1);
            spdlog::debug(
                "Received message of length {}: {}", message.length(), message);
            p.set_value(std::move(message));
            return;
          }
        }

        // If we get here, the transport was closed during the read
        p.set_exception(std::make_exception_ptr(
            std::runtime_error("Transport was closed")));
      } catch (const std::exception &e) {
        spdlog::error("Exception receiving message: {}", e.what());
        p.set_exception(std::current_exception());
      }
    }).detach();

    return future;
  } catch (const std::exception &e) {
    spdlog::error("Exception setting up receive: {}", e.what());
    promise.set_exception(std::current_exception());
    return future;
  }
}

auto PipeTransport::Close() -> std::future<void> {
  spdlog::info("Closing pipe transport");
  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    if (!is_closed_) {
      is_closed_ = true;

      if (socket_.is_open()) {
        socket_.close();
        spdlog::debug("Socket closed successfully");
      }

      // If we're a server, remove the socket file
      if (is_server_) {
        RemoveExistingSocketFile();
      }
    }
    promise.set_value();
  } catch (const std::exception &e) {
    spdlog::error("Error closing transport: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

auto PipeTransport::GetSocket() -> asio::local::stream_protocol::socket & {
  return socket_;
}

void PipeTransport::RemoveExistingSocketFile() {
  try {
    if (std::filesystem::exists(socket_path_)) {
      std::filesystem::remove(socket_path_);
      spdlog::debug("Removed existing socket file: {}", socket_path_);
    }
  } catch (const std::exception &e) {
    spdlog::warn("Failed to remove socket file: {}", e.what());
  }
}

auto PipeTransport::Connect() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    spdlog::info("Connecting to socket: {}", socket_path_);

    // Create the endpoint
    asio::local::stream_protocol::endpoint endpoint(socket_path_);

    // Set up a timer for connection timeout
    asio::steady_timer timer(io_context_);
    timer.expires_after(std::chrono::seconds(5));

    // Start an asynchronous connect operation
    socket_.async_connect(
        endpoint, [this, &promise, &timer](const std::error_code &error) {
          timer.cancel();  // Cancel the timeout timer

          if (error) {
            spdlog::error("Connection failed: {}", error.message());
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Error connecting to socket")));
          } else {
            spdlog::info("Connected to socket: {}", socket_path_);
            promise.set_value();
          }
        });

    // Set up a timeout handler
    timer.async_wait([this, &promise](const std::error_code &error) {
      if (!error) {  // Timer expired
        socket_.close();
        spdlog::error("Connection timed out");
        promise.set_exception(std::make_exception_ptr(
            std::runtime_error("Connection timed out")));
      }
    });

    // Run the io_context to process the async operations
    io_context_.run();
    io_context_.restart();
  } catch (const std::exception &e) {
    spdlog::error("Error during connect: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

auto PipeTransport::BindAndListen() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    spdlog::info("Starting server on socket: {}", socket_path_);

    // Create the endpoint
    asio::local::stream_protocol::endpoint endpoint(socket_path_);
    asio::local::stream_protocol::acceptor acceptor(io_context_);

    // Open the acceptor
    acceptor.open();

    // Bind to the endpoint
    acceptor.bind(endpoint);

    // Start listening
    acceptor.listen();

    // Accept one connection
    acceptor.async_accept(
        socket_, [this, &promise](const std::error_code &error) {
          if (error) {
            spdlog::error("Accept failed: {}", error.message());
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Accept failed: " + error.message())));
          } else {
            spdlog::info("Client connected to socket: {}", socket_path_);
            promise.set_value();
          }
        });

    // Run the io_context to process the async operations
    io_context_.run();
    io_context_.restart();
  } catch (const std::exception &e) {
    spdlog::error("Error during bind and listen: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

}  // namespace jsonrpc::transport
