#pragma once

#include <expected>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/types.hpp"
#include "jsonrpc/error/error.hpp"

namespace jsonrpc::endpoint {

using jsonrpc::error::ErrorCode;

class Response {
 public:
  Response() = default;
  Response(const Response&) = default;
  Response(Response&& other) = default;
  auto operator=(const Response&) -> Response& = default;
  auto operator=(Response&& other) noexcept -> Response& = default;

  ~Response() = default;

  static auto FromJson(const nlohmann::json& json)
      -> std::expected<Response, error::RpcError>;

  static auto CreateResult(
      const nlohmann::json& result, const std::optional<RequestId>& id)
      -> Response;

  static auto CreateLibError(
      ErrorCode error_code, const std::optional<RequestId>& id = std::nullopt)
      -> Response;

  static auto CreateUserError(
      const nlohmann::json& error, const std::optional<RequestId>& id)
      -> Response;

  [[nodiscard]] auto IsSuccess() const -> bool;

  [[nodiscard]] auto GetResult() const -> const nlohmann::json&;

  [[nodiscard]] auto GetError() const -> const nlohmann::json&;

  [[nodiscard]] auto GetId() const -> std::optional<RequestId>;

  [[nodiscard]] auto GetJson() const -> const nlohmann::json& {
    return response_;
  }

  [[nodiscard]] auto ToJson() const -> nlohmann::json;
  [[nodiscard]] auto ToStr() const -> std::string;

 private:
  explicit Response(nlohmann::json response) : response_(std::move(response)) {
  }

  [[nodiscard]] auto ValidateResponse() const
      -> std::expected<void, error::RpcError>;

  static auto CreateErrorResponse(
      const std::string& message, int code, const std::optional<RequestId>& id)
      -> nlohmann::json;

  nlohmann::json response_;
};

}  // namespace jsonrpc::endpoint
