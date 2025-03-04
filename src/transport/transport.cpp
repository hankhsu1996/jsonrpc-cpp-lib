#include "jsonrpc/transport/transport.hpp"

#include <thread>

#include <spdlog/spdlog.h>

namespace jsonrpc::transport {

Transport::Transport(asio::io_context* external_io_context)
    : owns_io_context_(external_io_context == nullptr),
      io_context_(nullptr),
      owned_io_context_(nullptr),
      work_guard_(nullptr) {
  // Initialize the io_context
  if (owns_io_context_) {
    owned_io_context_ = std::make_unique<asio::io_context>();
    io_context_ = owned_io_context_.get();
  } else {
    io_context_ = external_io_context;
  }

  spdlog::debug(
      "Transport initialized with {} io_context",
      owns_io_context_ ? "owned" : "external");
}

void Transport::EnsureIoContextRunning() {
  if (owns_io_context_ && !work_guard_) {
    // Create a work guard to keep the io_context running
    work_guard_ = std::make_unique<
        asio::executor_work_guard<asio::io_context::executor_type>>(
        io_context_->get_executor());

    // Start the io_context in a background thread
    std::thread([this]() {
      try {
        spdlog::debug("Starting io_context run loop in background thread");
        io_context_->run();
        spdlog::debug("io_context run loop exited");
      } catch (const std::exception& e) {
        spdlog::error("Error in io_context run loop: {}", e.what());
      }
    }).detach();
  }
}

}  // namespace jsonrpc::transport
