#include "jsonrpc/endpoint/dispatcher.hpp"

namespace jsonrpc::endpoint {

namespace {}  // namespace

Dispatcher::Dispatcher(bool enable_multithreading, size_t num_threads)
    : enable_multithreading_(enable_multithreading),
      thread_pool_(enable_multithreading ? num_threads : 0) {
}

auto Dispatcher::DispatchRequest(const std::string& request_str)
    -> std::optional<std::string> {
  auto request_json = ParseAndValidateJson(request_str);
  if (!request_json.has_value()) {
    return Response::CreateLibError(ErrorCode::kParseError).ToStr();
  }

  if (request_json->is_array()) {
    return DispatchBatchRequest(*request_json);
  }

  return DispatchSingleRequest(*request_json);
}

auto Dispatcher::ParseAndValidateJson(const std::string& request_str)
    -> std::optional<nlohmann::json> {
  try {
    return nlohmann::json::parse(request_str);
  } catch (const nlohmann::json::parse_error&) {
    return std::nullopt;
  }
}

auto Dispatcher::DispatchSingleRequest(const nlohmann::json& request_json)
    -> std::optional<std::string> {
  auto response_json = DispatchSingleRequestInner(request_json);
  if (response_json.has_value()) {
    return response_json->dump();
  }
  return std::nullopt;
}

auto Dispatcher::DispatchSingleRequestInner(const nlohmann::json& request_json)
    -> std::optional<nlohmann::json> {
  auto validation_error = ValidateRequest(request_json);
  if (validation_error.has_value()) {
    return validation_error->ToJson();
  }

  Request request = Request::FromJson(request_json);
  auto optional_handler = FindHandler(handlers_, request.GetMethod());
  if (!optional_handler.has_value()) {
    if (!request.IsNotification()) {
      return Response::CreateLibError(
                 ErrorCode::kMethodNotFound, request.GetId())
          .ToJson();
    }
    return std::nullopt;
  }

  return HandleRequest(request, optional_handler.value());
}

auto Dispatcher::DispatchBatchRequest(const nlohmann::json& request_json)
    -> std::optional<std::string> {
  if (request_json.empty()) {
    return Response::CreateLibError(ErrorCode::kInvalidRequest).ToStr();
  }

  auto response_jsons = DispatchBatchRequestInner(request_json);
  if (response_jsons.empty()) {
    return std::nullopt;
  }

  return nlohmann::json(response_jsons).dump();
}

auto Dispatcher::DispatchBatchRequestInner(const nlohmann::json& request_json)
    -> std::vector<nlohmann::json> {
  std::vector<std::future<std::optional<nlohmann::json>>> futures;

  for (const auto& element : request_json) {
    if (enable_multithreading_) {
      futures.emplace_back(thread_pool_.submit_task(
          [this, element]() -> std::optional<nlohmann::json> {
            return DispatchSingleRequestInner(element);
          }));
    } else {
      futures.emplace_back(std::async(
          std::launch::deferred,
          [this, element]() -> std::optional<nlohmann::json> {
            return DispatchSingleRequestInner(element);
          }));
    }
  }

  std::vector<nlohmann::json> responses;
  for (auto& future : futures) {
    std::optional<nlohmann::json> response = future.get();
    if (response.has_value()) {
      responses.push_back(response.value());
    }
  }

  return responses;
}

auto Dispatcher::ValidateRequest(const nlohmann::json& request_json)
    -> std::optional<Response> {
  if (!request_json.is_object()) {
    return Response::CreateLibError(ErrorCode::kInvalidRequest);
  }

  if (!request_json.contains("jsonrpc") || request_json["jsonrpc"] != "2.0") {
    return Response::CreateLibError(ErrorCode::kInvalidRequest);
  }

  if (!request_json.contains("method")) {
    if (request_json.contains("id")) {
      return Response::CreateLibError(ErrorCode::kInvalidRequest);
    }
    return std::nullopt;
  }

  if (!request_json["method"].is_string()) {
    return Response::CreateLibError(ErrorCode::kInvalidRequest);
  }

  return std::nullopt;
}

auto Dispatcher::FindHandler(
    const std::unordered_map<std::string, Handler>& handlers,
    const std::string& method) -> std::optional<Handler> {
  auto it = handlers.find(method);
  if (it != handlers.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto Dispatcher::HandleRequest(const Request& request, const Handler& handler)
    -> std::optional<nlohmann::json> {
  if (!request.IsNotification()) {
    if (std::holds_alternative<MethodCallHandler>(handler)) {
      const auto& method_call_handler = std::get<MethodCallHandler>(handler);
      Response response = HandleMethodCall(request, method_call_handler);
      return response.ToJson();
    }
    return Response::CreateLibError(ErrorCode::kInvalidRequest, request.GetId())
        .ToJson();
  }

  if (std::holds_alternative<NotificationHandler>(handler)) {
    const auto& notification_handler = std::get<NotificationHandler>(handler);
    HandleNotification(request, notification_handler);
    return std::nullopt;
  }

  return std::nullopt;
}

auto Dispatcher::HandleMethodCall(
    const Request& request, const MethodCallHandler& handler) -> Response {
  try {
    nlohmann::json response_json = handler(request.GetParams());
    return Response::CreateResult(response_json, request.GetId());
  } catch (const std::exception&) {
    return Response::CreateLibError(ErrorCode::kInternalError, request.GetId());
  }
}

void Dispatcher::HandleNotification(
    const Request& request, const NotificationHandler& handler) {
  try {
    handler(request.GetParams());
  } catch (const std::exception&) {
    // Notifications don't return errors
  }
}

void Dispatcher::RegisterMethodCall(
    const std::string& method, const MethodCallHandler& handler) {
  handlers_[method] = handler;
}

void Dispatcher::RegisterNotification(
    const std::string& method, const NotificationHandler& handler) {
  handlers_[method] = handler;
}

}  // namespace jsonrpc::endpoint
