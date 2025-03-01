#include "jsonrpc/server/server.hpp"

#include <spdlog/spdlog.h>

namespace jsonrpc::server {

Server::Server(std::unique_ptr<transport::Transport> transport)
    : transport_(std::move(transport)) {
  dispatcher_ = std::make_unique<Dispatcher>();
  spdlog::info("Server initialized with transport");
}

void Server::Start() {
  spdlog::info("Server starting");
  running_.store(true);
  Listen();
}

void Server::Stop() {
  spdlog::info("Server stopping");
  running_.store(false);
}

auto Server::IsRunning() const -> bool {
  return running_.load();
}

void Server::Listen() {
  if (!dispatcher_) {
    spdlog::error("Dispatcher is not set.");
    return;
  }
  if (!transport_) {
    spdlog::error("Transport is not set.");
    return;
  }

  while (IsRunning()) {
    std::string request;
    {
      std::lock_guard<std::mutex> lock(transport_mutex_);
      request = transport_->ReceiveMessage();
    }

    if (request.empty()) {
      continue;
    }

    std::optional<std::string> response = dispatcher_->DispatchRequest(request);
    if (response.has_value()) {
      std::lock_guard<std::mutex> lock(transport_mutex_);
      transport_->SendMessage(response.value());
    }
  }
}

void Server::RegisterMethodCall(
    const std::string &method, const MethodCallHandler &handler) {
  dispatcher_->RegisterMethodCall(method, handler);
}

void Server::RegisterNotification(
    const std::string &method, const NotificationHandler &handler) {
  dispatcher_->RegisterNotification(method, handler);
}

void Server::SendNotification(
    const std::string &method, std::optional<nlohmann::json> params) {
  if (!transport_) {
    throw std::runtime_error("Transport is not initialized");
  }

  if (!IsRunning()) {
    throw std::runtime_error("Server is not running");
  }

  try {
    // Construct JSON-RPC notification message
    nlohmann::json notification = {{"jsonrpc", "2.0"}, {"method", method}};

    // Only add params if they are provided
    if (params.has_value()) {
      notification["params"] = params.value();
    }

    // Convert JSON to string and send with thread safety
    const auto message = notification.dump();
    {
      std::lock_guard<std::mutex> lock(transport_mutex_);
      transport_->SendMessage(message);
    }
  } catch (const nlohmann::json::exception &e) {
    throw std::runtime_error(
        std::string("Failed to create notification message: ") + e.what());
  } catch (const std::exception &e) {
    throw std::runtime_error(
        std::string("Failed to send notification: ") + e.what());
  }
}

}  // namespace jsonrpc::server
