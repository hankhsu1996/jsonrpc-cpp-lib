#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/dispatcher.hpp"
#include "jsonrpc/endpoint/pending_request.hpp"
#include "jsonrpc/endpoint/response.hpp"
#include "jsonrpc/endpoint/task_executor.hpp"
#include "jsonrpc/endpoint/types.hpp"
#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::endpoint {

using ErrorHandler = std::function<void(ErrorCode, const std::string &)>;

/**
 * @brief RPC endpoint for sending and receiving JSON-RPC messages
 *
 * This class provides a JSON-RPC endpoint that can be used to call methods
 * on a remote endpoint and register method handlers for handling incoming
 * requests.
 */
class RpcEndpoint {
 public:
  /**
   * @brief Construct a new RPC endpoint
   *
   * @param io_ctx The IO context to use
   * @param transport The transport layer to use
   */
  explicit RpcEndpoint(
      asio::io_context &io_ctx,
      std::unique_ptr<transport::Transport> transport);

  /**
   * @brief Create a client endpoint asynchronously
   *
   * Creates and initializes a client endpoint. The returned awaitable resolves
   * when the endpoint is fully initialized and ready to use.
   *
   * @param io_ctx The IO context to use
   * @param transport The transport layer to use
   * @return asio::awaitable<std::unique_ptr<RpcEndpoint>> Awaitable that
   * resolves to the initialized client
   */
  static auto CreateClient(
      asio::io_context &io_ctx, std::unique_ptr<transport::Transport> transport)
      -> asio::awaitable<std::unique_ptr<RpcEndpoint>>;

  // Delete copy and move constructors/assignments
  RpcEndpoint(const RpcEndpoint &) = delete;
  auto operator=(const RpcEndpoint &) -> RpcEndpoint & = delete;
  RpcEndpoint(RpcEndpoint &&) = delete;
  auto operator=(RpcEndpoint &&) -> RpcEndpoint & = delete;

  /**
   * @brief Destructor
   */
  ~RpcEndpoint() = default;

  /**
   * @brief Start the endpoint
   *
   * Begins processing incoming messages and allows outgoing calls
   *
   * @return asio::awaitable<void>
   */
  auto Start() -> asio::awaitable<void>;

  /**
   * @brief Wait for the endpoint to shut down
   *
   * This will poll periodically until is_running_ is false
   *
   * @return asio::awaitable<void>
   */
  auto WaitForShutdown() -> asio::awaitable<void>;

  /**
   * @brief Shut down the endpoint
   *
   * Stops processing messages and cancels pending requests
   *
   * @return asio::awaitable<void>
   */
  auto Shutdown() -> asio::awaitable<void>;

  /**
   * @brief Check if the endpoint is running
   *
   * @return bool True if running, false otherwise
   */
  [[nodiscard]] auto IsRunning() const -> bool {
    return is_running_.load();
  }

  /**
   * @brief Call a method on the remote endpoint
   *
   * @param method The method name
   * @param params The method parameters (optional)
   * @return asio::awaitable<nlohmann::json> The result
   */
  auto CallMethod(
      const std::string &method,
      std::optional<nlohmann::json> params = std::nullopt)
      -> asio::awaitable<nlohmann::json>;

  /**
   * @brief Send a notification to the remote endpoint
   *
   * @param method The method name
   * @param params The method parameters (optional)
   * @return asio::awaitable<void>
   */
  auto SendNotification(
      const std::string &method, std::optional<nlohmann::json> params =
                                     std::nullopt) -> asio::awaitable<void>;

  /**
   * @brief Register a method call handler
   *
   * @param method The method name
   * @param handler The handler
   */
  void RegisterMethodCall(
      const std::string &method,
      typename Dispatcher::MethodCallHandler handler);

  /**
   * @brief Register a notification handler
   *
   * @param method The method name
   * @param handler The handler
   */
  void RegisterNotification(
      const std::string &method,
      typename Dispatcher::NotificationHandler handler);

  /**
   * @brief Check if there are pending requests
   *
   * @return bool True if there are pending requests, false otherwise
   */
  [[nodiscard]] auto HasPendingRequests() const -> bool;

  /**
   * @brief Set the error handler
   *
   * @param handler The error handler
   */
  void SetErrorHandler(ErrorHandler handler);

  /**
   * @brief Report an error
   *
   * @param code The error code
   * @param message The error message
   */
  void ReportError(ErrorCode code, const std::string &message);

 private:
  /**
   * @brief Start message processing
   */
  void StartMessageProcessing();

  /**
   * @brief Process the next message
   *
   * @return asio::awaitable<void>
   */
  auto ProcessNextMessage() -> asio::awaitable<void>;

  /**
   * @brief Handle a message
   *
   * @param message The message
   */
  auto HandleMessage(const std::string &message) -> asio::awaitable<void>;

  /**
   * @brief Handle a response
   *
   * @param response The response
   */
  auto HandleResponse(const Response &response) -> asio::awaitable<void>;

  /**
   * @brief Get the next request ID
   *
   * @return int64_t The next request ID
   */
  auto GetNextRequestId() -> int64_t {
    return next_request_id_++;
  }

  /**
   * @brief Schedule a retry for message processing
   */
  void ScheduleRetryProcessing();

  /// Reference to the IO context
  asio::io_context &io_ctx_;

  /// Transport layer
  std::unique_ptr<transport::Transport> transport_;

  /// Task executor for async processing
  std::shared_ptr<TaskExecutor> task_executor_;

  /// Dispatcher for handling requests
  Dispatcher dispatcher_;

  /// Pending requests
  std::unordered_map<int64_t, std::shared_ptr<PendingRequest>>
      pending_requests_;

  /// Running state flag
  std::atomic<bool> is_running_{false};

  /// Error handler
  ErrorHandler error_handler_;

  /// Strand for endpoint operations
  asio::strand<asio::any_io_executor> endpoint_strand_;

  /// Next request ID
  std::atomic<int64_t> next_request_id_{0};
};

/**
 * @brief Exception class for RPC errors
 */
class RpcError : public std::runtime_error {
 public:
  RpcError(ErrorCode code, const std::string &message)
      : std::runtime_error(message), code_(code) {
  }

  [[nodiscard]] auto GetCode() const -> ErrorCode {
    return code_;
  }

 private:
  ErrorCode code_;
};

}  // namespace jsonrpc::endpoint
