#pragma once

#include <type_traits>

#include <asio.hpp>
#include <nlohmann/json.hpp>

namespace jsonrpc::endpoint {

/**
 * @brief Handles scheduling of async JSON-RPC tasks.
 */
class TaskExecutor {
 public:
  explicit TaskExecutor(std::size_t num_threads) : thread_pool_(num_threads) {
  }

  // Destructor - thread_pool automatically joins on destruction
  ~TaskExecutor() = default;

  // Rule of five - delete copy/move operations
  TaskExecutor(const TaskExecutor&) = delete;
  auto operator=(const TaskExecutor&) -> TaskExecutor& = delete;
  TaskExecutor(TaskExecutor&&) = delete;
  auto operator=(TaskExecutor&&) -> TaskExecutor& = delete;

  /**
   * @brief Execute a function in the thread pool
   *
   * @param func The function to execute
   * @return An awaitable that resolves to the result of the function
   */
  template <typename Func>
  auto Execute(Func&& func)
      -> asio::awaitable<typename std::invoke_result_t<Func>::value_type> {
    co_return co_await asio::co_spawn(
        thread_pool_, std::forward<Func>(func), asio::use_awaitable);
  }

  /**
   * @brief Execute a function in the thread pool without waiting for it to
   * complete
   *
   * @param func The function to execute
   */
  template <typename Func>
  void ExecuteDetached(Func&& func) {
    asio::co_spawn(thread_pool_, std::forward<Func>(func), asio::detached);
  }

 private:
  asio::thread_pool thread_pool_;
};

}  // namespace jsonrpc::endpoint
