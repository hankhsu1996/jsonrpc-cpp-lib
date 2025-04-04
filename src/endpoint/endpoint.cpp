#include "jsonrpc/endpoint/endpoint.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <jsonrpc/error/error.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/endpoint/request.hpp"
#include "jsonrpc/endpoint/response.hpp"

namespace jsonrpc::endpoint {

using jsonrpc::error::CreateClientError;
using jsonrpc::error::CreateServerError;
using jsonrpc::error::ErrorCode;
using jsonrpc::error::RpcError;

RpcEndpoint::RpcEndpoint(
    asio::any_io_executor executor,
    std::unique_ptr<transport::Transport> transport)
    : executor_(std::move(executor)),
      transport_(std::move(transport)),
      dispatcher_(executor_),
      endpoint_strand_(asio::make_strand(executor_)) {
}

auto RpcEndpoint::CreateClient(
    asio::any_io_executor executor,
    std::unique_ptr<transport::Transport> transport)
    -> asio::awaitable<std::unique_ptr<RpcEndpoint>> {
  // Create the endpoint
  auto endpoint = std::make_unique<RpcEndpoint>(executor, std::move(transport));

  // Start the endpoint and wait for it to be ready
  try {
    co_await endpoint->Start();
    spdlog::debug("Client endpoint initialized");
  } catch (const std::exception &e) {
    spdlog::error("Error starting client endpoint: {}", e.what());
    throw;
  }

  // Return the fully initialized endpoint
  co_return endpoint;
}

auto RpcEndpoint::Start() -> asio::awaitable<std::expected<void, RpcError>> {
  if (is_running_.exchange(true)) {
    co_return CreateClientError("RPC endpoint is already running");
  }

  spdlog::info("Starting RPC endpoint");
  pending_requests_.clear();

  // Start the transport
  auto start_result = co_await transport_->Start();
  if (!start_result) {
    co_return std::unexpected(start_result.error());
  }

  // Start message processing on the endpoint strand
  StartMessageProcessing();

  // Ensure Start completes before Wait checks is_running_
  co_return std::expected<void, RpcError>{};
}

auto RpcEndpoint::WaitForShutdown() -> asio::awaitable<void> {
  // If already shut down, return immediately
  if (!is_running_) {
    co_return;
  }

  // Create a timer to check if the service is running
  auto executor = co_await asio::this_coro::executor;
  asio::steady_timer timer(executor, std::chrono::milliseconds(100));

  // Check periodically until shutdown is complete
  while (is_running_) {
    co_await timer.async_wait(asio::use_awaitable);
    timer.expires_after(std::chrono::milliseconds(100));
  }
}

auto RpcEndpoint::Shutdown() -> asio::awaitable<void> {
  if (!is_running_.exchange(false)) {
    co_return;
  }

  spdlog::info("Shutting down RPC endpoint");

  // Cancel all pending requests
  asio::post(endpoint_strand_, [this]() {
    for (auto &[id, request] : pending_requests_) {
      request->Cancel(-32603, "RPC endpoint shutting down");
    }
    pending_requests_.clear();
  });

  // Close the transport - this ensures any pending operations are canceled
  co_await transport_->Close();

  co_return;
}

auto RpcEndpoint::SendMethodCall(
    std::string method, std::optional<nlohmann::json> params)
    -> asio::awaitable<std::expected<nlohmann::json, RpcError>> {
  if (!is_running_) {
    co_return CreateClientError("RPC endpoint is not running");
  }

  auto request_id = GetNextRequestId();
  Request request(method, std::move(params), request_id);
  std::string message = request.ToJson().dump();

  auto pending_request = std::make_shared<PendingRequest>(endpoint_strand_);
  asio::post(endpoint_strand_, [this, request_id, pending_request] {
    pending_requests_[request_id] = pending_request;
  });

  auto send_result = co_await transport_->SendMessage(message);
  if (!send_result) {
    co_return std::unexpected(send_result.error());
  }

  auto result = co_await pending_request->GetResult();
  if (result.contains("error")) {
    auto err = result["error"];
    co_return CreateClientError(err["message"].get<std::string>());
  }

  co_return result["result"];
}

auto RpcEndpoint::SendNotification(
    std::string method, std::optional<nlohmann::json> params)
    -> asio::awaitable<std::expected<void, RpcError>> {
  if (!is_running_) {
    co_return CreateClientError("RPC endpoint is not running");
  }

  Request request(method, std::move(params));
  std::string message = request.ToJson().dump();

  auto send_result = co_await transport_->SendMessage(message);
  if (!send_result) {
    co_return std::unexpected(send_result.error());
  }

  co_return std::expected<void, RpcError>{};
}

void RpcEndpoint::RegisterMethodCall(
    std::string method, typename Dispatcher::MethodCallHandler handler) {
  dispatcher_.RegisterMethodCall(method, handler);
}

void RpcEndpoint::RegisterNotification(
    std::string method, typename Dispatcher::NotificationHandler handler) {
  dispatcher_.RegisterNotification(method, handler);
}

auto RpcEndpoint::HasPendingRequests() const -> bool {
  // This is safe to call without a strand because we're just checking if empty
  return !pending_requests_.empty();
}

void RpcEndpoint::StartMessageProcessing() {
  asio::co_spawn(
      endpoint_strand_, [this] { return this->ProcessMessagesLoop(); },
      asio::detached);
}

auto RpcEndpoint::ProcessMessagesLoop() -> asio::awaitable<void> {
  while (is_running_) {
    try {
      // Wait for the next message
      auto message = co_await transport_->ReceiveMessage();
      if (!message.has_value()) {
        spdlog::error("Error receiving message: {}", message.error().message);
        continue;
      }

      // Process the message
      co_await HandleMessage(message.value());
    } catch (const std::exception &e) {
      spdlog::error("Error processing message: {}", e.what());

      if (!is_running_) {
        break;  // Exit loop if we're shutting down
      }

      // We can't use co_await in a catch block, so we need to continue the loop
      // and do the pause in the next iteration
      asio::steady_timer retry_timer(executor_, std::chrono::milliseconds(100));

      // Use a non-coroutine wait to prevent co_await in catch handler
      asio::error_code ec;
      retry_timer.wait(ec);

      if (ec) {
        spdlog::warn("Error waiting for retry timer: {}", ec.message());
      }
    }
  }
}

auto RpcEndpoint::HandleMessage(std::string message) -> asio::awaitable<void> {
  try {
    // Try to parse as a JSON object
    auto json_message = nlohmann::json::parse(message);

    // Check if it's a response
    if (json_message.contains("id") &&
        (json_message.contains("result") || json_message.contains("error"))) {
      auto response = Response::FromJson(json_message);
      if (!response.has_value()) {
        co_return;
      }
      co_await HandleResponse(std::move(response.value()));
      co_return;
    }

    // Try to handle as a request
    auto response = co_await dispatcher_.DispatchRequest(message);
    if (response) {
      co_await transport_->SendMessage(*response);
    }
  } catch (const std::exception &e) {
    spdlog::error("Error handling message: {}", e.what());
    throw;
  }
}

auto RpcEndpoint::HandleResponse(Response response) -> asio::awaitable<void> {
  // Get the request ID
  auto id_variant = response.GetId();
  if (!id_variant.has_value()) {
    // ReportError(ErrorCode::kInvalidRequest, "Response missing ID");
    co_return;
  }

  // Extract the integer ID - we only support integer IDs in our implementation
  if (!std::holds_alternative<int64_t>(*id_variant)) {
    // ReportError(ErrorCode::kInvalidRequest, "Response ID must be an
    // integer");
    co_return;
  }

  auto id = std::get<int64_t>(*id_variant);

  // Use a local variable to capture the pending request when found
  std::shared_ptr<PendingRequest> request;
  bool found = false;

  // Find the request in a strand-protected way
  asio::post(endpoint_strand_, [this, id, &request, &found]() {
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
      request = it->second;
      pending_requests_.erase(it);
      found = true;
    }
  });

  // Wait for the post to complete
  co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);

  if (!found || !request) {
    // ReportError(
    //     ErrorCode::kInvalidRequest,
    //     "Received response for unknown request ID: " + std::to_string(id));
    co_return;
  }

  // Set the result
  request->SetResult(response.ToJson());

  co_return;
}

}  // namespace jsonrpc::endpoint
