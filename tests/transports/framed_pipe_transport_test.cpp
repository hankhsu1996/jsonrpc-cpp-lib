#include "jsonrpc/transport/framed_pipe_transport.hpp"

#include <memory>
#include <string>

#include <asio.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/parallel_group.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "jsonrpc/transport/pipe_transport.hpp"

using jsonrpc::transport::FramedPipeTransport;
using jsonrpc::transport::PipeTransport;

namespace {

// Helper function to run async tests
template <typename Func>
void RunTest(Func&& test_func) {
  // Setup logging
  auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("test", console);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);

  asio::io_context io_ctx;
  auto executor = io_ctx.get_executor();
  asio::co_spawn(
      executor,
      [test_func = std::forward<Func>(test_func), executor]() {
        return test_func(executor);
      },
      asio::detached);
  io_ctx.run();
}

// Helper function to frame a message (since FrameMessage is protected)
auto FrameMessage(const std::string& message) -> std::string {
  std::ostringstream framed;
  framed << "Content-Length: " << message.length() << "\r\n\r\n" << message;
  return framed.str();
}

}  // namespace

auto StartSender(
    asio::any_io_executor executor, std::shared_ptr<PipeTransport> raw_sender)
    -> asio::awaitable<void> {
  co_await raw_sender->Start();
}

auto StartReceiver(
    asio::any_io_executor executor,
    std::shared_ptr<FramedPipeTransport> framed_receiver)
    -> asio::awaitable<void> {
  co_await framed_receiver->Start();
}

TEST_CASE("FramedPipeTransport basic communication", "[FramedPipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    const std::string socket_path = "/tmp/test_framed_transport";
    auto raw_sender =
        std::make_shared<PipeTransport>(executor, socket_path, true);
    auto framed_receiver =
        std::make_shared<FramedPipeTransport>(executor, socket_path, false);

    // Start both in parallel and wait for connection
    co_await asio::experimental::make_parallel_group(
        asio::co_spawn(
            executor, StartSender(executor, raw_sender), asio::deferred),
        asio::co_spawn(
            executor, StartReceiver(executor, framed_receiver), asio::deferred))
        .async_wait(asio::experimental::wait_for_all(), asio::use_awaitable);

    // After connection, sender and receiver operate independently
    std::string msg1 = R"({"jsonrpc":"2.0","method":"test1"})";
    std::string msg2 = R"({"jsonrpc":"2.0","method":"test2"})";

    // Start sender in separate coroutine
    asio::co_spawn(
        executor,
        [&raw_sender, msg1 = FrameMessage(msg1),
         msg2 = FrameMessage(msg2)]() -> asio::awaitable<void> {
          co_await raw_sender->SendMessage(msg1);
          co_await raw_sender->SendMessage(msg2);
        },
        asio::detached);

    // Receiver gets complete messages regardless of how they were sent
    auto received1 = co_await framed_receiver->ReceiveMessage();
    REQUIRE(received1.has_value());
    REQUIRE(received1.value() == msg1);
    auto received2 = co_await framed_receiver->ReceiveMessage();
    REQUIRE(received2.has_value());
    REQUIRE(received2.value() == msg2);

    co_await raw_sender->Close();
    co_await framed_receiver->Close();
  });
}

