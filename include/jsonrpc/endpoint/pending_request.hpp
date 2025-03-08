#pragma once

#include <condition_variable>
#include <mutex>

#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

namespace jsonrpc::endpoint {

/**
 * @brief A class representing a pending RPC request
 *
 * This class provides an awaitable interface for RPC requests using ASIO's
 * awaitable model directly.
 */
class PendingRequest {
 public:
  /**
   * @brief Construct a new Pending Request object
   *
   * @param strand The strand to use for synchronization
   */
  explicit PendingRequest(asio::strand<asio::any_io_executor> strand)
      : strand_(std::move(strand)) {
  }

  // Prevent copying and moving
  PendingRequest(const PendingRequest&) = delete;
  auto operator=(const PendingRequest&) -> PendingRequest& = delete;
  PendingRequest(PendingRequest&&) = delete;
  auto operator=(PendingRequest&&) -> PendingRequest& = delete;

  /**
   * @brief Destructor
   */
  ~PendingRequest() = default;

  /**
   * @brief Sets the result of the request
   *
   * @param result The JSON result
   */
  void SetResult(nlohmann::json result) {
    // Execute within the strand to ensure thread safety
    asio::post(strand_, [this, result = std::move(result)]() mutable {
      if (!is_ready_) {
        result_ = std::move(result);
        is_ready_ = true;

        // Notify any waiting consumers
        std::lock_guard<std::mutex> lock(mutex_);
        ready_cv_.notify_all();
      }
    });
  }

  /**
   * @brief Cancels the request with an error
   *
   * @param code The error code
   * @param message The error message
   */
  void Cancel(int code, const std::string& message) {
    // Create a JSON-RPC error object
    nlohmann::json error = {{"error", {{"code", code}, {"message", message}}}};

    has_error_ = true;

    // Set the result with the error
    SetResult(std::move(error));
  }

  /**
   * @brief Get the result asynchronously
   *
   * @return asio::awaitable<nlohmann::json> The result
   */
  auto GetResult() -> asio::awaitable<nlohmann::json> {
    // If the result is already ready, return it immediately
    if (is_ready_) {
      co_return result_;
    }

    // Otherwise, wait for the result using a timer and polling
    // This is a simplified approach that works with ASIO
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, std::chrono::milliseconds(10));

    while (!is_ready_) {
      co_await timer.async_wait(asio::use_awaitable);
      timer.expires_after(std::chrono::milliseconds(10));
    }

    // Return the result
    co_return result_;
  }

  /**
   * @brief Check if the request is ready
   *
   * @return true if the result is ready, false otherwise
   */
  [[nodiscard]] bool IsReady() const {
    return is_ready_;
  }

  /**
   * @brief Check if the request has an error
   *
   * @return true if the request has an error, false otherwise
   */
  [[nodiscard]] bool HasError() const {
    return has_error_;
  }

 private:
  /// Strand for synchronization
  asio::strand<asio::any_io_executor> strand_;

  /// The JSON result
  nlohmann::json result_;

  /// Flag indicating if the result is ready
  bool is_ready_{};

  /// Flag indicating if the request has an error
  bool has_error_{};

  /// Mutex for condition variable
  std::mutex mutex_;

  /// Condition variable for signaling
  std::condition_variable ready_cv_;
};

}  // namespace jsonrpc::endpoint
