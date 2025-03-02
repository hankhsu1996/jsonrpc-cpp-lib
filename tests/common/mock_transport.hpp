#pragma once

#include <queue>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/transport/transport.hpp"

class MockTransport : public jsonrpc::transport::Transport {
 public:
  std::vector<std::string> sent_requests;
  std::queue<std::string> responses;

  void SendMessage(const std::string& message) override {
    sent_requests.push_back(message);
    spdlog::debug("MockTransport: Message sent");
  }

  auto ReceiveMessage() -> std::string override {
    if (!responses.empty()) {
      auto response = responses.front();
      responses.pop();
      spdlog::debug("MockTransport: Response returned");
      return response;
    }
    return "";
  }

  void Close() override {
  }  // Empty implementation for mock

  void SetResponse(const nlohmann::json& response) {
    responses.push(response.dump());
    spdlog::debug("MockTransport: Response queued");
  }

  void SetRawResponse(const std::string& response) {
    responses.push(response);
    spdlog::debug("MockTransport: Raw response queued");
  }

  auto GetLastSentMessage() const -> std::string {
    return sent_requests.empty() ? "" : sent_requests.back();
  }
};
