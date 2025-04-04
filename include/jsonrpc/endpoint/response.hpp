#pragma once

#include <expected>
#include <optional>

#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/types.hpp"
#include "jsonrpc/error/error.hpp"

namespace jsonrpc::endpoint {

using jsonrpc::error::ErrorCode;
using jsonrpc::error::RpcError;

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

  static auto CreateSuccess(
      const nlohmann::json& result, const std::optional<RequestId>& id)
      -> Response;

  static auto CreateError(
      ErrorCode code, const std::optional<RequestId>& id = std::nullopt)
      -> Response;

  static auto CreateError(
      const RpcError& error, const std::optional<RequestId>& id = std::nullopt)
      -> Response;

  static auto CreateError(
      const nlohmann::json& error, const std::optional<RequestId>& id)
      -> Response;

  [[nodiscard]] auto IsSuccess() const -> bool;

  [[nodiscard]] auto GetResult() const -> const nlohmann::json&;

  [[nodiscard]] auto GetError() const -> const nlohmann::json&;

  [[nodiscard]] auto GetId() const -> std::optional<RequestId>;

  [[nodiscard]] auto ToJson() const -> nlohmann::json;

 private:
  explicit Response(nlohmann::json response) : response_(std::move(response)) {
  }

  [[nodiscard]] auto ValidateResponse() const
      -> std::expected<void, error::RpcError>;

  nlohmann::json response_;
};

}  // namespace jsonrpc::endpoint

namespace nlohmann {
template <>
struct adl_serializer<jsonrpc::endpoint::Response> {
  static void to_json(json& j, const jsonrpc::endpoint::Response& r) {
    j = r.ToJson();
  }
};
}  // namespace nlohmann
