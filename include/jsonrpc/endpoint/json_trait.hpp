#pragma once

#include <concepts>

#include "nlohmann/json.hpp"

namespace jsonrpc::endpoint {

template <typename T>
concept FromJson = requires(nlohmann::json j) {
  { j.get<T>() } -> std::same_as<T>;
};

template <typename T>
concept ToJson = requires(T value) {
  { nlohmann::json(value) } -> std::same_as<nlohmann::json>;
};

template <typename T>
concept JsonConvertible = FromJson<T> && ToJson<T>;

template <typename T>
concept NotJsonLike =
    !std::is_same_v<std::decay_t<T>, nlohmann::json> &&
    !std::is_same_v<std::decay_t<T>, std::optional<nlohmann::json>>;

}  // namespace jsonrpc::endpoint
