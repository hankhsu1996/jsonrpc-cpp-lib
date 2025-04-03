#pragma once

#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

namespace jsonrpc::error {

enum class ErrorCode {
  // Standard errors
  kParseError = -32700,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kInvalidParams = -32602,
  kInternalError = -32603,

  // Implementation-defined server errors
  kServerError = -32000,
  kTransportError = -32010,
  kTimeoutError = -32001,
};

// Base error type for all JSON-RPC errors
struct RpcError {
  RpcError(ErrorCode code, std::string message)
      : code(code), message(std::move(message)) {
  }
  ErrorCode code;
  std::string message;

  // Convert to JSON-RPC error object
  [[nodiscard]] auto to_json() const -> nlohmann::json {
    nlohmann::json json;
    json["code"] = static_cast<int>(code);
    json["message"] = message;
    return json;
  }
};

// Transport-related errors (network, IO failures, etc)
struct TransportError : RpcError {
  std::error_code system_error;

  explicit TransportError(std::string msg, std::error_code ec = {})
      : RpcError(ErrorCode::kTransportError, std::move(msg)), system_error(ec) {
    if (system_error) {
      message += ": " + system_error.message();
    }
  }

  [[nodiscard]] auto to_json() const -> nlohmann::json {
    nlohmann::json json = RpcError::to_json();
    if (system_error) {
      nlohmann::json data;
      data["system_code"] = system_error.value();
      data["system_message"] = system_error.message();
      json["data"] = data;
    }
    return json;
  }
};

// Server lifecycle errors (start/stop failures, resource issues)
struct ServerError : RpcError {
  explicit ServerError(std::string msg)
      : RpcError(ErrorCode::kServerError, std::move(msg)) {
  }
};

// Factory functions to create errors
[[nodiscard]] inline auto CreateProtocolError(
    ErrorCode code, std::string message) -> RpcError {
  return RpcError{code, std::move(message)};
}

[[nodiscard]] inline auto CreateParseError(std::string message = "Parse error")
    -> RpcError {
  return RpcError{ErrorCode::kParseError, std::move(message)};
}

[[nodiscard]] inline auto CreateInvalidRequest(
    std::string message = "Invalid request") -> RpcError {
  return RpcError{ErrorCode::kInvalidRequest, std::move(message)};
}

[[nodiscard]] inline auto CreateMethodNotFound(
    std::string message = "Method not found") -> RpcError {
  return RpcError{ErrorCode::kMethodNotFound, std::move(message)};
}

[[nodiscard]] inline auto CreateInvalidParams(
    std::string message = "Invalid parameters") -> RpcError {
  return RpcError{ErrorCode::kInvalidParams, std::move(message)};
}

[[nodiscard]] inline auto CreateInternalError(
    std::string message = "Internal error") -> RpcError {
  return RpcError{ErrorCode::kInternalError, std::move(message)};
}

[[nodiscard]] inline auto CreateTransportError(
    std::string message, std::error_code ec = {}) -> TransportError {
  return TransportError(std::move(message), ec);
}

[[nodiscard]] inline auto CreateServerError(
    std::string message = "Server error") -> ServerError {
  return ServerError(std::move(message));
}

}  // namespace jsonrpc::error
