#pragma once

#include <atomic>
#include <memory>

#include "json_rpc/core/dispatcher.h"
#include "json_rpc/core/types.h"

namespace json_rpc {

class ClientTransport {
public:
  virtual ~ClientTransport() = default;

  // Send an RPC method to the server.
  virtual Json SendMethodCall(const Json &request) = 0;

  // Send an RPC notification to the server.
  virtual void SendNotification(const Json &notification) = 0;
};

class ServerTransport {
public:
  virtual ~ServerTransport() = default;

  // Start the transport.
  void Start() {
    running_.store(true, std::memory_order_release);
    Listen();
  }

  // Stop the transport.
  virtual void Stop() {
    running_.store(false, std::memory_order_release);
  }

  // Set the dispatcher.
  void SetDispatcher(Dispatcher *dispatcher) {
    dispatcher_ = dispatcher;
  }

protected:
  // Listen for incoming messages.
  virtual void Listen() = 0;

  std::atomic<bool> running_{false};
  Dispatcher *dispatcher_;
};

} // namespace json_rpc
