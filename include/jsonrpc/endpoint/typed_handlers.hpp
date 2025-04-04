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

#include <optional>
#include <utility>

#include <asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/types.hpp"

namespace jsonrpc::endpoint {
template <typename ParamsType, typename ResultType>
class TypedMethodHandler {
 private:
  std::function<asio::awaitable<ResultType>(ParamsType)> handler_;

 public:
  explicit TypedMethodHandler(
      std::function<asio::awaitable<ResultType>(ParamsType)> handler)
      : handler_(std::move(handler)) {
  }

  auto operator()(std::optional<nlohmann::json> params)
      -> asio::awaitable<nlohmann::json> {
    try {
      // Convert JSON params to typed params
      ParamsType typed_params =
          params.has_value() ? params.value().get<ParamsType>() : ParamsType{};

      // Call the typed handler
      ResultType result = co_await handler_(typed_params);

      // Convert result back to JSON
      co_return nlohmann::json(result);
    } catch (const nlohmann::json::exception &ex) {
      // throw RpcError(
      //     ErrorCode::kInvalidParams,
      //     std::string("Parameter conversion error: ") + ex.what());
    } catch (const std::exception &ex) {
      // throw RpcError(
      //     ErrorCode::kInternalError,
      //     std::string("Handler error: ") + ex.what());
    }
  }
};

template <typename ParamsType>
class TypedNotificationHandler {
 private:
  std::function<asio::awaitable<void>(ParamsType)> handler_;

 public:
  explicit TypedNotificationHandler(
      std::function<asio::awaitable<void>(ParamsType)> handler)
      : handler_(std::move(handler)) {
  }

  auto operator()(std::optional<nlohmann::json> params)
      -> asio::awaitable<void> {
    try {
      // Convert JSON params to typed params
      ParamsType typed_params =
          params.has_value() ? params.value().get<ParamsType>() : ParamsType{};

      // Call the typed handler
      co_await handler_(typed_params);
    } catch (const nlohmann::json::exception &ex) {
      // throw RpcError(
      //     ErrorCode::kInvalidParams,
      //     std::string("Parameter conversion error: ") + ex.what());
    } catch (const std::exception &ex) {
      // throw RpcError(
      //     ErrorCode::kInternalError,
      //     std::string("Handler error: ") + ex.what());
    }
  }
};
}  // namespace jsonrpc::endpoint
