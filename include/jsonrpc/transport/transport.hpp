#pragma once

#include <expected>
#include <string>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "jsonrpc/error/error.hpp"

namespace jsonrpc::detail {

inline auto GetTransportLogger() -> std::shared_ptr<spdlog::logger> {
  auto logger = spdlog::get("transport");
  if (!logger) {
    logger = spdlog::default_logger();
  }
  return logger;
}

}  // namespace jsonrpc::detail

namespace jsonrpc::transport {

class Transport {
 public:
  explicit Transport(asio::any_io_executor executor)
      : logger_(jsonrpc::detail::GetTransportLogger()),
        executor_(std::move(executor)),
        strand_(asio::make_strand(executor_)) {
  }

  Transport(const Transport &) = delete;
  Transport(Transport &&) = delete;

  auto operator=(const Transport &) -> Transport & = delete;
  auto operator=(Transport &&) -> Transport & = delete;

  virtual ~Transport() = default;

  virtual auto Start()
      -> asio::awaitable<std::expected<void, error::RpcError>> = 0;

  virtual auto Close()
      -> asio::awaitable<std::expected<void, error::RpcError>> = 0;

  virtual auto CloseNow() -> void = 0;

  virtual auto SendMessage(std::string message)
      -> asio::awaitable<std::expected<void, error::RpcError>> = 0;

  virtual auto ReceiveMessage()
      -> asio::awaitable<std::expected<std::string, error::RpcError>> = 0;

  [[nodiscard]] auto GetExecutor() const -> asio::any_io_executor {
    return executor_;
  }

  [[nodiscard]] auto GetStrand() -> asio::strand<asio::any_io_executor> & {
    return strand_;
  }

 protected:
  auto Logger() -> spdlog::logger & {
    return *logger_;
  }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;
};

}  // namespace jsonrpc::transport
