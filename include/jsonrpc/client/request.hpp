#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace jsonrpc {
namespace client {

class ClientRequest {
public:
  // Constructor for a single request with ID generation
  ClientRequest(const std::string &method, std::optional<nlohmann::json> params,
      bool isNotification, const std::function<int()> &idGenerator);

  // Method to check if a response is required
  bool RequiresResponse() const;

  // Method to get the unique key
  int GetKey() const;

  // Method to dump the request to a JSON string
  std::string Dump() const;

private:
  std::string method_;
  std::optional<nlohmann::json> params_;
  bool isNotification_;
  int id_;
};

} // namespace client
} // namespace jsonrpc
