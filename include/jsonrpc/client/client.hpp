#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json_fwd.hpp>

#include "jsonrpc/client/request.hpp"
#include "jsonrpc/server/types.hpp"
#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::client {

/**
 * @brief A JSON-RPC client for sending requests and receiving responses.
 *
 * This client allows for synchronous and asynchronous method calls as well as
 * notifications.
 */
class Client {
 public:
  /**
   * @brief Constructs a JSON-RPC client with a specified transport layer.
   *
   * @param transport A unique pointer to a transport layer used for
   * communication.
   */
  explicit Client(std::unique_ptr<transport::Transport> transport);

  /// @brief Destructor.
  ~Client() = default;

  // Delete copy constructor and copy assignment operator
  Client(const Client &) = delete;
  auto operator=(const Client &) -> Client & = delete;

  // Delete move constructor and move assignment operator
  Client(Client &&) noexcept = delete;
  auto operator=(Client &&) noexcept -> Client & = delete;

  /**
   * @brief Starts the JSON-RPC client listener thread.
   *
   * Initializes and starts a background thread that listens for responses from
   * the server. This must be called before sending any requests.
   */
  void Start();

  /**
   * @brief Stops the JSON-RPC client listener thread.
   *
   * Stops the background listener thread and waits for it to join.
   * This should be called before the client is destroyed or when the client no
   * longer needs to listen for responses.
   */
  void Stop();

  /**
   * @brief Checks if the client listener is running.
   *
   * @return True if the listener thread is running, false otherwise.
   */
  auto IsRunning() const -> bool;

  /**
   * @brief Sends a JSON-RPC method call and waits for the response.
   *
   * This is a blocking call that sends a method request to the server and waits
   * for the corresponding response. If no response is received within the
   * timeout period, throws a runtime_error.
   *
   * @param method The name of the method to call.
   * @param params Optional parameters to pass to the method.
   * @param timeout Maximum time to wait for response.
   * @return The JSON response received from the server.
   * @throws std::runtime_error if the request times out.
   */
  auto SendMethodCall(
      const std::string &method,
      std::optional<nlohmann::json> params = std::nullopt,
      std::chrono::milliseconds timeout = std::chrono::seconds(30))
      -> nlohmann::json;

  /**
   * @brief Sends a JSON-RPC method call asynchronously.
   *
   * This method sends a request to the server without blocking the calling
   * thread.
   *
   * @param method The name of the method to call.
   * @param params Optional parameters to pass to the method.
   * @return A future that will hold the JSON response from the server.
   */
  auto SendMethodCallAsync(
      const std::string &method,
      std::optional<nlohmann::json> params = std::nullopt)
      -> std::future<nlohmann::json>;

  /**
   * @brief Sends a JSON-RPC notification.
   *
   * Notifications do not expect a response from the server.
   *
   * @param method The name of the method to notify.
   * @param params Optional parameters to pass to the method.
   */
  void SendNotification(
      const std::string &method,
      std::optional<nlohmann::json> params = std::nullopt);

  /**
   * @brief Checks if there are any pending requests.
   *
   * @return True if there are pending requests, false otherwise.
   */
  auto HasPendingRequests() const -> bool;

  /**
   * @brief Registers a handler for notifications received from the server.
   *
   * @param method The name of the notification method to handle.
   * @param handler The function to handle the notification.
   */
  void RegisterNotification(
      const std::string &method, const server::NotificationHandler &handler);

 private:
  /// @brief Listener thread function for receiving responses from the transport
  /// layer.
  void Listener();

  /**
   * @brief Helper function to send a request and wait for a response.
   *
   * This function sends a JSON-RPC method call and waits for the response
   * synchronously. It wraps the asynchronous request handling with
   * blocking logic to return the result.
   *
   * @param request The JSON-RPC request to be sent.
   * @return The JSON response received from the server.
   */
  auto SendRequest(const Request &request) -> nlohmann::json;

  /**
   * @brief Sends a request asynchronously.
   *
   * This method handles the logic for sending asynchronous requests.
   *
   * @param request The JSON-RPC request to be sent.
   * @return A future that will hold the JSON response from the server.
   */
  auto SendRequestAsync(const Request &request) -> std::future<nlohmann::json>;

  /**
   * @brief Generates the next unique request ID.
   *
   * @return An integer representing the next unique request ID.
   */
  auto GetNextRequestId() -> int;

  /**
   * @brief Handles a JSON-RPC response received from the transport layer.
   *
   * Parses the response and resolves the associated promise with the response
   * data.
   *
   * @param response The JSON-RPC response as a string.
   */
  void HandleResponse(const std::string &response);

  /**
   * @brief Validates a JSON-RPC response.
   *
   * Ensures that the response conforms to the JSON-RPC 2.0 specification.
   *
   * @param response The JSON-RPC response as a JSON object.
   * @return True if the response is valid, false otherwise.
   */
  static auto ValidateResponse(const nlohmann::json &response) -> bool;

  /// Transport layer for communication.
  std::unique_ptr<transport::Transport> transport_;

  /// Counter for generating unique request IDs.
  std::atomic<int> req_id_counter_{0};

  /// Number of expected responses.
  std::atomic<int> expected_count_{0};

  /// Map of pending requests and their associated promises.
  std::unordered_map<int, std::promise<nlohmann::json>> requests_map_;

  /// Mutex to protect access to the pending requests map.
  mutable std::mutex requests_mutex_;

  /// Listener thread for receiving responses.
  std::thread listener_;

  /// Flag to control the running state of the listener thread.
  std::atomic<bool> is_running_{false};

  /// Map of notification handlers
  std::unordered_map<std::string, server::NotificationHandler>
      notification_handlers_;

  /// Mutex to protect access to the notification handlers map
  mutable std::mutex notification_handlers_mutex_;
};

}  // namespace jsonrpc::client
