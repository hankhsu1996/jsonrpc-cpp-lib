#pragma once

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <jsonrpc/endpoint/endpoint.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

using Json = nlohmann::json;

auto ParsePipeArguments(const std::vector<std::string>& args) -> std::string {
  const std::string pipe_prefix = "--pipe=";
  if (args.size() < 2 || args[1].rfind(pipe_prefix, 0) != 0) {
    throw std::invalid_argument("Usage: <executable> --pipe=<pipe name>");
  }
  return args[1].substr(pipe_prefix.length());
}

void RegisterLSPHandlers(jsonrpc::endpoint::RpcEndpoint& server) {
  server.RegisterMethodCall(
      "initialize", [](const std::optional<Json>&) -> asio::awaitable<Json> {
        spdlog::info("LSP Server initialized");
        Json response = {
            {"capabilities",
             {{"positionEncoding", "utf-16"},
              {"textDocumentSync",
               {{"openClose", true},
                {"change", 1},
                {"save", {{"includeText", false}}}}},
              {"completionProvider",
               {{"resolveProvider", false}, {"triggerCharacters", {" "}}}}}},
            {"serverInfo",
             {{"name", "LSP Example Server"}, {"version", "1.0"}}}};
        co_return response;
      });

  server.RegisterNotification(
      "initialized", [](const std::optional<Json>&) -> asio::awaitable<void> {
        spdlog::info("Client initialized");
        co_return;
      });

  server.RegisterMethodCall(
      "textDocument/completion",
      [](const std::optional<Json>& params) -> asio::awaitable<Json> {
        Json response;
        if (params && params->contains("textDocument") &&
            params->contains("position")) {
          response = Json::array(
              {{{"label", "world"}, {"kind", 1}, {"insertText", "world"}}});
        } else {
          response = Json::array();
        }
        co_return response;
      });

  server.RegisterMethodCall(
      "shutdown", [](const std::optional<Json>&) -> asio::awaitable<Json> {
        spdlog::info("Server shutting down");
        co_return Json::object();
      });

  server.RegisterNotification(
      "exit", [&server](const std::optional<Json>&) -> asio::awaitable<void> {
        spdlog::info("Server exiting");
        co_await server.Shutdown();
      });
}

void SetupLogger() {
  auto cout_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(std::cout);
  auto logger = std::make_shared<spdlog::logger>("server_logger", cout_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);
}
