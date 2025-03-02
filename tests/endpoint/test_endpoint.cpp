#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "../common/mock_transport.hpp"
#include "jsonrpc/endpoint/endpoint.hpp"

using Json = nlohmann::json;

TEST_CASE("RpcEndpoint starts and stops correctly", "[RpcEndpoint]") {
  auto transport = std::make_unique<MockTransport>();
  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));

  endpoint.Start();
  REQUIRE(endpoint.IsRunning() == true);
  REQUIRE(endpoint.HasPendingRequests() == false);

  endpoint.Stop();
  REQUIRE(endpoint.IsRunning() == false);
}

TEST_CASE(
    "RpcEndpoint handles method call responses correctly", "[RpcEndpoint]") {
  auto transport = std::make_unique<MockTransport>();
  transport->SetResponse(R"({"jsonrpc":"2.0","result":"success","id":0})");

  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));
  endpoint.Start();

  REQUIRE(endpoint.HasPendingRequests() == false);
  auto response = endpoint.SendMethodCall("test_method");
  REQUIRE(endpoint.HasPendingRequests() == false);

  REQUIRE(response["result"] == "success");

  endpoint.Stop();
}

TEST_CASE(
    "RpcEndpoint sends notifications without expecting response",
    "[RpcEndpoint]") {
  auto transport = std::make_unique<MockTransport>();
  MockTransport* transport_ptr = transport.get();

  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));
  endpoint.Start();

  endpoint.SendNotification(
      "notify_event", nlohmann::json({{"param1", "value1"}}));

  REQUIRE(endpoint.HasPendingRequests() == false);
  REQUIRE(transport_ptr->sent_requests.size() == 1);
  REQUIRE(
      transport_ptr->sent_requests[0].find("notify_event") !=
      std::string::npos);

  endpoint.Stop();
}

TEST_CASE(
    "RpcEndpoint handles async method calls correctly",
    "[RpcEndpoint][Async]") {
  auto transport = std::make_unique<MockTransport>();
  MockTransport* transport_ptr = transport.get();

  transport_ptr->SetResponse(
      R"({"jsonrpc":"2.0","result":"async_success","id":0})");

  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));
  endpoint.Start();

  auto future_response = endpoint.SendMethodCallAsync("async_test_method");

  REQUIRE(
      future_response.wait_for(std::chrono::seconds(1)) ==
      std::future_status::ready);
  nlohmann::json response = future_response.get();

  REQUIRE(endpoint.HasPendingRequests() == false);
  REQUIRE(response["result"] == "async_success");

  endpoint.Stop();
}

TEST_CASE(
    "RpcEndpoint handles multiple async calls concurrently",
    "[RpcEndpoint][Async]") {
  auto transport = std::make_unique<MockTransport>();
  MockTransport* transport_ptr = transport.get();

  const int num_requests = 5;
  for (int i = 0; i < num_requests; ++i) {
    transport_ptr->SetResponse(fmt::format(
        R"({{"jsonrpc":"2.0","result":"success_{}","id":{}}})", i, i));
  }

  jsonrpc::endpoint::RpcEndpoint endpoint(std::move(transport));
  endpoint.Start();

  std::vector<std::future<nlohmann::json>> futures;

  // Send all requests first
  for (int i = 0; i < num_requests; ++i) {
    futures.push_back(
        endpoint.SendMethodCallAsync(fmt::format("async_test_method_{}", i)));
  }

  // Wait for and verify each response
  for (int i = 0; i < num_requests; ++i) {
    REQUIRE(
        futures[i].wait_for(std::chrono::seconds(1)) ==
        std::future_status::ready);
    nlohmann::json response = futures[i].get();
    REQUIRE(response["result"] == fmt::format("success_{}", i));
  }

  // Give some time for the message processing thread to clean up
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  REQUIRE(endpoint.HasPendingRequests() == false);
  endpoint.Stop();
}
