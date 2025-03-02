#pragma once

#include <functional>
#include <optional>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

namespace jsonrpc::endpoint {

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

}  // namespace jsonrpc::endpoint
