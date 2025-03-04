#pragma once

#include <asio.hpp>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <system_error>

namespace jsonrpc::transport {

/**
 * @brief Abstract base class for transport implementations.
 *
 * This class defines the interface for transport layers used by the JSON-RPC
 * library. A transport is responsible for sending and receiving messages
 * between endpoints.
 */
class Transport {
 public:
  // Common type definitions for handlers
  using SendHandler = std::function<void(const asio::error_code&)>;
  using ReceiveHandler =
      std::function<void(const asio::error_code&, std::string)>;
  using CloseHandler = std::function<void(const asio::error_code&)>;

  /**
   * @brief Constructs a Transport with optional external io_context.
   *
   * @param external_io_context Optional external io_context to use. If nullptr,
   * creates an internal io_context.
   */
  explicit Transport(asio::io_context* external_io_context = nullptr);

  virtual ~Transport() = default;
  Transport(const Transport&) = delete;
  auto operator=(const Transport&) -> Transport& = delete;
  Transport(Transport&&) = delete;
  auto operator=(Transport&&) -> Transport& = delete;

  /**
   * @brief Gets the underlying io_context used by this transport.
   * @return A pointer to the io_context.
   */
  auto GetIoContext() -> asio::io_context* {
    return io_context_;
  }

  /**
   * @brief Sends a message through the transport.
   *
   * @param message The message to send.
   * @return A future that completes when the message is sent.
   * @throws std::runtime_error if an error occurs during sending.
   *
   * @deprecated Use AsyncSendMessage with a completion handler instead
   */
  virtual auto SendMessage(const std::string& message) -> std::future<void> = 0;

  /**
   * @brief Receives a message from the transport.
   *
   * This method will wait until a message is available or the transport
   * is closed.
   *
   * @return A future that resolves to the received message.
   * @throws std::runtime_error if the transport is closed or an error occurs.
   *
   * @deprecated Use AsyncReceiveMessage with a completion handler instead
   */
  virtual auto ReceiveMessage() -> std::future<std::string> = 0;

  /**
   * @brief Closes the transport.
   *
   * After calling this method, no more messages can be sent or received.
   *
   * @return A future that completes when the transport is closed.
   *
   * @deprecated Use AsyncClose with a completion handler instead
   */
  virtual auto Close() -> std::future<void> = 0;

  /**
   * @brief Asynchronously sends a message through the transport.
   *
   * This is the ASIO-native version of SendMessage that directly accepts a
   * completion handler rather than returning a future.
   *
   * @param message The message to send.
   * @param handler The completion handler to invoke when the operation
   * completes. The handler should have the signature void(const
   * asio::error_code&).
   * @tparam CompletionHandler The type of the completion handler.
   */
  template <typename CompletionHandler>
  void AsyncSendMessage(
      const std::string& message, CompletionHandler&& handler) {
    // Wrap the handler in a std::function for type erasure
    DoAsyncSendMessage(
        message, [handler = std::forward<CompletionHandler>(handler)](
                     const asio::error_code& ec) mutable { handler(ec); });
  }

  /**
   * @brief Asynchronously receives a message from the transport.
   *
   * This is the ASIO-native version of ReceiveMessage that directly accepts a
   * completion handler rather than returning a future.
   *
   * @param handler The completion handler to invoke when the operation
   * completes. The handler should have the signature void(const
   * asio::error_code&, std::string).
   * @tparam CompletionHandler The type of the completion handler.
   */
  template <typename CompletionHandler>
  void AsyncReceiveMessage(CompletionHandler&& handler) {
    // Wrap the handler in a std::function for type erasure
    DoAsyncReceiveMessage(
        [handler = std::forward<CompletionHandler>(handler)](
            const asio::error_code& ec, std::string msg) mutable {
          handler(ec, std::move(msg));
        });
  }

  /**
   * @brief Asynchronously closes the transport.
   *
   * This is the ASIO-native version of Close that directly accepts a
   * completion handler rather than returning a future.
   *
   * @param handler The completion handler to invoke when the operation
   * completes. The handler should have the signature void(const
   * asio::error_code&).
   * @tparam CompletionHandler The type of the completion handler.
   */
  template <typename CompletionHandler>
  void AsyncClose(CompletionHandler&& handler) {
    // Wrap the handler in a std::function for type erasure
    DoAsyncClose([handler = std::forward<CompletionHandler>(handler)](
                     const asio::error_code& ec) mutable { handler(ec); });
  }

 protected:
  /**
   * @brief Ensures the io_context is running in a background thread if needed.
   *
   * This helper method starts the io_context in a background thread if this
   * transport owns its io_context.
   */
  void EnsureIoContextRunning();

  /**
   * @brief Implementation of the async send message operation.
   *
   * Derived classes should override this to provide an efficient
   * implementation. The default implementation uses the future-based method for
   * backward compatibility.
   *
   * @param message The message to send.
   * @param handler The completion handler to invoke when the operation
   * completes.
   */
  virtual void DoAsyncSendMessage(
      const std::string& message, SendHandler handler) {
    // Default implementation uses the future-based method for backward
    // compatibility
    asio::post(
        GetIoContext()->get_executor(),
        [this, message, handler = std::move(handler)]() mutable {
          try {
            SendMessage(message).get();
            handler(asio::error_code());  // Success, no error
          } catch (const std::exception& e) {
            // Create error_code with a generic category
            handler(asio::error_code(
                asio::error::operation_aborted, asio::system_category()));
          }
        });
  }

  /**
   * @brief Implementation of the async receive message operation.
   *
   * Derived classes should override this to provide an efficient
   * implementation. The default implementation uses the future-based method for
   * backward compatibility.
   *
   * @param handler The completion handler to invoke when the operation
   * completes.
   */
  virtual void DoAsyncReceiveMessage(ReceiveHandler handler) {
    // Default implementation uses the future-based method for backward
    // compatibility
    asio::post(
        GetIoContext()->get_executor(),
        [this, handler = std::move(handler)]() mutable {
          try {
            std::string message = ReceiveMessage().get();
            handler(
                asio::error_code(), std::move(message));  // Success, no error
          } catch (const std::exception& e) {
            // Create error_code with a generic category
            handler(
                asio::error_code(
                    asio::error::operation_aborted, asio::system_category()),
                std::string{});
          }
        });
  }

  /**
   * @brief Implementation of the async close operation.
   *
   * Derived classes should override this to provide an efficient
   * implementation. The default implementation uses the future-based method for
   * backward compatibility.
   *
   * @param handler The completion handler to invoke when the operation
   * completes.
   */
  virtual void DoAsyncClose(CloseHandler handler) {
    // Default implementation uses the future-based method for backward
    // compatibility
    asio::post(
        GetIoContext()->get_executor(),
        [this, handler = std::move(handler)]() mutable {
          try {
            Close().get();
            handler(asio::error_code());  // Success, no error
          } catch (const std::exception& e) {
            // Create error_code with a generic category
            handler(asio::error_code(
                asio::error::operation_aborted, asio::system_category()));
          }
        });
  }

  /// Whether this transport owns the io_context
  bool owns_io_context_;

  /// The io_context used for asynchronous operations
  asio::io_context* io_context_;

  /// Pointer to the owned io_context (if any)
  std::unique_ptr<asio::io_context> owned_io_context_;

  /// Work guard to keep the io_context running
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>
      work_guard_;
};

}  // namespace jsonrpc::transport
