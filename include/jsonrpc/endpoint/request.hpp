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
 * including both method calls and notifications. It supports both client-side
 * request creation and server-side request parsing.
 */
class Request {
 public:
  /**
   * @brief Constructs a new Request object for client-side use.
   *
   * @param method The name of the method to be invoked.
   * @param params Optional parameters to be passed with the request.
   * @param id_generator Function to generate unique request IDs.
   */
  Request(
      std::string method, std::optional<nlohmann::json> params,
      const std::function<RequestId()>& id_generator);

  /**
   * @brief Constructs a new Request object with a specific ID.
   *
   * @param method The name of the method to be invoked.
   * @param params Optional parameters to be passed with the request.
   * @param id The request ID.
   */
  Request(
      std::string method, std::optional<nlohmann::json> params, RequestId id);

  /**
   * @brief Constructs a new notification Request object (no response expected).
   *
   * @param method The name of the method to be invoked.
   * @param params Optional parameters to be passed with the request.
   */
  explicit Request(
      std::string method, std::optional<nlohmann::json> params = std::nullopt);

  /**
   * @brief Creates a Request object from a JSON object for server-side use.
   *
   * @param json_obj The JSON object representing the request.
   * @return A Request object.
   * @throws std::invalid_argument if the JSON is not a valid request.
   */
  static auto FromJson(const nlohmann::json& json_obj) -> Request;

  /**
   * @brief Validates a JSON object as a request.
   *
   * @param json_obj The JSON object to validate.
   * @return true if the JSON represents a valid request.
   */
  static auto ValidateJson(const nlohmann::json& json_obj) -> bool;

  /// @brief Gets the method name.
  [[nodiscard]] auto GetMethod() const -> const std::string& {
    return method_;
  }

  /// @brief Gets the parameters.
  [[nodiscard]] auto GetParams() const -> const std::optional<nlohmann::json>& {
    return params_;
  }

  /// @brief Checks if the request is a notification (no response expected).
  [[nodiscard]] auto IsNotification() const -> bool {
    return is_notification_;
  }

  /// @brief Checks if the request requires a response.
  [[nodiscard]] auto RequiresResponse() const -> bool;

  /// @brief Returns the unique ID for the request.
  [[nodiscard]] auto GetId() const -> RequestId;

  /// @brief Serializes the request to a JSON string.
  [[nodiscard]] auto Dump() const -> std::string;

  /// @brief Converts the request to a JSON object.
  [[nodiscard]] auto ToJson() const -> nlohmann::json;

 private:
  std::string method_;
  std::optional<nlohmann::json> params_;
  bool is_notification_;
  RequestId id_;
};

}  // namespace jsonrpc::endpoint
