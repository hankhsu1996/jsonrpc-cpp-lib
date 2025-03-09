import * as path from "path";
import { ExtensionContext, window, OutputChannel } from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";
import * as vscode from "vscode";

/**
 * LSP Client Example for VS Code Extension
 *
 * This is a simple demonstration of a Language Server Protocol (LSP) client
 * implementation for VS Code. It connects to our C++ LSP server using pipe
 * transport and demonstrates basic language server capabilities.
 */

let client: LanguageClient;
let outputChannel: OutputChannel;

export function activate(context: ExtensionContext) {
  outputChannel = window.createOutputChannel("Example Language Server");

  const serverPath = context.asAbsolutePath(
    path.join("..", "..", "..", "bazel-bin", "examples", "pipe_lsp_server")
  );

  // Add test command
  let testCommand = vscode.commands.registerCommand(
    "lsp-example.testConnection",
    async () => {
      try {
        outputChannel.appendLine("Testing LSP connection...");

        // Get server capabilities
        const capabilities = client.initializeResult?.capabilities;
        outputChannel.appendLine("\nServer capabilities:");
        outputChannel.appendLine(JSON.stringify(capabilities, null, 2));

        // Test completion request
        const position = { line: 0, character: 0 };
        const textDocument = { uri: "test:///file.txt" };
        const result = await client.sendRequest("textDocument/completion", {
          textDocument,
          position,
          context: { triggerKind: 1 }, // Invoked manually
        });

        outputChannel.appendLine("\nCompletion request result:");
        outputChannel.appendLine(JSON.stringify(result, null, 2));
      } catch (err) {
        outputChannel.appendLine("Error testing server connection: " + err);
      }
    }
  );
  context.subscriptions.push(testCommand);

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
    middleware: {
      handleDiagnostics: (uri, diagnostics, next) => {
        outputChannel.appendLine(`Received diagnostics for ${uri}:`);
        diagnostics.forEach((d) => outputChannel.appendLine(`  ${d.message}`));
        return next(uri, diagnostics);
      },
    },
  };

  client = new LanguageClient(
    "languageServerExample",
    "Language Server Example",
    serverOptions,
    clientOptions
  );

  client.onDidChangeState((e) => {
    outputChannel.appendLine(
      `Client state changed: ${e.oldState} -> ${e.newState}`
    );
  });

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
