# Quick Start Guide - ALang VSCode Extension

Get syntax highlighting for ALang files in VSCode in under 5 minutes!

## TL;DR (Fastest Method)

For development/testing, the quickest way to get started:

### Linux/macOS
```bash
# From the ALang repository root
cd vscode-extension
mkdir -p ~/.vscode/extensions/alang-language-support-0.1.0
cp -r * ~/.vscode/extensions/alang-language-support-0.1.0/
# Reload VSCode (Ctrl+Shift+P → "Reload Window")
```

### Windows (PowerShell)
```powershell
# From the ALang repository root
cd vscode-extension
xcopy /E /I /Y . "$env:USERPROFILE\.vscode\extensions\alang-language-support-0.1.0"
# Reload VSCode (Ctrl+Shift+P → "Reload Window")
```

## What You Get

Once installed, the extension provides:

✅ Syntax highlighting for all ALang language features  
✅ Auto-closing brackets and quotes  
✅ Comment toggling with Ctrl+/  
✅ Code folding  
✅ Bracket matching  

## Testing the Extension

1. Create a new file with `.alang` extension
2. Try typing some ALang code:

```alang
// Test the syntax highlighting
import std.io.*;

class Greeter {
    function greet(name) {
        println(`Hello, ${name}!`);
    }
}

async function main() {
    let greeter = new Greeter();
    greeter.greet("World");
    
    await sleep(1000);
    println("Done!");
}

go main();
```

3. Check that:
   - Keywords like `class`, `function`, `async`, `await` are highlighted
   - Strings and template literals are colored
   - Comments are grayed out
   - The language indicator in the bottom-right shows "ALang"

## Troubleshooting

### Extension not working?

1. **Check the language mode**: Click the language indicator in the bottom-right corner of VSCode and ensure "ALang" is selected
2. **Reload VSCode**: Press `Ctrl+Shift+P` and run "Reload Window"
3. **Verify installation**: Check that the extension folder exists:
   - Linux/macOS: `~/.vscode/extensions/alang-language-support-0.1.0/`
   - Windows: `%USERPROFILE%\.vscode\extensions\alang-language-support-0.1.0\`

### Still having issues?

- See the full [Installation Guide](INSTALL.md) for alternative methods
- Check the [Troubleshooting](INSTALL.md#troubleshooting) section
- Open an issue on GitHub

## Next Steps

- Read the [Syntax Reference](SYNTAX-REFERENCE.md) to learn about all supported features
- Check the [examples/](examples/) directory for comprehensive syntax demonstrations
- For contributors: See the [Developer Guide](DEVELOPER.md)

## Building a Package

If you want to create a `.vsix` package for distribution:

```bash
# Install vsce (one-time setup)
npm install -g vsce

# Package the extension
cd vscode-extension
vsce package

# Install the package
code --install-extension alang-language-support-0.1.0.vsix
```

## Help

For detailed information:
- Installation: [INSTALL.md](INSTALL.md)
- Syntax: [SYNTAX-REFERENCE.md](SYNTAX-REFERENCE.md)
- Contributing: [DEVELOPER.md](DEVELOPER.md)
- Repository: https://github.com/Asultop/AsulLang

Happy coding with ALang! 🚀
