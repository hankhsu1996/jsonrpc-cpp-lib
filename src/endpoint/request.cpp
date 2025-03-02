#include "jsonrpc/endpoint/request.hpp"

namespace jsonrpc::endpoint {

Request::Request(
    std::string method, std::optional<nlohmann::json> params,
    bool is_notification, const std::function<RequestId()>& id_generator)
    : method_(std::move(method)),
      params_(std::move(params)),
      is_notification_(is_notification) {
  if (!is_notification_) {
    id_ = id_generator();
  }
}

auto Request::RequiresResponse() const -> bool {
  return !is_notification_;
}

auto Request::GetId() const -> RequestId {
  return id_;
}

auto Request::Dump() const -> std::string {
  nlohmann::json json_request;
  json_request["jsonrpc"] = "2.0";
  json_request["method"] = method_;
  if (params_) {
    json_request["params"] = *params_;
  }
  if (!is_notification_) {
    // Handle both int64_t and string variants
    std::visit(
        [&json_request](const auto& id) { json_request["id"] = id; }, id_);
  }
  return json_request.dump();
}

}  // namespace jsonrpc::endpoint
