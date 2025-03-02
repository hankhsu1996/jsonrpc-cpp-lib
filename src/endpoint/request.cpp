#include "jsonrpc/endpoint/request.hpp"

namespace jsonrpc::endpoint {

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

auto Request::FromJson(const nlohmann::json& json_obj) -> Request {
  if (!ValidateJson(json_obj)) {
    throw std::invalid_argument("Invalid JSON-RPC request");
  }

  auto method = json_obj["method"].get<std::string>();
  auto params = json_obj.contains("params")
                    ? std::optional<nlohmann::json>(json_obj["params"])
                    : std::nullopt;

  if (!json_obj.contains("id")) {
    return Request(std::move(method), std::move(params));  // Notification
  }

  const auto& id_json = json_obj["id"];
  RequestId id;
  if (id_json.is_string()) {
    id = id_json.get<std::string>();
  } else {
    id = id_json.get<int64_t>();
  }

  return Request(std::move(method), std::move(params), std::move(id));
}

auto Request::ValidateJson(const nlohmann::json& json_obj) -> bool {
  if (!json_obj.is_object()) {
    return false;
  }
  if (!json_obj.contains("jsonrpc") || json_obj["jsonrpc"] != "2.0") {
    return false;
  }
  if (!json_obj.contains("method") || !json_obj["method"].is_string()) {
    return false;
  }

  if (json_obj.contains("params")) {
    const auto& params = json_obj["params"];
    if (!params.is_array() && !params.is_object() && !params.is_null()) {
      return false;
    }
  }

  if (json_obj.contains("id")) {
    const auto& id = json_obj["id"];
    if (!id.is_string() && !id.is_number_integer()) {
      return false;
    }
  }

  return true;
}

auto Request::RequiresResponse() const -> bool {
  return !is_notification_;
}

auto Request::GetId() const -> RequestId {
  return id_;
}

auto Request::Dump() const -> std::string {
  return ToJson().dump();
}

auto Request::ToJson() const -> nlohmann::json {
  nlohmann::json json_obj;
  json_obj["jsonrpc"] = "2.0";
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
