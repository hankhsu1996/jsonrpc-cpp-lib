#pragma once

#include <atomic>

#include "jsonrpc/endpoint/types.hpp"

namespace jsonrpc::endpoint {

/**
 * @brief Interface for generating request IDs.
 *
 * This interface allows different strategies for generating request IDs
 * to be plugged into the RPC endpoint.
 */
class IdGenerator {
 public:
  IdGenerator() = default;
  IdGenerator(const IdGenerator &) = default;
  IdGenerator(IdGenerator &&) = delete;
  auto operator=(const IdGenerator &) -> IdGenerator & = default;
  auto operator=(IdGenerator &&) -> IdGenerator & = delete;
  virtual ~IdGenerator() = default;

  /**
   * @brief Generate the next request ID.
   *
   * @return A unique request ID.
   */
  virtual auto NextId() -> RequestId = 0;
};

/**
 * @brief Generates incrementing numeric IDs starting from 0.
 */
class IncrementalIdGenerator : public IdGenerator {
 public:
  auto NextId() -> RequestId override {
    return counter_++;
  }

 private:
  std::atomic<int64_t> counter_{0};
};

}  // namespace jsonrpc::endpoint
