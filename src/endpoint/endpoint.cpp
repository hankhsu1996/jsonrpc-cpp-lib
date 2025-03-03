#include "jsonrpc/endpoint/endpoint.hpp"

#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/request.hpp"
#include "jsonrpc/endpoint/response.hpp"

namespace jsonrpc::endpoint {

RpcEndpoint::RpcEndpoint(
    std::unique_ptr<transport::Transport> transport,
    std::unique_ptr<IdGenerator> id_generator)
    : transport_(std::move(transport)),
      dispatcher_(std::make_unique<Dispatcher>()),
      id_generator_(std::move(id_generator)),
      is_running_(false) {
}

auto RpcEndpoint::CreateClient(std::unique_ptr<transport::Transport> transport)
    -> std::unique_ptr<RpcEndpoint> {
  auto endpoint = std::make_unique<RpcEndpoint>(std::move(transport));
  // Start processing immediately for clients
  endpoint->Start().get();
  return endpoint;
}

RpcEndpoint::~RpcEndpoint() {
  if (is_running_) {
    spdlog::debug("RPC endpoint still running in destructor, shutting down");
    Shutdown().get();
  }
}

auto RpcEndpoint::Start() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  // Ensure we don't start multiple message threads
  {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    if (is_running_) {
      spdlog::error("RPC endpoint already running");
      throw std::runtime_error("RPC endpoint already running");
    }
    is_running_ = true;
  }

  // Start message processing
  message_task_ = ProcessMessages();

  promise.set_value();
  return future;
}

