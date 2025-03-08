#pragma once

#include <asio/awaitable.hpp>
#include <nlohmann/json.hpp>

static constexpr int kDivideByZeroErrorCode = -32000;

class Calculator {
 public:
  static auto Add(const std::optional<nlohmann::json>& params)
      -> asio::awaitable<nlohmann::json> {
    const auto& p = params.value_or(nlohmann::json::object());
    double a_double = p["a"];
    double b_double = p["b"];

    co_return nlohmann::json{{"result", a_double + b_double}};
  }

  static auto Divide(const std::optional<nlohmann::json>& params)
      -> asio::awaitable<nlohmann::json> {
    const auto& p = params.value_or(nlohmann::json::object());
    double a_double = p["a"];
    double b_double = p["b"];

    if (b_double == 0) {
      co_return nlohmann::json{
          {"error",
           {{"code", kDivideByZeroErrorCode},
            {"message", "Division by zero"}}}};
    }

    co_return nlohmann::json{{"result", a_double / b_double}};
  }
};
