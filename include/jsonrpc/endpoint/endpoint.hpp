#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
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
 * @brief Error handler function type.
 */
using ErrorHandler = std::function<void(ErrorCode, const std::string&)>;

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
   * This constructor is typically used for server-style endpoints where you
   * want explicit control over the lifecycle. Call Start() to begin processing.
   *
   * @param transport A unique pointer to a transport layer for communication.
   * @param id_generator A unique pointer to an ID generator strategy.
   */
  explicit RpcEndpoint(
      std::unique_ptr<transport::Transport> transport,
      std::unique_ptr<IdGenerator> id_generator =
          std::make_unique<IncrementalIdGenerator>());

  /**
   * @brief Creates a client endpoint that starts automatically.
   *
   * This factory method creates and starts an endpoint configured for client
   * usage. The endpoint begins processing messages immediately.
   *
   * @param transport A unique pointer to a transport layer for communication.
   * @return A unique pointer to the configured and started RpcEndpoint.
   */
  static auto CreateClient(std::unique_ptr<transport::Transport> transport)
      -> std::unique_ptr<RpcEndpoint>;

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
   * This method is typically used with server-style endpoints after
   * construction. For client endpoints created with CreateClient(), this is
   * called automatically.
   *
   * @throws std::runtime_error if the endpoint is already running
   * @return A task that completes when the endpoint is started
   */
  auto Start() -> std::future<void>;

  /**
   * @brief Blocks until the endpoint is shut down.
   *
   * This method blocks until Shutdown() is called from another thread
   * and all cleanup is complete. Typically used with server-style endpoints.
   */
  void Wait();

  /**
   * @brief Initiates a graceful shutdown of the endpoint.
   *
   * This method stops message processing and cleans up resources.
   * It is safe to call multiple times and from any thread.
   *
   * @return A task that completes when shutdown is complete
   */
  auto Shutdown() -> std::future<void>;

  /**
   * @brief Checks if the endpoint is currently running.
   * @return True if the endpoint is running; false otherwise.
   */
  auto IsRunning() const -> bool;

  /**
   * @brief Sends a method call and waits for the response.
   *
   * This is a blocking call that sends a request and waits for the response.
   *
   * @param method The method name to call.
   * @param params Optional parameters for the method call.
   * @return The JSON-RPC response.
   * @throws std::runtime_error if the transport is closed or on I/O errors
   */
  auto SendMethodCall(
      const std::string& method,
      std::optional<nlohmann::json> params = std::nullopt) -> nlohmann::json;

  /**
   * @brief Sends a method call asynchronously.
   *
   * This is a non-blocking call that returns a future for the response.
   *
   * @param method The method name to call.
   * @param params Optional parameters for the method call.
   * @return A task that resolves to the JSON-RPC response.
   * @throws std::runtime_error if the transport is closed or on I/O errors
   */
  auto SendMethodCallAsync(
      const std::string& method,
      std::optional<nlohmann::json> params = std::nullopt)
      -> std::future<nlohmann::json>;

  /**
   * @brief Sends a notification.
   *
   * This is a fire-and-forget call that doesn't expect a response.
   *
   * @param method The method name for the notification.
   * @param params Optional parameters for the notification.
   * @return A task that completes when the notification is sent
   * @throws std::runtime_error if the transport is closed or on I/O errors
   */
  auto SendNotification(
      const std::string& method,
      std::optional<nlohmann::json> params = std::nullopt) -> std::future<void>;

  /**
   * @brief Registers a method handler for incoming method calls.
   *
   * @param method The method name to register.
   * @param handler The handler function to call when the method is invoked.
   */
  void RegisterMethodCall(
      const std::string& method,
      std::function<nlohmann::json(const nlohmann::json&)> handler);

  /**
   * @brief Registers a notification handler for incoming notifications.
   *
   * @param method The method name to register.
   * @param handler The handler function to call when the notification is
   * received.
   */
  void RegisterNotification(
      const std::string& method,
      std::function<void(const nlohmann::json&)> handler);

  /**
   * @brief Checks if there are any pending requests.
   * @return True if there are pending requests; false otherwise.
   */
  [[nodiscard]] auto HasPendingRequests() const -> bool;

  /**
   * @brief Sets a handler for transport errors.
   * @param handler The error handler function.
   */
  void SetErrorHandler(ErrorHandler handler);

 private:
  /**
   * @brief Processes incoming messages from the transport layer.
   */
  auto ProcessMessages() -> std::future<void>;

  /**
   * @brief Handles a received message.
   *
   * This method parses the message and dispatches it to the appropriate
   * handler based on whether it's a request, notification, or response.
   *
   * @param message The JSON-RPC message as a string.
   */
  void HandleMessage(const std::string& message);

  /**
   * @brief Handles a response to a previous request.
   *
   * This method finds the corresponding promise for the response ID
   * and fulfills it with the response.
   *
   * @param response The JSON-RPC response.
   */
  void HandleResponse(const Response& response);

  /**
   * @brief Gets the next request ID from the ID generator.
   * @return The next request ID.
   */
  auto GetNextRequestId() -> RequestId;

  /**
   * @brief Executes a function with the transport.
   *
   * This method acquires the transport mutex and executes the provided
   * function with the transport.
   *
   * @param f The function to execute with the transport.
   * @return The result of the function.
   */
  template <typename Func>
  auto WithTransport(Func&& f) -> decltype(auto) {
    std::lock_guard<std::mutex> lock(transport_mutex_);
    return f(*transport_);
  }

  /**
   * @brief Reports an error through the error handler if set.
   *
   * @param code The error code.
   * @param message The error message.
   */
  void ReportError(ErrorCode code, const std::string& message);

  /// Default timeout for shutdown
  static constexpr auto kShutdownTimeout = std::chrono::milliseconds(5000);

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

  /// Flag indicating if the endpoint is running
  std::atomic<bool> is_running_{false};

  /// Mutex for protecting transport access
  mutable std::mutex transport_mutex_;

  /// Error handler
  ErrorHandler error_handler_;

  /// Mutex for protecting error handler
  mutable std::mutex error_handler_mutex_;

  /// Task for message processing
  std::optional<std::future<void>> message_task_;

  // Thread synchronization for clean shutdown
  std::mutex shutdown_mutex_;
  std::condition_variable shutdown_cv_;
  std::thread message_thread_;
  bool message_thread_started_ = false;

  // Helper methods for message processing
  void ProcessMessagesLoop();
  bool ProcessSingleMessage();
};

}  // namespace jsonrpc::endpoint
