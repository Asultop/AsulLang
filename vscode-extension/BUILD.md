# Building and Using the ALang Extension

## Prerequisites

- Node.js 14+ and npm
- Visual Studio Code 1.60+
- TypeScript 5.0+ (installed via npm)

## Building the Extension

### 1. Install Dependencies

```bash
cd vscode-extension

# Install root dependencies and client/server dependencies
npm run postinstall

# This will install dependencies in:
# - ./node_modules
# - ./client/node_modules
# - ./server/node_modules
```

### 2. Compile TypeScript

```bash
# Compile both client and server
npm run compile

# Or watch for changes during development
npm run watch
```

This compiles:
- `client/src/extension.ts` → `client/out/extension.js`
- `server/src/server.ts` → `server/out/server.js`

### 3. Install the Extension

#### Option A: Development Installation

```bash
# Create symbolic link to VSCode extensions directory
# Linux/macOS:
ln -s "$(pwd)" ~/.vscode/extensions/alang-language-support-0.2.0

# Windows (as Administrator):
mklink /D "%USERPROFILE%\.vscode\extensions\alang-language-support-0.2.0" "%CD%"
```

#### Option B: Package and Install

```bash
# Install vsce if not already installed
npm install -g vsce

# Package the extension
vsce package

# Install the .vsix file
code --install-extension alang-language-support-0.2.0.vsix
```

### 4. Reload VSCode

After installation, reload VSCode:
- Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on macOS)
- Type "Reload Window" and press Enter

## Using the Features

### Syntax Highlighting

Syntax highlighting works automatically for `.alang` files.

### Color Theme

1. Open Command Palette (`Ctrl+Shift+P`)
2. Type "Preferences: Color Theme"
3. Select "ALang Default Dark"

The theme provides special colors for ALang operators:
- `=~=` - Pink (interface matching)
- `<-`, `->`, `=>` - Red (arrows)
- `?.`, `??` - Gold (nullish)
- `...` - Blue (spread)
- `@` - Yellow (decorator)

### Syntax Checking

Open any `.alang` file and the language server will:
- Automatically check for syntax errors
- Display errors as red underlines
- Show error messages on hover

Example errors detected:
```alang
let str = "unclosed string    // Error: Unclosed double quote
```

### Go to Definition

1. Place cursor on a symbol (function, class, variable)
2. Right-click and select "Go to Definition"
   - Or press `F12`
   - Or `Ctrl+Click` (macOS: `Cmd+Click`)

Works for:
```alang
// Define a function
function calculateSum(a, b) {
    return a + b;
}

// Later in the code
let result = calculateSum(5, 10);  // F12 on 'calculateSum' jumps to definition
```

### Auto-completion

Start typing and suggestions will appear:

```alang
fu|  // Type 'fu' and get 'function' suggestion
cl|  // Type 'cl' and get 'class' suggestion
```

Trigger manually:
- `Ctrl+Space` (macOS: `Cmd+Space`)

### Diagnostics Panel

View all errors in the current file:
1. Open Problems panel: `Ctrl+Shift+M` (macOS: `Cmd+Shift+M`)
2. See list of all syntax errors
3. Click to jump to error location

## Debugging the Extension

### Debug the Extension Client

1. Open the extension folder in VSCode
2. Press `F5` to start debugging
3. A new VSCode window opens with the extension loaded
4. Set breakpoints in `client/src/extension.ts`

### Debug the Language Server

1. Add this to `.vscode/launch.json`:

```json
{
  "name": "Attach to Server",
  "type": "node",
  "request": "attach",
  "port": 6009,
  "restart": true,
  "outFiles": ["${workspaceFolder}/server/out/**/*.js"]
}
```

2. Start the extension in debug mode (`F5`)
3. Start "Attach to Server" debug configuration
4. Set breakpoints in `server/src/server.ts`

### View Language Server Logs

Enable trace logging:
1. Open Settings (`Ctrl+,`)
2. Search for "alangLanguageServer.trace.server"
3. Set to "verbose"
4. View logs in "Output" panel → "ALang Language Server"

## Configuration

### Extension Settings

Configure in VSCode settings (`Ctrl+,`):

```json
{
  // Maximum number of problems to report
  "alangLanguageServer.maxNumberOfProblems": 100,
  
  // Enable detailed logging
  "alangLanguageServer.trace.server": "verbose"
}
```

### Workspace Settings

Create `.vscode/settings.json` in your ALang project:

```json
{
  "files.associations": {
    "*.alang": "alang"
  },
  "editor.semanticHighlighting.enabled": true,
  "alangLanguageServer.maxNumberOfProblems": 50
}
```

## Troubleshooting

### Extension Not Activating

Check the Output panel:
1. View → Output (`Ctrl+Shift+U`)
2. Select "ALang Language Server" from dropdown
3. Check for error messages

### TypeScript Compilation Errors

```bash
# Clean and rebuild
rm -rf client/out server/out
npm run compile
```

### Language Server Not Working

1. Check that compiled files exist:
   - `client/out/extension.js`
   - `server/out/server.js`

2. Verify installation:
   ```bash
   ls ~/.vscode/extensions/alang-language-support-*
   ```

3. Check extension is activated:
   - Open Command Palette
   - Type "Developer: Show Running Extensions"
   - Find "alang-language-support"

### Node Modules Issues

```bash
# Remove all node_modules and reinstall
rm -rf node_modules client/node_modules server/node_modules
npm run postinstall
npm run compile
```

## Development Workflow

1. **Make changes** to TypeScript files
2. **Compile**: `npm run compile` (or use watch mode)
3. **Reload**: `Ctrl+Shift+P` → "Reload Window"
4. **Test**: Open a `.alang` file and verify features work

For continuous development:
```bash
# Terminal 1: Watch and compile
npm run watch

# Terminal 2: Run VSCode in extension development mode
code --extensionDevelopmentPath=/path/to/vscode-extension
```

## Testing

Create test ALang files in the `examples/` directory:

```alang
// test-syntax.alang

// Test go to definition
function testFunction() {
    return 42;
}

class TestClass {
    function method() {
        return "test";
    }
}

let x = testFunction();  // F12 should jump to definition
let obj = new TestClass();  // F12 should jump to class
```

Test all features:
- ✅ Syntax highlighting
- ✅ Color theme
- ✅ Error detection
- ✅ Go to definition
- ✅ Auto-completion

## Publishing

When ready to publish to VS Marketplace:

```bash
# Update version in package.json
# Update CHANGELOG.md

# Package
vsce package

# Publish (requires publisher account)
vsce publish
```

## Further Reading

- [Language Server Protocol](https://microsoft.github.io/language-server-protocol/)
- [VSCode Extension API](https://code.visualstudio.com/api)
- [TextMate Grammars](https://macromates.com/manual/en/language_grammars)
- [LANGUAGE-SERVER.md](./LANGUAGE-SERVER.md) - Detailed server documentation
