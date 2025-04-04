#pragma once

#include <expected>
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

  // Client errors
  kClientError = -32099,
};

inline auto DefaultMessageFor(ErrorCode code) -> std::string_view {
  switch (code) {
    case ErrorCode::kParseError:
      return "Parse error";
    case ErrorCode::kInvalidRequest:
      return "Invalid request";
    case ErrorCode::kMethodNotFound:
      return "Method not found";
    case ErrorCode::kInvalidParams:
      return "Invalid parameters";
    case ErrorCode::kInternalError:
      return "Internal error";
    case ErrorCode::kServerError:
      return "Server error";
    case ErrorCode::kTransportError:
      return "Transport error";
    case ErrorCode::kTimeoutError:
      return "Timeout error";
    case ErrorCode::kClientError:
      return "Client error";
  }
  return "Unknown error";
}

// Base error type for all JSON-RPC errors
struct RpcError {
  explicit RpcError(ErrorCode code, std::string message = "")
      : code(code),
        message(
            message.empty() ? std::string(DefaultMessageFor(code))
                            : std::move(message)) {
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

// Client errors
struct ClientError : RpcError {
  explicit ClientError(std::string msg)
      : RpcError(ErrorCode::kClientError, std::move(msg)) {
  }
};

// Server lifecycle errors (start/stop failures, resource issues)
struct ServerError : RpcError {
  explicit ServerError(std::string msg)
      : RpcError(ErrorCode::kServerError, std::move(msg)) {
  }
};

[[nodiscard]] inline auto CreateTransportError(
    std::string message, std::error_code ec = {})
    -> std::unexpected<TransportError> {
  return std::unexpected(TransportError(std::move(message), ec));
}

[[nodiscard]] inline auto CreateServerError(
    std::string message = "Server error") -> std::unexpected<ServerError> {
  return std::unexpected(ServerError(std::move(message)));
}

[[nodiscard]] inline auto CreateClientError(
    std::string message = "Client error") -> std::unexpected<ClientError> {
  return std::unexpected(ClientError(std::move(message)));
}

}  // namespace jsonrpc::error
