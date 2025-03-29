#pragma once

#include <asio/awaitable.hpp>
#include <nlohmann/json.hpp>

// Simple parameter structs
struct AddParams {
  double a;
  double b;
};

struct DivideParams {
  double a;
  double b;
};

// Simple result struct
struct Result {
  double value;
};

// Calculator implementation with typed methods
class TypedCalculator {
 public:
  // Typed method for addition
  static auto Add(AddParams params) -> asio::awaitable<Result> {
    co_return Result{params.a + params.b};
  }

  // Typed method for division
  static auto Divide(DivideParams params) -> asio::awaitable<Result> {
    if (params.b == 0) {
      throw std::runtime_error("Division by zero");
    }
    co_return Result{params.a / params.b};
  }
};

// Free functions for JSON serialization
void to_json(nlohmann::json& j, const AddParams& params) {
  j = nlohmann::json{{"a", params.a}, {"b", params.b}};
}

void from_json(const nlohmann::json& j, AddParams& params) {
  j.at("a").get_to(params.a);
  j.at("b").get_to(params.b);
}

void to_json(nlohmann::json& j, const DivideParams& params) {
  j = nlohmann::json{{"a", params.a}, {"b", params.b}};
}

void from_json(const nlohmann::json& j, DivideParams& params) {
  j.at("a").get_to(params.a);
  j.at("b").get_to(params.b);
}

void to_json(nlohmann::json& j, const Result& result) {
  j = nlohmann::json{{"value", result.value}};
}

void from_json(const nlohmann::json& j, Result& result) {
  j.at("value").get_to(result.value);
}
