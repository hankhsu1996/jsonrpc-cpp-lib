#include "jsonrpc/endpoint/response.hpp"

namespace jsonrpc::endpoint {

Response::Response(Response&& other) noexcept
    : response_(std::move(other.response_)) {
}

Response::Response(nlohmann::json response) : response_(std::move(response)) {
  ValidateResponse();
}

auto Response::FromJson(const nlohmann::json& json) -> Response {
  if (!json.contains("jsonrpc") || json["jsonrpc"] != "2.0") {
    throw std::invalid_argument("Invalid JSON-RPC version");
  }
  return Response(json);
}

auto Response::CreateResult(
    const nlohmann::json& result,
    const std::optional<RequestId>& id) -> Response {
  nlohmann::json response = {{"jsonrpc", "2.0"}, {"result", result}};
  if (id) {
    std::visit([&response](const auto& v) { response["id"] = v; }, *id);
  }
  return Response{std::move(response)};
}

auto Response::CreateLibError(
    ErrorCode error_code, const std::optional<RequestId>& id) -> Response {
  const int code = static_cast<int>(error_code);
  std::string message;
  switch (error_code) {
    case ErrorCode::kParseError:
      message = "Parse error";
      break;
    case ErrorCode::kInvalidRequest:
      message = "Invalid Request";
      break;
    case ErrorCode::kMethodNotFound:
      message = "Method not found";
      break;
    case ErrorCode::kInvalidParams:
      message = "Invalid params";
      break;
    case ErrorCode::kInternalError:
      message = "Internal error";
      break;
    case ErrorCode::kServerError:
      message = "Server error";
      break;
    case ErrorCode::kTransportError:
      message = "Transport error";
      break;
    case ErrorCode::kTimeoutError:
      message = "Timeout error";
      break;
  }
  return Response{CreateErrorResponse(message, code, id)};
}

auto Response::CreateUserError(
    const nlohmann::json& error,
    const std::optional<RequestId>& id) -> Response {
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

auto Response::ToStr() const -> std::string {
  return response_.dump();
}

auto Response::CreateErrorResponse(
    const std::string& message, int code,
    const std::optional<RequestId>& id) -> nlohmann::json {
  nlohmann::json error = {{"code", code}, {"message", message}};
  nlohmann::json response = {{"jsonrpc", "2.0"}, {"error", error}};
  if (id) {
    std::visit([&response](const auto& v) { response["id"] = v; }, *id);
  } else {
    response["id"] = nullptr;
  }
  return response;
}

void Response::ValidateResponse() const {
  if (!response_.contains("jsonrpc") || response_["jsonrpc"] != "2.0") {
    throw std::invalid_argument("Invalid JSON-RPC version");
  }

  if (!response_.contains("result") && !response_.contains("error")) {
    throw std::invalid_argument(
        "Response must contain either 'result' or 'error' field");
  }

  if (response_.contains("result") && response_.contains("error")) {
    throw std::invalid_argument(
        "Response cannot contain both 'result' and 'error' fields");
  }

  if (response_.contains("error")) {
    const auto& error = response_["error"];
    if (!error.contains("code") || !error.contains("message")) {
      throw std::invalid_argument(
          "Error object must contain 'code' and 'message' fields");
    }
  }
}

}  // namespace jsonrpc::endpoint
