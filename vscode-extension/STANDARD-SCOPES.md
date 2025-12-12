# Standard TextMate Scopes for ALang

## Overview

ALang now uses **standard TextMate scopes** for core language elements like variables, properties, and language keywords. This means that ALang code will be properly highlighted **without requiring users to activate the custom "ALang Default Dark" theme**.

## Why Standard Scopes?

### Problem with Language-Specific Scopes
Previously, all our scopes ended with `.alang`:
- `variable.language.this.alang`
- `variable.other.readwrite.alang`

This meant:
- Only our custom theme could color these tokens
- Users had to activate "ALang Default Dark" theme
- Code wouldn't be highlighted in other themes

### Solution: Use Standard Scopes
By using standard TextMate scope names (without the `.alang` suffix), ALang tokens are automatically recognized by **all VSCode themes**:
- `variable.other.readwrite` - Recognized by all themes
- `variable.other.property` - Recognized by all themes
- `variable.language.this` - Recognized by all themes

## Scope Mapping

### Variables

| Element | Scope | Color (Dark+ Theme) |
|---------|-------|---------------------|
| Variable declaration | `variable.other.readwrite` | Light blue (#9CDCFE) |
| Object property | `variable.other.property` | Light blue (#9CDCFE) |
| `this` keyword | `variable.language.this` | Blue (#569CD6) |

**Example**:
```alang
let myVariable = 42;        // myVariable → light blue
let obj = { name: "test" }; // name → light blue
obj.name;                   // name → light blue (property)
this.value;                 // this → blue
```

### Why This Works

VSCode's built-in themes (Dark+, Light+, Monokai, etc.) all define colors for standard scopes:

**Dark+ Theme** (default dark):
- `variable.other.readwrite` → `#9CDCFE` (light blue)
- `variable.other.property` → `#9CDCFE` (light blue)
- `variable.language` → `#569CD6` (blue)

**Light+ Theme** (default light):
- `variable.other.readwrite` → `#001080` (dark blue)
- `variable.other.property` → `#001080` (dark blue)
- `variable.language` → `#0000FF` (blue)

## Standard vs. Language-Specific Scopes

### Standard Scopes (Recommended for Core Elements)
Use standard scopes for common language elements:
- ✅ `variable.other.readwrite` - Variables
- ✅ `variable.other.property` - Properties
- ✅ `variable.language.this` - Language keywords like `this`
- ✅ `entity.name.function` - Function names
- ✅ `entity.name.type.class` - Class names
- ✅ `storage.type` - Keywords like `let`, `var`, `const`
- ✅ `keyword.control` - Control flow keywords

**Benefit**: Works with ALL themes automatically

### Language-Specific Scopes (Use for Unique Features)
Use `.alang` suffix for ALang-specific features:
- ✅ `keyword.operator.interface-match.alang` - `=~=` operator
- ✅ `keyword.operator.arrow.alang` - `<-`, `=>` operators
- ✅ `keyword.operator.nullish.alang` - `?.`, `??` operators
- ✅ `keyword.operator.spread.alang` - `...` operator
- ✅ `keyword.operator.decorator.alang` - `@` operator

**Benefit**: Can be colored specially in our custom theme

## Grammar Implementation

### Variable Patterns

```json
"variables": {
  "patterns": [
    {
      "name": "meta.variable.declaration.alang",
      "match": "\\b(let|var|const)\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\b",
      "captures": {
        "1": { "name": "storage.type.alang" },
        "2": { "name": "variable.other.readwrite" }
      }
    },
    {
      "name": "variable.other.readwrite",
      "match": "\\b[a-zA-Z_][a-zA-Z0-9_]*\\b(?!\\s*\\()"
    },
    {
      "name": "variable.other.property",
      "match": "(?<=\\.)([a-zA-Z_][a-zA-Z0-9_]*)\\b"
    }
  ]
}
```

### How It Works

1. **Variable Declaration**: `let x = 5;`
   - `let` → `storage.type.alang` (keyword)
   - `x` → `variable.other.readwrite` (standard scope)

2. **Variable Use**: `x + y`
   - `x`, `y` → `variable.other.readwrite` (standard scope)

3. **Property Access**: `obj.property`
   - `property` → `variable.other.property` (standard scope)

4. **Function Call**: `functionName()`
   - `functionName` → `entity.name.function` (not matched as variable due to `(?!\\s*\\()`)

## Benefits

### 1. Universal Compatibility
```alang
let myVar = 42;  // Light blue in Dark+, dark blue in Light+, etc.
```
Works in **all themes** without configuration.

### 2. User Choice
Users can:
- Use their preferred theme (Dark+, Monokai, Solarized, etc.)
- Still get proper ALang variable highlighting
- Optionally activate "ALang Default Dark" for special operator colors

### 3. Consistent Experience
Variables look like variables in all languages:
```javascript
// JavaScript
let jsVar = 42;  // Light blue

// ALang
let alangVar = 42;  // Also light blue
```

## Custom Theme Still Available

The "ALang Default Dark" theme is still useful for:
- **Special Operators**: `=~=`, `<-`, `?.`, `??`, `...`, `@` get unique colors
- **Enhanced Highlighting**: If you want ALang-specific styling
- **Personal Preference**: If you like the color choices

But it's now **optional**, not required.

## Testing

### Test 1: Default Theme (Dark+)
1. Open VSCode with default Dark+ theme
2. Open `examples/variable-highlighting-demo.alang`
3. **Expected**: Variables appear in light blue
4. **Result**: ✅ Works without activating custom theme

### Test 2: Different Theme (Light+)
1. Switch to Light+ theme
2. Open the same file
3. **Expected**: Variables appear in appropriate light theme color
4. **Result**: ✅ Works in any theme

### Test 3: Custom Theme (ALang Default Dark)
1. Activate "ALang Default Dark" theme
2. Open the same file
3. **Expected**: Variables in light blue + special operators in custom colors
4. **Result**: ✅ Enhanced highlighting with custom theme

## Migration

### For Users
No changes needed! Variables will now be highlighted automatically in any theme.

### For Extension Developers
When adding new language features:
1. **Common features** (variables, functions, classes) → Use standard scopes
2. **Unique features** (special operators) → Use `.alang` suffix and define in custom theme

## Standard Scope Reference

Common TextMate scopes that work in all themes:

### Variables
- `variable.other.readwrite` - Regular variables
- `variable.other.constant` - Constants
- `variable.other.property` - Object properties
- `variable.language` - Special keywords (this, self, super)
- `variable.parameter` - Function parameters

### Functions
- `entity.name.function` - Function name
- `support.function` - Built-in functions

### Classes
- `entity.name.type.class` - Class name
- `entity.name.type.interface` - Interface name
- `entity.other.inherited-class` - Parent class

### Keywords
- `keyword.control` - Control flow (if, for, while)
- `keyword.operator` - Operators (+, -, *, /)
- `storage.type` - Type keywords (let, var, const)
- `storage.modifier` - Modifiers (static, public, private)

### Literals
- `constant.numeric` - Numbers
- `constant.language` - Boolean, null
- `string.quoted` - Strings
- `comment.line` - Line comments
- `comment.block` - Block comments

## Conclusion

By using standard TextMate scopes for common language elements, ALang provides a **better out-of-box experience** while still supporting enhanced highlighting through our optional custom theme.

**Key Takeaway**: Variables and other core elements now work in **any theme**, not just "ALang Default Dark".
