#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/dispatcher.hpp"
#include "jsonrpc/endpoint/id_generator.hpp"
#include "jsonrpc/endpoint/response.hpp"
#include "jsonrpc/endpoint/types.hpp"
#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::endpoint {

/**
 * @brief A JSON-RPC endpoint that can act as both client and server.
 *
 * This class implements the full JSON-RPC 2.0 specification, allowing an
 * endpoint to both send and receive method calls and notifications. Each
 * endpoint can:
 * - Send method calls and receive responses (client role)
 * - Send notifications (client role)
 * - Receive and handle method calls (server role)
 * - Receive and handle notifications (server role)
 *
 * The endpoint is symmetric in its capabilities, meaning it can simultaneously:
 * - Act as a client by making requests to other endpoints
 * - Act as a server by handling requests from other endpoints
 *
 * This unified approach follows the JSON-RPC 2.0 specification where endpoints
 * are peers that can freely exchange requests and responses.
 */
class RpcEndpoint {
 public:
  /**
   * @brief Constructs an RPC endpoint with a specified transport layer.
   *
   * @param transport A unique pointer to a transport layer for communication.
   * @param id_generator A unique pointer to an ID generator strategy.
   * @param enable_multithreading Whether to enable multithreaded request
   * processing.
   * @param num_threads Number of threads to use when multithreading is enabled.
   */
  explicit RpcEndpoint(
      std::unique_ptr<transport::Transport> transport,
      std::unique_ptr<IdGenerator> id_generator =
          std::make_unique<IncrementalIdGenerator>(),
      bool enable_multithreading = true,
      size_t num_threads = std::thread::hardware_concurrency());

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

  /**
   * @brief Sets the timeout for method call requests.
   *
   * @param timeout The timeout duration. Use 0 for no timeout.
   */
  void SetRequestTimeout(std::chrono::milliseconds timeout);

  /**
   * @brief Sets the maximum number of requests in a batch.
   *
   * @param max_size The maximum number of requests allowed in a batch.
   */
  void SetMaxBatchSize(size_t max_size);

  /**
   * @brief Sets a handler for protocol-level errors.
   *
   * @param handler The function to handle errors.
   */
  void SetErrorHandler(ErrorHandler handler);

  /**
   * @brief Gets the number of pending requests.
   *
   * @return The number of requests awaiting responses.
   */
  [[nodiscard]] auto GetPendingRequestCount() const -> size_t;

  /**
   * @brief Gets the current request timeout.
   *
   * @return The current timeout duration.
   */
  [[nodiscard]] auto GetRequestTimeout() const -> std::chrono::milliseconds;

  /**
   * @brief Gets the maximum batch size.
   *
   * @return The maximum number of requests allowed in a batch.
   */
  [[nodiscard]] auto GetMaxBatchSize() const -> size_t;

 private:
  /// @brief Processes incoming messages from the transport layer.
  void ProcessMessages();

  /**
   * @brief Handles an incoming message.
   *
   * Routes messages to appropriate handlers:
   * - For responses: resolves pending client requests
   * - For requests/notifications: forwards to dispatcher
   *
   * @param message The raw message string.
   */
  void HandleMessage(const std::string& message);

  /**
   * @brief Handles an incoming response.
   *
   * Processes a response by finding and resolving the corresponding pending
   * request's promise.
   *
   * @param response The parsed response object.
   */
  void HandleResponse(const Response& response);

  /**
   * @brief Generates the next request ID.
   *
   * @return A unique request ID.
   */
  auto GetNextRequestId() -> RequestId;

  /**
   * @brief Executes a function with the transport under mutex protection.
   *
   * @tparam Func The type of the function to execute.
   * @param f The function to execute with the transport.
   * @return The result of the function.
   */
  template <typename Func>
  auto WithTransport(Func&& f) -> decltype(auto) {
    std::lock_guard<std::mutex> lock(transport_mutex_);
    return std::forward<Func>(f)(*transport_);
  }

  /**
   * @brief Reports an error through the error handler if set.
   *
   * @param code The error code.
   * @param message The error message.
   */
  void ReportError(ErrorCode code, const std::string& message);

  /// Transport layer for communication
  std::unique_ptr<transport::Transport> transport_;

  /// Dispatcher for handling incoming requests and notifications
  std::unique_ptr<Dispatcher> dispatcher_;

  /// ID generator for generating request IDs
  std::unique_ptr<IdGenerator> id_generator_;

  /// Map of pending requests and their promises
  std::unordered_map<RequestId, std::promise<nlohmann::json>> pending_requests_;

  /// Mutex for protecting the pending requests map
  mutable std::mutex pending_requests_mutex_;

  /// Message processing thread
  std::thread message_thread_;

  /// Flag indicating if the endpoint is running
  std::atomic<bool> is_running_{false};

  /// Mutex for protecting transport access
  mutable std::mutex transport_mutex_;

  /// Request timeout duration
  std::chrono::milliseconds request_timeout_{kDefaultRequestTimeout};

  /// Maximum batch size
  size_t max_batch_size_{kDefaultMaxBatchSize};

  /// Error handler
  ErrorHandler error_handler_;

  /// Mutex for protecting error handler
  mutable std::mutex error_handler_mutex_;
};

}  // namespace jsonrpc::endpoint
