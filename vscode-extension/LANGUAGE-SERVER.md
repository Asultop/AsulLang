# ALang Language Server

This directory contains the Language Server Protocol (LSP) implementation for ALang, providing advanced IDE features.

## Native C++ language server (alang-lsp)

This repo also includes a native C++ LSP server executable named `alang-lsp`.

- Build target: `alang-lsp` (CMake)
- Default output (non-Windows): `vscode-extension/bin/alang-lsp`
- Transport: stdio (JSON-RPC with `Content-Length` framing)

The VS Code client will:

1. Use `alangLanguageServer.serverPath` if configured
2. Else use the bundled `vscode-extension/bin/alang-lsp` if present
3. Else fall back to the Node.js server in `server/out/server.js`

### Build (macOS/Linux)

From the repo root:

```bash
cmake -S . -B build
cmake --build build --target alang-lsp -j
```

Note: the produced binary is architecture-specific. If you run VS Code as Apple Silicon (`arm64`), make sure you build an `arm64` binary (or a universal binary) accordingly.

## Features

### Syntax Checking (Diagnostics)
- Real-time syntax validation
- Unclosed string detection
- Basic bracket matching
- Error highlighting with diagnostics

### Go to Definition
- Navigate to function definitions
- Navigate to class definitions
- Navigate to interface definitions
- Navigate to variable declarations

### Auto-completion
- Keyword completion
- Symbol completion (functions, classes, variables)
- Context-aware suggestions

## Architecture

The language server is split into two parts:

### Server (`server/`)
- Node.js language server implementation
- Processes ALang documents
- Provides diagnostics, definitions, and completions
- Communicates via LSP

### Client (`client/`)
- VSCode extension client
- Activates the language server
- Manages communication between VSCode and the server

## Building

```bash
# Install dependencies
npm run postinstall

# Compile TypeScript
npm run compile

# Or watch for changes
npm run watch
```

## Development

The language server uses:
- **vscode-languageserver**: LSP server library
- **vscode-languageclient**: LSP client library
- **TypeScript**: Type-safe development

## Current Capabilities

### Diagnostics
- Detects unclosed strings
- Reports syntax errors in real-time

### Symbol Resolution
- Tracks function definitions
- Tracks class definitions
- Tracks interface definitions
- Tracks variable declarations

### Navigation
- Go to definition for symbols
- Jump to symbol declarations

### Completion
- All ALang keywords
- Document symbols
- Context-aware suggestions

## Future Enhancements

Planned features:
- [ ] Semantic highlighting
- [ ] Code formatting
- [ ] Rename symbol
- [ ] Find all references
- [ ] Hover documentation
- [ ] Signature help
- [ ] Code actions (quick fixes)
- [ ] Full AST-based parsing
- [ ] Cross-file symbol resolution

## Integration with Parser

The language server will integrate with the ALang parser (`src/AsulParser.h`) for:
- Full syntax tree analysis
- Accurate error reporting
- Type inference
- Semantic analysis

## Configuration

Settings can be configured in VSCode:

```json
{
  "alangLanguageServer.maxNumberOfProblems": 100,
  "alangLanguageServer.trace.server": "off"
}
```
