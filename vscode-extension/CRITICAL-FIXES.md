# Critical Fixes - v0.3.1

## Mac Environment Issues Resolved

Two critical bugs discovered during Mac testing have been fixed in v0.3.1.

## Issue 1: Theme Breaking Other Languages ❌→✅

### Problem
**Reported**: C++ highlighter and other languages lost syntax highlighting when ALang themes were active.

**Root Cause**: Themes were using generic scope names without `.alang` suffix:
```json
{
  "scope": [
    "variable.other.readwrite",      // ❌ Applies to ALL languages
    "variable.other.property"        // ❌ Applies to ALL languages
  ]
}
```

This caused ALang theme colors to override ALL variable colors in ALL languages, breaking:
- C++ syntax highlighting
- JavaScript variable colors
- Python variable colors
- Any language using standard variable scopes

### Solution
**Added `.alang` suffix to all variable scopes**:

#### Grammar Changes
```json
{
  "name": "variable.other.readwrite variable.other.readwrite.alang",
  "match": "\\b[a-zA-Z_][a-zA-Z0-9_]*\\b(?!\\s*\\()"
}
```

**Dual Scope Strategy**:
- `variable.other.readwrite` - Standard scope (all themes recognize)
- `variable.other.readwrite.alang` - ALang-specific scope (only ALang themes target)

#### Theme Changes
**Before**:
```json
{
  "scope": ["variable.other.readwrite", "variable.other.property"]
}
```

**After**:
```json
{
  "scope": ["variable.other.readwrite.alang", "variable.other.property.alang"]
}
```

### Result ✅
- ALang files: Variables in light blue (dark theme) / dark blue (light theme)
- C++ files: Normal highlighting restored
- JavaScript files: Normal highlighting restored
- Python files: Normal highlighting restored
- All other languages: Completely unaffected

## Issue 2: String Interpolation Not Rendering ❌→✅

### Problem
**Reported**: Inside `${}` in template literals, the code was not being syntax highlighted properly.

**Example**:
```alang
let msg = `Result: ${a + b}`;  // a, +, b had no syntax highlighting ❌
```

**Root Cause**: Theme was overriding the `meta.embedded.line.alang` scope with a single foreground color:
```json
{
  "name": "ALang - Template Interpolation Expression",
  "scope": "meta.embedded.line.alang",
  "settings": {
    "foreground": "#9CDCFE"  // ❌ Single color for entire expression
  }
}
```

This prevented individual tokens (keywords, operators, variables) inside `${}` from getting their proper colors.

### Solution
**Removed the theme override for interpolation expressions**:

The grammar already had the correct pattern:
```json
{
  "name": "meta.embedded.line.alang",
  "begin": "(\\$)(\\{)",
  "end": "(\\})",
  "patterns": [
    { "include": "$self" }  // ✅ Recursively applies all ALang patterns
  ]
}
```

By removing the theme override, tokens inside `${}` now get proper highlighting:
- Keywords → Purple
- Operators → Orange/blue
- Variables → Light blue
- Strings → Orange
- Numbers → Light green

### Result ✅
**Before**:
```alang
let msg = `Sum: ${a + b}`;
// Inside ${}:  a + b → all one color ❌
```

**After**:
```alang
let msg = `Sum: ${a + b}`;
// Inside ${}:  a → light blue, + → white, b → light blue ✅
```

**Complex expressions now work**:
```alang
let complex = `Value: ${obj?.prop ?? "default"}`;
// obj → light blue
// ?.  → gold (special operator)
// prop → light blue
// ??  → gold (special operator)
// "default" → orange (string)
```

## Technical Architecture

### Dual Scope Strategy
All language-specific features use **two scopes**:

1. **Standard scope** - Recognized by all themes (Dark+, Light+, Monokai, etc.)
2. **`.alang` suffix scope** - Targeted only by ALang themes

**Example**:
```json
{
  "name": "variable.other.readwrite variable.other.readwrite.alang",
  "match": "..."
}
```

**How it works**:
- In Dark+ (without ALang theme): Uses `variable.other.readwrite` → Light blue
- In "ALang for Dark+" theme: Uses `variable.other.readwrite.alang` → Custom light blue
- In C++ files: `variable.other.readwrite` not overridden → Normal C++ colors

### Theme Scope Targeting
**ALang themes only target `.alang` suffix scopes**:

```json
{
  "name": "ALang - Variables",
  "scope": [
    "variable.other.readwrite.alang",     // ✅ Only ALang
    "variable.other.property.alang"       // ✅ Only ALang
  ],
  "settings": {
    "foreground": "#9CDCFE"
  }
}
```

**This ensures**:
- ALang files get enhanced colors
- Other languages completely unaffected
- No theme conflicts

## Testing Performed

### Mac Environment ✅
- C++ files: Syntax highlighting working correctly
- ALang files: Variables in light blue, special operators colored
- Template literals: `${}` interior properly highlighted

### Multi-Language Testing ✅
- **C++**: `int myVar = 42;` → Normal C++ colors
- **JavaScript**: `let myVar = 42;` → Normal JS colors
- **Python**: `my_var = 42` → Normal Python colors
- **ALang**: `let myVar = 42;` → Light blue variable color

### String Interpolation Testing ✅
```alang
// Simple interpolation
let name = "Alice";
let msg = `Hello, ${name}!`;  // name → light blue ✅

// Complex expression
let calc = `Sum: ${10 + 20}`;  // 10 → light green, + → white, 20 → light green ✅

// Nested operators
let complex = `Value: ${obj?.prop ?? "default"}`;
// obj → light blue, ?. → gold, prop → light blue, ?? → gold, "default" → orange ✅

// Function calls
let result = `Result: ${compute(a, b)}`;
// compute → yellow, a → light blue, b → light blue ✅
```

## Files Changed

1. **syntaxes/alang.tmLanguage.json**
   - Added `.alang` suffix to variable scopes
   - Maintains dual scope strategy (standard + `.alang`)

2. **themes/alang-dark-plus.json**
   - Updated variable scopes to use `.alang` suffix only
   - Removed `meta.embedded.line.alang` override

3. **themes/alang-light-plus.json**
   - Updated variable scopes to use `.alang` suffix only
   - Removed `meta.embedded.line.alang` override

4. **package.json**
   - Version bump: 0.3.0 → 0.3.1

5. **CHANGELOG.md**
   - Added v0.3.1 entry with fix details

## Validation

- ✅ All JSON files validated
- ✅ No syntax errors in grammar or themes
- ✅ Mac environment tested (C++ highlighting working)
- ✅ String interpolation tested (full syntax support)
- ✅ Multi-language testing passed

## Migration

**For Users**:
- No action required if already on v0.3.0
- Extension will update automatically
- C++ and other languages will work correctly again
- String interpolation will render properly

**For Developers**:
- Pattern established: Always use `.alang` suffix for language-specific theme scopes
- Grammar uses dual scopes for compatibility
- Themes target `.alang` scopes to avoid conflicts

## Status

✅ **Both Critical Issues Resolved**
✅ **Production Ready**: v0.3.1
✅ **Mac Tested**: No more C++ highlighter issues
✅ **String Interpolation**: Full syntax support inside `${}`
