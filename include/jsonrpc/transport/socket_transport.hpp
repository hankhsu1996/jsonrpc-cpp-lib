#pragma once

#include <asio.hpp>
#include <string>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::transport {

/**
 * @brief Transport implementation using TCP/IP sockets.
 *
 * This class provides transport functionality over TCP/IP sockets,
 * supporting both client and server modes for communication over a network.
 */
class SocketTransport : public Transport {
 public:
  /**
   * @brief Constructs a SocketTransport.
   * @param host The host address (IP or domain name).
   * @param port The port number.
   * @param isServer True if the transport acts as a server; false if it acts as
   * a client.
   */
  SocketTransport(const std::string &host, uint16_t port, bool is_server);

  ~SocketTransport() override;

  SocketTransport(const SocketTransport &) = delete;
  auto operator=(const SocketTransport &) -> SocketTransport & = delete;

  SocketTransport(SocketTransport &&) = delete;
  auto operator=(SocketTransport &&) -> SocketTransport & = delete;

  void SendMessage(const std::string &message) override;
  auto ReceiveMessage() -> std::string override;
  void Close() override;

 protected:
  auto GetSocket() -> asio::ip::tcp::socket &;

 private:
  void Connect();
  void BindAndListen();

  asio::io_context io_context_;
  asio::ip::tcp::socket socket_;
  std::string host_;
  uint16_t port_;
  bool is_server_;
  bool is_closed_{false};
};

}  // namespace jsonrpc::transport
