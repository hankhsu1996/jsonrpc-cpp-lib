#include "jsonrpc/endpoint/dispatcher.hpp"

#include <jsonrpc/endpoint/request.hpp>
#include <spdlog/spdlog.h>

namespace jsonrpc::endpoint {

Dispatcher::Dispatcher(std::shared_ptr<TaskExecutor> executor)
    : executor_(std::move(executor)) {
}

void Dispatcher::RegisterMethodCall(
    const std::string& method, const MethodCallHandler& handler) {
  method_handlers_[method] = handler;
}

void Dispatcher::RegisterNotification(
    const std::string& method, const NotificationHandler& handler) {
  notification_handlers_[method] = handler;
}

auto Dispatcher::DispatchRequest(const std::string& request)
    -> asio::awaitable<std::optional<std::string>> {
  auto request_json = Request::ParseAndValidateJson(request);
  if (!request_json) {
    co_return Response::CreateLibError(ErrorCode::kParseError).ToStr();
  }

  // Handle empty batch requests
  if (request_json->is_array() && request_json->empty()) {
    co_return Response::CreateLibError(ErrorCode::kInvalidRequest).ToStr();
  }

  if (request_json->is_array()) {
    co_return co_await DispatchBatchRequest(*request_json);
  }

  auto response_json = co_await DispatchSingleRequest(*request_json);
  if (!response_json) {
    co_return std::nullopt;
  }

  co_return response_json->dump();
}

auto Dispatcher::DispatchSingleRequest(const nlohmann::json& request_json)
    -> asio::awaitable<std::optional<nlohmann::json>> {
  auto validation_result = ValidateRequest(request_json);
  if (validation_result) {
    co_return validation_result->ToJson();
  }

  Request request = Request::FromJson(request_json);
  auto method = request.GetMethod();

  spdlog::info("Dispatching request: {}", request_json.dump());
  if (request.IsNotification()) {
    spdlog::info("Dispatching notification: {}", method);
    auto it = notification_handlers_.find(method);
    if (it != notification_handlers_.end()) {
      executor_->ExecuteDetached(
          [handler = it->second,
           params = request.GetParams()]() -> asio::awaitable<void> {
            return handler(params);
          });
    }
    co_return std::nullopt;
  }

  auto it = method_handlers_.find(method);
  if (it != method_handlers_.end()) {
    auto result = co_await executor_->Execute(
        [handler = it->second,
         params = request.GetParams()]() -> asio::awaitable<nlohmann::json> {
          return handler(params);
        });

    co_return Response::CreateResult(result, request.GetId()).ToJson();
  }

  co_return Response::CreateLibError(
      ErrorCode::kMethodNotFound, request.GetId())
      .ToJson();
}

auto Dispatcher::DispatchBatchRequest(const nlohmann::json& request_json)
    -> asio::awaitable<std::optional<std::string>> {
  if (request_json.empty()) {
    co_return Response::CreateLibError(ErrorCode::kInvalidRequest).ToStr();
  }

  std::vector<asio::awaitable<std::optional<nlohmann::json>>> pending_requests;
  pending_requests.reserve(request_json.size());

  // Queue all requests in parallel
  for (const auto& element : request_json) {
    // Validate individual request objects in the batch
    if (!element.is_object() || !Request::ValidateJson(element)) {
      // For invalid requests, create an immediate error response as a coroutine
      pending_requests.push_back(
          []() -> asio::awaitable<std::optional<nlohmann::json>> {
            auto error_json = Response::CreateLibError(
                                  ErrorCode::kInvalidRequest, std::nullopt)
                                  .ToJson();
            co_return error_json;
          }());
    } else {
      // For valid requests, dispatch them normally
      pending_requests.push_back(DispatchSingleRequest(element));
    }
  }

  // Wait for all requests to complete
  std::vector<nlohmann::json> responses;
  for (auto& pending_request : pending_requests) {
    auto response = co_await std::move(pending_request);
    if (response) {
      responses.push_back(*response);
    }
  }

  if (responses.empty()) {
    co_return std::nullopt;
  }

  co_return nlohmann::json(responses).dump();
}

auto Dispatcher::ValidateRequest(const nlohmann::json& request_json)
    -> std::optional<Response> {
  // We've already validated basic structure in ParseAndValidateJson
  // Here we validate method existence and other semantics

  if (!request_json.contains("method")) {
    return Response::CreateLibError(
        ErrorCode::kInvalidRequest, "Method is required");
  }

  const auto& method = request_json["method"];
  if (!method.is_string()) {
    return Response::CreateLibError(
        ErrorCode::kInvalidRequest, "Method must be a string");
  }

  // For params, if present, must be object or array
  if (request_json.contains("params")) {
    const auto& params = request_json["params"];
    if (!params.is_object() && !params.is_array() && !params.is_null()) {
      return Response::CreateLibError(
          ErrorCode::kInvalidParams, "Params must be object or array");
    }
  }

  // Request is valid
  return std::nullopt;
}

}  // namespace jsonrpc::endpoint
