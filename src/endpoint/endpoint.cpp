#include "jsonrpc/endpoint/endpoint.hpp"

#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/request.hpp"

namespace jsonrpc::endpoint {

RpcEndpoint::RpcEndpoint(std::unique_ptr<transport::Transport> transport)
    : transport_(std::move(transport)) {
  spdlog::info("Initializing JSON-RPC endpoint");
}

RpcEndpoint::~RpcEndpoint() {
  Stop();
}

void RpcEndpoint::Start() {
  spdlog::info("Starting JSON-RPC endpoint");
  is_running_.store(true);
  message_thread_ = std::thread(&RpcEndpoint::ProcessMessages, this);
}

void RpcEndpoint::Stop() {
  if (is_running_) {
    spdlog::info("Stopping JSON-RPC endpoint");
    is_running_.store(false);
    if (message_thread_.joinable()) {
      message_thread_.join();
    }
  }
}

auto RpcEndpoint::IsRunning() const -> bool {
  return is_running_.load();
}

auto RpcEndpoint::SendMethodCall(
    const std::string& method,
    std::optional<nlohmann::json> params) -> nlohmann::json {
  auto future = SendMethodCallAsync(method, std::move(params));
  return future.get();
}

auto RpcEndpoint::SendMethodCallAsync(
    const std::string& method,
    std::optional<nlohmann::json> params) -> std::future<nlohmann::json> {
  Request request(method, std::move(params), false, [this]() {
    return GetNextRequestId();
  });

  std::promise<nlohmann::json> response_promise;
  auto future_response = response_promise.get_future();

  {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    pending_requests_[request.GetId()] = std::move(response_promise);
  }

  transport_->SendMessage(request.Dump());
  return future_response;
}

void RpcEndpoint::SendNotification(
    const std::string& method, std::optional<nlohmann::json> params) {
  Request request(
      method, std::move(params), true, [this]() { return GetNextRequestId(); });
  transport_->SendMessage(request.Dump());
}

void RpcEndpoint::ProcessMessages() {
  spdlog::info("Starting message processing thread");
  while (is_running_) {
    auto message = transport_->ReceiveMessage();
    if (!message.empty()) {
      HandleMessage(message);
    }
  }
}

void RpcEndpoint::HandleMessage(const std::string& message) {
  nlohmann::json json_message;
  try {
    json_message = nlohmann::json::parse(message);
  } catch (const std::exception& e) {
    spdlog::error("Failed to parse JSON message: {}", e.what());
    return;
  }

  if (!ValidateMessage(json_message)) {
    spdlog::error("Invalid JSON-RPC message: {}", message);
    return;
  }

  // For now, handle only responses (client capability)
  if (json_message.contains("id") &&
      (json_message.contains("result") || json_message.contains("error"))) {
    // This is a response to our method call
    auto id = json_message["id"];
    RequestId request_id;

    try {
      if (id.is_number()) {
        request_id = id.get<int64_t>();
      } else if (id.is_string()) {
        request_id = id.get<std::string>();
      } else {
        throw std::runtime_error("Invalid ID type");
      }
    } catch (const std::exception& e) {
      spdlog::error("Failed to parse response ID: {}", e.what());
      return;
    }

    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    auto it = pending_requests_.find(request_id);
    if (it != pending_requests_.end()) {
      it->second.set_value(json_message);
      pending_requests_.erase(it);
    } else {
      spdlog::warn("Received response for unknown request ID: {}", message);
    }
  }
  // Server capabilities (handling method calls and notifications) will be added
  // later
}

auto RpcEndpoint::ValidateMessage(const nlohmann::json& json) -> bool {
  if (!json.contains("jsonrpc") || json["jsonrpc"] != "2.0") {
    return false;
  }

  // For now, only validate responses (client capability)
  if (json.contains("id")) {
    bool has_result = json.contains("result");
    bool has_error = json.contains("error");

    if (has_result == has_error) {  // Must have exactly one
      return false;
    }

    if (has_error) {
      const auto& error = json["error"];
      if (!error.contains("code") || !error["code"].is_number() ||
          !error.contains("message") || !error["message"].is_string()) {
        return false;
      }
    }
  }

  return true;
}

auto RpcEndpoint::HasPendingRequests() const -> bool {
  std::lock_guard<std::mutex> lock(pending_requests_mutex_);
  return !pending_requests_.empty();
}

auto RpcEndpoint::GetNextRequestId() -> RequestId {
  return request_id_counter_++;
}

}  // namespace jsonrpc::endpoint
