#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "jsonrpc/server/dispatcher.hpp"
#include "jsonrpc/server/types.hpp"
#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::server {

/**
 * @brief A JSON-RPC server for handling requests and notifications.
 *
 * Manages the lifecycle of a JSON-RPC server, including starting and stopping,
 * and registering RPC methods and notifications.
 */
class Server {
 public:
  /**
   * @brief Constructs a Server with the specified transport.
   *
   * @param transport A unique pointer to the transport layer to use for
   * communication.
   */
  explicit Server(std::unique_ptr<transport::Transport> transport);

  /// @brief Starts the server to handle incoming JSON-RPC requests.
  void Start();

  /// @brief Stops the server from handling requests.
  void Stop();

  /**
   * @brief Registers an RPC method handler with the dispatcher.
   *
   * @param method The name of the RPC method to handle.
   * @param handler The function to handle the RPC method call.
   *
   * @see MethodCallHandler for the signature of the handler function.
   */
  void RegisterMethodCall(
      const std::string &method, const MethodCallHandler &handler);

  /**
   * @brief Registers an RPC notification handler with the dispatcher.
   *
   * @param method The name of the RPC notification to handle.
   * @param handler The function to handle the RPC notification.
   *
   * @see NotificationHandler for the signature of the handler function.
   */
  void RegisterNotification(
      const std::string &method, const NotificationHandler &handler);

  /**
   * @brief Sends a notification to the client.
   *
   * @param method The name of the notification method.
   * @param params Optional parameters to include in the notification.
   */
  void SendNotification(
      const std::string &method,
      std::optional<nlohmann::json> params = std::nullopt);

  /// @brief Checks if the server is currently running.
  [[nodiscard]] auto IsRunning() const -> bool;

 private:
  /// @brief Listens for incoming JSON-RPC requests and dispatches them.
  void Listen();

  /// Dispatcher for routing requests to the appropriate handlers.
  std::unique_ptr<Dispatcher> dispatcher_;

  /// Transport layer for communication.
  std::unique_ptr<transport::Transport> transport_;

  /// Mutex to protect transport access
  mutable std::mutex transport_mutex_;

  /// Flag indicating if the server is running.
  std::atomic<bool> running_{false};
};

}  // namespace jsonrpc::server
