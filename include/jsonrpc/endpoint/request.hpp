#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/types.hpp"
#include "jsonrpc/error/error.hpp"

namespace jsonrpc::endpoint {

class Request {
 public:
  Request(
      std::string method, std::optional<nlohmann::json> params,
      const std::function<RequestId()>& id_generator);

  Request(
      std::string method, std::optional<nlohmann::json> params, RequestId id);

  explicit Request(
      std::string method, std::optional<nlohmann::json> params = std::nullopt);

  static auto FromJson(const nlohmann::json& json_obj)
      -> std::expected<Request, error::RpcError>;

  [[nodiscard]] auto GetMethod() const -> const std::string& {
    return method_;
  }

  [[nodiscard]] auto GetParams() const -> const std::optional<nlohmann::json>& {
    return params_;
  }

  [[nodiscard]] auto IsNotification() const -> bool {
    return is_notification_;
  }

  [[nodiscard]] auto RequiresResponse() const -> bool;

  [[nodiscard]] auto GetId() const -> RequestId;

  [[nodiscard]] auto Dump() const -> std::string;

  [[nodiscard]] auto ToJson() const -> nlohmann::json;

 private:
  std::string method_;
  std::optional<nlohmann::json> params_;
  bool is_notification_;
  RequestId id_;
};

}  // namespace jsonrpc::endpoint
