#pragma once

#include <string>

namespace jsonrpc::transport {

/**
 * @brief Base class for JSON-RPC transport.
 *
 * This class defines the interface for communication transport layers.
 */
class Transport {
 public:
  Transport() = default;
  virtual ~Transport() = default;

  Transport(const Transport &) = default;
  auto operator=(const Transport &) -> Transport & = default;

  Transport(Transport &&) = delete;
  auto operator=(Transport &&) -> Transport & = delete;

  /**
   * @brief Sends a message in string to the transport layer.
   * @param request The JSON-RPC request as a string.
   */
  virtual void SendMessage(const std::string &message) = 0;

  /**
   * @brief Receives a message from the transport layer.
   * @return The JSON-RPC response as a string.
   */
  virtual auto ReceiveMessage() -> std::string = 0;

  /**
   * @brief Closes the transport, interrupting any pending operations.
   */
  virtual void Close() = 0;
};

}  // namespace jsonrpc::transport
