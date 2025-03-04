#include "jsonrpc/transport/framed_pipe_transport.hpp"

#include <future>
#include <stdexcept>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

FramedPipeTransport::FramedPipeTransport(
    const std::string& socket_path, bool is_server,
    asio::io_context* external_io_context)
    : PipeTransport(socket_path, is_server, external_io_context) {
  spdlog::info(
      "FramedPipeTransport initialized with socket path: {}", socket_path);
}

auto FramedPipeTransport::SendMessage(const std::string& message)
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

auto FramedPipeTransport::ReceiveMessage() -> std::future<std::string> {
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

auto FramedPipeTransport::ReadFramedMessage(std::chrono::milliseconds timeout)
    -> std::string {
  asio::streambuf buffer;
  asio::error_code ec;
  GetSocket().non_blocking(true);

  auto deadline = std::chrono::steady_clock::now() + timeout;

  // Read headers until \r\n\r\n delimiter
  ReadHeaders(buffer, deadline, ec);

  // Use FramedTransport to process the message
  std::istream stream(&buffer);

  // Try to use ReceiveFramedMessage directly if possible
  try {
    // Extract content length from the headers
    int content_length = ReadContentLengthFromStream(stream);

    // Read remaining content
    ReadContent(buffer, content_length, deadline, ec);

    // Extract the message content
    std::string content(
        asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
    return content;
  } catch (const std::exception& e) {
    throw std::runtime_error(
        std::string("Failed to read framed message: ") + e.what());
  }
}

void FramedPipeTransport::ReadHeaders(
    asio::streambuf& buffer, std::chrono::steady_clock::time_point deadline,
    asio::error_code& ec) {
  while (std::chrono::steady_clock::now() < deadline) {
    asio::read_until(GetSocket(), buffer, kHeaderDelimiter, ec);
    if (ec == asio::error::would_block) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (ec) {
      throw std::runtime_error(
          "Failed to read message headers: " + ec.message());
    }
    return;
  }
  throw std::runtime_error("Timeout waiting for response");
}

void FramedPipeTransport::ReadContent(
    asio::streambuf& buffer, int content_length,
    std::chrono::steady_clock::time_point deadline, asio::error_code& ec) {
  // Calculate how much more content we need to read
  std::size_t remaining_content_length = content_length - buffer.size();

  // Read any remaining content directly into the buffer
  if (remaining_content_length > 0) {
    while (std::chrono::steady_clock::now() < deadline) {
      asio::read(GetSocket(), buffer.prepare(remaining_content_length), ec);
      if (ec == asio::error::would_block) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      if (ec && ec != asio::error::eof) {
        throw std::runtime_error(
            "Failed to read message content: " + ec.message());
      }
      buffer.commit(remaining_content_length);
      return;
    }
    throw std::runtime_error("Timeout waiting for response");
  }
}

void FramedPipeTransport::DoAsyncSendMessage(
    const std::string& message, SendHandler handler) {
  // Post to the strand of the underlying PipeTransport to ensure thread safety
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
                      "FramedPipeTransport failed to send message: {}",
                      ec.message());
                  handler(ec);
                } else {
                  spdlog::info(
                      "FramedPipeTransport sent message with {} bytes",
                      bytes_written);
                  handler(asio::error_code());
                }
              });
        } catch (const std::exception& e) {
          spdlog::error(
              "FramedPipeTransport failed to setup send message: {}", e.what());
          asio::error_code ec(asio::error::operation_aborted);
          asio::post(
              GetIoContext()->get_executor(),
              [handler = std::move(handler), ec]() mutable { handler(ec); });
        }
      });
}

void FramedPipeTransport::DoAsyncReceiveMessage(ReceiveHandler handler) {
  // Post to the strand of the underlying PipeTransport to ensure thread safety
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
                      "FramedPipeTransport failed to read headers: {}",
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

                  // Parse the content length - using the static method with
                  // proper scope
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
                              "FramedPipeTransport failed to read content: {}",
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
                              "FramedPipeTransport received message with {} "
                              "bytes",
                              content.length());
                          handler(asio::error_code(), content);
                        } catch (const std::exception& e) {
                          spdlog::error(
                              "FramedPipeTransport failed to process content: "
                              "{}",
                              e.what());
                          asio::error_code ec(asio::error::operation_aborted);
                          handler(ec, "");
                        }
                      });
                } catch (const std::exception& e) {
                  spdlog::error(
                      "FramedPipeTransport failed to process headers: {}",
                      e.what());
                  asio::error_code ec(asio::error::operation_aborted);
                  handler(ec, "");
                }
              });
        } catch (const std::exception& e) {
          spdlog::error(
              "FramedPipeTransport failed to setup receive message: {}",
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

void FramedPipeTransport::DoAsyncClose(CloseHandler handler) {
  // Delegate to the base class implementation
  PipeTransport::DoAsyncClose(std::move(handler));
}

}  // namespace jsonrpc::transport
