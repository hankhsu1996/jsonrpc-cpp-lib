#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/request.hpp"
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

  void RegisterMethodCall(
      const std::string& method, const MethodCallHandler& handler);

  void RegisterNotification(
      const std::string& method, const NotificationHandler& handler);

  auto DispatchRequest(std::string request)
      -> asio::awaitable<std::optional<std::string>>;

 private:
  auto DispatchSingleRequest(Request request)
      -> asio::awaitable<std::optional<Response>>;

  auto DispatchBatchRequest(std::vector<Request> requests)
      -> asio::awaitable<std::vector<Response>>;

  std::unordered_map<std::string, MethodCallHandler> method_handlers_;

  std::unordered_map<std::string, NotificationHandler> notification_handlers_;

  asio::any_io_executor executor_;
};

}  // namespace jsonrpc::endpoint
