#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/dispatcher.hpp"
#include "jsonrpc/endpoint/pending_request.hpp"
#include "jsonrpc/endpoint/response.hpp"
#include "jsonrpc/endpoint/typed_handlers.hpp"
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
   * @param executor The executor to use
   * @param transport The transport layer to use
   */
  explicit RpcEndpoint(
      asio::any_io_executor executor,
      std::unique_ptr<transport::Transport> transport);

  /**
   * @brief Create a client endpoint asynchronously
   *
   * Creates and initializes a client endpoint. The returned awaitable resolves
   * when the endpoint is fully initialized and ready to use.
   *
   * @param executor The executor to use
   * @param transport The transport layer to use
   * @return asio::awaitable<std::unique_ptr<RpcEndpoint>> Awaitable that
   * resolves to the initialized client
   */
  static auto CreateClient(
      asio::any_io_executor executor,
      std::unique_ptr<transport::Transport> transport)
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
  auto SendMethodCall(
      std::string method, std::optional<nlohmann::json> params = std::nullopt)
      -> asio::awaitable<nlohmann::json>;

  /**
   * @brief Call a method on the remote endpoint with typed params and result
   *
   * @tparam ParamsType The type of the parameters
   * @tparam ResultType The type of the result
   * @param method The method name
   * @param params The method parameters
   * @return asio::awaitable<ResultType> The typed result
   */
  template <typename ParamsType, typename ResultType>
  auto SendMethodCall(std::string method, ParamsType params)
      -> asio::awaitable<ResultType>;

  /**
   * @brief Send a notification to the remote endpoint
   *
   * @param method The method name
   * @param params The method parameters (optional)
   * @return asio::awaitable<void>
   */
  auto SendNotification(
      std::string method, std::optional<nlohmann::json> params = std::nullopt)
      -> asio::awaitable<void>;

  /**
   * @brief Send a notification to the remote endpoint with typed params
   *
   * @tparam ParamsType The type of the parameters
   * @param method The method name
   * @param params The method parameters
   * @return asio::awaitable<void>
   */
  template <typename ParamsType>
  auto SendNotification(std::string method, ParamsType params)
      -> asio::awaitable<void>;

  /**
   * @brief Register a method call handler
   *
   * @param method The method name
   * @param handler The handler
   */
  void RegisterMethodCall(
      std::string method, typename Dispatcher::MethodCallHandler handler);

  /**
   * @brief Register a typed method call handler
   *
   * @tparam ParamsType The type of the parameters
   * @tparam ResultType The type of the result
   * @param method The method name
   * @param handler The handler
   */
  template <typename ParamsType, typename ResultType>
  void RegisterMethodCall(
      std::string method,
      std::function<asio::awaitable<ResultType>(ParamsType)> handler);

  /**
   * @brief Register a notification handler
   *
   * @param method The method name
   * @param handler The handler
   */
  void RegisterNotification(
      std::string method, typename Dispatcher::NotificationHandler handler);

  /**
   * @brief Register a typed notification handler
   *
   * @tparam ParamsType The type of the parameters
   * @param method The method name
   * @param handler The handler
   */
  template <typename ParamsType>
  void RegisterNotification(
      std::string method,
      std::function<asio::awaitable<void>(ParamsType)> handler);

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
   * @brief Process messages in a continuous loop
   *
   * @return asio::awaitable<void>
   */
  auto ProcessMessagesLoop() -> asio::awaitable<void>;

  /**
   * @brief Handle a message
   *
   * @param message The message
   */
  auto HandleMessage(std::string message) -> asio::awaitable<void>;

  /**
   * @brief Handle a response
   *
   * @param response The response
   */
  auto HandleResponse(Response response) -> asio::awaitable<void>;

  /**
   * @brief Get the next request ID
   *
   * @return int64_t The next request ID
   */
  auto GetNextRequestId() -> int64_t {
    return next_request_id_++;
  }

  /// The executor to use for operations
  asio::any_io_executor executor_;

  /// Transport layer
  std::unique_ptr<transport::Transport> transport_;

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

// Send method template implementations

template <typename ParamsType, typename ResultType>
auto RpcEndpoint::SendMethodCall(std::string method, ParamsType params)
    -> asio::awaitable<ResultType> {
  try {
    // Convert typed params to JSON
    nlohmann::json json_params = params;

    // Call the method
    nlohmann::json result = co_await SendMethodCall(method, json_params);

    // Convert result to typed result
    ResultType typed_result = result.get<ResultType>();

    co_return typed_result;
  } catch (const nlohmann::json::exception &ex) {
    // Handle JSON conversion errors
    throw RpcError(
        ErrorCode::kInvalidParams,
        std::string("Result conversion error: ") + ex.what());
  }
}

template <typename ParamsType>
auto RpcEndpoint::SendNotification(std::string method, ParamsType params)
    -> asio::awaitable<void> {
  try {
    // Convert typed params to JSON and send
    nlohmann::json json_params = params;
    co_await SendNotification(method, json_params);
  } catch (const nlohmann::json::exception &ex) {
    // Log the error but don't propagate it (notifications are
    // fire-and-forget)
    spdlog::error(
        "Failed to convert parameters for notification method '{}': {}", method,
        ex.what());
  } catch (const std::exception &ex) {
    // Log any other errors that might occur
    spdlog::error(
        "Unexpected error in notification method '{}': {}", method, ex.what());
  }
}

// Register method template implementations

template <typename ParamsType, typename ResultType>
void RpcEndpoint::RegisterMethodCall(
    std::string method,
    std::function<asio::awaitable<ResultType>(ParamsType)> handler) {
  // Create a handler object and store its function object
  // NOTE: We use a shared_ptr to ensure the handler object stays alive
  // throughout the entire lifetime of any coroutine that might use it. This
  // prevents the use-after-free issues that can occur with lambda captures in
  // coroutines.
  auto typed_handler =
      std::make_shared<TypedMethodHandler<ParamsType, ResultType>>(
          std::move(handler));

  // Register a lambda that calls the handler object
  RegisterMethodCall(
      method,
      [handler = std::move(typed_handler)](
          std::optional<nlohmann::json> params) { return (*handler)(params); });
}

template <typename ParamsType>
void RpcEndpoint::RegisterNotification(
    std::string method,
    std::function<asio::awaitable<void>(ParamsType)> handler) {
  // Create a handler object and store its function object
  // NOTE: Using a class-based approach with shared_ptr ownership guarantees
  // the handler remains valid even when coroutines are suspended and resumed,
  // which is safer than direct lambda captures that may go out of scope.
  auto typed_handler = std::make_shared<TypedNotificationHandler<ParamsType>>(
      std::move(handler));

  // Register a lambda that calls the handler object
  RegisterNotification(
      method,
      [handler = std::move(typed_handler)](
          std::optional<nlohmann::json> params) { return (*handler)(params); });
}

}  // namespace jsonrpc::endpoint
