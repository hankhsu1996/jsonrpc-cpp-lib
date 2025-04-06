#pragma once

#include <queue>
#include <string>
#include <vector>

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/transport/transport.hpp"

namespace jsonrpc::test {

using error::Ok;
using error::RpcError;
using error::RpcErrorCode;

/**
 * @brief A mock transport implementation for testing.
 */
class MockTransport : public jsonrpc::transport::Transport {
 public:
  explicit MockTransport(asio::any_io_executor executor)
      : Transport(executor),
        strand_(asio::make_strand(executor)),
        receive_timer_(executor) {
  }

  ~MockTransport() override {
    Logger().debug("Destroying mock transport");
    CloseNow();
  }

  MockTransport(const MockTransport&) = delete;
  MockTransport(MockTransport&&) = delete;
  auto operator=(const MockTransport&) -> MockTransport& = delete;
  auto operator=(MockTransport&&) -> MockTransport& = delete;

  auto Start()
      -> asio::awaitable<std::expected<void, error::RpcError>> override {
    co_await asio::post(strand_, asio::use_awaitable);

    if (is_started_) {
      Logger().debug("MockTransport already started");
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError, "MockTransport already started");
    }

    if (is_closed_) {
      Logger().error("Cannot start a closed transport");
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError, "Cannot start a closed transport");
    }

    is_started_ = true;
    Logger().debug("MockTransport started");
    co_return Ok();
  }

  auto Close()
      -> asio::awaitable<std::expected<void, error::RpcError>> override {
    // Get strand protection
    co_await asio::post(strand_, asio::use_awaitable);

    Logger().debug("MockTransport: Closing transport");

    if (is_closed_) {
      Logger().debug("MockTransport: Already closed");
      co_return std::expected<void, error::RpcError>{};
    }

    // Set the closed flag first
    is_closed_ = true;
    is_started_ = false;

    asio::error_code ec;
    receive_timer_.cancel(ec);
    if (ec) {
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError,
          "Failed to cancel receive timer during close: " + ec.message());
    }

    Logger().debug("MockTransport: Closed");

    // Optional sync point for strand tasks to flush
    co_await asio::post(strand_, asio::use_awaitable);

    co_return Ok();
  }

  void CloseNow() override {
    is_closed_ = true;
    is_started_ = false;
    receive_timer_.cancel();

    Logger().debug("MockTransport closed synchronously");
  }

  auto SendMessage(std::string message)
      -> asio::awaitable<std::expected<void, error::RpcError>> override {
    co_await asio::post(strand_, asio::use_awaitable);

    if (is_closed_) {
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError, "Cannot send on closed transport");
    }

    if (!is_started_) {
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError,
          "Cannot send before transport is started");
    }

    sent_requests_.push_back(message);
    co_return Ok();
  }

  auto ReceiveMessage()
      -> asio::awaitable<std::expected<std::string, error::RpcError>> override {
    if (is_closed_) {
      Logger().debug(
          "MockTransport: ReceiveMessage called after transport was closed");
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError,
          "ReceiveMessage called after transport was closed");
    }

    if (!is_started_) {
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError,
          "Cannot receive before transport is started");
    }

    co_await asio::post(strand_, asio::use_awaitable);

    if (is_closed_) {
      Logger().debug("MockTransport: ReceiveMessage found transport closed");
      co_return RpcError::UnexpectedFromCode(
          RpcErrorCode::kTransportError,
          "ReceiveMessage called after transport was closed");
    }

    if (!incoming_messages_.empty()) {
      auto message = incoming_messages_.front();
      incoming_messages_.pop();
      co_return message;
    }

    // Poll periodically for new messages
    while (!is_closed_) {
      receive_timer_.expires_after(std::chrono::milliseconds(10));

      std::error_code ec;
      co_await receive_timer_.async_wait(
          asio::redirect_error(asio::use_awaitable, ec));

      if (ec && ec != asio::error::operation_aborted) {
        co_return RpcError::UnexpectedFromCode(
            RpcErrorCode::kTransportError,
            "Error during receive wait: " + ec.message());
      }

      co_await asio::post(strand_, asio::use_awaitable);

      if (is_closed_) {
        co_return RpcError::UnexpectedFromCode(
            RpcErrorCode::kTransportError,
            "ReceiveMessage called after transport was closed");
      }

      if (!incoming_messages_.empty()) {
        auto message = incoming_messages_.front();
        incoming_messages_.pop();
        co_return message;
      }
    }

    co_return RpcError::UnexpectedFromCode(
        RpcErrorCode::kTransportError,
        "ReceiveMessage called after transport was closed");
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
