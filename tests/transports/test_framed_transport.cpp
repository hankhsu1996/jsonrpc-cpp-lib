#include <sstream>
#include <string>

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "jsonrpc/transport/framed_transport.hpp"

namespace jsonrpc::transport {

class FramedTransportTest : public jsonrpc::transport::FramedTransport {
 public:
  using jsonrpc::transport::FramedTransport::FrameMessage;
  using jsonrpc::transport::FramedTransport::ReadContent;
  using jsonrpc::transport::FramedTransport::ReadHeadersFromBuffer;

  static auto TestParseContentLength(const std::string& header_value) -> int {
    return ParseContentLength(header_value);
  }

  // Helper method to simulate receiving a message
  static auto ProcessMessage(const std::string& framed_message) -> std::string {
    asio::streambuf buffer;
    std::ostream os(&buffer);
    os << framed_message;

    auto headers = ReadHeadersFromBuffer(buffer);
    auto content_length = ReadContentLength(headers);
    return ReadContent(buffer, content_length);
  }
};

}  // namespace jsonrpc::transport

TEST_CASE("FramedTransport correctly frames a message", "[FramedTransport]") {
  std::ostringstream output;
  std::string message = R"({"jsonrpc":"2.0","method":"testMethod"})";

  jsonrpc::transport::FramedTransportTest::FrameMessage(output, message);

  std::string expected_output =
      "Content-Length: 39\r\n"
      "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
      "\r\n" +
      message;

  REQUIRE(output.str() == expected_output);
}

TEST_CASE("FramedTransport parses headers correctly", "[FramedTransport]") {
  std::string header_string =
      "Content-Length: 37\r\nContent-Type: "
      "application/vscode-jsonrpc; charset=utf-8\r\n\r\n";
  asio::streambuf buffer;
  std::ostream os(&buffer);
  os << header_string;

  auto headers =
      jsonrpc::transport::FramedTransportTest::ReadHeadersFromBuffer(buffer);

  REQUIRE(headers.size() == 2);
  REQUIRE(headers["Content-Length"] == "37");
  REQUIRE(
      headers["Content-Type"] == "application/vscode-jsonrpc; charset=utf-8");
}

TEST_CASE("FramedTransport reads correct content", "[FramedTransport]") {
  std::string content = R"({"jsonrpc":"2.0","method":"testMethod"})";
  asio::streambuf buffer;
  std::ostream os(&buffer);
  os << content;

  std::string result = jsonrpc::transport::FramedTransportTest::ReadContent(
      buffer, content.size());

  REQUIRE(result == content);
}

TEST_CASE(
    "FramedTransport correctly processes framed message", "[FramedTransport]") {
  std::string framed_message =
      "Content-Length: 39\r\nContent-Type: application/vscode-jsonrpc; "
      "charset=utf-8\r\n\r\n"
      R"({"jsonrpc":"2.0","method":"testMethod"})";

  auto result =
      jsonrpc::transport::FramedTransportTest::ProcessMessage(framed_message);

  REQUIRE(result == R"({"jsonrpc":"2.0","method":"testMethod"})");
}

TEST_CASE(
    "FramedTransport throws error on invalid content length",
    "[FramedTransport]") {
  std::string invalid_message = "Content-Length: invalid\r\n\r\n";

  REQUIRE_THROWS_WITH(
      jsonrpc::transport::FramedTransportTest::ProcessMessage(invalid_message),
      "Invalid Content-Length value");
}

TEST_CASE(
    "FramedTransport throws error on missing Content-Length",
    "[FramedTransport]") {
  std::string invalid_message =
      "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\n";

  REQUIRE_THROWS_WITH(
      jsonrpc::transport::FramedTransportTest::ProcessMessage(invalid_message),
      "Content-Length header missing");
}

TEST_CASE(
    "FramedTransport throws error on out of range content length",
    "[FramedTransport]") {
  std::string invalid_message =
      "Content-Length: 9999999999999999999999\r\n\r\n";

  REQUIRE_THROWS_WITH(
      jsonrpc::transport::FramedTransportTest::ProcessMessage(invalid_message),
      "Content-Length value out of range");
}

TEST_CASE("FramedTransport parses valid content length", "[FramedTransport]") {
  REQUIRE(
      jsonrpc::transport::FramedTransportTest::TestParseContentLength("42") ==
      42);
  REQUIRE(
      jsonrpc::transport::FramedTransportTest::TestParseContentLength("0") ==
      0);
  REQUIRE(
      jsonrpc::transport::FramedTransportTest::TestParseContentLength("1000") ==
      1000);
}
