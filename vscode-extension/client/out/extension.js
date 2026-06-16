"use strict";
/* --------------------------------------------------------------------------------------------
 * ALang Language Client
 * VSCode extension that activates the ALang language server
 * ------------------------------------------------------------------------------------------ */
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
const path = __importStar(require("path"));
const fs = __importStar(require("fs"));
const vscode_1 = require("vscode");
const node_1 = require("vscode-languageclient/node");
let client;
function isUsableNativeServer(serverPath) {
    try {
        if (!fs.existsSync(serverPath))
            return false;
        // macOS: avoid spawning an x86_64 binary from an arm64 VS Code (or vice versa)
        if (process.platform === 'darwin') {
            const buf = fs.readFileSync(serverPath);
            if (buf.length < 8)
                return false;
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
    }
    catch {
        return false;
    }
}
async function activate(context) {
    // window.showInformationMessage('ALang Extension Activating...');
    const out = vscode_1.window.createOutputChannel('ALang Language Support');
    function log(msg) {
        const now = new Date();
        const timeStr = now.toISOString().replace('T', ' ').replace('Z', '');
        out.appendLine(`[${timeStr}] ${msg}`);
    }
    log('activate()');
    log(`platform=${process.platform} arch=${process.arch}`);
    log(`extensionPath=${context.extensionPath}`);
    out.show(true);
    // Prefer a native C++ LSP server (stdio) when available.
    const cfg = vscode_1.workspace.getConfiguration('alangLanguageServer');
    const configuredPath = cfg.get('serverPath');
    const bundledServer = context.asAbsolutePath(path.join('bin', process.platform === 'win32' ? 'alang-lsp.exe' : 'alang-lsp'));
    log(`configured serverPath=${configuredPath ?? ''}`);
    log(`bundled server path=${bundledServer}`);
    const serverCommand = (configuredPath && configuredPath.trim().length > 0)
        ? configuredPath
        : (isUsableNativeServer(bundledServer) ? bundledServer : undefined);
    log(`selected server=${serverCommand ?? '(node fallback)'} `);
    const serverOptions = serverCommand
        ? {
            run: { command: serverCommand, transport: node_1.TransportKind.stdio },
            debug: { command: serverCommand, transport: node_1.TransportKind.stdio }
        }
        : (() => {
            // Fallback to the Node.js implementation if the native binary isn't present.
            const serverModule = context.asAbsolutePath(path.join('server', 'out', 'server.js'));
            const debugOptions = { execArgv: ['--nolazy', '--inspect=6009'] };
            return {
                run: { module: serverModule, transport: node_1.TransportKind.ipc },
                debug: {
                    module: serverModule,
                    transport: node_1.TransportKind.ipc,
                    options: debugOptions
                }
            };
        })();
    // Options to control the language client
    const clientOptions = {
        // Register the server for ALang documents
        documentSelector: [
            { scheme: 'file', language: 'alang' },
            { scheme: 'file', pattern: '**/*.alang' }
        ],
        errorHandler: {
            error: (error) => {
                out.appendLine(`[client] connection error: ${String(error)}`);
                return { action: node_1.ErrorAction.Continue };
            },
            closed: () => {
                out.appendLine('[client] connection closed');
                return { action: node_1.CloseAction.DoNotRestart };
            }
        },
        synchronize: {
            // Notify the server about file changes to '.alang files contained in the workspace
            fileEvents: vscode_1.workspace.createFileSystemWatcher('**/*.alang')
        }
    };
    // Create the language client and start the client.
    client = new node_1.LanguageClient('alangLanguageServer', 'ALang Language Server', serverOptions, clientOptions);
    client.onDidChangeState((e) => {
        out.appendLine(`[client] state=${e.newState}`);
    });
    // Start the client. This will also launch the server
    try {
        await client.start();
        out.appendLine('[client] client started successfully');
    }
    catch (e) {
        out.appendLine(`[client] client start failed: ${e}`);
        // window.showErrorMessage(`ALang LSP failed to start: ${e}`);
    }
}
function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
//# sourceMappingURL=extension.js.map