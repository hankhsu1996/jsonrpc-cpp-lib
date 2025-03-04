#include "jsonrpc/transport/socket_transport.hpp"

#include <array>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

SocketTransport::SocketTransport(
    std::string host, uint16_t port, bool is_server,
    asio::io_context *external_io_context)
    : Transport(external_io_context),
      strand_(*io_context_),
      socket_(*io_context_),
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
    // Add a newline at the end of the message for line-based protocol
    auto message_with_newline = message + "\n";

    // Use asio's async_write with strand for thread safety
    asio::async_write(
        socket_, asio::buffer(message_with_newline),
        asio::bind_executor(
            strand_, [promise = std::move(promise)](
                         const asio::error_code &ec,
                         std::size_t /*bytes_transferred*/) mutable {
              if (ec) {
                spdlog::error("SocketTransport: Send error: {}", ec.message());
                promise.set_exception(
                    std::make_exception_ptr(std::runtime_error(ec.message())));
              } else {
                spdlog::debug("Message sent successfully");
                promise.set_value();
              }
            }));

    // Make sure the io_context is running
    Transport::EnsureIoContextRunning();
  } catch (const std::exception &e) {
    spdlog::error("SocketTransport: Send setup error: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

void SocketTransport::DoAsyncSendMessage(
    const std::string &message, SendHandler handler) {
  if (is_closed_) {
    spdlog::error("Cannot send message - transport is closed");
    asio::post(
        GetIoContext()->get_executor(), [handler = std::move(handler)]() {
          handler(asio::error_code(
              asio::error::not_connected, asio::system_category()));
        });
    return;
  }

  // Add a newline at the end of the message for line-based protocol
  auto shared_message = std::make_shared<std::string>(message + "\n");

  // Use asio's async_write with strand for thread safety
  asio::async_write(
      socket_, asio::buffer(*shared_message),
      asio::bind_executor(
          strand_, [handler = std::move(handler), shared_message](
                       const asio::error_code &ec,
                       std::size_t /*bytes_transferred*/) mutable {
            if (ec) {
              spdlog::error("SocketTransport: Send error: {}", ec.message());
              handler(ec);
            } else {
              spdlog::debug("Message sent successfully");
              handler(asio::error_code());
            }
          }));
}

auto SocketTransport::ReceiveMessage() -> std::future<std::string> {
  if (is_closed_) {
    throw std::runtime_error("Transport is closed");
  }

  std::promise<std::string> promise;
  auto future = promise.get_future();

  // Create a shared pointer to the buffer to ensure it lives until the async
  // operation completes
  auto buffer = std::make_shared<asio::streambuf>();

  // Use asio's async_read_until to read a line with proper strand management
  asio::async_read_until(
      socket_, *buffer, '\n',
      asio::bind_executor(
          strand_, [this, promise = std::move(promise), buffer](
                       const asio::error_code &ec,
                       [[maybe_unused]] std::size_t bytes_transferred) mutable {
            if (ec) {
              if (ec == asio::error::eof ||
                  ec == asio::error::connection_reset) {
                spdlog::info("Connection closed by peer");
                socket_.close();
                is_closed_ = true;
                promise.set_exception(std::make_exception_ptr(
                    std::runtime_error("Transport was closed")));
              } else {
                spdlog::error("Error receiving message: {}", ec.message());
                promise.set_exception(
                    std::make_exception_ptr(std::runtime_error(ec.message())));
              }
            } else {
              // Extract the message from the buffer
              std::istream is(buffer.get());
              std::string message;
              std::getline(is, message);

              spdlog::debug("Message received: {}", message);
              promise.set_value(message);
            }
          }));

  // Make sure the io_context is running
  Transport::EnsureIoContextRunning();

  return future;
}

void SocketTransport::DoAsyncReceiveMessage(ReceiveHandler handler) {
  if (is_closed_) {
    spdlog::error("Cannot receive message - transport is closed");
    asio::post(
        GetIoContext()->get_executor(), [handler = std::move(handler)]() {
          handler(
              asio::error_code(
                  asio::error::not_connected, asio::system_category()),
              "");
        });
    return;
  }

  // Create a shared pointer to the buffer to ensure it lives until the async
  // operation completes
  auto buffer = std::make_shared<asio::streambuf>();

  // Use asio's async_read_until to read a line with proper strand management
  asio::async_read_until(
      socket_, *buffer, '\n',
      asio::bind_executor(
          strand_, [this, handler = std::move(handler), buffer](
                       const asio::error_code &ec,
                       [[maybe_unused]] std::size_t bytes_transferred) mutable {
            if (ec) {
              if (ec == asio::error::eof ||
                  ec == asio::error::connection_reset) {
                spdlog::info("Connection closed by peer");
                socket_.close();
                is_closed_ = true;
                handler(
                    asio::error_code(
                        asio::error::connection_reset, asio::system_category()),
                    "");
              } else {
                spdlog::error("Error receiving message: {}", ec.message());
                handler(ec, "");
              }
            } else {
              // Extract the message from the buffer
              std::istream is(buffer.get());
              std::string message;
              std::getline(is, message);

              spdlog::debug("Message received: {}", message);
              handler(asio::error_code(), message);
            }
          }));
}

auto SocketTransport::Close() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  // Post the close operation to the strand to ensure thread safety
  asio::post(strand_, [this, promise = std::move(promise)]() mutable {
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
  });

  return future;
}

void SocketTransport::DoAsyncClose(CloseHandler handler) {
  // Post the close operation to the strand to ensure thread safety
  asio::post(strand_, [this, handler = std::move(handler)]() mutable {
    try {
      if (!is_closed_) {
        spdlog::info("Closing socket transport");
        is_closed_ = true;

        if (socket_.is_open()) {
          socket_.close();
        }

        handler(asio::error_code());
      } else {
        handler(asio::error_code());  // Already closed
      }
    } catch (const std::exception &e) {
      spdlog::error("Error closing socket: {}", e.what());
      handler(asio::error_code(
          asio::error::operation_aborted, asio::system_category()));
    }
  });
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
    asio::ip::tcp::resolver resolver(*io_context_);
    auto endpoints = resolver.resolve(host_, std::to_string(port_));

    // Set up a timer for connection timeout
    asio::steady_timer timer(*io_context_);
    timer.expires_after(std::chrono::seconds(5));

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
    io_context_->run();
    io_context_->restart();
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
        *io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port_));

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
    io_context_->run();
    io_context_->restart();
  } catch (const std::exception &e) {
    spdlog::error("Error during bind and listen: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

}  // namespace jsonrpc::transport
