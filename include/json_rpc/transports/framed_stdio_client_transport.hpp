#pragma once

#include <string>

#include "json_rpc/core/client_transport.hpp"
#include "json_rpc/core/types.hpp"
#include "json_rpc/transports/framed_transport.hpp"

namespace json_rpc {

/**
 * @brief Client transport using framed stdio for communication.
 *
 * This class implements the ClientTransport interface using framed stdio
 * as the underlying transport mechanism.
 */
class FramedStdioClientTransport :
    public ClientTransport,
    protected FramedTransport {
public:
  /**
   * @brief Sends an RPC method call to the server.
   *
   * @param request The JSON-RPC request as a JSON object.
   * @return The response from the server as a JSON object.
   */
  Json SendMethodCall(const Json &request) override;

  /**
   * @brief Sends an RPC notification to the server.
   *
   * @param notification The JSON-RPC notification as a JSON object.
   */
  void SendNotification(const Json &notification) override;
};

} // namespace json_rpc