TEST_CASE(
    "FramedPipeTransport handles split messages", "[FramedPipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    const std::string socket_path = "/tmp/test_framed_transport_split";
    auto raw_sender =
        std::make_unique<PipeTransport>(executor, socket_path, true);
    auto framed_receiver =
        std::make_unique<FramedPipeTransport>(executor, socket_path, false);

    // Start both in parallel
    co_await asio::experimental::make_parallel_group(
        asio::co_spawn(
            executor,
            [&raw_sender]() -> asio::awaitable<void> {
              co_await raw_sender->Start();
            },
            asio::deferred),
        asio::co_spawn(
            executor,
            [&framed_receiver]() -> asio::awaitable<void> {
              co_await framed_receiver->Start();
            },
            asio::deferred))
        .async_wait(asio::experimental::wait_for_all(), asio::use_awaitable);

    // Test different split scenarios
    SECTION("Split in Content-Length") {
      std::string message = R"({"jsonrpc":"2.0","method":"test1"})";
      std::string framed = FrameMessage(message);

      // Split between "Content-" and "Length"
      std::string part1 = framed.substr(0, 8);  // "Content-"
      std::string part2 = framed.substr(8);     // "Length: ..." + rest

      asio::co_spawn(
          executor,
          [&raw_sender, part1, part2]() -> asio::awaitable<void> {
            co_await raw_sender->SendMessage(part1);
            co_await raw_sender->SendMessage(part2);
          },
          asio::detached);

      auto received = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received.has_value());
      REQUIRE(received.value() == message);
    }

    SECTION("Split in middle of length value") {
      std::string message = R"({"jsonrpc":"2.0","method":"test2"})";
      std::string framed = FrameMessage(message);

      // Find where the number starts and split in middle
      size_t length_pos = framed.find(": ") + 2;
      size_t header_end = framed.find("\r\n\r\n");
      size_t mid_number = length_pos + 1;

      std::string part1 = framed.substr(0, mid_number);  // "Content-Length: 3"
      std::string part2 =
          framed.substr(mid_number, header_end - mid_number);  // rest of number
      std::string part3 = framed.substr(header_end);  // "\r\n\r\n" + content

      asio::co_spawn(
          executor,
          [&raw_sender, part1, part2, part3]() -> asio::awaitable<void> {
            co_await raw_sender->SendMessage(part1);
            co_await raw_sender->SendMessage(part2);
            co_await raw_sender->SendMessage(part3);
          },
          asio::detached);

      auto received = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received.has_value());
      REQUIRE(received.value() == message);
    }

    SECTION("Split at header boundary") {
      std::string message = R"({"jsonrpc":"2.0","method":"test3"})";
      std::string framed = FrameMessage(message);

      // Split exactly at \r\n\r\n
      size_t header_end = framed.find("\r\n\r\n");

      std::string part1 = framed.substr(0, header_end);   // Up to \r\n\r\n
      std::string part2 = framed.substr(header_end, 4);   // \r\n\r\n
      std::string part3 = framed.substr(header_end + 4);  // content

      asio::co_spawn(
          executor,
          [&raw_sender, part1, part2, part3]() -> asio::awaitable<void> {
            co_await raw_sender->SendMessage(part1);
            co_await raw_sender->SendMessage(part2);
            co_await raw_sender->SendMessage(part3);
          },
          asio::detached);

      auto received = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received.has_value());
      REQUIRE(received.value() == message);
    }

    SECTION("Split in middle of delimiter") {
      std::string message = R"({"jsonrpc":"2.0","method":"test4"})";
      std::string framed = FrameMessage(message);

      // Find the header delimiter and split it
      size_t delimiter_start = framed.find("\r\n\r\n");

      std::string part1 =
          framed.substr(0, delimiter_start + 2);  // Up to first \r\n
      std::string part2 = framed.substr(delimiter_start + 2, 2);  // Second \r\n
      std::string part3 = framed.substr(delimiter_start + 4);     // content

      asio::co_spawn(
          executor,
          [&raw_sender, part1, part2, part3]() -> asio::awaitable<void> {
            co_await raw_sender->SendMessage(
                part1);  // "Content-Length: 32\r\n"
            co_await raw_sender->SendMessage(part2);  // "\r\n"
            co_await raw_sender->SendMessage(part3);  // actual message
          },
          asio::detached);

      auto received = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received.has_value());
      REQUIRE(received.value() == message);
    }

    SECTION("Split into tiny chunks") {
      std::string message = R"({"jsonrpc":"2.0","method":"test5"})";
      std::string framed = FrameMessage(message);

      // Send the message one byte at a time
      asio::co_spawn(
          executor,
          [&raw_sender, framed]() -> asio::awaitable<void> {
            for (size_t i = 0; i < framed.length(); ++i) {
              co_await raw_sender->SendMessage(framed.substr(i, 1));
            }
          },
          asio::detached);

      auto received = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received.has_value());
      REQUIRE(received.value() == message);
    }

    co_await raw_sender->Close();
    co_await framed_receiver->Close();
  });
}

