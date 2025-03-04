#include "jsonrpc/endpoint/dispatcher.hpp"

#include "spdlog/spdlog.h"

namespace jsonrpc::endpoint {

namespace {}  // namespace

Dispatcher::Dispatcher(asio::io_context::strand* strand) : strand_(strand) {
}

auto Dispatcher::DispatchRequest(const std::string& request_str)
    -> std::optional<std::string> {
  spdlog::debug("Dispatching request: {}", request_str);
  auto request_json = ParseAndValidateJson(request_str);
  if (!request_json.has_value()) {
    spdlog::error("Failed to parse request JSON");
    return Response::CreateLibError(ErrorCode::kParseError).ToStr();
  }

  if (request_json->is_array()) {
    spdlog::debug("Processing batch request");
    return DispatchBatchRequest(*request_json);
  }

  return DispatchSingleRequest(*request_json);
}

auto Dispatcher::ParseAndValidateJson(const std::string& request_str)
    -> std::optional<nlohmann::json> {
  try {
    auto json = nlohmann::json::parse(request_str);
    return json;
  } catch (const nlohmann::json::parse_error& e) {
    spdlog::error("JSON parse error: {}", e.what());
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
  spdlog::debug("Dispatching single request: {}", request_json.dump());
  auto validation_error = ValidateRequest(request_json);
  if (validation_error.has_value()) {
    spdlog::error("Request validation failed");
    return validation_error->ToJson();
  }

  Request request = Request::FromJson(request_json);
  spdlog::debug("Looking for handler for method: {}", request.GetMethod());
  auto optional_handler = FindHandler(handlers_, request.GetMethod());
  if (!optional_handler.has_value()) {
    spdlog::error("Method not found: {}", request.GetMethod());
    if (!request.IsNotification()) {
      return Response::CreateLibError(
                 ErrorCode::kMethodNotFound, request.GetId())
          .ToJson();
    }
    return std::nullopt;
  }

  spdlog::debug("Handler found for method: {}", request.GetMethod());
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
  // If a strand is provided, use it to process requests in a thread-safe way
  if (strand_ != nullptr) {
    // Create a shared state for collecting responses
    struct SharedState {
      std::mutex mutex;
      std::vector<nlohmann::json> responses;
      size_t pending_count;

      explicit SharedState(size_t count) : pending_count(count) {
      }
    };

    auto shared_state = std::make_shared<SharedState>(request_json.size());

    // Post each request processing to the strand
    for (const auto& element : request_json) {
      asio::post(*strand_, [this, element, shared_state]() {
        auto response = DispatchSingleRequestInner(element);

        if (response.has_value()) {
          // Add response to the shared vector in a thread-safe way
          std::lock_guard<std::mutex> lock(shared_state->mutex);
          shared_state->responses.push_back(response.value());
        }

        // Decrement the pending count
        {
          std::lock_guard<std::mutex> lock(shared_state->mutex);
          shared_state->pending_count--;
        }
      });
    }

    // Wait for all requests to be processed
    // This is not ideal as it introduces synchronous waiting, but
    // replacing with a fully asynchronous model would require API changes
    while (true) {
      std::unique_lock<std::mutex> lock(shared_state->mutex);
      if (shared_state->pending_count == 0) {
        return shared_state->responses;
      }
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  } else {
    // No strand available, process sequentially
    std::vector<nlohmann::json> responses;

    for (const auto& element : request_json) {
      auto response = DispatchSingleRequestInner(element);
      if (response.has_value()) {
        responses.push_back(response.value());
      }
    }

    return responses;
  }
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
  spdlog::debug("Handling request for method: {}", request.GetMethod());

  if (!request.IsNotification()) {
    spdlog::debug("Processing method call");
    if (std::holds_alternative<MethodCallHandler>(handler)) {
      const auto& method_call_handler = std::get<MethodCallHandler>(handler);
      Response response = HandleMethodCall(request, method_call_handler);
      return response.ToJson();
    }
    spdlog::error("Invalid handler type for method call");
    return Response::CreateLibError(ErrorCode::kInvalidRequest, request.GetId())
        .ToJson();
  }

  spdlog::debug("Processing notification");
  if (std::holds_alternative<NotificationHandler>(handler)) {
    const auto& notification_handler = std::get<NotificationHandler>(handler);
    HandleNotification(request, notification_handler);
  }

  return std::nullopt;
}

auto Dispatcher::HandleMethodCall(
    const Request& request, const MethodCallHandler& handler) -> Response {
  try {
    spdlog::debug("Executing method call handler for: {}", request.GetMethod());
    nlohmann::json response_json = handler(request.GetParams());
    spdlog::debug("Method handler executed successfully");
    return Response::CreateResult(response_json, request.GetId());
  } catch (const std::exception& e) {
    spdlog::error("Error in method call handler: {}", e.what());
    return Response::CreateLibError(ErrorCode::kInternalError, request.GetId());
  }
}

void Dispatcher::HandleNotification(
    const Request& request, const NotificationHandler& handler) {
  try {
    spdlog::debug(
        "Executing notification handler for: {}", request.GetMethod());
    handler(request.GetParams());
  } catch (const std::exception& e) {
    spdlog::error("Error in notification handler: {}", e.what());
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
