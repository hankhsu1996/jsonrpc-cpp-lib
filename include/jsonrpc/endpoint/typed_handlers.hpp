#pragma once

/**
 * @file typed_handlers.hpp
 * @brief Type-safe handlers for JSON-RPC method calls and notifications
 *
 * This file contains handler classes that safely adapt typed C++ functions
 * to work with the JSON-RPC protocol. We use class-based handlers instead of
 * direct lambda functions because:
 *
 * 1. Lambda captures in coroutines can lead to use-after-free issues when the
 *    lambda object is destroyed while the coroutine is suspended.
 * 2. These classes provide well-defined lifetime semantics when
 * shared_ptr-wrapped, ensuring the handler and its state remain valid
 * throughout coroutine execution.
 * 3. They offer cleaner separation of type conversion logic from the endpoint
 * code.
 *
 * This design prevents potential memory corruption that could occur with direct
 * lambda captures in coroutines, especially when those lambdas capture by
 * reference.
 */

#include <expected>
#include <optional>
#include <utility>
#include <variant>

#include <asio/awaitable.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
namespace jsonrpc::endpoint {

template <
    typename ParamsType, typename ResultType,
    typename ErrorType = std::monostate>
class TypedMethodHandler {
 private:
  using SimpleHandler = std::function<asio::awaitable<ResultType>(ParamsType)>;
  using ExpectedHandler =
      std::function<asio::awaitable<std::expected<ResultType, ErrorType>>(
          ParamsType)>;
  using HandlerType = std::conditional_t<
      std::is_same_v<ErrorType, std::monostate>, SimpleHandler,
      ExpectedHandler>;

  HandlerType handler_;

 public:
  // Constructor for non-error-aware handler
  explicit TypedMethodHandler(SimpleHandler handler)
    requires(std::is_same_v<ErrorType, std::monostate>)
      : handler_(std::move(handler)) {
  }

  // Constructor for error-aware handler
  explicit TypedMethodHandler(ExpectedHandler handler)
    requires(!std::is_same_v<ErrorType, std::monostate>)
      : handler_(std::move(handler)) {
  }

  auto operator()(std::optional<nlohmann::json> params)
      -> asio::awaitable<nlohmann::json> {
    ParamsType typed_params{};
    try {
      if (params.has_value()) {
        typed_params = params.value().get<ParamsType>();
      }
    } catch (const nlohmann::json::exception& ex) {
      throw std::runtime_error(
          "Failed to parse parameters: " + std::string(ex.what()));
    }

    if constexpr (std::is_same_v<ErrorType, std::monostate>) {
      if constexpr (std::is_void_v<ResultType>) {
        co_await handler_(typed_params);
        co_return nlohmann::json();
      } else {
        ResultType result = co_await handler_(typed_params);
        co_return nlohmann::json(result);
      }
    } else {
      auto result = co_await handler_(typed_params);
      if (result) {
        if constexpr (std::is_void_v<ResultType>) {
          co_return nlohmann::json();
        } else {
          co_return nlohmann::json(*result);
        }
      } else {
        throw result.error();
      }
    }
  }
};

template <typename ParamsType, typename ErrorType = std::monostate>
class TypedNotificationHandler {
 private:
  using SimpleHandler = std::function<asio::awaitable<void>(ParamsType)>;
  using ExpectedHandler =
      std::function<asio::awaitable<std::expected<void, ErrorType>>(
          ParamsType)>;
  using HandlerType = std::conditional_t<
      std::is_same_v<ErrorType, std::monostate>, SimpleHandler,
      ExpectedHandler>;

  HandlerType handler_;

 public:
  explicit TypedNotificationHandler(SimpleHandler handler)
    requires(std::is_same_v<ErrorType, std::monostate>)
      : handler_(std::move(handler)) {
  }

  explicit TypedNotificationHandler(ExpectedHandler handler)
    requires(!std::is_same_v<ErrorType, std::monostate>)
      : handler_(std::move(handler)) {
  }

  auto operator()(std::optional<nlohmann::json> params)
      -> asio::awaitable<void> {
    ParamsType typed_params{};
    try {
      if (params.has_value()) {
        typed_params = params.value().get<ParamsType>();
      }
    } catch (const nlohmann::json::exception& ex) {
      spdlog::error("Failed to parse parameters: {}", ex.what());
      co_return;
    }

    if constexpr (std::is_same_v<ErrorType, std::monostate>) {
      co_await handler_(typed_params);
    } else {
      auto result = co_await handler_(typed_params);
      if (!result) {
        spdlog::warn("TypedNotificationHandler ignored error");
      }
    }
  }
};

}  // namespace jsonrpc::endpoint
