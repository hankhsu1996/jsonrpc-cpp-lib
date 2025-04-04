#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "jsonrpc/error/error.hpp"

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

  void RegisterMethodCall(
      const std::string& method, const MethodCallHandler& handler);

  void RegisterNotification(
      const std::string& method, const NotificationHandler& handler);

  auto DispatchRequest(std::string request)
      -> asio::awaitable<std::optional<std::string>>;

 private:
  auto DispatchSingleRequest(nlohmann::json request_json)
      -> asio::awaitable<std::optional<nlohmann::json>>;

  auto DispatchBatchRequest(nlohmann::json request_json)
      -> asio::awaitable<std::optional<std::string>>;

  static auto ValidateRequest(const nlohmann::json& request_json)
      -> std::expected<void, error::RpcError>;

  std::unordered_map<std::string, MethodCallHandler> method_handlers_;

  std::unordered_map<std::string, NotificationHandler> notification_handlers_;

  asio::any_io_executor executor_;
};

}  // namespace jsonrpc::endpoint
