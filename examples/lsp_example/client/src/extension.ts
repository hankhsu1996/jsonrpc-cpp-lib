import * as path from "path";
import { ExtensionContext, window, OutputChannel } from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient;
let outputChannel: OutputChannel;

export function activate(context: ExtensionContext) {
  outputChannel = window.createOutputChannel("Example Language Server");

  const serverPath = context.asAbsolutePath(
    path.join("..", "..", "..", "bazel-bin", "examples", "pipe_lsp_server")
  );

  const serverOptions: ServerOptions = {
    run: {
      command: serverPath,
      transport: TransportKind.pipe,
    },
    debug: {
      command: serverPath,
      transport: TransportKind.pipe,
    },
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "plaintext" }],
    outputChannel,
  };

  client = new LanguageClient(
    "languageServerExample",
    "Language Server Example",
    serverOptions,
    clientOptions
  );

  client.start();

  outputChannel.appendLine("LSP client started.");
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) {
    return undefined;
  }
  outputChannel.appendLine("LSP client stopped.");
  return client.stop();
}
