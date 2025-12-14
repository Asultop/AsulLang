/* --------------------------------------------------------------------------------------------
 * ALang Language Client
 * VSCode extension that activates the ALang language server
 * ------------------------------------------------------------------------------------------ */

import * as path from 'path';
import * as fs from 'fs';
import { workspace, ExtensionContext } from 'vscode';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
	TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: ExtensionContext) {
	// Prefer a native C++ LSP server (stdio) when available.
	const cfg = workspace.getConfiguration('alangLanguageServer');
	const configuredPath = cfg.get<string>('serverPath');
	const bundledServer = context.asAbsolutePath(
		path.join('bin', process.platform === 'win32' ? 'alang-lsp.exe' : 'alang-lsp')
	);
	const serverCommand = (configuredPath && configuredPath.trim().length > 0)
		? configuredPath
		: (fs.existsSync(bundledServer) ? bundledServer : undefined);

	const serverOptions: ServerOptions = serverCommand
		? {
			run: { command: serverCommand, transport: TransportKind.stdio },
			debug: { command: serverCommand, transport: TransportKind.stdio }
		}
		: (() => {
			// Fallback to the Node.js implementation if the native binary isn't present.
			const serverModule = context.asAbsolutePath(
				path.join('server', 'out', 'server.js')
			);
			const debugOptions = { execArgv: ['--nolazy', '--inspect=6009'] };
			return {
				run: { module: serverModule, transport: TransportKind.ipc },
				debug: {
					module: serverModule,
					transport: TransportKind.ipc,
					options: debugOptions
				}
			};
		})();

	// Options to control the language client
	const clientOptions: LanguageClientOptions = {
		// Register the server for ALang documents
		documentSelector: [{ scheme: 'file', language: 'alang' }],
		synchronize: {
			// Notify the server about file changes to '.alang files contained in the workspace
			fileEvents: workspace.createFileSystemWatcher('**/*.alang')
		}
	};

	// Create the language client and start the client.
	client = new LanguageClient(
		'alangLanguageServer',
		'ALang Language Server',
		serverOptions,
		clientOptions
	);

	// Start the client. This will also launch the server
	client.start();
}

export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}
