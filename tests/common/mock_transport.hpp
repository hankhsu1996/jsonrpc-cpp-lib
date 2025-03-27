#pragma once

#include <queue>
#include <string>
#include <vector>

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::test {

/**
 * @brief A mock transport implementation for testing.
 */
class MockTransport : public jsonrpc::transport::Transport {
 public:
  explicit MockTransport(asio::any_io_executor executor)
      : Transport(executor),
        strand_(asio::make_strand(executor)),
        receive_timer_(executor) {
    spdlog::debug("Created mock transport");
  }

  ~MockTransport() override {
    // Ensure we're closed when destroyed - use the synchronous method
    spdlog::debug("Destroying mock transport");
    CloseNow();
  }

  // Delete copy/move operations to follow Rule of Five
  MockTransport(const MockTransport&) = delete;
  MockTransport(MockTransport&&) = delete;
  auto operator=(const MockTransport&) -> MockTransport& = delete;
  auto operator=(MockTransport&&) -> MockTransport& = delete;

  /**
   * @brief Close the transport synchronously
   *
   * This is safe to use in destructors
   */
  void CloseNow() override {
    // Set closed flag and cancel any timers
    is_closed_ = true;
    is_started_ = false;
    receive_timer_.cancel();

    spdlog::debug("MockTransport closed synchronously");
  }

  /**
   * @brief Start the transport
   *
   * For MockTransport, this just sets the is_started_ flag.
   *
   * @return asio::awaitable<void>
   */
  auto Start() -> asio::awaitable<void> override {
    co_await asio::post(strand_, asio::use_awaitable);

    if (is_started_) {
      spdlog::debug("MockTransport already started");
      co_return;
    }

    if (is_closed_) {
      throw std::runtime_error("Cannot start a closed transport");
    }

    is_started_ = true;
    spdlog::debug("MockTransport started");
    co_return;
  }

  auto SendMessage(std::string message) -> asio::awaitable<void> override {
    co_await asio::post(strand_, asio::use_awaitable);

    if (is_closed_) {
      throw std::runtime_error("Cannot send on closed transport");
    }

    if (!is_started_) {
      throw std::runtime_error("Cannot send before transport is started");
    }

    sent_requests_.push_back(message);
    co_return;
  }

  auto ReceiveMessage() -> asio::awaitable<std::string> override {
    try {
      // Start with a simple check for closed state
      if (is_closed_) {
        spdlog::debug(
            "MockTransport: ReceiveMessage called after transport was closed");
        co_return std::string();
      }

      // Check if transport is started
      if (!is_started_) {
        throw std::runtime_error("Cannot receive before transport is started");
      }

      // Get strand protection
      co_await asio::post(strand_, asio::use_awaitable);

      // Check again if closed (might have changed while waiting for strand)
      if (is_closed_) {
        spdlog::debug("MockTransport: ReceiveMessage found transport closed");
        co_return std::string();
      }

      // If we already have messages, return one immediately
      if (!incoming_messages_.empty()) {
        auto message = incoming_messages_.front();
        incoming_messages_.pop();
        co_return message;
      }

      // No message available, need to wait
      // Use a simpler loop with shorter timeouts for more frequent closed
      // checks
      while (!is_closed_) {
        // Set up a short timer so we'll wake up periodically even without
        // messages
        receive_timer_.expires_after(std::chrono::milliseconds(10));

        try {
          // Wait for the timer to expire or be canceled
          co_await receive_timer_.async_wait(asio::use_awaitable);
        } catch (const asio::system_error& e) {
          // Just continue if timer was canceled
          if (e.code() != asio::error::operation_aborted) {
            throw;
          }
        }

        // Get strand protection to check state
        co_await asio::post(strand_, asio::use_awaitable);

        // Check if closed while we were waiting
        if (is_closed_) {
          co_return std::string();
        }

        // Check for new messages
        if (!incoming_messages_.empty()) {
          auto message = incoming_messages_.front();
          incoming_messages_.pop();
          co_return message;
        }
      }

      // Transport was closed
      co_return std::string();
    } catch (const std::exception& e) {
      spdlog::error("MockTransport: Error in ReceiveMessage: {}", e.what());
      throw;
    }
  }

  auto Close() -> asio::awaitable<void> override {
    try {
      // Get strand protection
      co_await asio::post(strand_, asio::use_awaitable);

      spdlog::debug("MockTransport: Closing transport");

      if (is_closed_) {
        spdlog::debug("MockTransport: Already closed");
        co_return;  // Already closed
      }

      // Set the closed flag first
      is_closed_ = true;
      is_started_ = false;

      // Cancel timer to interrupt any waiting operations
      receive_timer_.cancel();

      spdlog::debug("MockTransport: Closed");

      // Add an additional synchronization point to ensure all operations posted
      // to the strand complete
      co_await asio::post(strand_, asio::use_awaitable);

      co_return;
    } catch (const std::exception& e) {
      spdlog::error("MockTransport: Error in Close: {}", e.what());
      throw;
    }
  }

  auto SetMessage(const std::string& message) -> void {
    asio::post(strand_, [this, message]() {
      // Add the message to the queue
      incoming_messages_.push(message);

      // Wake up any waiting receive operation
      receive_timer_.cancel();
    });
  }

  [[nodiscard]] auto GetLastSentMessage() const -> std::string {
    return sent_requests_.empty() ? "" : sent_requests_.back();
  }

  [[nodiscard]] auto GetSentRequests() const
      -> const std::vector<std::string>& {
    return sent_requests_;
  }

 private:
  std::vector<std::string> sent_requests_;
  std::queue<std::string> incoming_messages_;
  bool is_closed_ = false;
  bool is_started_ = false;
  asio::strand<asio::any_io_executor> strand_;
  asio::steady_timer receive_timer_;  // Timer used for receive operations
};

}  // namespace jsonrpc::test
