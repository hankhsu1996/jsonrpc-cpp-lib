#include "jsonrpc/transport/pipe_transport.hpp"

#include <filesystem>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

PipeTransport::PipeTransport(
    std::string socket_path, bool is_server,
    asio::io_context *external_io_context)
    : Transport(external_io_context),
      strand_(*io_context_),
      socket_(*io_context_),
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

  // Create a shared pointer to the message to ensure it lives until the async
  // operation completes
  auto shared_message = std::make_shared<std::string>(message + "\n");

  // Use asio's async_write with a strand to ensure thread safety
  asio::async_write(
      socket_, asio::buffer(*shared_message),
      asio::bind_executor(
          strand_, [promise = std::move(promise), shared_message](
                       const asio::error_code &ec,
                       std::size_t /*bytes_transferred*/) mutable {
            if (ec) {
              spdlog::error("Error sending message: {}", ec.message());
              promise.set_exception(
                  std::make_exception_ptr(std::runtime_error(ec.message())));
            } else {
              spdlog::debug("Message sent successfully");
              promise.set_value();
            }
          }));

  // Make sure the io_context is running
  Transport::EnsureIoContextRunning();

  return future;
}

void PipeTransport::DoAsyncSendMessage(
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

  spdlog::debug(
      "Async sending message of length {}: {}", message.length(), message);

  // Create a shared pointer to the message to ensure it lives until the async
  // operation completes
  auto shared_message = std::make_shared<std::string>(message + "\n");

  // Use asio's async_write with a strand to ensure thread safety
  asio::async_write(
      socket_, asio::buffer(*shared_message),
      asio::bind_executor(
          strand_, [handler = std::move(handler), shared_message](
                       const asio::error_code &ec,
                       std::size_t /*bytes_transferred*/) mutable {
            if (ec) {
              spdlog::error("Error sending message: {}", ec.message());
              handler(ec);
            } else {
              spdlog::debug("Message sent successfully");
              handler(asio::error_code());
            }
          }));
}

auto PipeTransport::ReceiveMessage() -> std::future<std::string> {
  if (is_closed_) {
    spdlog::error("Cannot receive message - transport is closed");
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

void PipeTransport::DoAsyncReceiveMessage(ReceiveHandler handler) {
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

auto PipeTransport::Close() -> std::future<void> {
  spdlog::info("Closing pipe transport");
  std::promise<void> promise;
  auto future = promise.get_future();

  // Post the close operation to the strand to ensure thread safety
  asio::post(strand_, [this, promise = std::move(promise)]() mutable {
    try {
      if (!is_closed_) {
        is_closed_ = true;

        if (socket_.is_open()) {
          socket_.close();
        }

        // If this is a server, remove the socket file
        if (is_server_) {
          try {
            std::filesystem::remove(socket_path_);
          } catch (const std::exception &e) {
            spdlog::warn("Failed to remove socket file: {}", e.what());
          }
        }

        promise.set_value();
      } else {
        // Already closed
        promise.set_value();
      }
    } catch (const std::exception &e) {
      spdlog::error("Error closing transport: {}", e.what());
      promise.set_exception(std::current_exception());
    }
  });

  return future;
}

void PipeTransport::DoAsyncClose(CloseHandler handler) {
  spdlog::info("Async closing pipe transport");

  // Post the close operation to the strand to ensure thread safety
  asio::post(strand_, [this, handler = std::move(handler)]() mutable {
    try {
      if (!is_closed_) {
        is_closed_ = true;

        if (socket_.is_open()) {
          socket_.close();
        }

        // If this is a server, remove the socket file
        if (is_server_) {
          try {
            std::filesystem::remove(socket_path_);
          } catch (const std::exception &e) {
            spdlog::warn("Failed to remove socket file: {}", e.what());
          }
        }

        handler(asio::error_code());
      } else {
        // Already closed
        handler(asio::error_code());
      }
    } catch (const std::exception &e) {
      spdlog::error("Error closing transport: {}", e.what());
      handler(asio::error_code(
          asio::error::operation_aborted, asio::system_category()));
    }
  });
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

    // Setup the endpoint
    asio::local::stream_protocol::endpoint endpoint(socket_path_);

    // Setup a timeout handler
    asio::steady_timer timer(*io_context_);
    timer.expires_after(std::chrono::seconds(5));

    // Connect asynchronously
    socket_.async_connect(
        endpoint, [this, &promise, &timer](const std::error_code &error) {
          // Cancel the timeout timer
          timer.cancel();

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
    io_context_->run();
    io_context_->restart();
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

    // Create and bind the acceptor
    asio::local::stream_protocol::acceptor acceptor(*io_context_);
    acceptor.open();
    acceptor.bind(endpoint);
    acceptor.listen();

    // Accept a connection asynchronously
    acceptor.async_accept(
        socket_, [this, &promise](const std::error_code &error) {
          if (error) {
            spdlog::error("Accept failed: {}", error.message());
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Error accepting connection")));
          } else {
            spdlog::info("Accepted connection on socket: {}", socket_path_);
            promise.set_value();
          }
        });

    // Run the io_context to process the async operations
    io_context_->run();
    io_context_->restart();
  } catch (const std::exception &e) {
    spdlog::error("Error during bind/listen: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

}  // namespace jsonrpc::transport