TEST_CASE(
    "FramedPipeTransport handles multiple messages", "[FramedPipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    const std::string socket_path = "/tmp/test_framed_transport_multi";
    auto raw_sender =
        std::make_unique<PipeTransport>(executor, socket_path, true);
    auto framed_receiver =
        std::make_unique<FramedPipeTransport>(executor, socket_path, false);

    // Start both in parallel
    co_await asio::experimental::make_parallel_group(
        asio::co_spawn(
            executor,
            [&raw_sender]() -> asio::awaitable<void> {
              co_await raw_sender->Start();
            },
            asio::deferred),
        asio::co_spawn(
            executor,
            [&framed_receiver]() -> asio::awaitable<void> {
              co_await framed_receiver->Start();
            },
            asio::deferred))
        .async_wait(asio::experimental::wait_for_all(), asio::use_awaitable);

    // Create multiple messages
    std::vector<std::string> messages = {
        R"({"jsonrpc":"2.0","method":"test1","id":1})",
        R"({"jsonrpc":"2.0","method":"test2","id":2})",
        R"({"jsonrpc":"2.0","method":"test3","id":3})"};

    // Send all messages in one coroutine
    asio::co_spawn(
        executor,
        [&raw_sender, messages]() -> asio::awaitable<void> {
          for (const auto& msg : messages) {
            co_await raw_sender->SendMessage(FrameMessage(msg));
          }
        },
        asio::detached);

    // Receive all messages
    for (const auto& expected : messages) {
      auto received = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received.has_value());
      REQUIRE(received.value() == expected);
    }

    co_await raw_sender->Close();
    co_await framed_receiver->Close();
  });
}

TEST_CASE(
    "FramedPipeTransport handles back-to-back partial messages",
    "[FramedPipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    const std::string socket_path = "/tmp/test_framed_transport_backtoback";
    auto raw_sender =
        std::make_unique<PipeTransport>(executor, socket_path, true);
    auto framed_receiver =
        std::make_unique<FramedPipeTransport>(executor, socket_path, false);

    // Start both in parallel
    co_await asio::experimental::make_parallel_group(
        asio::co_spawn(
            executor,
            [&raw_sender]() -> asio::awaitable<void> {
              co_await raw_sender->Start();
            },
            asio::deferred),
        asio::co_spawn(
            executor,
            [&framed_receiver]() -> asio::awaitable<void> {
              co_await framed_receiver->Start();
            },
            asio::deferred))
        .async_wait(asio::experimental::wait_for_all(), asio::use_awaitable);

    SECTION("Back-to-back complete messages in small chunks") {
      std::string msg1 =
          R"({"jsonrpc":"2.0","method":"test1","params":{"data":"aaa"}})";
      std::string msg2 =
          R"({"jsonrpc":"2.0","method":"test2","params":{"data":"bbb"}})";

      std::string framed1 = FrameMessage(msg1);
      std::string framed2 = FrameMessage(msg2);

      // Send both messages in small chunks
      asio::co_spawn(
          executor,
          [&raw_sender, framed1, framed2]() -> asio::awaitable<void> {
            const size_t chunk_size = 5;
            // Send first message in chunks
            for (size_t i = 0; i < framed1.length(); i += chunk_size) {
              co_await raw_sender->SendMessage(framed1.substr(i, chunk_size));
            }
            // Immediately start sending second message in chunks
            for (size_t i = 0; i < framed2.length(); i += chunk_size) {
              co_await raw_sender->SendMessage(framed2.substr(i, chunk_size));
            }
          },
          asio::detached);

      // Receive both messages
      auto received1 = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received1.has_value());
      REQUIRE(received1.value() == msg1);

      auto received2 = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received2.has_value());
      REQUIRE(received2.value() == msg2);
    }

    SECTION("Overlapping messages with proper header-content order") {
      std::string msg1 =
          R"({"jsonrpc":"2.0","method":"test1","params":{"data":"aaa"}})";
      std::string msg2 =
          R"({"jsonrpc":"2.0","method":"test2","params":{"data":"bbb"}})";

      std::string framed1 = FrameMessage(msg1);
      std::string framed2 = FrameMessage(msg2);

      // Split each message at header/content boundary
      size_t header_end1 = framed1.find("\r\n\r\n") + 4;
      size_t header_end2 = framed2.find("\r\n\r\n") + 4;

      std::string msg1_header = framed1.substr(0, header_end1);
      std::string msg1_content = framed1.substr(header_end1);
      std::string msg2_header = framed2.substr(0, header_end2);
      std::string msg2_content = framed2.substr(header_end2);

      // Send messages with slight overlap but maintaining header-content order
      asio::co_spawn(
          executor,
          [&raw_sender, msg1_header, msg1_content, msg2_header,
           msg2_content]() -> asio::awaitable<void> {
            // Send first message header
            co_await raw_sender->SendMessage(msg1_header);
            // Send first message content
            co_await raw_sender->SendMessage(msg1_content);
            // Quickly follow with second message
            co_await raw_sender->SendMessage(msg2_header);
            co_await raw_sender->SendMessage(msg2_content);
          },
          asio::detached);

      // Receive both messages
      auto received1 = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received1.has_value());
      REQUIRE(received1.value() == msg1);

      auto received2 = co_await framed_receiver->ReceiveMessage();
      REQUIRE(received2.has_value());
      REQUIRE(received2.value() == msg2);
    }

    co_await raw_sender->Close();
    co_await framed_receiver->Close();
  });
}

