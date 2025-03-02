#include "jsonrpc/transport/pipe_transport.hpp"

#include <stdexcept>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

PipeTransport::PipeTransport(const std::string &socket_path, bool is_server)
    : socket_(io_context_), socket_path_(socket_path), is_server_(is_server) {
  spdlog::info(
      "Initializing PipeTransport with socket path: {}. IsServer: {}",
      socket_path, is_server);

  if (is_server_) {
    RemoveExistingSocketFile();
    BindAndListen();
  } else {
    Connect();
  }
}

auto PipeTransport::GetSocket() -> asio::local::stream_protocol::socket & {
  return socket_;
}

PipeTransport::~PipeTransport() {
  Close();
  io_context_.stop();
}

void PipeTransport::Close() {
  spdlog::info("Closing socket");
  socket_.close();
}

void PipeTransport::RemoveExistingSocketFile() {
  if (unlink(socket_path_.c_str()) == 0) {
    spdlog::info("Removed existing socket file: {}", socket_path_);
  } else if (errno != ENOENT) {
    spdlog::error(
        "Failed to remove existing socket file: {}. Error: {}", socket_path_,
        strerror(errno));
    throw std::runtime_error("Failed to remove existing socket file.");
  }
}

void PipeTransport::Connect() {
  try {
    asio::local::stream_protocol::endpoint endpoint(socket_path_);
    socket_.connect(endpoint);
    spdlog::info("Connected to socket at path: {}", socket_path_);
  } catch (const std::exception &e) {
    spdlog::error("Error connecting to socket: {}", e.what());
    throw std::runtime_error("Error connecting to socket");
  }
}

void PipeTransport::BindAndListen() {
  try {
    asio::local::stream_protocol::acceptor acceptor(
        io_context_, asio::local::stream_protocol::endpoint(socket_path_));
    acceptor.listen();
    spdlog::info("Listening on socket path: {}", socket_path_);
    acceptor.accept(socket_);
    spdlog::info("Accepted connection on socket path: {}", socket_path_);
  } catch (const std::exception &e) {
    spdlog::error("Error binding/listening on socket: {}", e.what());
    throw std::runtime_error("Error binding/listening on socket");
  }
}

void PipeTransport::SendMessage(const std::string &message) {
  try {
    std::string full_message = message + "\n";
    asio::write(socket_, asio::buffer(full_message));
    spdlog::debug("Sent message: {}", message);
  } catch (const std::exception &e) {
    spdlog::error("Error sending message: {}", e.what());
    throw std::runtime_error("Error sending message");
  }
}

auto PipeTransport::ReceiveMessage() -> std::string {
  try {
    asio::streambuf buffer;
    asio::read_until(socket_, buffer, '\n');
    std::istream is(&buffer);
    std::string message;
    std::getline(is, message);
    spdlog::debug("Received message: {}", message);
    return message;
  } catch (const std::exception &e) {
    spdlog::error("Error receiving message: {}", e.what());
    socket_.close();
    throw std::runtime_error("Error receiving message");
  }
}

}  // namespace jsonrpc::transport