void RpcEndpoint::Wait() {
  // Wait until the endpoint is no longer running
  while (is_running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

auto RpcEndpoint::Shutdown() -> std::future<void> {
  if (!is_running_) {
    std::promise<void> promise;
    auto future = promise.get_future();
    promise.set_value();
    return future;
  }

  // Create a promise for the shutdown completion
  std::promise<void> promise;
  auto future = promise.get_future();

  // Set running flag to false and notify waiting threads
  {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    is_running_.store(false);
  }
  shutdown_cv_.notify_all();

  // Close the transport to unblock any pending operations
  try {
    WithTransport([](transport::Transport& transport) {
      transport.Close().get();
      return;
    });
  } catch (const std::exception& e) {
    spdlog::warn("Error during transport close: {}", e.what());
  }

  // Mark the message thread as stopped
  {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    message_thread_started_ = false;
  }

  promise.set_value();
  return future;
}

auto RpcEndpoint::IsRunning() const -> bool {
  return is_running_.load();
}

auto RpcEndpoint::SendMethodCall(
    const std::string& method,
    std::optional<nlohmann::json> params) -> nlohmann::json {
  // This is a blocking call, so we'll get the result from the async version
  return SendMethodCallAsync(method, std::move(params)).get();
}

auto RpcEndpoint::SendMethodCallAsync(
    const std::string& method,
    std::optional<nlohmann::json> params) -> std::future<nlohmann::json> {
  auto request_id = GetNextRequestId();
  Request request(method, std::move(params), request_id);
  auto request_json = request.ToJson();

  std::promise<nlohmann::json> promise;
  auto future = promise.get_future();

  {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    pending_requests_[request_id] = std::move(promise);
  }

  try {
    WithTransport([&request_json](transport::Transport& transport) {
      transport.SendMessage(request_json.dump()).get();
    });
  } catch (const std::exception& e) {
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    auto it = pending_requests_.find(request_id);
    if (it != pending_requests_.end()) {
      it->second.set_exception(std::current_exception());
      pending_requests_.erase(it);
    }
  }

  return future;
}

auto RpcEndpoint::SendNotification(
    const std::string& method,
    std::optional<nlohmann::json> params) -> std::future<void> {
  Request request(method, std::move(params));
  auto request_json = request.ToJson();

  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    WithTransport([&request_json](transport::Transport& transport) {
      transport.SendMessage(request_json.dump()).get();
    });
    promise.set_value();
  } catch (const std::exception& e) {
    spdlog::error("Failed to send notification: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

auto RpcEndpoint::ProcessMessages() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  try {
    // Start the message processing loop in a separate thread
    std::thread([this, p = std::move(promise)]() mutable {
      try {
        ProcessMessagesLoop();
        p.set_value();
      } catch (const std::exception& e) {
        spdlog::error("Error in message processing loop: {}", e.what());
        try {
          p.set_exception(std::current_exception());
        } catch (...) {
          spdlog::error("Failed to set exception on promise");
        }
      }
    }).detach();
  } catch (const std::exception& e) {
    spdlog::error("Failed to start message processing: {}", e.what());
    promise.set_exception(std::current_exception());
  }

  return future;
}

void RpcEndpoint::ProcessMessagesLoop() {
  while (is_running_) {
    bool processed = false;
    try {
      processed = ProcessSingleMessage();
    } catch (const std::exception& e) {
      spdlog::error(
          "Unexpected exception in ProcessSingleMessage: {}", e.what());
      // Continue the loop despite the error
    }

    if (!processed) {
      // If no message was processed, wait a bit to avoid busy-waiting
      std::unique_lock<std::mutex> lock(shutdown_mutex_);
      shutdown_cv_.wait_for(
          lock, std::chrono::milliseconds(10), [this] { return !is_running_; });
    }
  }
}

auto RpcEndpoint::ProcessSingleMessage() -> bool {
  try {
    // Check if we're still running before attempting to receive
    if (!is_running_) {
      return false;
    }

    // Try to receive a message with non-blocking approach
    auto receive_future = WithTransport([](transport::Transport& transport) {
      auto future = transport.ReceiveMessage();
      return future;
    });

    // Check if message is ready without blocking
    auto status = receive_future.wait_for(std::chrono::milliseconds(10));
    if (status == std::future_status::ready) {
      try {
        std::string message = receive_future.get();
        if (!message.empty()) {
          HandleMessage(message);
          return true;
        }

      } catch (const std::exception& e) {
        spdlog::error("Exception getting message from future: {}", e.what());
      }
    } else if (status == std::future_status::timeout) {
      // Timeout handling
    } else {
      // Deferred handling
    }
  } catch (const std::runtime_error& e) {
    // Only report errors if we're still running
    if (is_running_) {
      // Always log transport errors
      spdlog::error("Transport error in ProcessSingleMessage: {}", e.what());
      ReportError(ErrorCode::kTransportError, e.what());
    }
  } catch (const std::exception& e) {
    if (is_running_) {
      // Always log unexpected errors
      spdlog::error("Unexpected error in ProcessSingleMessage: {}", e.what());
      ReportError(ErrorCode::kInternalError, e.what());
    }
  }

  return false;
}

void RpcEndpoint::HandleMessage(const std::string& message) {
  try {
    try {
      auto json_message = nlohmann::json::parse(message);

      // Check if this is a response or a request
      bool is_response = false;
      if (json_message.contains("jsonrpc") &&
          json_message["jsonrpc"] == "2.0") {
        if (json_message.contains("id")) {
          bool has_result = json_message.contains("result");
          bool has_error = json_message.contains("error");

          // A valid response must have exactly one of result or error
          if (has_result != has_error) {
            is_response = true;
          }
        }
      }

      if (!is_response) {
        // Check if dispatcher is available
        if (!dispatcher_) {
          spdlog::error("Dispatcher is null when trying to dispatch request");
          throw std::runtime_error("Dispatcher is null");
        }

        // If not a valid response, try dispatching as request
        try {
          auto response = dispatcher_->DispatchRequest(message);

          if (response) {
            // Send the response asynchronously
            try {
              WithTransport(
                  [response = *response](transport::Transport& transport) {
                    try {
                      transport.SendMessage(response).get();
                    } catch (const std::exception& e) {
                      spdlog::error("Error sending response: {}", e.what());
                    }
                    return true;
                  });
            } catch (const std::exception& e) {
              spdlog::error(
                  "Error in WithTransport when sending response: {}", e.what());
            }
          }
        } catch (const std::exception& e) {
          spdlog::error("Exception in DispatchRequest: {}", e.what());
        }
        return;
      }

      // Handle response
      if (json_message.contains("id")) {
        Response response(json_message);
        HandleResponse(response);
      } else {
      }
    } catch (const nlohmann::json::exception& e) {
      spdlog::error("JSON parsing error: {}", e.what());
    }
  } catch (const std::exception& e) {
    spdlog::error("Error handling message: {}", e.what());
  }
}

void RpcEndpoint::HandleResponse(const Response& response) {
  auto request_id = response.GetId();
  if (!request_id) {
    spdlog::warn("Response missing ID");
    return;
  }

  std::lock_guard<std::mutex> lock(pending_requests_mutex_);
  auto it = pending_requests_.find(*request_id);
  if (it != pending_requests_.end()) {
    it->second.set_value(response.GetJson());
    pending_requests_.erase(it);
  } else {
    spdlog::warn(
        "Received response for unknown request ID: {}",
        response.GetJson().dump());
  }
}

auto RpcEndpoint::HasPendingRequests() const -> bool {
  std::lock_guard<std::mutex> lock(pending_requests_mutex_);
  return !pending_requests_.empty();
}

void RpcEndpoint::RegisterMethodCall(
    const std::string& method,
    std::function<nlohmann::json(const nlohmann::json&)> handler) {
  if (!dispatcher_) {
    spdlog::error("Dispatcher is null when registering method: {}", method);
    throw std::runtime_error("Dispatcher is null");
  }

  // Adapt the handler to match what the dispatcher expects
  auto adapted_handler =
      [handler](const std::optional<nlohmann::json>& params) -> nlohmann::json {
    return handler(params.value_or(nlohmann::json::object()));
  };

  dispatcher_->RegisterMethodCall(method, adapted_handler);
}

void RpcEndpoint::RegisterNotification(
    const std::string& method,
    std::function<void(const nlohmann::json&)> handler) {
  if (!dispatcher_) {
    spdlog::error(
        "Dispatcher is null when registering notification: {}", method);
    throw std::runtime_error("Dispatcher is null");
  }

  // Adapt the handler to match what the dispatcher expects
  auto adapted_handler =
      [handler](const std::optional<nlohmann::json>& params) -> void {
    handler(params.value_or(nlohmann::json::object()));
  };

  dispatcher_->RegisterNotification(method, adapted_handler);
}

auto RpcEndpoint::GetNextRequestId() -> RequestId {
  return id_generator_->NextId();
}

void RpcEndpoint::SetErrorHandler(ErrorHandler handler) {
  std::lock_guard<std::mutex> lock(error_handler_mutex_);
  error_handler_ = std::move(handler);
}

void RpcEndpoint::ReportError(ErrorCode code, const std::string& message) {
  std::lock_guard<std::mutex> lock(error_handler_mutex_);
  if (error_handler_) {
    error_handler_(code, message);
  }
}

}  // namespace jsonrpc::endpoint
