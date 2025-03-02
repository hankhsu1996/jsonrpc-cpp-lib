#pragma once

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/types.hpp"

namespace jsonrpc::endpoint {

/**
 * @brief Represents a JSON-RPC request.
 *
 * This class handles the creation and management of JSON-RPC requests,
 * including both method calls and notifications.
 */
class Request {
 public:
  /**
   * @brief Constructs a new Request object.
   *
   * @param method The name of the method to be invoked.
   * @param params Optional parameters to be passed with the request.
   * @param is_notification True if this is a notification (no response
   * expected).
   * @param id_generator Function to generate unique request IDs.
   */
  Request(
      std::string method, std::optional<nlohmann::json> params,
      bool is_notification, const std::function<RequestId()>& id_generator);

  /// @brief Checks if the request requires a response.
  [[nodiscard]] auto RequiresResponse() const -> bool;

  /// @brief Returns the unique ID for the request.
  [[nodiscard]] auto GetId() const -> RequestId;

  /// @brief Serializes the request to a JSON string.
  [[nodiscard]] auto Dump() const -> std::string;

 private:
  std::string method_;
  std::optional<nlohmann::json> params_;
  bool is_notification_;
  RequestId id_;
};

}  // namespace jsonrpc::endpoint
