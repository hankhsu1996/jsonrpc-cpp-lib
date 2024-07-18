#pragma once

#include <iostream>

#include "json_rpc/core/dispatcher.h"
#include "json_rpc/core/transport.h"
#include "json_rpc/core/types.h"

namespace json_rpc {

class StdioClientTransport : public ClientTransport {
public:
  Json SendMethodCall(const Json &request) override;
  void SendNotification(const Json &notification) override;
};

class StdioServerTransport : public ServerTransport {
protected:
  void Listen() override;
};

} // namespace json_rpc
