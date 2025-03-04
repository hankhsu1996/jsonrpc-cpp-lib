#pragma once

#include <queue>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/transport/transport.hpp"

class MockTransport : public jsonrpc::transport::Transport {
 public:
  MockTransport() = default;

  auto SendMessage(const std::string& message) -> std::future<void> override {
    if (is_closed_) {
      throw std::runtime_error("Transport is closed");
    }
    sent_requests_.push_back(message);

    // Create a promise and immediately fulfill it
    std::promise<void> promise;
    auto future = promise.get_future();
    promise.set_value();
    return future;
  }

  [[nodiscard]] auto ReceiveMessage() -> std::future<std::string> override {
    if (is_closed_) {
      throw std::runtime_error("Transport is closed");
    }

    std::promise<std::string> promise;
    auto future = promise.get_future();

    if (!incoming_messages_.empty()) {
      auto message = incoming_messages_.front();
      incoming_messages_.pop();
      promise.set_value(message);
    } else {
      promise.set_value("");  // Empty string indicates no message available
    }

    return future;
  }

  auto Close() -> std::future<void> override {
    if (!is_closed_) {
      spdlog::info("Closing mock transport");
      is_closed_ = true;
    }

    // Create a promise and immediately fulfill it
    std::promise<void> promise;
    auto future = promise.get_future();
    promise.set_value();
    return future;
  }

  // Override the async methods from Transport to provide efficient testing
  // implementations
  void DoAsyncSendMessage(
      const std::string& message, SendHandler handler) override {
    if (is_closed_) {
      asio::post(
          GetIoContext()->get_executor(), [handler = std::move(handler)]() {
            handler(asio::error_code(
                asio::error::not_connected, asio::system_category()));
          });
      return;
    }

    sent_requests_.push_back(message);

    // Post successful completion to the io_context to ensure async behavior
    asio::post(
        GetIoContext()->get_executor(),
        [handler = std::move(handler)]() { handler(asio::error_code()); });
  }

  void DoAsyncReceiveMessage(ReceiveHandler handler) override {
    if (is_closed_) {
      asio::post(
          GetIoContext()->get_executor(), [handler = std::move(handler)]() {
            handler(
                asio::error_code(
                    asio::error::not_connected, asio::system_category()),
                "");
          });
      return;
    }

    // Post completion to the io_context to ensure async behavior
    asio::post(
        GetIoContext()->get_executor(),
        [this, handler = std::move(handler)]() mutable {
          if (!incoming_messages_.empty()) {
            auto message = incoming_messages_.front();
            incoming_messages_.pop();
            handler(asio::error_code(), message);
          } else {
            handler(
                asio::error_code(),
                "");  // Empty string indicates no message available
          }
        });
  }

  void DoAsyncClose(CloseHandler handler) override {
    if (!is_closed_) {
      is_closed_ = true;
      spdlog::info("Closing mock transport asynchronously");
    }

    // Post successful completion to the io_context to ensure async behavior
    asio::post(
        GetIoContext()->get_executor(),
        [handler = std::move(handler)]() { handler(asio::error_code()); });
  }

  // Add a message to the incoming queue
  auto SetMessage(const std::string& message) -> void {
    incoming_messages_.push(message);
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
};
