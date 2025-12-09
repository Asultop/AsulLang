# ALang VSCode Extension - Feature Summary

## Version 0.2.0 - Complete Feature Set

### 🎨 1. Color Theme with Default Token Colors

**Feature**: Custom color theme highlighting ALang-specific operators

**Activation**:
1. Open Command Palette (`Ctrl+Shift+P`)
2. Type "Preferences: Color Theme"
3. Select "ALang Default Dark"

**Special Operator Colors**:

| Operator | Color | Purpose | Example |
|----------|-------|---------|---------|
| `=~=` | **Pink (#FF79C6)** | Interface/class matching | `obj =~= Drawable` |
| `<-` | **Red (#FF6B6B)** | Inheritance | `class Dog <- (Animal)` |
| `->` `=>` | **Red (#FF6B6B)** | Arrows/lambdas | `x => x * 2` |
| `?.` | **Gold (#FFD700)** | Optional chaining | `user?.profile?.name` |
| `??` | **Gold (#FFD700)** | Nullish coalescing | `value ?? default` |
| `...` | **Blue (#61AFEF)** | Spread/rest | `...args`, `[...array]` |
| `@` | **Yellow (#E5C07B)** | Decorator | `@logged` |

**All Tokens Colored**:
- ✅ Keywords (let, var, const, function, class, etc.) - Blue/Purple
- ✅ Strings - Orange
- ✅ Numbers - Green
- ✅ Comments - Green italic
- ✅ Functions - Yellow
- ✅ Classes - Cyan
- ✅ Operators - Gold/Red/Blue (context-dependent)
- ✅ Brackets - Gold

**Demo File**: `examples/color-theme-demo.alang`

---

### 🔍 2. Syntax Checking (Diagnostics)

**Feature**: Real-time syntax validation with error highlighting

**How it Works**:
- Automatic checking as you type
- Errors shown with red underlines
- Hover over error to see message
- All errors listed in Problems panel

**Detected Errors**:
- ✅ Unclosed strings (double and single quotes)
- ✅ Bracket mismatches (coming soon)
- ✅ Invalid syntax patterns (coming soon)

**Example**:
```alang
let str = "unclosed string
         ^~~~~~~~~~~~~~~~~~~~~~~ Error: Unclosed double quote

let valid = "closed string";  // No error
```

**Usage**:
1. Open any `.alang` file
2. Errors appear automatically
3. View all errors: `Ctrl+Shift+M` (Problems panel)

---

### 🎯 3. Go to Definition

**Feature**: Navigate to symbol definitions

**How to Use**:
- **Method 1**: Right-click symbol → "Go to Definition"
- **Method 2**: Press `F12` on symbol
- **Method 3**: `Ctrl+Click` (macOS: `Cmd+Click`)

**Supported Symbols**:
- ✅ Functions
- ✅ Classes
- ✅ Interfaces
- ✅ Variables (let, var, const)

**Example**:
```alang
// Definition
function calculateSum(a, b) {
    return a + b;
}

class Calculator {
    function add(x, y) {
        return x + y;
    }
}

// Usage - press F12 on these symbols
let result = calculateSum(5, 10);  // Jump to calculateSum definition
let calc = new Calculator();        // Jump to Calculator class
```

---

### 💡 4. Auto-completion

**Feature**: Intelligent code suggestions

**How to Use**:
- Start typing - suggestions appear automatically
- Manual trigger: `Ctrl+Space` (macOS: `Cmd+Space`)

**Completions Provided**:
- ✅ All ALang keywords (36 total)
- ✅ Functions defined in current file
- ✅ Classes defined in current file
- ✅ Variables defined in current file
- ✅ Interfaces defined in current file

**Trigger Characters**:
- `.` - Member access
- `@` - Decorators

**Example**:
```alang
fu|  → function
cl|  → class
le|  → let
asy|  → async

myObj.|  → Shows object members
@|      → Shows decorator suggestions
```

---

### ⚙️ 5. Configuration

**Settings**:

```json
{
  // Maximum number of problems to report
  "alangLanguageServer.maxNumberOfProblems": 100,
  
  // Enable detailed logging (off, messages, verbose)
  "alangLanguageServer.trace.server": "off"
}
```

**Access Settings**:
1. `Ctrl+,` to open Settings
2. Search for "alangLanguageServer"
3. Adjust values

---

## Installation

### Quick Install (Development)

```bash
cd vscode-extension
npm run postinstall  # Install dependencies
npm run compile      # Build TypeScript
```

Then copy to extensions directory:
```bash
# Linux/macOS
mkdir -p ~/.vscode/extensions/alang-language-support-0.2.0
cp -r * ~/.vscode/extensions/alang-language-support-0.2.0/

# Windows
xcopy /E /I /Y . "%USERPROFILE%\.vscode\extensions\alang-language-support-0.2.0"
```

Reload VSCode: `Ctrl+Shift+P` → "Reload Window"

### Package Install

```bash
npm install -g vsce
vsce package
code --install-extension alang-language-support-0.2.0.vsix
```

---

## Testing Features

### Test Color Theme

1. Open `examples/color-theme-demo.alang`
2. Select "ALang Default Dark" theme
3. Verify special operators are highlighted:
   - `=~=` in pink
   - `<-`, `=>` in red
   - `?.`, `??` in gold
   - `...` in blue
   - `@` in yellow

### Test Syntax Checking

1. Create a new `.alang` file
2. Type: `let str = "unclosed`
3. See error underline
4. Hover to see: "Unclosed double quote"
5. Check Problems panel (`Ctrl+Shift+M`)

### Test Go to Definition

1. Open `examples/syntax-demo.alang`
2. Find a function call
3. Press `F12` on function name
4. Should jump to function definition
5. Try with classes and variables too

### Test Auto-completion

1. Create new `.alang` file
2. Type `fu` and wait
3. See `function` suggestion
4. Type `@` and see decorator suggestions
5. Define a function and see it in completions

---

## Troubleshooting

### Extension Not Working

1. **Check extension is installed**:
   ```bash
   ls ~/.vscode/extensions/alang-language-support-*
   ```

2. **Check TypeScript compiled**:
   ```bash
   ls vscode-extension/client/out/extension.js
   ls vscode-extension/server/out/server.js
   ```

3. **Recompile if needed**:
   ```bash
   cd vscode-extension
   npm run compile
   ```

4. **Reload VSCode**: `Ctrl+Shift+P` → "Reload Window"

### Language Server Not Running

1. Open Output panel: `Ctrl+Shift+U`
2. Select "ALang Language Server" from dropdown
3. Check for errors
4. Enable verbose logging:
   ```json
   "alangLanguageServer.trace.server": "verbose"
   ```

### Colors Not Showing

1. Make sure "ALang Default Dark" theme is selected
2. Try switching themes and back
3. Check file has `.alang` extension
4. Verify language mode is "ALang" (bottom-right corner)

---

## Feature Comparison

| Feature | v0.1.0 | v0.2.0 |
|---------|--------|--------|
| Syntax Highlighting | ✅ | ✅ |
| Color Theme | ❌ | ✅ |
| Syntax Checking | ❌ | ✅ |
| Go to Definition | ❌ | ✅ |
| Auto-completion | ❌ | ✅ |
| Bracket Matching | ✅ | ✅ |
| Auto-closing | ✅ | ✅ |
| Comment Toggle | ✅ | ✅ |
| Code Folding | ✅ | ✅ |

---

## Next Steps

### Planned Enhancements

- [ ] Find All References
- [ ] Rename Symbol
- [ ] Hover Documentation
- [ ] Signature Help
- [ ] Code Formatting
- [ ] Semantic Highlighting
- [ ] Cross-file Symbol Resolution
- [ ] Integration with ALang Parser (AsulParser.h)

---

## Documentation

- **BUILD.md** - Building and development guide
- **LANGUAGE-SERVER.md** - LSP architecture details
- **INSTALL.md** - Installation instructions
- **SYNTAX-REFERENCE.md** - Complete syntax reference
- **DEVELOPER.md** - Contributing guide

---

## Support

For issues or questions:
- GitHub Issues: https://github.com/Asultop/AsulLang/issues
- Repository: https://github.com/Asultop/AsulLang

---

**Version**: 0.2.0  
**Release Date**: 2025-12-09  
**Status**: Production Ready ✅
