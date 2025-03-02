#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/types.hpp"

namespace jsonrpc::endpoint {

/// @brief Enumeration for library error kinds.
enum class LibErrorKind {
  kParseError,
  kInvalidRequest,
  kMethodNotFound,
  kInternalError,
  kServerError
};

using ErrorInfoMap =
    std::unordered_map<LibErrorKind, std::pair<int, const char*>>;

/// @brief Represents a JSON-RPC response.
class Response {
 public:
  Response() = delete;
  Response(const Response&) = delete;
  auto operator=(const Response&) -> Response& = delete;

  Response(Response&& other) noexcept;
  auto operator=(Response&& other) noexcept -> Response& = delete;

  ~Response() = default;

  /**
   * @brief Creates a Response object from a JSON object.
   * @param json The JSON object to parse.
   * @return A Response object.
   * @throws std::invalid_argument if the JSON is not a valid response.
   */
  static auto FromJson(const nlohmann::json& json) -> Response;

  /**
   * @brief Creates a successful Response object.
   * @param result The result of the method call.
   * @param id The ID of the request.
   * @return A Response object indicating success.
   */
  static auto CreateResult(
      const nlohmann::json& result,
      const std::optional<RequestId>& id) -> Response;

  /**
   * @brief Creates a Response object for a library error.
   * @param error_code The error code.
   * @param id The ID of the request.
   * @return A Response object indicating a library error.
   */
  static auto CreateLibError(
      ErrorCode error_code,
      const std::optional<RequestId>& id = std::nullopt) -> Response;

  /**
   * @brief Creates a Response object for a user error.
   * @param error The JSON object representing the error.
   * @param id The ID of the request.
   * @return A Response object indicating a user error.
   */
  static auto CreateUserError(
      const nlohmann::json& error,
      const std::optional<RequestId>& id) -> Response;

  /// @brief Checks if the response indicates success.
  [[nodiscard]] auto IsSuccess() const -> bool;

  /// @brief Gets the result if this is a success response.
  [[nodiscard]] auto GetResult() const -> const nlohmann::json&;

  /// @brief Gets the error if this is an error response.
  [[nodiscard]] auto GetError() const -> const nlohmann::json&;

  /// @brief Gets the ID of the request this response is for.
  [[nodiscard]] auto GetId() const -> std::optional<RequestId>;

  /**
   * @brief Serializes the Response object to a JSON object.
   * @return The JSON representation of the response.
   */
  [[nodiscard]] auto ToJson() const -> nlohmann::json;

  /**
   * @brief Serializes the Response object to a string.
   * @return The string representation of the response.
   */
  [[nodiscard]] auto ToStr() const -> std::string;

 private:
  explicit Response(nlohmann::json response);

  /// @brief Validates the Response object.
  void ValidateResponse() const;

  /**
   * @brief Creates a JSON object representing an error response.
   * @param message The error message.
   * @param code The error code.
   * @param id The ID of the request.
   * @return The JSON representation of the error response.
   */
  static auto CreateErrorResponse(
      const std::string& message, int code,
      const std::optional<RequestId>& id) -> nlohmann::json;

  /// @brief The JSON object representing the response.
  nlohmann::json response_;

  /// @brief Static data member for mapping error kinds to messages.
  static const ErrorInfoMap kErrorInfoMap;
};

}  // namespace jsonrpc::endpoint
