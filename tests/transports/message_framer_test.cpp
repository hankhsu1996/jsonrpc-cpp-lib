#include <string>

#include <catch2/catch_test_macros.hpp>

#include "jsonrpc/transport/message_framer.hpp"

using jsonrpc::transport::MessageFramer;

TEST_CASE("MessageFramer basic functionality") {
  MessageFramer framer;

  SECTION("Frame and deframe single message") {
    std::string original = R"({"method":"test"})";

    // Frame it
    std::string framed = MessageFramer::Frame(original);

    // Deframe it
    auto result = framer.TryDeframe(framed);
    REQUIRE(result.complete);
    REQUIRE(result.message == original);
  }

  SECTION("Partial message returns incomplete") {
    std::string partial = "Content-Length: 10\r\n\r\nonly5";

    auto result = framer.TryDeframe(partial);
    REQUIRE_FALSE(result.complete);
  }

  SECTION("Two messages in sequence") {
    std::string msg1 = R"({"id":1})";
    std::string msg2 = R"({"id":2})";

    std::string framed =
        MessageFramer::Frame(msg1) + MessageFramer::Frame(msg2);

    // First message
    auto result1 = framer.TryDeframe(framed);
    REQUIRE(result1.complete);
    REQUIRE(result1.message == msg1);

    // Second message
    auto result2 = framer.TryDeframe(framed.substr(result1.consumed_bytes));
    REQUIRE(result2.complete);
    REQUIRE(result2.message == msg2);
  }
}
