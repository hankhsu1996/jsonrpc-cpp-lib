#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/response.hpp"

namespace jsonrpc::endpoint {

class Dispatcher {
 public:
  using MethodCallHandler = std::function<asio::awaitable<nlohmann::json>(
      const std::optional<nlohmann::json>&)>;
  using NotificationHandler = std::function<asio::awaitable<void>(
      const std::optional<nlohmann::json>&)>;

  explicit Dispatcher(asio::any_io_executor executor);

  Dispatcher(const Dispatcher&) = delete;
  Dispatcher(Dispatcher&&) = delete;
  auto operator=(const Dispatcher&) -> Dispatcher& = delete;
  auto operator=(Dispatcher&&) -> Dispatcher& = delete;
  virtual ~Dispatcher() = default;

  /// @brief Register a method call handler
  void RegisterMethodCall(
      const std::string& method, const MethodCallHandler& handler);

  /// @brief Register a notification handler
  void RegisterNotification(
      const std::string& method, const NotificationHandler& handler);

  /// @brief Dispatch a request
  auto DispatchRequest(std::string request)
      -> asio::awaitable<std::optional<std::string>>;

 private:
  /// @brief Dispatch a single request
  auto DispatchSingleRequest(nlohmann::json request_json)
      -> asio::awaitable<std::optional<nlohmann::json>>;

  /// @brief Dispatch a batch request
  auto DispatchBatchRequest(nlohmann::json request_json)
      -> asio::awaitable<std::optional<std::string>>;

  /// @brief Validate a request
  static auto ValidateRequest(const nlohmann::json& request_json)
      -> std::optional<Response>;

  /// @brief Method call handlers
  std::unordered_map<std::string, MethodCallHandler> method_handlers_;

  /// @brief Notification handlers
  std::unordered_map<std::string, NotificationHandler> notification_handlers_;

  /// @brief Executor
  asio::any_io_executor executor_;
};

}  // namespace jsonrpc::endpoint
