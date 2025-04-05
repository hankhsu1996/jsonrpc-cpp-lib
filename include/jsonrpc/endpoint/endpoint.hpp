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
#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::endpoint {

using jsonrpc::error::RpcError;

using ErrorHandler = std::function<void(ErrorCode, const std::string &)>;

class RpcEndpoint {
 public:
  explicit RpcEndpoint(
      asio::any_io_executor executor,
      std::unique_ptr<transport::Transport> transport);

  static auto CreateClient(
      asio::any_io_executor executor,
      std::unique_ptr<transport::Transport> transport)
      -> asio::awaitable<std::expected<std::unique_ptr<RpcEndpoint>, RpcError>>;

  RpcEndpoint(const RpcEndpoint &) = delete;
  RpcEndpoint(RpcEndpoint &&) = delete;
  auto operator=(const RpcEndpoint &) -> RpcEndpoint & = delete;
  auto operator=(RpcEndpoint &&) -> RpcEndpoint & = delete;

  ~RpcEndpoint() = default;

  auto Start() -> asio::awaitable<std::expected<void, RpcError>>;

  auto WaitForShutdown() -> asio::awaitable<std::expected<void, RpcError>>;

  auto Shutdown() -> asio::awaitable<std::expected<void, RpcError>>;

  [[nodiscard]] auto IsRunning() const -> bool {
    return is_running_.load();
  }

  auto SendMethodCall(
      std::string method, std::optional<nlohmann::json> params = std::nullopt)
      -> asio::awaitable<std::expected<nlohmann::json, RpcError>>;

  template <typename ParamsType, typename ResultType>
  auto SendMethodCall(std::string method, ParamsType params)
      -> asio::awaitable<std::expected<ResultType, RpcError>>
    requires(
        !std::is_same_v<std::decay_t<ParamsType>, nlohmann::json> &&
        !std::is_same_v<
            std::decay_t<ParamsType>, std::optional<nlohmann::json>>);

  auto SendNotification(
      std::string method, std::optional<nlohmann::json> params = std::nullopt)
      -> asio::awaitable<std::expected<void, RpcError>>;

  template <typename ParamsType>
  auto SendNotification(std::string method, ParamsType params)
      -> asio::awaitable<std::expected<void, RpcError>>
    requires(
        !std::is_same_v<std::decay_t<ParamsType>, nlohmann::json> &&
        !std::is_same_v<
            std::decay_t<ParamsType>, std::optional<nlohmann::json>>);

  void RegisterMethodCall(
      std::string method, typename Dispatcher::MethodCallHandler handler);

  template <typename ParamsType, typename ResultType>
  void RegisterMethodCall(
      std::string method,
      std::function<asio::awaitable<ResultType>(ParamsType)> handler);

  void RegisterNotification(
      std::string method, typename Dispatcher::NotificationHandler handler);

  template <typename ParamsType>
  void RegisterNotification(
      std::string method,
      std::function<asio::awaitable<void>(ParamsType)> handler);

  [[nodiscard]] auto HasPendingRequests() const -> bool;

 private:
  void StartMessageProcessing();

  auto ProcessMessagesLoop(asio::cancellation_slot slot)
      -> asio::awaitable<void>;

  auto HandleMessage(std::string message)
      -> asio::awaitable<std::expected<void, RpcError>>;

  auto HandleResponse(Response response)
      -> asio::awaitable<std::expected<void, RpcError>>;

  auto GetNextRequestId() -> int64_t {
    return next_request_id_++;
  }

  asio::any_io_executor executor_;

  std::unique_ptr<transport::Transport> transport_;

  Dispatcher dispatcher_;

  std::unordered_map<int64_t, std::shared_ptr<PendingRequest>>
      pending_requests_;

  std::atomic<bool> is_running_{false};

  ErrorHandler error_handler_;

  asio::strand<asio::any_io_executor> endpoint_strand_;

  std::atomic<int64_t> next_request_id_{0};

  asio::cancellation_signal cancel_signal_;

  asio::awaitable<void> message_loop_;
};

template <typename ParamsType, typename ResultType>
auto RpcEndpoint::SendMethodCall(std::string method, ParamsType params)
    -> asio::awaitable<std::expected<ResultType, RpcError>>
  requires(
      !std::is_same_v<std::decay_t<ParamsType>, nlohmann::json> &&
      !std::is_same_v<std::decay_t<ParamsType>, std::optional<nlohmann::json>>)
{
  spdlog::debug("RpcEndpoint sending typed method call: {}", method);
  nlohmann::json json_params;
  try {
    json_params = params;
  } catch (const nlohmann::json::exception &ex) {
    spdlog::error(
        "RpcEndpoint failed to convert parameters to JSON: {}", ex.what());
    co_return error::CreateClientError(
        "Failed to convert parameters to JSON: " + std::string(ex.what()));
  }

  auto result = co_await RpcEndpoint::SendMethodCall(method, json_params);
  if (!result) {
    spdlog::error("RpcEndpoint failed to send method call: {}", method);
    co_return std::unexpected(result.error());
  }

  try {
    co_return result->template get<ResultType>();
  } catch (const nlohmann::json::exception &ex) {
    spdlog::error("RpcEndpoint failed to convert result: {}", ex.what());
    co_return error::CreateClientError(
        "Failed to convert result: " + std::string(ex.what()));
  }
}

template <typename ParamsType>
auto RpcEndpoint::SendNotification(std::string method, ParamsType params)
    -> asio::awaitable<std::expected<void, RpcError>>
  requires(
      !std::is_same_v<std::decay_t<ParamsType>, nlohmann::json> &&
      !std::is_same_v<std::decay_t<ParamsType>, std::optional<nlohmann::json>>)
{
  spdlog::debug("RpcEndpoint sending typed notification: {}", method);
  nlohmann::json json_params;
  try {
    json_params = params;
  } catch (const nlohmann::json::exception &ex) {
    spdlog::error(
        "RpcEndpoint failed to convert notification parameters: {}", ex.what());
    co_return error::CreateClientError(
        "Failed to convert notification parameters: " + std::string(ex.what()));
  }

  auto result = co_await RpcEndpoint::SendNotification(method, json_params);
  if (!result) {
    spdlog::error("RpcEndpoint failed to send notification: {}", method);
    co_return std::unexpected(result.error());
  }

  co_return std::expected<void, RpcError>{};
}

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
