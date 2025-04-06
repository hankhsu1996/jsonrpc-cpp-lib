#pragma once

#include <expected>
#include <string>

#include <nlohmann/json.hpp>

namespace jsonrpc::error {

enum class RpcErrorCode {
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

namespace detail {
inline auto DefaultMessageFor(RpcErrorCode code) -> std::string_view {
  switch (code) {
    case RpcErrorCode::kParseError:
      return "Parse error";
    case RpcErrorCode::kInvalidRequest:
      return "Invalid request";
    case RpcErrorCode::kMethodNotFound:
      return "Method not found";
    case RpcErrorCode::kInvalidParams:
      return "Invalid parameters";
    case RpcErrorCode::kInternalError:
      return "Internal error";
    case RpcErrorCode::kServerError:
      return "Server error";
    case RpcErrorCode::kTransportError:
      return "Transport error";
    case RpcErrorCode::kTimeoutError:
      return "Timeout error";
    case RpcErrorCode::kClientError:
      return "Client error";
  }
  return "Unknown error";
}
}  // namespace detail

// Base error type for all JSON-RPC errors
class RpcError {
 public:
  RpcError(RpcErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {
  }

  [[nodiscard]] auto to_json() const -> nlohmann::json {
    nlohmann::json json;
    json["code"] = static_cast<int>(Code());
    json["message"] = Message();
    return json;
  }

  [[nodiscard]] auto Code() const -> RpcErrorCode {
    return code_;
  }

  [[nodiscard]] auto Message() const -> std::string_view {
    return message_;
  }

  auto operator==(const RpcError& other) const -> bool {
    return Code() == other.Code() && Message() == other.Message();
  }

  auto operator!=(const RpcError& other) const -> bool {
    return !(*this == other);
  }

  static auto FromCode(RpcErrorCode code, std::string message = "")
      -> RpcError {
    if (message.empty()) {
      message = std::string(detail::DefaultMessageFor(code));
    }
    return {code, std::move(message)};
  }

  static auto UnexpectedFromCode(RpcErrorCode code, std::string message = "")
      -> std::unexpected<RpcError> {
    if (message.empty()) {
      message = std::string(detail::DefaultMessageFor(code));
    }
    return std::unexpected(RpcError(code, std::move(message)));
  }

 private:
  RpcErrorCode code_;
  std::string message_;
};

inline auto Ok() -> std::expected<void, RpcError> {
  return {};
}

}  // namespace jsonrpc::error
