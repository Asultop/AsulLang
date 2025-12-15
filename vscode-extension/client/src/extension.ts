/* --------------------------------------------------------------------------------------------
 * ALang Language Client
 * VSCode extension that activates the ALang language server
 * ------------------------------------------------------------------------------------------ */

import * as path from 'path';
import * as fs from 'fs';
import { workspace, ExtensionContext, window } from 'vscode';

import {
	LanguageClient,
	LanguageClientOptions,
	ErrorAction,
	CloseAction,
	ServerOptions,
	TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

function isUsableNativeServer(serverPath: string): boolean {
	try {
		if (!fs.existsSync(serverPath)) return false;
		// macOS: avoid spawning an x86_64 binary from an arm64 VS Code (or vice versa)
		if (process.platform === 'darwin') {
			const buf = fs.readFileSync(serverPath);
			if (buf.length < 8) return false;
			const magicLE = buf.readUInt32LE(0);
			// 64-bit Mach-O magic (little-endian): 0xfeedfacf
			if (magicLE === 0xfeedfacf && buf.length >= 12) {
				const cputype = buf.readInt32LE(4);
				const CPU_TYPE_X86_64 = 0x01000007;
				const CPU_TYPE_ARM64 = 0x0100000c;
				// Allow running x86_64 binary on arm64 (Rosetta)
				// if (process.arch === 'arm64' && cputype !== CPU_TYPE_ARM64) return false;
				// if (process.arch === 'x64' && cputype !== CPU_TYPE_X86_64) return false;
			}
			// FAT/universal binaries or unknown formats: assume usable
			
			// Allow running x86_64 binary on arm64 (Rosetta) and vice versa if needed
			// if (process.arch === 'arm64' && cputype !== CPU_TYPE_ARM64) return false;
			// if (process.arch === 'x64' && cputype !== CPU_TYPE_X86_64) return false;
		}
		return true;
	} catch {
		return false;
	}
}

export async function activate(context: ExtensionContext) {
	// window.showInformationMessage('ALang Extension Activating...');
	const out = window.createOutputChannel('ALang Language Support');
	function log(msg: string) {
		const now = new Date();
		const timeStr = now.toISOString().replace('T', ' ').replace('Z', '');
		out.appendLine(`[${timeStr}] ${msg}`);
	}

	log('activate()');
	log(`platform=${process.platform} arch=${process.arch}`);
	log(`extensionPath=${context.extensionPath}`);
	out.show(true);

	// Prefer a native C++ LSP server (stdio) when available.
	const cfg = workspace.getConfiguration('alangLanguageServer');
	const configuredPath = cfg.get<string>('serverPath');
	const bundledServer = context.asAbsolutePath(
		path.join('bin', process.platform === 'win32' ? 'alang-lsp.exe' : 'alang-lsp')
	);
	log(`configured serverPath=${configuredPath ?? ''}`);
	log(`bundled server path=${bundledServer}`);
	const serverCommand = (configuredPath && configuredPath.trim().length > 0)
		? configuredPath
		: (isUsableNativeServer(bundledServer) ? bundledServer : undefined);
	log(`selected server=${serverCommand ?? '(node fallback)'} `);

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
		documentSelector: [
			{ scheme: 'file', language: 'alang' },
			{ scheme: 'file', pattern: '**/*.alang' }
		],
		errorHandler: {
			error: (error) => {
				out.appendLine(`[client] connection error: ${String(error)}`);
				return { action: ErrorAction.Continue };
			},
			closed: () => {
				out.appendLine('[client] connection closed');
				return { action: CloseAction.DoNotRestart };
			}
		},
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
	client.onDidChangeState((e) => {
		out.appendLine(`[client] state=${e.newState}`);
	});

	// Start the client. This will also launch the server
	try {
		await client.start();
		out.appendLine('[client] client started successfully');
	} catch (e) {
		out.appendLine(`[client] client start failed: ${e}`);
		// window.showErrorMessage(`ALang LSP failed to start: ${e}`);
	}
}

export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}
