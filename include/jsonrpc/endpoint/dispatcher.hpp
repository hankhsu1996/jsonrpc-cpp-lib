#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <BS_thread_pool.hpp>
#include <nlohmann/json.hpp>

#include "jsonrpc/endpoint/request.hpp"
#include "jsonrpc/endpoint/response.hpp"
#include "jsonrpc/endpoint/types.hpp"

namespace jsonrpc::endpoint {

/**
 * @brief Dispatcher for JSON-RPC requests.
 *
 * Dispatcher manages the registration and execution of method call and
 * notification handlers for JSON-RPC requests. It can operate in
 * single-threaded or multi-threaded mode.
 */
class Dispatcher {
 public:
  /**
   * @brief Constructs a Dispatcher.
   *
   * @param enableMultithreading Enable multi-threading support.
   * @param numThreads Number of threads to use if multi-threading is enabled.
   */
  explicit Dispatcher(
      bool enable_multithreading = true,
      size_t num_threads = std::thread::hardware_concurrency());

  Dispatcher(const Dispatcher &) = delete;
  Dispatcher(Dispatcher &&) = delete;
  auto operator=(const Dispatcher &) -> Dispatcher & = delete;
  auto operator=(Dispatcher &&) -> Dispatcher & = delete;

  /// @brief Destructor.
  virtual ~Dispatcher() = default;

  /**
   * @brief Processes a JSON-RPC request.
   *
   * Dispatches the request to the appropriate handler.
   *
   * @param request The JSON-RPC request as a string.
   * @return The response from the handler as a JSON string, or std::nullopt if
   * no response is needed.
   */
  auto DispatchRequest(const std::string &request)
      -> std::optional<std::string>;

  /**
   * @brief Registers a method call handler.
   *
   * @param method The name of the RPC method.
   * @param handler The handler function for this method.
   */
  void RegisterMethodCall(
      const std::string &method, const MethodCallHandler &handler);

  /**
   * @brief Registers a notification handler.
   *
   * @param method The name of the RPC notification.
   * @param handler The handler function for this notification.
   */
  void RegisterNotification(
      const std::string &method, const NotificationHandler &handler);

 private:
  /**
   * @brief Parses and validates the JSON request string.
   *
   * Attempts to parse the input string as JSON. If parsing fails, logs an error
   * and returns std::nullopt.
   *
   * @param requestStr The JSON-RPC request as a string.
   * @return The parsed JSON object, or std::nullopt if parsing failed.
   */
  static auto ParseAndValidateJson(const std::string &request_str)
      -> std::optional<nlohmann::json>;

  /**
   * @brief Dispatches a single request to the appropriate handler and returns a
   * JSON string.
   *
   * This method handles single JSON-RPC requests, delegating to the appropriate
   * handler or generating an error response.
   *
   * @param requestJson The parsed JSON request.
   * @return The response as a JSON string, or std::nullopt if no response is
   * needed.
   */
  auto DispatchSingleRequest(const nlohmann::json &request_json)
      -> std::optional<std::string>;

  /**
   * @brief Internal method to dispatch a single request to the appropriate
   * handler and returns a JSON object.
   *
   * Validates the request, finds the appropriate handler, and processes the
   * request. If an error occurs, a JSON error response is generated.
   *
   * @param requestJson The parsed JSON request.
   * @return The response as a JSON object, or std::nullopt if no response is
   * needed.
   */
  auto DispatchSingleRequestInner(const nlohmann::json &request_json)
      -> std::optional<nlohmann::json>;

  /**
   * @brief Dispatches a batch request to the appropriate handlers and returns a
   * JSON string.
   *
   * Handles a batch of JSON-RPC requests, processing each one concurrently if
   * multithreading is enabled.
   *
   * @param requestJson The parsed JSON batch request.
   * @return The batch response as a JSON string, or std::nullopt if no
   * responses are needed.
   */
  auto DispatchBatchRequest(const nlohmann::json &request_json)
      -> std::optional<std::string>;

  /**
   * @brief Internal method to dispatch a batch request to the appropriate
   * handlers and returns a vector of JSON objects.
   *
   * Processes each request in the batch, potentially using multithreading to
   * handle multiple requests concurrently.
   *
   * @param requestJson The parsed JSON batch request.
   * @return A vector of JSON objects representing the responses.
   */
  auto DispatchBatchRequestInner(const nlohmann::json &request_json)
      -> std::vector<nlohmann::json>;

  /**
   * @brief Validates the request JSON object.
   *
   * Checks the structure and required fields of a JSON-RPC request. If
   * validation fails, returns an appropriate error response.
   *
   * @param requestJson The JSON-RPC request object.
   * @return An error response if validation fails, or std::nullopt if
   * validation succeeds.
   */
  static auto ValidateRequest(const nlohmann::json &request_json)
      -> std::optional<Response>;

  /**
   * @brief Finds the handler for the specified method.
   *
   * Looks up the handler for a given method name in the registered handlers
   * map.
   *
   * @param handlers The map of method names to handlers.
   * @param method The name of the method to find.
   * @return The handler for the method, or std::nullopt if not found.
   */
  static auto FindHandler(
      const std::unordered_map<std::string, Handler> &handlers,
      const std::string &method) -> std::optional<Handler>;

  /**
   * @brief Handles the request (method call or notification) using the
   * appropriate handler.
   *
   * Determines whether the request is a method call or notification, and
   * processes it accordingly using the provided handler.
   *
   * @param request The parsed JSON-RPC request.
   * @param handler The handler to process the request.
   * @return The response as a JSON object, or std::nullopt if no response is
   * needed.
   */
  static auto HandleRequest(const Request &request, const Handler &handler)
      -> std::optional<nlohmann::json>;

  /**
   * @brief Handles a method call request.
   *
   * Executes the registered method call handler and returns the response.
   *
   * @param request The parsed JSON-RPC request.
   * @param handler The method call handler to execute.
   * @return The JSON-RPC response object.
   */
  static auto HandleMethodCall(
      const Request &request, const MethodCallHandler &handler) -> Response;

  /**
   * @brief Handles a notification request.
   *
   * Executes the registered notification handler. Notifications do not require
   * a response.
   *
   * @param request The parsed JSON-RPC request.
   * @param handler The notification handler to execute.
   */
  static void HandleNotification(
      const Request &request, const NotificationHandler &handler);

  /// @brief A map of method names to method call handlers.
  std::unordered_map<std::string, Handler> handlers_;

  /// @brief Flag to enable multi-threading support.
  bool enable_multithreading_;

  /// @brief Thread pool for multi-threading.
  BS::thread_pool thread_pool_;
};

}  // namespace jsonrpc::endpoint