TEST_CASE(
    "FramedPipeTransport handles invalid Content-Length",
    "[FramedPipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    const std::string socket_path = "/tmp/test_framed_transport_errors";
    auto raw_sender =
        std::make_unique<PipeTransport>(executor, socket_path, true);
    auto framed_receiver =
        std::make_unique<FramedPipeTransport>(executor, socket_path, false);

    // Start both in parallel
    co_await asio::experimental::make_parallel_group(
        asio::co_spawn(
            executor,
            [&raw_sender]() -> asio::awaitable<void> {
              co_await raw_sender->Start();
            },
            asio::deferred),
        asio::co_spawn(
            executor,
            [&framed_receiver]() -> asio::awaitable<void> {
              co_await framed_receiver->Start();
            },
            asio::deferred))
        .async_wait(asio::experimental::wait_for_all(), asio::use_awaitable);

    // Send a message with non-numeric Content-Length
    std::string invalid_header =
        "Content-Length: abc\r\n\r\n{\"method\":\"test\"}";

    asio::co_spawn(
        executor,
        [&raw_sender, invalid_header]() -> asio::awaitable<void> {
          co_await raw_sender->SendMessage(invalid_header);
        },
        asio::detached);

    // Should throw or return error when trying to parse invalid length
    auto result = co_await framed_receiver->ReceiveMessage();
    REQUIRE(!result.has_value());
    REQUIRE(
        result.error().message.find("Invalid Content-Length header") !=
        std::string::npos);

    co_await raw_sender->Close();
    co_await framed_receiver->Close();
  });
}

TEST_CASE(
    "FramedPipeTransport handles missing Content-Length header",
    "[FramedPipeTransport]") {
  RunTest([](asio::any_io_executor executor) -> asio::awaitable<void> {
    const std::string socket_path = "/tmp/test_framed_transport_missing_header";
    auto raw_sender =
        std::make_unique<PipeTransport>(executor, socket_path, true);
    auto framed_receiver =
        std::make_unique<FramedPipeTransport>(executor, socket_path, false);

    // Start both in parallel
    co_await asio::experimental::make_parallel_group(
        asio::co_spawn(
            executor,
            [&raw_sender]() -> asio::awaitable<void> {
              co_await raw_sender->Start();
            },
            asio::deferred),
        asio::co_spawn(
            executor,
            [&framed_receiver]() -> asio::awaitable<void> {
              co_await framed_receiver->Start();
            },
            asio::deferred))
        .async_wait(asio::experimental::wait_for_all(), asio::use_awaitable);

    // Send a message without Content-Length header
    std::string missing_header = "\r\n\r\n{\"method\":\"test\"}";

    asio::co_spawn(
        executor,
        [&raw_sender, missing_header]() -> asio::awaitable<void> {
          co_await raw_sender->SendMessage(missing_header);
        },
        asio::detached);

    // Should throw or return error when trying to parse message without header
    auto result = co_await framed_receiver->ReceiveMessage();
    REQUIRE(!result.has_value());
    REQUIRE(
        result.error().message.find("Missing Content-Length header") !=
        std::string::npos);

    co_await raw_sender->Close();
    co_await framed_receiver->Close();
  });
}
