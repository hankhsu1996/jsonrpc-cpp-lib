#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

#include "jsonrpc/transport/transport.hpp"

class MockTransport : public jsonrpc::transport::Transport {
 public:
  std::vector<std::string> sent_requests;
  std::queue<std::string> responses;

  void SendMessage(const std::string &request) override {
    sent_requests.push_back(request);
  }

  auto ReceiveMessage() -> std::string override {
    if (responses.empty()) {
      return "";
    }
    std::string response = responses.front();
    responses.pop();
    return response;
  }

  void SetResponse(const std::string &response) {
    responses.push(response);
  }

  auto GetLastSentMessage() const -> const std::string & {
    if (sent_requests.empty()) {
      throw std::runtime_error("No messages have been sent");
    }
    return sent_requests.back();
  }
};
