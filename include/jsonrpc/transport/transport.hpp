#pragma once

#include <future>
#include <string>

namespace jsonrpc::transport {

/**
 * @brief Abstract base class for transport implementations.
 *
 * This class defines the interface for transport layers used by the JSON-RPC
 * library. A transport is responsible for sending and receiving messages
 * between endpoints.
 */
class Transport {
 public:
  virtual ~Transport() = default;
  Transport() = default;
  Transport(const Transport&) = default;
  auto operator=(const Transport&) -> Transport& = default;
  Transport(Transport&&) = default;
  auto operator=(Transport&&) -> Transport& = default;

  /**
   * @brief Sends a message through the transport.
   *
   * @param message The message to send.
   * @return A future that completes when the message is sent.
   * @throws std::runtime_error if an error occurs during sending.
   */
  virtual auto SendMessage(const std::string& message) -> std::future<void> = 0;

  /**
   * @brief Receives a message from the transport.
   *
   * This method will wait until a message is available or the transport
   * is closed.
   *
   * @return A future that resolves to the received message.
   * @throws std::runtime_error if the transport is closed or an error occurs.
   */
  virtual auto ReceiveMessage() -> std::future<std::string> = 0;

  /**
   * @brief Closes the transport.
   *
   * After calling this method, no more messages can be sent or received.
   *
   * @return A future that completes when the transport is closed.
   */
  virtual auto Close() -> std::future<void> = 0;
};

}  // namespace jsonrpc::transport
