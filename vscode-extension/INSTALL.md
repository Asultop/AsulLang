# ALang VSCode Extension Installation Guide

This guide provides detailed instructions for installing and using the ALang language support extension for Visual Studio Code.

## Prerequisites

- Visual Studio Code 1.60.0 or higher
- (Optional) Node.js and npm for building the extension from source

## Installation Methods

### Method 1: Install from Source (Development)

This method is recommended for contributors and users who want to test the latest version.

1. **Clone the repository** (if you haven't already):
   ```bash
   git clone https://github.com/Asultop/AsulLang.git
   cd AsulLang
   ```

2. **Locate the extension directory**:
   ```bash
   cd vscode-extension
   ```

3. **Install the extension**:

   **Option A: Copy to VS Code extensions folder**
   
   - **Windows**:
     ```powershell
     xcopy /E /I /Y . "%USERPROFILE%\.vscode\extensions\alang-language-support-0.1.0"
     ```
   
   - **macOS/Linux**:
     ```bash
     mkdir -p ~/.vscode/extensions/alang-language-support-0.1.0
     cp -r . ~/.vscode/extensions/alang-language-support-0.1.0/
     ```

   **Option B: Create a symbolic link (recommended for development)**
   
   - **Windows** (run as Administrator):
     ```powershell
     mklink /D "%USERPROFILE%\.vscode\extensions\alang-language-support-0.1.0" "%CD%"
     ```
   
   - **macOS/Linux**:
     ```bash
     ln -s "$(pwd)" ~/.vscode/extensions/alang-language-support-0.1.0
     ```

4. **Reload VS Code**:
   - Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on macOS)
   - Type "Reload Window" and press Enter
   - Or simply restart VS Code

5. **Verify installation**:
   - Open any `.alang` file
   - Check the status bar at the bottom right - it should show "ALang" as the language mode
   - You should see syntax highlighting applied

### Method 2: Install from VSIX Package

If a .vsix package is available:

1. **Download the .vsix file** from the releases page

2. **Install using VS Code UI**:
   - Open VS Code
   - Go to Extensions view (`Ctrl+Shift+X`)
   - Click the `...` menu at the top right of the Extensions view
   - Select `Install from VSIX...`
   - Navigate to the downloaded .vsix file and select it

3. **Install using command line**:
   ```bash
   code --install-extension alang-language-support-0.1.0.vsix
   ```

### Method 3: Build and Package (for distribution)

To create a .vsix package for distribution:

1. **Install vsce** (Visual Studio Code Extension Manager):
   ```bash
   npm install -g vsce
   ```

2. **Navigate to the extension directory**:
   ```bash
   cd vscode-extension
   ```

3. **Package the extension**:
   ```bash
   vsce package
   ```
   
   This will create a file named `alang-language-support-0.1.0.vsix`

4. **Install the package** using Method 2 above

## Verification

After installation, verify the extension is working:

1. **Create a test file**:
   - Create a new file with the extension `.alang`
   - For example: `test.alang`

2. **Add some ALang code**:
   ```alang
   // Test syntax highlighting
   import std.io.*;
   
   class Calculator {
       function add(a, b) {
           return a + b;
       }
   }
   
   async function main() {
       let calc = new Calculator();
       let result = calc.add(10, 20);
       println(`Result: ${result}`);
       
       await sleep(1000);
       println("Done!");
   }
   
   go main();
   ```

3. **Check the highlighting**:
   - Keywords like `import`, `class`, `function`, `async`, `await`, `let`, `new` should be highlighted
   - Strings and template literals should be colored
   - Comments should be grayed out
   - Numbers should be highlighted
   - Operators should be distinct

4. **Test editor features**:
   - Type `{` - it should auto-close with `}`
   - Select code and press `Ctrl+/` - it should toggle line comments
   - Type `/*` - it should auto-complete with `*/`

## Troubleshooting

### Extension not showing up

- Make sure you've reloaded VS Code after installation
- Check that the extension folder is named correctly: `alang-language-support-0.1.0`
- Verify the extension is in the correct directory:
  - Windows: `%USERPROFILE%\.vscode\extensions`
  - macOS/Linux: `~/.vscode/extensions`

### Syntax highlighting not working

- Check the language mode in the bottom-right corner of VS Code
- If it doesn't say "ALang", click on it and select "ALang" from the list
- Verify the file has a `.alang` extension
- Try reloading the window: `Ctrl+Shift+P` → "Reload Window"

### Auto-complete or bracket matching not working

- Make sure the `language-configuration.json` file is present in the extension directory
- Try reloading the window
- Check the VS Code developer console for errors: `Help` → `Toggle Developer Tools`

### Updating the extension

If you installed from source and want to update:

1. Navigate to the repository:
   ```bash
   cd AsulLang
   ```

2. Pull the latest changes:
   ```bash
   git pull origin main
   ```

3. Reload VS Code:
   - Press `Ctrl+Shift+P`
   - Type "Reload Window" and press Enter

## Uninstallation

### If installed via VSIX or package:

- Open Extensions view (`Ctrl+Shift+X`)
- Find "ALang Language Support"
- Click the gear icon and select "Uninstall"

### If installed manually:

Remove the extension directory:

- **Windows**:
  ```powershell
  rmdir /S /Q "%USERPROFILE%\.vscode\extensions\alang-language-support-0.1.0"
  ```

- **macOS/Linux**:
  ```bash
  rm -rf ~/.vscode/extensions/alang-language-support-0.1.0
  ```

Then reload VS Code.

## Additional Resources

- [VSCode Extension API](https://code.visualstudio.com/api)
- [TextMate Grammar](https://macromates.com/manual/en/language_grammars)
- [ALang Repository](https://github.com/Asultop/AsulLang)
- [ALang Documentation](https://github.com/Asultop/AsulLang/blob/main/README.md)

## Support

If you encounter any issues:

1. Check the troubleshooting section above
2. Search for existing issues on [GitHub Issues](https://github.com/Asultop/AsulLang/issues)
3. Create a new issue with:
   - Your VS Code version
   - Your operating system
   - Steps to reproduce the problem
   - Any error messages from the developer console
