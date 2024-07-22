#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "json_rpc/core/request.hpp"
#include "json_rpc/core/response.hpp"
#include "json_rpc/core/types.hpp"

#include "BS_thread_pool.hpp"

namespace json_rpc {

class Dispatcher {
public:
  Dispatcher(bool enableMultithreading = true,
      size_t numThreads = std::thread::hardware_concurrency())
      : enableMultithreading_(enableMultithreading),
        thread_pool_(enableMultithreading ? numThreads : 0) {
  }

  virtual ~Dispatcher() = default;

  // Dispatch an RPC request to the appropriate handler.
  std::optional<std::string> DispatchRequest(const std::string &request);

  // Register a method handler for a specific method.
  void RegisterMethodCall(
      const std::string &method, const MethodCallHandler &handler);

  // Register a notification handler for a specific method.
  void RegisterNotification(
      const std::string &method, const NotificationHandler &handler);

private:
  std::unordered_map<std::string, Handler> handlers_;
  bool enableMultithreading_;
  BS::thread_pool thread_pool_;

  // Parse and validate the JSON request string.
  std::optional<Json> ParseAndValidateJson(const std::string &requestStr);

  // Dispatch a single request to the appropriate handler.
  std::optional<std::string> DispatchSingleRequest(const Json &requestJson);
  std::optional<Json> DispatchSingleRequestInner(const Json &requestJson);

  // Dispatch a batch request to the appropriate handlers.
  std::optional<std::string> DispatchBatchRequest(const Json &requestJson);
  std::vector<Json> DispatchBatchRequestInner(const Json &requestJson);

  // Validate the request JSON object.
  std::optional<Response> ValidateRequest(const Json &requestJson);

  // Find the handler for the specified method.
  std::optional<Handler> FindHandler(
      const std::unordered_map<std::string, Handler> &handlers,
      const std::string &method);

  // Handle the request (method call or notification) using the appropriate
  // handler.
  std::optional<Json> HandleRequest(
      const Request &request, const Handler &handler);

  // Handle a method call request.
  Response HandleMethodCall(
      const Request &request, const MethodCallHandler &handler);

  // Handle a notification request.
  void HandleNotification(
      const Request &request, const NotificationHandler &handler);
};

} // namespace json_rpc
