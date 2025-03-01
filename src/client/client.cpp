#include "jsonrpc/client/client.hpp"

#include <spdlog/spdlog.h>

namespace jsonrpc::client {

Client::Client(std::unique_ptr<transport::Transport> transport)
    : transport_(std::move(transport)) {
  spdlog::info("Initializing JSON-RPC client");
}

void Client::Start() {
  spdlog::info("Starting JSON-RPC client");
  is_running_.store(true);
  listener_ = std::thread(&Client::Listener, this);
}

void Client::Stop() {
  spdlog::info("Stopping JSON-RPC client");
  is_running_.store(false);
  if (listener_.joinable()) {
    listener_.join();
  }
}

auto Client::IsRunning() const -> bool {
  return is_running_.load();
}

auto Client::HasPendingRequests() const -> bool {
  std::lock_guard<std::mutex> lock(requests_mutex_);
  return !requests_map_.empty();
}

void Client::Listener() {
  spdlog::info("Starting JSON-RPC client listener thread");
  while (is_running_) {
    std::string response = transport_->ReceiveMessage();
    if (!response.empty()) {
      HandleResponse(response);
      if (expected_count_ > 0) {  // Only decrement for method call responses
        expected_count_--;
      }
    }
  }
}

auto Client::SendMethodCall(
    const std::string &method, std::optional<nlohmann::json> params,
    std::chrono::milliseconds timeout) -> nlohmann::json {
  Request request(method, std::move(params), false, [this]() {
    return GetNextRequestId();
  });
  auto future = SendRequestAsync(request);

  if (future.wait_for(timeout) != std::future_status::ready) {
    // Remove the pending request since we're timing out
    {
      std::lock_guard<std::mutex> lock(requests_mutex_);
      requests_map_.erase(request.GetKey());
    }
    throw std::runtime_error(fmt::format(
        "Request timed out after {} ms: {}", timeout.count(), method));
  }
  return future.get();
}

auto Client::SendMethodCallAsync(
    const std::string &method,
    std::optional<nlohmann::json> params) -> std::future<nlohmann::json> {
  Request request(method, std::move(params), false, [this]() {
    return GetNextRequestId();
  });
  return SendRequestAsync(request);
}

void Client::SendNotification(
    const std::string &method, std::optional<nlohmann::json> params) {
  Request request(
      method, std::move(params), true, [this]() { return GetNextRequestId(); });
  transport_->SendMessage(request.Dump());
}

auto Client::SendRequest(const Request &request) -> nlohmann::json {
  auto future_response = SendRequestAsync(request);
  return future_response.get();
}

auto Client::SendRequestAsync(const Request &request)
    -> std::future<nlohmann::json> {
  assert(
      request.RequiresResponse() &&
      "SendRequestAsync called for a request "
      "that does not require a response.");

  std::promise<nlohmann::json> response_promise;
  auto future_response = response_promise.get_future();

  {
    std::lock_guard<std::mutex> lock(requests_mutex_);
    requests_map_[request.GetKey()] = std::move(response_promise);
  }
  expected_count_++;

  transport_->SendMessage(request.Dump());

  return future_response;
}

auto Client::GetNextRequestId() -> int {
  return req_id_counter_++;
}

void Client::HandleResponse(const std::string &response) {
  nlohmann::json json_response;
  try {
    json_response = nlohmann::json::parse(response);
  } catch (const std::exception &e) {
    spdlog::error("Failed to parse JSON response: {}", e.what());
    throw std::runtime_error(
        "Failed to parse JSON response: " + std::string(e.what()));
  }

  // Check if this is a notification (no "id" field)
  if (json_response.contains("method")) {
    // This is a notification from server
    try {
      const std::string &method = json_response["method"].get<std::string>();
      std::lock_guard<std::mutex> lock(notification_handlers_mutex_);
      auto it = notification_handlers_.find(method);
      if (it != notification_handlers_.end()) {
        // Extract params if they exist
        nlohmann::json params = json_response.contains("params")
                                    ? json_response["params"]
                                    : nlohmann::json::object();
        it->second(params);
      } else {
        spdlog::warn(
            "No handler registered for notification method: {}", method);
      }
      return;  // Exit early for notifications
    } catch (const std::exception &e) {
      spdlog::error("Error handling notification: {}", e.what());
      throw std::runtime_error(
          "Error handling notification: " + std::string(e.what()));
    }
  }

  // Handle regular responses
  if (ValidateResponse(json_response)) {
    int request_id = json_response["id"].get<int>();

    std::lock_guard<std::mutex> lock(requests_mutex_);
    auto req_iter = requests_map_.find(request_id);
    if (req_iter != requests_map_.end()) {
      req_iter->second.set_value(json_response);
      requests_map_.erase(req_iter);
    } else {
      spdlog::error("Received response for unknown request ID: {}", request_id);
      throw std::runtime_error(
          "Received response for unknown request ID: " +
          std::to_string(request_id));
    }
  } else {
    spdlog::error("Invalid JSON-RPC response: {}", json_response.dump());
    throw std::runtime_error(
        "Invalid JSON-RPC response: " + json_response.dump());
  }
}

auto Client::ValidateResponse(const nlohmann::json &response) -> bool {
  if (!response.contains("jsonrpc") || response["jsonrpc"] != "2.0") {
    return false;
  }

  if (!response.contains("id")) {
    return false;
  }

  bool has_result = response.contains("result");
  bool has_error = response.contains("error");

  if (has_result == has_error) {  // Mutually exclusive
    return false;
  }

  if (has_error) {
    const nlohmann::json &error = response["error"];
    if (!error.contains("code") || !error["code"].is_number() ||
        !error.contains("message") || !error["message"].is_string()) {
      return false;
    }
  }

  return true;
}

void Client::RegisterNotification(
    const std::string &method, const server::NotificationHandler &handler) {
  std::lock_guard<std::mutex> lock(notification_handlers_mutex_);
  if (handler) {
    notification_handlers_[method] = handler;
    spdlog::debug("Registered handler for notification method: {}", method);
  } else {
    // If handler is null, remove the registration
    notification_handlers_.erase(method);
    spdlog::debug("Unregistered handler for notification method: {}", method);
  }
}

}  // namespace jsonrpc::client
