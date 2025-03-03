#include "jsonrpc/transport/socket_transport.hpp"

#include <array>
#include <stdexcept>
#include <thread>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

SocketTransport::SocketTransport(
    std::string host, uint16_t port, bool is_server)
    : socket_(io_context_),
      host_(std::move(host)),
      port_(port),
      is_server_(is_server),
      read_buffer_() {
  try {
    if (is_server) {
      BindAndListen().get();
    } else {
      Connect().get();
    }
  } catch (const std::exception &e) {
    spdlog::error("Failed to initialize socket transport: {}", e.what());
    throw;
  }
}

SocketTransport::~SocketTransport() {
  if (!is_closed_) {
    try {
      Close().get();
    } catch (const std::exception &e) {
      spdlog::error("Error during transport close: {}", e.what());
    }
  }
}

auto SocketTransport::SendMessage(const std::string &message)
    -> std::future<void> {
  if (is_closed_) {
    throw std::runtime_error("Transport is closed");
  }

  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    // For now, we'll use synchronous write and wrap it in a promise
    // In a future version, we could use asio's async_write
    asio::write(socket_, asio::buffer(message + "\n"));
    spdlog::debug("SocketTransport sent message: {}", message);
    promise.set_value();
  } catch (const std::exception &e) {
    spdlog::error("SocketTransport: Send error: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

auto SocketTransport::ReceiveMessage() -> std::future<std::string> {
  if (is_closed_) {
    throw std::runtime_error("Transport is closed");
  }

  std::promise<std::string> promise;
  auto future = promise.get_future();

  try {
    // Use a shared_ptr to track if this particular request has been closed
    // This helps prevent use-after-close issues
    auto request_active = std::make_shared<std::atomic<bool>>(true);

    socket_.non_blocking(true);

    // Create a thread to handle the asynchronous read
    std::thread([this, p = std::move(promise), request_active]() mutable {
      try {
        while (!is_closed_ && request_active->load()) {
          std::error_code ec;

          // Guard against using a closed socket
          if (!socket_.is_open()) {
            is_closed_ = true;
            p.set_value("");  // Signal closed connection with empty string
            return;
          }

          size_t bytes = socket_.read_some(asio::buffer(read_buffer_), ec);

          if (ec == asio::error::would_block) {
            // No data available yet, sleep briefly and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
          }

          if (ec == asio::error::eof || ec == asio::error::connection_reset ||
              ec == asio::error::bad_descriptor) {
            // Handle connection closed or socket error
            try {
              if (socket_.is_open()) {
                socket_.close();
              }
            } catch (...) {
              // Ignore errors during close
            }

            is_closed_ = true;
            p.set_value("");  // Signal closed connection with empty string
            return;
          }

          if (ec) {
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
            spdlog::debug("SocketTransport received message: {}", message);
            p.set_value(std::move(message));
            return;
          }
        }

        // If we get here, the transport was closed during the read
        p.set_value("");
      } catch (const std::exception &e) {
        spdlog::error("SocketTransport: Receive error: {}", e.what());
        p.set_exception(std::current_exception());
      }
    }).detach();

    return future;
  } catch (const std::exception &e) {
    spdlog::error("SocketTransport: Receive setup error: {}", e.what());
    throw;
  }
}

auto SocketTransport::Close() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    if (!is_closed_) {
      spdlog::info("Closing socket transport");
      is_closed_ = true;

      if (socket_.is_open()) {
        socket_.close();
      }

      promise.set_value();
    } else {
      promise.set_value();  // Already closed, just fulfill the promise
    }
  } catch (const std::exception &e) {
    spdlog::error("Error closing socket: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

auto SocketTransport::GetSocket() -> asio::ip::tcp::socket & {
  return socket_;
}

auto SocketTransport::Connect() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    spdlog::info("Connecting to {}:{}", host_, port_);

    // Resolve the host name
    asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host_, std::to_string(port_));

    // Set up a timer for connection timeout
    asio::steady_timer timer(io_context_);
    timer.expires_after(std::chrono::seconds(5));

    // Track the connection result
    std::error_code ec;

    // Start an asynchronous connect operation
    asio::async_connect(
        socket_, endpoints,
        [&promise, &timer](
            const std::error_code &error,
            const asio::ip::tcp::endpoint &endpoint) {
          timer.cancel();  // Cancel the timeout timer

          if (error) {
            spdlog::error("Connection failed: {}", error.message());
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Connection failed: " + error.message())));
          } else {
            spdlog::info(
                "Connected to {}:{}", endpoint.address().to_string(),
                endpoint.port());
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

auto SocketTransport::BindAndListen() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    spdlog::info("Starting server on port {}", port_);

    // Create an acceptor
    asio::ip::tcp::acceptor acceptor(
        io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port_));

    // Accept one connection
    acceptor.async_accept(
        socket_, [this, &promise](const std::error_code &error) {
          if (error) {
            spdlog::error("Accept failed: {}", error.message());
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Accept failed: " + error.message())));
          } else {
            spdlog::info(
                "Client connected from {}:{}",
                socket_.remote_endpoint().address().to_string(),
                socket_.remote_endpoint().port());
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
