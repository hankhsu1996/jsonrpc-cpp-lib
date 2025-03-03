#pragma once

#include <queue>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/transport/transport.hpp"

class MockTransport : public jsonrpc::transport::Transport {
 public:
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
