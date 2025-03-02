#include "jsonrpc/endpoint/endpoint.hpp"

#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/request.hpp"

namespace jsonrpc::endpoint {

namespace {
auto ValidateResponse(const nlohmann::json& json) -> bool {
  if (!json.contains("jsonrpc") || json["jsonrpc"] != "2.0") {
    return false;
  }

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
}  // namespace

RpcEndpoint::RpcEndpoint(
    std::unique_ptr<transport::Transport> transport,
    std::unique_ptr<IdGenerator> id_generator, bool enable_multithreading,
    size_t num_threads)
    : transport_(std::move(transport)),
      dispatcher_(
          std::make_unique<Dispatcher>(enable_multithreading, num_threads)),
      id_generator_(std::move(id_generator)),
      is_running_(false) {
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

    // Close transport first to unblock any pending reads
    WithTransport([](transport::Transport& transport) { transport.Close(); });

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
  Request request(
      method, std::move(params), [this]() { return GetNextRequestId(); });

  std::promise<nlohmann::json> response_promise;
  auto future_response = response_promise.get_future();

  {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    pending_requests_[request.GetId()] = std::move(response_promise);
  }

  WithTransport([&request](transport::Transport& transport) {
    transport.SendMessage(request.Dump());
  });

  return future_response;
}

void RpcEndpoint::SendNotification(
    const std::string& method, std::optional<nlohmann::json> params) {
  Request request(method, std::move(params));

  WithTransport([&request](transport::Transport& transport) {
    transport.SendMessage(request.Dump());
  });
}

void RpcEndpoint::ProcessMessages() {
  spdlog::info("Starting message processing thread");
  while (is_running_) {
    std::string message;
    WithTransport([&message](transport::Transport& transport) {
      message = transport.ReceiveMessage();
    });

    if (!message.empty()) {
      HandleMessage(message);
    }
  }
}

void RpcEndpoint::HandleMessage(const std::string& message) {
  try {
    auto json_message = nlohmann::json::parse(message);
    if (!ValidateResponse(json_message)) {
      // If not a valid response, try dispatching as request
      auto response = dispatcher_->DispatchRequest(message);
      if (response) {
        WithTransport([&response](transport::Transport& transport) {
          transport.SendMessage(*response);
        });
      }
      return;
    }

    // Handle response
    if (json_message.contains("id")) {
      RequestId request_id;
      const auto& id = json_message["id"];

      if (id.is_number()) {
        request_id = id.get<int64_t>();
      } else if (id.is_string()) {
        request_id = id.get<std::string>();
      } else {
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
  } catch (const std::exception&) {
    // Invalid message, ignore
  }
}

auto RpcEndpoint::HasPendingRequests() const -> bool {
  std::lock_guard<std::mutex> lock(pending_requests_mutex_);
  return !pending_requests_.empty();
}

void RpcEndpoint::RegisterMethodCall(
    const std::string& method, const MethodCallHandler& handler) {
  dispatcher_->RegisterMethodCall(method, handler);
}

void RpcEndpoint::RegisterNotification(
    const std::string& method, const NotificationHandler& handler) {
  dispatcher_->RegisterNotification(method, handler);
}

auto RpcEndpoint::GetNextRequestId() -> RequestId {
  return id_generator_->NextId();
}

void RpcEndpoint::SetRequestTimeout(std::chrono::milliseconds timeout) {
  request_timeout_ = timeout;
}

void RpcEndpoint::SetMaxBatchSize(size_t max_size) {
  max_batch_size_ = max_size;
}

void RpcEndpoint::SetErrorHandler(ErrorHandler handler) {
  std::lock_guard<std::mutex> lock(error_handler_mutex_);
  error_handler_ = std::move(handler);
}

auto RpcEndpoint::GetPendingRequestCount() const -> size_t {
  std::lock_guard<std::mutex> lock(pending_requests_mutex_);
  return pending_requests_.size();
}

auto RpcEndpoint::GetRequestTimeout() const -> std::chrono::milliseconds {
  return request_timeout_;
}

auto RpcEndpoint::GetMaxBatchSize() const -> size_t {
  return max_batch_size_;
}

}  // namespace jsonrpc::endpoint
