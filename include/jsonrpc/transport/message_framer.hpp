#pragma once

#include <sstream>
#include <string>

namespace jsonrpc::transport {

class MessageFramer {
 public:
  struct DeframeResult {
    bool complete{false};
    std::string message;
    std::size_t consumed_bytes{0};
    std::string error;
  };

  static auto Frame(
      const std::string& message,
      const std::string& content_type =
          "application/vscode-jsonrpc; charset=utf-8") -> std::string {
    std::ostringstream out;
    out << "Content-Length: " << message.size() << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "\r\n"
        << message;
    return out.str();
  }

  auto TryDeframe(const std::string& buffer) -> DeframeResult {
    if (!header_complete_) {
      auto header_end = buffer.find("\r\n\r\n");
      if (header_end == std::string::npos) {
        return {
            .complete = false,
            .message = "",
            .consumed_bytes = 0,
            .error = ""};  // Need more data
      }

      // Parse headers
      std::istringstream header_stream(buffer.substr(0, header_end));
      std::string header_line;
      bool found_length = false;
      while (std::getline(header_stream, header_line) && !header_line.empty()) {
        if (header_line.starts_with("Content-Length:")) {
          try {
            expected_length_ = std::stoi(header_line.substr(15));
            found_length = true;
          } catch (const std::exception&) {
            return {
                .complete = false,
                .message = "",
                .consumed_bytes = 0,
                .error = "Invalid Content-Length header"};
          }
        }
      }

      if (!found_length) {
        return {
            .complete = false,
            .message = "",
            .consumed_bytes = 0,
            .error = "Missing Content-Length header"};
      }

      header_complete_ = true;
      header_size_ = header_end + 4;  // Include \r\n\r\n
    }

    // Check if we have enough data for the content
    if (buffer.size() < header_size_ + expected_length_) {
      return {
          .complete = false,
          .message = "",
          .consumed_bytes = 0,
          .error = ""};  // Need more data
    }

    // Extract message
    std::string message = buffer.substr(header_size_, expected_length_);
    std::size_t total_consumed = header_size_ + expected_length_;

    // Reset state for next message
    header_complete_ = false;
    expected_length_ = 0;
    header_size_ = 0;

    return {
        .complete = true,
        .message = message,
        .consumed_bytes = total_consumed,
        .error = ""};
  }

 private:
  bool header_complete_{false};
  std::size_t expected_length_{0};
  std::size_t header_size_{0};
};

}  // namespace jsonrpc::transport
