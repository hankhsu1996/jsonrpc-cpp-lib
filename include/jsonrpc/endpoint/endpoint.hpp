#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/types.hpp"
#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::endpoint {

/**
 * @brief A JSON-RPC endpoint that can act as both client and server.
 *
 * This class implements the full JSON-RPC 2.0 specification, allowing an
 * endpoint to both send and receive method calls and notifications. Each
 * endpoint can:
 * - Send method calls and receive responses
 * - Send notifications
 * - Receive and handle method calls
 * - Receive and handle notifications
 */
class RpcEndpoint {
 public:
  /**
   * @brief Constructs an RPC endpoint with a specified transport layer.
   *
   * @param transport A unique pointer to a transport layer for communication.
   */
  explicit RpcEndpoint(std::unique_ptr<transport::Transport> transport);

  /// @brief Destructor ensures clean shutdown.
  ~RpcEndpoint();

  // Delete copy constructor and assignment
  RpcEndpoint(const RpcEndpoint&) = delete;
  auto operator=(const RpcEndpoint&) -> RpcEndpoint& = delete;

  // Delete move constructor and assignment
  RpcEndpoint(RpcEndpoint&&) = delete;
  auto operator=(RpcEndpoint&&) -> RpcEndpoint& = delete;

  /**
   * @brief Starts the endpoint's message processing.
   *
   * Initializes the message processing thread that handles incoming messages,
   * including method calls, notifications, and responses.
   */
  void Start();

  /**
   * @brief Stops the endpoint's message processing.
   *
   * Stops the message processing thread and cleans up resources.
   */
  void Stop();

  /**
   * @brief Checks if the endpoint is running.
   *
   * @return True if the message processing thread is active.
   */
  auto IsRunning() const -> bool;

  /**
   * @brief Sends a method call and waits for the response.
   *
   * @param method The name of the method to call.
   * @param params Optional parameters for the method.
   * @return The response from the remote endpoint.
   */
  auto SendMethodCall(
      const std::string& method,
      std::optional<nlohmann::json> params = std::nullopt) -> nlohmann::json;

  /**
   * @brief Sends a method call asynchronously.
   *
   * @param method The name of the method to call.
   * @param params Optional parameters for the method.
   * @return A future that will contain the response.
   */
  auto SendMethodCallAsync(
      const std::string& method,
      std::optional<nlohmann::json> params = std::nullopt)
      -> std::future<nlohmann::json>;

  /**
   * @brief Sends a notification.
   *
   * @param method The name of the notification method.
   * @param params Optional parameters for the notification.
   */
  void SendNotification(
      const std::string& method,
      std::optional<nlohmann::json> params = std::nullopt);

  /**
   * @brief Registers a method call handler.
   *
   * @param method The name of the method to handle.
   * @param handler The function to handle the method call.
   */
  void RegisterMethodCall(
      const std::string& method, const MethodCallHandler& handler);

  /**
   * @brief Registers a notification handler.
   *
   * @param method The name of the notification to handle.
   * @param handler The function to handle the notification.
   */
  void RegisterNotification(
      const std::string& method, const NotificationHandler& handler);

  /**
   * @brief Checks if there are any pending requests.
   *
   * @return True if there are pending requests awaiting responses.
   */
  [[nodiscard]] auto HasPendingRequests() const -> bool;

 private:
  /// @brief Processes incoming messages from the transport layer.
  void ProcessMessages();

  /**
   * @brief Handles an incoming message.
   *
   * Determines the type of message (request, notification, or response)
   * and processes it accordingly.
   *
   * @param message The raw message string.
   */
  void HandleMessage(const std::string& message);

  /**
   * @brief Validates a JSON-RPC message.
   *
   * @param json The message to validate.
   * @return True if the message is valid.
   */
  static auto ValidateMessage(const nlohmann::json& json) -> bool;

  /**
   * @brief Generates the next request ID.
   *
   * @return A unique request ID.
   */
  auto GetNextRequestId() -> RequestId;

  /// Transport layer for communication
  std::unique_ptr<transport::Transport> transport_;

  /// Counter for generating request IDs
  std::atomic<int64_t> request_id_counter_{0};

  /// Map of pending requests and their promises
  std::unordered_map<RequestId, std::promise<nlohmann::json>> pending_requests_;

  /// Mutex for protecting the pending requests map
  mutable std::mutex pending_requests_mutex_;

  /// Map of registered method call handlers
  std::unordered_map<std::string, MethodCallHandler> method_handlers_;

  /// Map of registered notification handlers
  std::unordered_map<std::string, NotificationHandler> notification_handlers_;

  /// Mutex for protecting handler maps
  mutable std::mutex handlers_mutex_;

  /// Message processing thread
  std::thread message_thread_;

  /// Flag indicating if the endpoint is running
  std::atomic<bool> is_running_{false};
};

}  // namespace jsonrpc::endpoint
