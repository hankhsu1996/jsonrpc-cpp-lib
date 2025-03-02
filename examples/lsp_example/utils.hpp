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
  server.RegisterMethodCall("initialize", [](const std::optional<Json>&) {
    spdlog::info("Received initialize request.");
    Json response;
    response["result"]["capabilities"] = {
        {"textDocumentSync", 1},
        {"completionProvider",
         {{"resolveProvider", false}, {"triggerCharacters", {" "}}}}};
    return response;
  });

  server.RegisterNotification("initialized", [](const std::optional<Json>&) {
    spdlog::info("Client initialized.");
  });

  server.RegisterMethodCall(
      "textDocument/completion", [](const std::optional<Json>& params) {
        spdlog::info("Received completion request.");
        Json response;
        if (params && params->contains("textDocument") &&
            params->contains("position")) {
          response["result"]["items"] = Json::array(
              {{{"label", "world"}, {"kind", 1}, {"insertText", "world"}}});
        } else {
          response["result"] = Json::array();
        }
        return response;
      });
}

void SetupLogger() {
  auto cout_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(std::cout);
  auto logger = std::make_shared<spdlog::logger>("server_logger", cout_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);
}
