#include "jsonrpc/transport/socket_transport.hpp"

#include <array>
#include <span>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

SocketTransport::SocketTransport(
    const std::string &host, uint16_t port, bool is_server)
    : socket_(io_context_), host_(host), port_(port), is_server_(is_server) {
  spdlog::info(
      "Initializing SocketTransport with host: {}, port: {}. IsServer: {}",
      host, port, is_server);

  if (is_server_) {
    BindAndListen();
  } else {
    Connect();
  }
}

auto SocketTransport::GetSocket() -> asio::ip::tcp::socket & {
  return socket_;
}

SocketTransport::~SocketTransport() {
  Close();
  io_context_.stop();
}

void SocketTransport::Close() {
  if (!is_closed_) {
    spdlog::info("SocketTransport: Closing socket transport");
    socket_.close();
    is_closed_ = true;
  }
}

void SocketTransport::Connect() {
  asio::ip::tcp::resolver resolver(io_context_);
  auto endpoints = resolver.resolve(host_, std::to_string(port_));

  asio::steady_timer timer(io_context_);
  timer.expires_after(std::chrono::seconds(3));

  std::error_code connect_error;
  asio::async_connect(
      socket_, endpoints,
      [&](const asio::error_code &error, const asio::ip::tcp::endpoint &) {
        if (!error) {
          timer.cancel();
        } else {
          connect_error = error;
        }
      });

  timer.async_wait([&](const asio::error_code &error) {
    if (!error) {
      connect_error = asio::error::timed_out;
      socket_.close();
    }
  });

  io_context_.run();

  if (connect_error) {
    spdlog::error(
        "Error connecting to {}:{}. Error: {}", host_, port_,
        connect_error.message());
    throw std::runtime_error("Error connecting to socket");
  }
}

void SocketTransport::BindAndListen() {
  try {
    asio::ip::tcp::acceptor acceptor(
        io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port_));
    acceptor.listen();
    spdlog::info("Listening on {}:{}", host_, port_);
    acceptor.accept(socket_);
    spdlog::info("Accepted connection on {}:{}", host_, port_);
  } catch (const std::exception &e) {
    spdlog::error(
        "Error binding/listening on {}:{}. Error: {}", host_, port_, e.what());
    throw std::runtime_error("Error binding/listening on socket");
  }
}

void SocketTransport::SendMessage(const std::string &message) {
  if (is_closed_) {
    throw std::runtime_error("Transport is closed");
  }
  try {
    std::string full_message = message + "\n";
    asio::write(socket_, asio::buffer(full_message));
    spdlog::debug("SocketTransport: Message sent");
  } catch (const std::exception &e) {
    spdlog::error("SocketTransport: Send error: {}", e.what());
    throw std::runtime_error("Error sending message");
  }
}

auto SocketTransport::ReceiveMessage() -> std::string {
  if (is_closed_) {
    throw std::runtime_error("Transport is closed");
  }
  try {
    asio::streambuf buffer;
    socket_.non_blocking(true);

    std::error_code ec;
    std::string message;
    std::array<char, 1024> data{};

    // Try to read until we get a complete message or error
    while (true) {
      size_t bytes =
          socket_.read_some(asio::buffer(data.data(), data.size()), ec);

      if (ec == asio::error::would_block) {
        // No data available yet, try again
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      if (ec == asio::error::eof || ec == asio::error::connection_reset) {
        socket_.close();
        is_closed_ = true;
        return "";
      }

      if (ec) {
        spdlog::error("SocketTransport: Receive error: {}", ec.message());
        throw std::runtime_error("Error receiving message");
      }

      // Look for newline in the received data
      std::span<const char> data_view(data.data(), bytes);
      for (const char c : data_view) {
        if (c == '\n') {
          return message;
        }
        message += c;
      }
    }
  } catch (const std::exception &e) {
    if (!is_closed_) {
      spdlog::error("SocketTransport: Receive error: {}", e.what());
      socket_.close();
      is_closed_ = true;
    }
    throw std::runtime_error("Error receiving message");
  }
}

}  // namespace jsonrpc::transport
