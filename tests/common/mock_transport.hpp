#pragma once

#include <queue>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jsonrpc/transport/transport.hpp"

class MockTransport : public jsonrpc::transport::Transport {
 public:
  std::vector<std::string> sent_requests;
  std::queue<nlohmann::json> responses;

  void SendMessage(const std::string& message) override {
    sent_requests.push_back(message);
  }

  auto ReceiveMessage() -> std::string override {
    if (!responses.empty()) {
      auto response = responses.front();
      responses.pop();
      return response.dump();
    }
    return "";
  }

  void SetResponse(const nlohmann::json& response) {
    responses.push(response);
  }

  auto GetLastSentMessage() const -> std::string {
    return sent_requests.empty() ? "" : sent_requests.back();
  }
};
