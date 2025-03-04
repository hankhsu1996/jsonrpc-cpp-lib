#include "jsonrpc/endpoint/endpoint.hpp"

#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/request.hpp"
#include "jsonrpc/endpoint/response.hpp"

namespace jsonrpc::endpoint {

RpcEndpoint::RpcEndpoint(
    std::unique_ptr<transport::Transport> transport,
    std::unique_ptr<IdGenerator> id_generator)
    : transport_(std::move(transport)),
      id_generator_(std::move(id_generator)),
      is_running_(false),
      shutdown_promise_(std::make_shared<std::promise<void>>()) {
  // Create the strand with the transport's io_context
  auto* io_context = transport_->GetIoContext();
  if (io_context != nullptr) {
    endpoint_strand_ = std::make_unique<asio::io_context::strand>(*io_context);
    spdlog::debug("Created endpoint strand with transport's io_context");
  } else {
    spdlog::error(
        "Transport has no io_context, endpoint operations may not be properly "
        "serialized");
  }

  // Create the dispatcher with the endpoint strand
  dispatcher_ = std::make_unique<Dispatcher>(endpoint_strand_.get());
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

  if (!endpoint_strand_) {
    promise.set_exception(
        std::make_exception_ptr(std::runtime_error("Endpoint strand is null")));
    return future;
  }

  // Use the strand to ensure thread safety
  asio::dispatch(
      *endpoint_strand_, [this, promise = std::move(promise)]() mutable {
        if (is_running_) {
          promise.set_exception(std::make_exception_ptr(
              std::runtime_error("RpcEndpoint is already running")));
          return;
        }

        is_running_.store(true);

        // Start the transport's io_context if it's not already running
        auto* io_context = transport_->GetIoContext();
        if ((io_context != nullptr) && !io_context->stopped()) {
          spdlog::debug("Ensuring io_context is running");
        }

        // Start the asynchronous message processing chain
        StartMessageProcessing();

        // Set the promise value to indicate start completed
        promise.set_value();
      });

  return future;
}

auto RpcEndpoint::Wait() -> std::future<void> {
  return shutdown_promise_->get_future();
}

auto RpcEndpoint::Shutdown() -> std::future<void> {
  std::promise<void> promise;
  auto future = promise.get_future();

  if (!endpoint_strand_) {
    promise.set_exception(
        std::make_exception_ptr(std::runtime_error("Endpoint strand is null")));
    return future;
  }

  // Use the strand to ensure thread safety
  asio::dispatch(
      *endpoint_strand_, [this, promise = std::move(promise)]() mutable {
        if (!is_running_) {
          promise.set_value();
          return;
        }

        is_running_.store(false);

        // Close the transport to cancel any pending operations
        try {
          transport_->Close().get();
        } catch (const std::exception& e) {
          spdlog::warn("Error during transport close: {}", e.what());
        }

        // Signal any waiting threads
        try {
          shutdown_promise_->set_value();
        } catch (const std::future_error&) {
          // Promise might already be satisfied, that's ok
        }

        promise.set_value();
      });

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
  if (!endpoint_strand_) {
    std::promise<nlohmann::json> error_promise;
    error_promise.set_exception(
        std::make_exception_ptr(std::runtime_error("Endpoint strand is null")));
    return error_promise.get_future();
  }

  if (!is_running_) {
    std::promise<nlohmann::json> error_promise;
    error_promise.set_exception(std::make_exception_ptr(
        std::runtime_error("RpcEndpoint is not running")));
    return error_promise.get_future();
  }

  // Create a shared promise/future pair that will be used across strand
  // invocations
  auto shared_promise = std::make_shared<std::promise<nlohmann::json>>();
  auto future = shared_promise->get_future();

  // Use the strand to ensure thread safety
  asio::post(
      *endpoint_strand_,
      [this, method, params = std::move(params), shared_promise]() mutable {
        try {
          auto request_id = GetNextRequestId();
          Request request(method, std::move(params), request_id);
          auto request_json = request.ToJson();

          // Store the promise in the pending requests map
          pending_requests_[request_id] = std::move(*shared_promise);

          // Send the request using the transport
          transport_->SendMessage(request_json.dump()).get();
        } catch (const std::exception& e) {
          // Set the exception on the promise in case of error
          shared_promise->set_exception(std::current_exception());
        }
      });

  return future;
}

auto RpcEndpoint::SendNotification(
    const std::string& method,
    std::optional<nlohmann::json> params) -> std::future<void> {
  if (!endpoint_strand_) {
    std::promise<void> error_promise;
    error_promise.set_exception(
        std::make_exception_ptr(std::runtime_error("Endpoint strand is null")));
    return error_promise.get_future();
  }

  if (!is_running_) {
    std::promise<void> error_promise;
    error_promise.set_exception(std::make_exception_ptr(
        std::runtime_error("RpcEndpoint is not running")));
    return error_promise.get_future();
  }

  // Create a shared promise/future pair that will be used across strand
  // invocations
  auto shared_promise = std::make_shared<std::promise<void>>();
  auto future = shared_promise->get_future();

  // Use the strand to ensure thread safety
  asio::post(
      *endpoint_strand_,
      [this, method, params = std::move(params), shared_promise]() mutable {
        try {
          Request request(method, std::move(params));
          auto request_json = request.ToJson();

          // Send the notification using the transport
          transport_->SendMessage(request_json.dump()).get();

          // Set the value on the promise
          shared_promise->set_value();
        } catch (const std::exception& e) {
          spdlog::error("Failed to send notification: {}", e.what());
          // Set the exception on the promise in case of error
          shared_promise->set_exception(std::current_exception());
        }
      });

  return future;
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
  // This needs to be checked from the strand
  if (!endpoint_strand_) {
    return false;
  }

  // Using a synchronous operation for simplicity in this check method
  // For production code, you might want to make this asynchronous too
  std::promise<bool> promise;
  auto future = promise.get_future();

  asio::post(*endpoint_strand_, [this, &promise]() {
    promise.set_value(!pending_requests_.empty());
  });

  return future.get();
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
  if (!endpoint_strand_) {
    throw std::runtime_error("Endpoint strand is null");
  }

  asio::dispatch(
      *endpoint_strand_, [this, handler = std::move(handler)]() mutable {
        error_handler_ = std::move(handler);
      });
}

void RpcEndpoint::ReportError(ErrorCode code, const std::string& message) {
  if (!endpoint_strand_) {
    spdlog::error("Cannot report error: Endpoint strand is null");
    return;
  }

  asio::dispatch(*endpoint_strand_, [this, code, message]() {
    if (error_handler_) {
      error_handler_(code, message);
    }
  });
}

void RpcEndpoint::StartMessageProcessing() {
  if (!endpoint_strand_) {
    spdlog::error("Cannot start message processing: endpoint_strand_ is null");
    return;
  }

  spdlog::debug("Starting asynchronous message processing chain");
  asio::post(*endpoint_strand_, [this]() { ProcessNextMessage(); });
}

void RpcEndpoint::ProcessNextMessage() {
  // Check if endpoint is still running before proceeding
  if (!is_running_) {
    spdlog::debug(
        "Message processing stopped because endpoint is no longer running");
    return;
  }

  auto* io_context = transport_->GetIoContext();
  if ((io_context == nullptr) || !endpoint_strand_) {
    spdlog::error("Cannot process messages: missing io_context or strand");
    return;
  }

  try {
    // Start a non-blocking async operation to initiate message receiving
    asio::post(*endpoint_strand_, [this, io_context_ptr = io_context]() {
      if (!is_running_) {
        return;
      }

      // Use a shared future to avoid copying the string multiple times
      auto receive_future_shared = std::make_shared<std::future<std::string>>();

      try {
        // Initialize the future with the transport's ReceiveMessage operation
        *receive_future_shared =
            WithTransport([](transport::Transport& transport) {
              return transport.ReceiveMessage();
            });

        // Create a timer to check for future completion without blocking a
        // thread
        auto poll_timer = std::make_shared<asio::steady_timer>(
            *io_context_ptr, std::chrono::milliseconds(10));

        // Define the future checking function
        std::function<void(const asio::error_code&)> check_future;

        // Initialize the function with a recursive lambda
        check_future = [this, receive_future_shared, poll_timer, io_context_ptr,
                        &check_future](const asio::error_code& ec) {
          if (ec) {
            // Timer was cancelled or errored
            if (ec != asio::error::operation_aborted && is_running_) {
              spdlog::error(
                  "Timer error in message processing: {}", ec.message());
              asio::post(*endpoint_strand_, [this]() { ProcessNextMessage(); });
            }
            return;
          }

          if (!is_running_) {
            return;  // Endpoint is shutting down
          }

          // Check if the future is ready without blocking
          auto status =
              receive_future_shared->wait_for(std::chrono::seconds(0));

          if (status == std::future_status::ready) {
            // Get the message and process it within the strand
            try {
              std::string message = receive_future_shared->get();
              asio::post(
                  *endpoint_strand_, [this, message = std::move(message)]() {
                    if (!is_running_) {
                      return;
                    }

                    if (!message.empty()) {
                      HandleMessage(message);
                    }

                    // Continue the message processing chain
                    ProcessNextMessage();
                  });
            } catch (const std::exception& e) {
              asio::post(
                  *endpoint_strand_,
                  [this, error = std::string(e.what()), io_context_ptr]() {
                    if (!is_running_) {
                      return;
                    }

                    spdlog::error("Error getting message: {}", error);
                    ReportError(ErrorCode::kTransportError, error);

                    // Retry after a delay
                    auto retry_timer = std::make_shared<asio::steady_timer>(
                        *io_context_ptr, std::chrono::milliseconds(100));
                    retry_timer->async_wait(
                        [this, retry_timer](const asio::error_code& ec) {
                          if (!ec && is_running_) {
                            ProcessNextMessage();
                          }
                        });
                  });
            }
          } else {
            // Future not ready yet, check again after a short delay
            poll_timer->expires_after(std::chrono::milliseconds(10));
            poll_timer->async_wait(check_future);
          }
        };

        // Start polling
        poll_timer->async_wait(check_future);

      } catch (const std::exception& e) {
        // Handle initialization errors
        spdlog::error("Error starting message receive: {}", e.what());
        ReportError(ErrorCode::kTransportError, e.what());

        // Retry after a delay
        auto retry_timer = std::make_shared<asio::steady_timer>(
            *io_context_ptr, std::chrono::milliseconds(100));
        retry_timer->async_wait(
            [this, retry_timer](const asio::error_code& ec) {
              if (!ec && is_running_) {
                ProcessNextMessage();
              }
            });
      }
    });
  } catch (const std::exception& e) {
    // Only log and report if we're still running
    if (is_running_) {
      spdlog::error("Error in message processing loop: {}", e.what());
      ReportError(ErrorCode::kTransportError, e.what());

      // Try to restart processing after a delay
      if ((io_context != nullptr) && endpoint_strand_) {
        auto timer = std::make_shared<asio::steady_timer>(
            *io_context, std::chrono::milliseconds(100));
        timer->async_wait([this, timer](const asio::error_code& ec) {
          if (!ec && is_running_) {
            ProcessNextMessage();
          }
        });
      }
    }
  }
}

}  // namespace jsonrpc::endpoint
