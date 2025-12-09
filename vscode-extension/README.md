# ALang Language Support for Visual Studio Code

Provides comprehensive language support for the ALang scripting language, including syntax highlighting, diagnostics, and navigation features.

## Features

### Syntax Highlighting
Full syntax highlighting for ALang files (.alang):
- Keywords (let, var, const, function, class, interface, etc.)
- Control flow statements (if, else, while, for, foreach, switch, etc.)
- Async/await keywords (async, await, go)
- Operators (arithmetic, logical, bitwise, special operators like =~=, ?., etc.)
- Literals (strings, numbers, booleans, null)
- Comments (line and block comments)
- String interpolation with template literals

### Color Theme
**NEW!** Default color theme with special highlighting for ALang-specific operators:
- `=~=` (interface match) - Bold pink
- `<-` `->` `=>` (arrows) - Bold red
- `?.` `??` (nullish) - Bold gold
- `...` (spread) - Bold blue
- `@` (decorator) - Bold yellow

Activate via: `Preferences: Color Theme` → `ALang Default Dark`

### Language Server Features
**NEW!** Advanced IDE features powered by Language Server Protocol:

#### Syntax Checking
- Real-time syntax validation
- Error diagnostics with inline highlighting
- Unclosed string detection
- Bracket mismatch warnings

#### Go to Definition
- Jump to function definitions
- Navigate to class declarations
- Find interface definitions
- Locate variable declarations

#### Auto-completion
- Keyword suggestions
- Symbol completion
- Context-aware recommendations

### Editor Features
- Bracket matching for (), [], {}
- Auto-closing pairs for brackets and quotes
- Comment toggling (Ctrl+/)
- Block comment toggling (Shift+Alt+A)
- Code folding support

## ALang Language Features

ALang is a lightweight, efficient scripting language interpreter with features including:

- Basic types: number, string, boolean, null, array, object
- Variables with block and function scopes (let/var/const)
- Functions and closures
- Object-oriented programming (classes, inheritance, interfaces)
- Async programming (async/await, Promises)
- Metaprogramming (eval, quote)
- Module system (import/from)
- Rich standard library

## Special Operators

The syntax highlighter recognizes ALang's special operators:

- `=~=` - Interface/class descriptor matching operator
- `?.` - Optional chaining operator
- `??` - Nullish coalescing operator
- `<-` - Inheritance operator
- `->` - Arrow operator
- `=>` - Lambda arrow operator
- `...` - Spread/rest operator
- `@` - Decorator operator

## Installation

### From VSIX file

1. Download the .vsix file
2. Open VS Code
3. Go to Extensions view (Ctrl+Shift+X)
4. Click on the ... menu at the top right
5. Select "Install from VSIX..."
6. Choose the downloaded .vsix file

### From source

1. Clone the repository
2. Copy the `vscode-extension` folder to your VS Code extensions directory:
   - Windows: `%USERPROFILE%\.vscode\extensions`
   - macOS/Linux: `~/.vscode/extensions`
3. Reload VS Code

## Building the Extension

### Automated Build (Recommended)

The extension includes an automated build system that handles everything:

```bash
cd vscode-extension
./build.sh
```

This will:
1. Install all dependencies
2. Compile TypeScript
3. Package the extension
4. Output `.vsix` file to `build/` directory

The packaged extension will be available at:
- `build/alang-language-support-0.2.0.vsix` (versioned)
- `build/alang-language-support-latest.vsix` (symlink to latest)

### Manual Build

If you prefer manual control:

```bash
npm install -g @vscode/vsce
cd vscode-extension
npm run postinstall  # Install dependencies
npm run compile      # Compile TypeScript
npm run package      # Create .vsix in build/
```

### Quick Build

For quick iterations during development:

```bash
npm run build:quick
```

See `build/README.md` for more details.

## Usage

The extension automatically activates for files with the `.alang` extension. Simply open any `.alang` file and enjoy syntax highlighting!

## Example Code

```alang
// ALang syntax highlighting example
import std.math.pi;
import std.io.*;

class Calculator {
    function constructor(name) {
        this.name = name;
    }
    
    function add(a, b) {
        return a + b;
    }
}

async function main() {
    let calc = new Calculator("MyCalc");
    let result = calc.add(10, 20);
    
    println(`Result: ${result}`);
    
    // Async operation
    await sleep(1000);
    println("Done!");
}

go main();
```

## Contributing

Contributions are welcome! Please visit the [ALang repository](https://github.com/Asultop/AsulLang) to contribute.

## License

This extension follows the same license as the ALang project. See the LICENSE file for details.

## Links

- [ALang Repository](https://github.com/Asultop/AsulLang)
- [ALang Documentation](https://github.com/Asultop/AsulLang/blob/main/README.md)
- [Report Issues](https://github.com/Asultop/AsulLang/issues)
