#include "jsonrpc/transport/framed_transport.hpp"

#include <stdexcept>

#include "jsonrpc/utils/string_utils.hpp"

namespace jsonrpc::transport {

void FramedTransport::FrameMessage(
    std::ostream& output, const std::string& message) {
  output << "Content-Length: " << message.size() << "\r\n"
         << "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
         << "\r\n"
         << message;
}

auto FramedTransport::ReadHeadersFromBuffer(asio::streambuf& buffer)
    -> FramedTransport::HeaderMap {
  HeaderMap headers;
  std::istream input(&buffer);
  std::string line;

  while (std::getline(input, line) && !line.empty() && line != "\r") {
    auto colon_pos = line.find(':');
    if (colon_pos != std::string::npos) {
      std::string header_key = utils::Trim(line.substr(0, colon_pos));
      std::string header_value = utils::Trim(line.substr(colon_pos + 1));
      headers[header_key] = header_value;
    }
  }

  if (headers.empty()) {
    throw std::runtime_error("Failed to read headers");
  }

  return headers;
}

auto FramedTransport::ReadContentLength(const HeaderMap& headers) -> int {
  auto it = headers.find("Content-Length");
  if (it == headers.end()) {
    throw std::runtime_error("Content-Length header missing");
  }
  return ParseContentLength(it->second);
}

auto FramedTransport::ReadContent(
    asio::streambuf& buffer, std::size_t content_length) -> std::string {
  std::istream input(&buffer);
  std::string content(content_length, '\0');
  input.read(content.data(), static_cast<std::streamsize>(content_length));
  if (input.gcount() != static_cast<std::streamsize>(content_length)) {
    throw std::runtime_error("Failed to read the expected content length");
  }
  return content;
}

auto FramedTransport::ParseContentLength(const std::string& header_value)
    -> int {
  try {
    return std::stoi(header_value);
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("Invalid Content-Length value");
  } catch (const std::out_of_range&) {
    throw std::runtime_error("Content-Length value out of range");
  }
}

}  // namespace jsonrpc::transport
