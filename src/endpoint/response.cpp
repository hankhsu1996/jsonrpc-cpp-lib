#include "jsonrpc/endpoint/response.hpp"

#include <jsonrpc/error/error.hpp>

namespace jsonrpc::endpoint {

using jsonrpc::error::RpcError;

auto Response::FromJson(const nlohmann::json& json)
    -> std::expected<Response, error::RpcError> {
  Response r{json};
  if (auto result = r.ValidateResponse(); !result) {
    return std::unexpected(result.error());
  }
  return r;
}

auto Response::CreateResult(
    const nlohmann::json& result, const std::optional<RequestId>& id)
    -> Response {
  nlohmann::json response = {{"jsonrpc", "2.0"}, {"result", result}};
  if (id) {
    std::visit([&response](const auto& v) { response["id"] = v; }, *id);
  }
  return Response{std::move(response)};
}

auto Response::CreateLibError(
    ErrorCode code, const std::optional<RequestId>& id) -> Response {
  RpcError err{code};

  nlohmann::json error = {
      {"code", static_cast<int>(err.code)}, {"message", err.message}};
  nlohmann::json response = {{"jsonrpc", "2.0"}, {"error", error}};
  if (id) {
    std::visit([&response](const auto& v) { response["id"] = v; }, *id);
  } else {
    response["id"] = nullptr;
  }
  return Response{std::move(response)};
}

auto Response::CreateUserError(
    const nlohmann::json& error, const std::optional<RequestId>& id)
    -> Response {
  nlohmann::json response = {{"jsonrpc", "2.0"}, {"error", error}};
  if (id) {
    std::visit([&response](const auto& v) { response["id"] = v; }, *id);
  }
  return Response{std::move(response)};
}

auto Response::IsSuccess() const -> bool {
  return response_.contains("result");
}

auto Response::GetResult() const -> const nlohmann::json& {
  if (!IsSuccess()) {
    throw std::runtime_error("Response is not a success response");
  }
  return response_["result"];
}

auto Response::GetError() const -> const nlohmann::json& {
  if (IsSuccess()) {
    throw std::runtime_error("Response is not an error response");
  }
  return response_["error"];
}

auto Response::GetId() const -> std::optional<RequestId> {
  if (!response_.contains("id") || response_["id"].is_null()) {
    return std::nullopt;
  }

  const auto& id = response_["id"];
  if (id.is_string()) {
    return id.get<std::string>();
  }
  return id.get<int64_t>();
}

auto Response::ToJson() const -> nlohmann::json {
  return response_;
}

inline auto InvalidRequestError(std::string message) {
  return std::unexpected(RpcError{ErrorCode::kInvalidRequest, message});
}

auto Response::ValidateResponse() const
    -> std::expected<void, error::RpcError> {
  if (!response_.contains("jsonrpc") || response_["jsonrpc"] != "2.0") {
    return InvalidRequestError("Invalid JSON-RPC version");
  }

  if (!response_.contains("result") && !response_.contains("error")) {
    return InvalidRequestError(
        "Response must contain either 'result' or 'error' field");
  }

  if (response_.contains("result") && response_.contains("error")) {
    return InvalidRequestError(
        "Response cannot contain both 'result' and 'error' fields");
  }

  if (response_.contains("error")) {
    const auto& error = response_["error"];
    if (!error.contains("code") || !error.contains("message")) {
      return InvalidRequestError(
          "Error object must contain 'code' and 'message' fields");
    }
  }

  return {};
}

}  // namespace jsonrpc::endpoint
