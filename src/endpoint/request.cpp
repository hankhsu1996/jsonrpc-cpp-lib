#include "jsonrpc/endpoint/request.hpp"

namespace jsonrpc::endpoint {

using error::RpcError;
using error::RpcErrorCode;

Request::Request(
    std::string method, std::optional<nlohmann::json> params,
    const std::function<RequestId()>& id_generator)
    : method_(std::move(method)),
      params_(std::move(params)),
      is_notification_(false),
      id_(id_generator()) {
}

Request::Request(
    std::string method, std::optional<nlohmann::json> params, RequestId id)
    : method_(std::move(method)),
      params_(std::move(params)),
      is_notification_(false),
      id_(std::move(id)) {
}

Request::Request(std::string method, std::optional<nlohmann::json> params)
    : method_(std::move(method)),
      params_(std::move(params)),
      is_notification_(true) {  // No ID for notifications
}

auto Request::FromJson(const nlohmann::json& json_obj)
    -> std::expected<Request, error::RpcError> {
  using error::RpcError;
  using error::RpcErrorCode;

  if (!json_obj.is_object()) {
    return RpcError::UnexpectedFromCode(
        RpcErrorCode::kInvalidRequest, "Request must be a JSON object");
  }

  if (!json_obj.contains("jsonrpc") ||
      (json_obj["jsonrpc"].get<std::string>() != kJsonRpcVersion)) {
    return RpcError::UnexpectedFromCode(
        RpcErrorCode::kInvalidRequest, "Missing or invalid 'jsonrpc' version");
  }

  if (!json_obj.contains("method") || !json_obj["method"].is_string()) {
    return RpcError::UnexpectedFromCode(
        RpcErrorCode::kInvalidRequest, "Missing or invalid 'method'");
  }

  auto method = json_obj["method"].get<std::string>();
  auto params = json_obj.contains("params")
                    ? std::optional<nlohmann::json>(json_obj["params"])
                    : std::nullopt;

  if (json_obj.contains("params")) {
    const auto& p = json_obj["params"];
    if (!p.is_array() && !p.is_object() && !p.is_null()) {
      return RpcError::UnexpectedFromCode(
          RpcErrorCode::kInvalidRequest,
          "'params' must be object, array, or null");
    }
  }

  if (!json_obj.contains("id")) {
    return Request(std::move(method), std::move(params));  // Notification
  }

  const auto& id_json = json_obj["id"];
  if (!id_json.is_string() && !id_json.is_number_integer()) {
    return RpcError::UnexpectedFromCode(
        RpcErrorCode::kInvalidRequest, "Invalid 'id' type");
  }

  RequestId id;
  if (id_json.is_string()) {
    id = id_json.get<std::string>();
  } else {
    id = id_json.get<int64_t>();
  }

  return Request(std::move(method), std::move(params), std::move(id));
}

auto Request::RequiresResponse() const -> bool {
  return !is_notification_;
}

auto Request::GetId() const -> RequestId {
  return id_;
}

auto Request::ToJson() const -> nlohmann::json {
  nlohmann::json json_obj;
  json_obj["jsonrpc"] = kJsonRpcVersion;
  json_obj["method"] = method_;

  if (params_.has_value()) {
    json_obj["params"] = params_.value();
  }

  if (!is_notification_) {
    if (std::holds_alternative<int64_t>(id_)) {
      json_obj["id"] = std::get<int64_t>(id_);
    } else {
      json_obj["id"] = std::get<std::string>(id_);
    }
  }

  return json_obj;
}

}  // namespace jsonrpc::endpoint
