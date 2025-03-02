#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

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
  kServerError = -32000,  ///< Generic server error
};

/// Type for request IDs that can be either integer or string
using RequestId = std::variant<int64_t, std::string>;

/// Type for handling responses to requests
using ResponseCallback = std::function<void(const nlohmann::json&)>;

/// Type for handling method calls
using MethodCallHandler =
    std::function<nlohmann::json(const std::optional<nlohmann::json>& params)>;

/// Type for handling notifications
using NotificationHandler =
    std::function<void(const std::optional<nlohmann::json>& params)>;

/// Type for handling errors
using ErrorHandler = std::function<void(ErrorCode, const std::string&)>;

/// Type for a handler which can be either a method call handler or notification
/// handler
using Handler = std::variant<MethodCallHandler, NotificationHandler>;

/// Default request timeout in milliseconds
constexpr auto kDefaultRequestTimeout = std::chrono::milliseconds(30000);

/// Default maximum batch size
constexpr size_t kDefaultMaxBatchSize = 100;

}  // namespace jsonrpc::endpoint
