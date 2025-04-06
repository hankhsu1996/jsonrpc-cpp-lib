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

auto Response::CreateSuccess(
    const nlohmann::json& result, const std::optional<RequestId>& id)
    -> Response {
  nlohmann::json response = {{"jsonrpc", kJsonRpcVersion}, {"result", result}};
  if (id) {
    std::visit([&response](const auto& v) { response["id"] = v; }, *id);
  }
  return Response{std::move(response)};
}

auto Response::CreateError(
    RpcErrorCode code, const std::optional<RequestId>& id) -> Response {
  RpcError err = RpcError::FromCode(code);

  nlohmann::json error = {
      {"code", static_cast<int>(err.Code())}, {"message", err.Message()}};
  nlohmann::json response = {{"jsonrpc", kJsonRpcVersion}, {"error", error}};
  if (id) {
    std::visit([&response](const auto& v) { response["id"] = v; }, *id);
  } else {
    response["id"] = nullptr;
  }
  return Response{std::move(response)};
}

auto Response::CreateError(
    const RpcError& error, const std::optional<RequestId>& id) -> Response {
  nlohmann::json error_json = error.to_json();
  return CreateError(error_json, id);
}

auto Response::CreateError(
    const nlohmann::json& error, const std::optional<RequestId>& id)
    -> Response {
  nlohmann::json response = {{"jsonrpc", kJsonRpcVersion}, {"error", error}};
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

auto Response::ValidateResponse() const
    -> std::expected<void, error::RpcError> {
  if (!response_.contains("jsonrpc") ||
      response_["jsonrpc"] != kJsonRpcVersion) {
    return RpcError::UnexpectedFromCode(
        RpcErrorCode::kInvalidRequest, "Invalid JSON-RPC version");
  }

  if (!response_.contains("result") && !response_.contains("error")) {
    return RpcError::UnexpectedFromCode(
        RpcErrorCode::kInvalidRequest,
        "Response must contain either 'result' or 'error' field");
  }

  if (response_.contains("result") && response_.contains("error")) {
    return RpcError::UnexpectedFromCode(
        RpcErrorCode::kInvalidRequest,
        "Response cannot contain both 'result' and 'error' fields");
  }

  if (response_.contains("error")) {
    const auto& error = response_["error"];
    if (!error.contains("code") || !error.contains("message")) {
      return RpcError::UnexpectedFromCode(
          RpcErrorCode::kInvalidRequest,
          "Error object must contain 'code' and 'message' fields");
    }
  }

  return {};
}

}  // namespace jsonrpc::endpoint
