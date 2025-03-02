#pragma once

#include <asio.hpp>
#include <string>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::transport {

/**
 * @brief Transport implementation using Unix domain sockets.
 *
 * This class provides transport functionality over Unix domain sockets,
 * supporting both client and server modes for inter-process communication
 * on the same machine.
 */
class PipeTransport : public Transport {
 public:
  /**
   * @brief Constructs a PipeTransport.
   * @param socketPath Path to the Unix domain socket.
   * @param isServer True if the transport acts as a server; false if it acts as
   * a client.
   */
  PipeTransport(const std::string &socket_path, bool is_server);

  ~PipeTransport() override;

  PipeTransport(const PipeTransport &) = delete;
  auto operator=(const PipeTransport &) -> PipeTransport & = delete;

  PipeTransport(PipeTransport &&) = delete;
  auto operator=(PipeTransport &&) -> PipeTransport & = delete;

  void SendMessage(const std::string &message) override;
  auto ReceiveMessage() -> std::string override;
  void Close() override;

 protected:
  auto GetSocket() -> asio::local::stream_protocol::socket &;

 private:
  void RemoveExistingSocketFile();
  void Connect();
  void BindAndListen();

  asio::io_context io_context_;
  asio::local::stream_protocol::socket socket_;
  std::string socket_path_;
  bool is_server_;
};

}  // namespace jsonrpc::transport
