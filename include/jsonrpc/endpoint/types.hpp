#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <asio.hpp>
#include <nlohmann/json.hpp>

namespace jsonrpc::endpoint {

/// @brief JSON-RPC 2.0 protocol version string
constexpr std::string_view kJsonRpcVersion = "2.0";

/// @brief Standard JSON-RPC 2.0 error codes
enum class ErrorCode {
  // Standard errors
  kParseError = -32700,      ///< Invalid JSON was received
  kInvalidRequest = -32600,  ///< The JSON sent is not a valid Request object
  kMethodNotFound = -32601,  ///< The method does not exist / is not available
  kInvalidParams = -32602,   ///< Invalid method parameter(s)
  kInternalError = -32603,   ///< Internal JSON-RPC error

  // Implementation-defined server errors
  kServerError = -32000,     ///< Generic server error
  kTransportError = -32010,  ///< Transport-related error
  kTimeoutError = -32001,    ///< Timeout error
};

/// Type for request IDs that can be either integer or string
using RequestId = std::variant<int64_t, std::string>;

/// Type for handling method calls - now synchronous for simplicity
using MethodCallHandler =
    nlohmann::json(const std::optional<nlohmann::json>& params);

/// Type for handling notifications - now synchronous for simplicity
using NotificationHandler = void(const std::optional<nlohmann::json>& params);

/// Type for a handler which can be either a method call handler or notification
/// handler
using Handler = std::variant<MethodCallHandler, NotificationHandler>;

/// Default request timeout in milliseconds
constexpr auto kDefaultRequestTimeout = std::chrono::milliseconds(30000);

/// Default maximum batch size
constexpr size_t kDefaultMaxBatchSize = 100;

}  // namespace jsonrpc::endpoint
