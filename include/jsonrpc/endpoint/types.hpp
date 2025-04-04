#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <asio.hpp>
#include <nlohmann/json.hpp>

namespace jsonrpc::endpoint {

constexpr std::string_view kJsonRpcVersion = "2.0";

using RequestId = std::variant<int64_t, std::string>;

using MethodCallHandler =
    nlohmann::json(const std::optional<nlohmann::json>& params);

using NotificationHandler = void(const std::optional<nlohmann::json>& params);

using Handler = std::variant<MethodCallHandler, NotificationHandler>;

constexpr auto kDefaultRequestTimeout = std::chrono::milliseconds(30000);

constexpr size_t kDefaultMaxBatchSize = 100;

}  // namespace jsonrpc::endpoint
