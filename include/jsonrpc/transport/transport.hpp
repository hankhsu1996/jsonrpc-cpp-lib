#pragma once

#include <string>

#include <asio.hpp>

namespace jsonrpc::transport {

/**
 * @brief Abstract base class for all transport implementations.
 */
class Transport {
 public:
  /**
   * @brief Constructs a Transport with the given executor.
   *
   * @param executor The executor to use for asynchronous operations.
   */
  explicit Transport(asio::any_io_executor executor)
      : executor_(std::move(executor)), strand_(asio::make_strand(executor_)) {
  }

  Transport(const Transport &) = delete;
  Transport(Transport &&) = delete;

  auto operator=(const Transport &) -> Transport & = delete;
  auto operator=(Transport &&) -> Transport & = delete;

  virtual ~Transport() = default;

  /**
   * @brief Starts the transport.
   *
   * This initializes connections and prepares the transport for communication.
   * For server transports, this typically involves binding and listening.
   * For client transports, this involves connecting to the server.
   *
   * @return asio::awaitable<void>
   */
  virtual auto Start() -> asio::awaitable<void> = 0;

  /// @brief Sends a message over the transport.
  virtual auto SendMessage(std::string message) -> asio::awaitable<void> = 0;

  /// @brief Receives a message over the transport.
  virtual auto ReceiveMessage() -> asio::awaitable<std::string> = 0;

  /**
   * @brief Closes the transport asynchronously.
   *
   * This is the asynchronous version of close, which should be used during
   * normal operation.
   *
   * @return asio::awaitable<void>
   */
  virtual auto Close() -> asio::awaitable<void> = 0;

  /**
   * @brief Closes the transport synchronously.
   *
   * This is a synchronous version of Close() that's safe to use in destructors.
   * Implementations should ensure this method doesn't throw exceptions.
   */
  virtual void CloseNow() = 0;

  /// @brief Gets the executor for this transport.
  [[nodiscard]] auto GetExecutor() const -> asio::any_io_executor {
    return executor_;
  }

  /**
   * @brief Get the strand used for synchronization.
   * @return Reference to the strand
   */
  [[nodiscard]] auto GetStrand() -> asio::strand<asio::any_io_executor> & {
    return strand_;
  }

 private:
  asio::any_io_executor executor_;
  asio::strand<asio::any_io_executor> strand_;
};

}  // namespace jsonrpc::transport
