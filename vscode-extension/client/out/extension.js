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
function activate(context) {
    // Prefer a native C++ LSP server (stdio) when available.
    const cfg = vscode_1.workspace.getConfiguration('alangLanguageServer');
    const configuredPath = cfg.get('serverPath');
    const bundledServer = context.asAbsolutePath(path.join('bin', process.platform === 'win32' ? 'alang-lsp.exe' : 'alang-lsp'));
    const serverCommand = (configuredPath && configuredPath.trim().length > 0)
        ? configuredPath
        : (fs.existsSync(bundledServer) ? bundledServer : undefined);
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
        documentSelector: [{ scheme: 'file', language: 'alang' }],
        synchronize: {
            // Notify the server about file changes to '.alang files contained in the workspace
            fileEvents: vscode_1.workspace.createFileSystemWatcher('**/*.alang')
        }
    };
    // Create the language client and start the client.
    client = new node_1.LanguageClient('alangLanguageServer', 'ALang Language Server', serverOptions, clientOptions);
    // Start the client. This will also launch the server
    client.start();
}
function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
//# sourceMappingURL=extension.js.map