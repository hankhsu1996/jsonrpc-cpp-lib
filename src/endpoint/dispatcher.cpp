#include "jsonrpc/endpoint/dispatcher.hpp"

#include <jsonrpc/endpoint/request.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/response.hpp"

namespace jsonrpc::endpoint {

using jsonrpc::error::ErrorCode;
using jsonrpc::error::RpcError;

Dispatcher::Dispatcher(asio::any_io_executor executor)
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

auto Dispatcher::DispatchRequest(std::string request)
    -> asio::awaitable<std::optional<std::string>> {
  nlohmann::json root;
  root = nlohmann::json::parse(request, nullptr, false);
  if (root.is_discarded()) {
    co_return Response::CreateError(ErrorCode::kParseError).ToJson().dump();
  }

  // Single request
  if (root.is_object()) {
    auto request = Request::FromJson(root);
    if (!request.has_value()) {
      co_return Response::CreateError(request.error()).ToJson().dump();
    }

    auto response = co_await DispatchSingleRequest(request.value());
    if (response.has_value()) {
      co_return response.value().ToJson().dump();
    }
    co_return std::nullopt;
  }

  // Batch request
  if (root.is_array()) {
    if (root.empty()) {
      co_return Response::CreateError(ErrorCode::kInvalidRequest)
          .ToJson()
          .dump();
    }

    std::vector<Request> requests;
    std::vector<Response> responses;
    for (const auto& element : root) {
      auto request = Request::FromJson(element);
      if (!request.has_value()) {
        responses.push_back(Response::CreateError(request.error()));
        continue;
      }
      requests.push_back(request.value());
    }

    auto dispatched = co_await DispatchBatchRequest(requests);
    for (const auto& response : dispatched) {
      responses.push_back(response);
    }

    co_return nlohmann::json(responses).dump();
  }

  co_return Response::CreateError(ErrorCode::kInvalidRequest).ToJson().dump();
}

auto Dispatcher::DispatchSingleRequest(Request request)
    -> asio::awaitable<std::optional<Response>> {
  auto method = request.GetMethod();

  if (request.IsNotification()) {
    auto it = notification_handlers_.find(method);
    if (it != notification_handlers_.end()) {
      co_spawn(
          executor_,
          [handler = it->second, params = request.GetParams()] {
            return handler(params);
          },
          asio::detached);
    }
    co_return std::nullopt;
  }

  auto it = method_handlers_.find(method);
  if (it != method_handlers_.end()) {
    auto result = co_await asio::co_spawn(
        executor_,
        [handler = it->second, params = request.GetParams()] {
          return handler(params);
        },
        asio::use_awaitable);
    co_return Response::CreateSuccess(result, request.GetId());
  }

  co_return Response::CreateError(ErrorCode::kMethodNotFound, request.GetId());
}

auto Dispatcher::DispatchBatchRequest(std::vector<Request> requests)
    -> asio::awaitable<std::vector<Response>> {
  std::vector<asio::awaitable<std::optional<Response>>> pending;
  pending.reserve(requests.size());

  // Queue all requests in parallel
  for (const auto& request : requests) {
    // For valid requests, dispatch them normally
    pending.push_back(DispatchSingleRequest(request));
  }

  // Wait for all requests to complete
  std::vector<Response> responses;
  for (auto& awaitable_response : pending) {
    auto response = co_await std::move(awaitable_response);
    if (response.has_value()) {
      responses.push_back(response.value());
    }
  }

  co_return responses;
}

}  // namespace jsonrpc::endpoint
