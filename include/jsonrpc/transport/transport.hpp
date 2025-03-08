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
   * @brief Constructs a Transport with the given io_context.
   *
   * @param io_context The io_context to use for asynchronous operations.
   */
  explicit Transport(asio::io_context &io_context)
      : io_context_(io_context), strand_(asio::make_strand(io_context)) {
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
  virtual auto SendMessage(const std::string &message)
      -> asio::awaitable<void> = 0;

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

  /// @brief Gets the executor from the io_context.
  [[nodiscard]] auto GetExecutor() const -> asio::any_io_executor {
    return io_context_.get_executor();
  }

  /**
   * @brief Get the IO context directly.
   * @return Reference to the io_context
   */
  [[nodiscard]] auto GetIoContext() -> asio::io_context & {
    return io_context_;
  }

  /**
   * @brief Get the strand used for synchronization.
   * @return Reference to the strand
   */
  [[nodiscard]] auto GetStrand()
      -> asio::strand<asio::io_context::executor_type> & {
    return strand_;
  }

 private:
  asio::io_context &io_context_;
  asio::strand<asio::io_context::executor_type> strand_;
};

}  // namespace jsonrpc::transport
