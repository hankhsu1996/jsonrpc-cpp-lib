#include "jsonrpc/transport/framed_socket_transport.hpp"

#include <future>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

FramedSocketTransport::FramedSocketTransport(
    const std::string& host, uint16_t port, bool is_server,
    asio::io_context* external_io_context)
    : SocketTransport(host, port, is_server, external_io_context) {
  spdlog::info(
      "FramedSocketTransport initialized with host: {} and port: {}", host,
      port);
}

auto FramedSocketTransport::SendMessage(const std::string& message)
    -> std::future<void> {
  auto promise_ptr = std::make_shared<std::promise<void>>();
  auto future = promise_ptr->get_future();

  AsyncSendMessage(message, [promise_ptr](const asio::error_code& ec) {
    if (ec) {
      promise_ptr->set_exception(
          std::make_exception_ptr(std::runtime_error(ec.message())));
    } else {
      promise_ptr->set_value();
    }
  });

  return future;
}

auto FramedSocketTransport::ReceiveMessage() -> std::future<std::string> {
  auto promise_ptr = std::make_shared<std::promise<std::string>>();
  auto future = promise_ptr->get_future();

  AsyncReceiveMessage(
      [promise_ptr](const asio::error_code& ec, std::string message) {
        if (ec) {
          promise_ptr->set_exception(
              std::make_exception_ptr(std::runtime_error(ec.message())));
        } else {
          promise_ptr->set_value(std::move(message));
        }
      });

  return future;
}

void FramedSocketTransport::DoAsyncSendMessage(
    const std::string& message, SendHandler handler) {
  // Post to the strand of the underlying SocketTransport to ensure thread
  // safety
  asio::post(
      GetIoContext()->get_executor(),
      [this, message, handler = std::move(handler)]() mutable {
        try {
          // Create a framed message
          asio::streambuf message_buf;
          std::ostream message_stream(&message_buf);
          FrameMessage(message_stream, message);

          // Use asio's async_write with the underlying socket
          asio::async_write(
              GetSocket(), message_buf.data(),
              [handler = std::move(handler)](
                  const asio::error_code& ec,
                  std::size_t bytes_written) mutable {
                if (ec) {
                  spdlog::error(
                      "FramedSocketTransport failed to send message: {}",
                      ec.message());
                  handler(ec);
                } else {
                  spdlog::info(
                      "FramedSocketTransport sent message with {} bytes",
                      bytes_written);
                  handler(asio::error_code());
                }
              });
        } catch (const std::exception& e) {
          spdlog::error(
              "FramedSocketTransport failed to setup send message: {}",
              e.what());
          asio::error_code ec(asio::error::operation_aborted);
          asio::post(
              GetIoContext()->get_executor(),
              [handler = std::move(handler), ec]() mutable { handler(ec); });
        }
      });
}

void FramedSocketTransport::DoAsyncReceiveMessage(ReceiveHandler handler) {
  // Post to the strand of the underlying SocketTransport to ensure thread
  // safety
  asio::post(
      GetIoContext()->get_executor(),
      [this, handler = std::move(handler)]() mutable {
        try {
          // Create a buffer for the headers
          auto buffer = std::make_shared<asio::streambuf>();

          // First read the headers up to the double newline
          asio::async_read_until(
              GetSocket(), *buffer, "\r\n\r\n",
              [this, buffer, handler = std::move(handler)](
                  const asio::error_code& ec,
                  std::size_t bytes_transferred) mutable {
                if (ec) {
                  spdlog::error(
                      "FramedSocketTransport failed to read headers: {}",
                      ec.message());
                  handler(ec, "");
                  return;
                }

                // Process the headers
                try {
                  // Extract the data from the buffer
                  std::istream is(buffer.get());
                  auto headers = ReadHeadersFromStream(is);

                  // Find the Content-Length header
                  auto it = headers.find("Content-Length");
                  if (it == headers.end()) {
                    throw std::runtime_error("Content-Length header not found");
                  }

                  // Parse the content length
                  int content_length =
                      FramedTransport::ParseContentLength(it->second);

                  // Read the content
                  asio::async_read(
                      GetSocket(), *buffer,
                      asio::transfer_exactly(
                          content_length - buffer->size() + bytes_transferred),
                      [buffer, content_length, handler = std::move(handler)](
                          const asio::error_code& ec,
                          std::size_t /*bytes_transferred*/) mutable {
                        if (ec) {
                          spdlog::error(
                              "FramedSocketTransport failed to read content: "
                              "{}",
                              ec.message());
                          handler(ec, "");
                          return;
                        }

                        try {
                          // Extract the content
                          std::istream is(buffer.get());
                          // Skip the headers and empty line
                          std::string line;
                          while (std::getline(is, line) && !line.empty() &&
                                 line != "\r") {
                            // Skip headers
                          }

                          // Read the content
                          std::string content(content_length, '\0');
                          is.read(content.data(), content_length);

                          spdlog::info(
                              "FramedSocketTransport received message with {} "
                              "bytes",
                              content.length());
                          handler(asio::error_code(), content);
                        } catch (const std::exception& e) {
                          spdlog::error(
                              "FramedSocketTransport failed to process "
                              "content: {}",
                              e.what());
                          asio::error_code ec(asio::error::operation_aborted);
                          handler(ec, "");
                        }
                      });
                } catch (const std::exception& e) {
                  spdlog::error(
                      "FramedSocketTransport failed to process headers: {}",
                      e.what());
                  asio::error_code ec(asio::error::operation_aborted);
                  handler(ec, "");
                }
              });
        } catch (const std::exception& e) {
          spdlog::error(
              "FramedSocketTransport failed to setup receive message: {}",
              e.what());
          asio::error_code ec(asio::error::operation_aborted);
          asio::post(
              GetIoContext()->get_executor(),
              [handler = std::move(handler), ec]() mutable {
                handler(ec, "");
              });
        }
      });
}

void FramedSocketTransport::DoAsyncClose(CloseHandler handler) {
  // Delegate to the base class implementation
  SocketTransport::DoAsyncClose(std::move(handler));
}

}  // namespace jsonrpc::transport
