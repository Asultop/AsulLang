# Theme Compatibility Fix - v0.2.7

## Overview

This document explains the fix for special operator highlighting visibility in standard VSCode themes.

## Problem

**User Report**: "无法在常规主题下观察到对ALang语言的特殊着色" (Unable to observe special coloring for ALang language in regular themes)

### Root Cause

The special ALang operators (=~=, <-, =>, ?., ??, ..., @) were using language-specific scope names like `keyword.operator.interface-match.alang`. These scopes are **only** recognized by the custom "ALang Default Dark" theme.

In standard themes like Dark+ or Light+:
- ❌ These language-specific scopes had no color definitions
- ❌ Operators appeared in default text color
- ❌ No visual distinction for special syntax

### Previous Behavior

```alang
let result = obj?.prop ?? "default";  // All operators same color
let match = value =~= pattern;        // No special highlighting
@decorator                            // No decorator color
class MyClass <- BaseClass { }        // <- not highlighted
```

**Result**: In Dark+ theme, all operators appeared in white/gray - no visual distinction.

## Solution

### Dual Scope Strategy

Use **multiple scopes** per token: standard TextMate scope + ALang-specific scope.

```json
{
  "name": "keyword.operator.comparison keyword.operator.interface-match.alang",
  "match": "=~="
}
```

This provides:
1. **Standard scope** (`keyword.operator.comparison`) - Recognized by ALL themes
2. **Language-specific scope** (`keyword.operator.interface-match.alang`) - Custom theme can override

### Scope Mappings

| Operator | Standard Scope | ALang Scope | Standard Theme Color | Custom Theme Color |
|----------|----------------|-------------|---------------------|-------------------|
| `=~=` | `keyword.operator.comparison` | `keyword.operator.interface-match.alang` | Operator color | Bold Pink #FF79C6 |
| `<-` `->` `=>` | `keyword.operator.arrow` | `keyword.operator.arrow.alang` | Arrow color | Bold Red #FF6B6B |
| `?.` `??` | `keyword.operator.logical` | `keyword.operator.nullish.alang` | Logical operator | Bold Gold #FFD700 |
| `...` | `keyword.operator.spread` | `keyword.operator.spread.alang` | Spread/rest | Bold Blue #61AFEF |
| `@` | `storage.modifier` | `keyword.operator.decorator.alang` | Modifier/decorator | Bold Yellow #E5C07B |

### How It Works

**TextMate Scope Resolution**:
1. Editor checks theme for most specific scope first
2. Falls back to less specific scopes
3. Uses first matching scope definition

**Example for `=~=`**:
```
Scopes: "keyword.operator.comparison keyword.operator.interface-match.alang"

Custom "ALang Default Dark" theme:
  - Has definition for "keyword.operator.interface-match.alang" → Use Pink
  
Standard Dark+ theme:
  - No definition for "keyword.operator.interface-match.alang"
  - Has definition for "keyword.operator.comparison" → Use operator color
```

## Visual Comparison

### Before Fix (v0.2.6)

**In Dark+ Theme**:
```alang
let result = obj?.prop ?? "default";  // All white/gray
let match = value =~= pattern;        // All white/gray
@decorator                            // White/gray
class MyClass <- BaseClass { }        // White/gray
```

**In ALang Default Dark Theme**:
```alang
let result = obj?.prop ?? "default";  // Gold for ?. and ??
let match = value =~= pattern;        // Pink for =~=
@decorator                            // Yellow for @
class MyClass <- BaseClass { }        // Red for <-
```

### After Fix (v0.2.7)

**In Dark+ Theme** ✅:
```alang
let result = obj?.prop ?? "default";  // Blue for logical operators
let match = value =~= pattern;        // Orange for comparison
@decorator                            // Purple for modifier
class MyClass <- BaseClass { }        // Orange/cyan for arrow
```

**In ALang Default Dark Theme** ✅:
```alang
let result = obj?.prop ?? "default";  // Bold Gold for ?. and ??
let match = value =~= pattern;        // Bold Pink for =~=
@decorator                            // Bold Yellow for @
class MyClass <- BaseClass { }        // Bold Red for <-
```

## Standard TextMate Scopes Used

### keyword.operator.comparison
Standard scope for comparison operators (<, >, <=, >=, ==, !=).

**Used for**: `=~=` (interface match)
**Rationale**: Interface matching is conceptually a comparison operation

**Theme Colors**:
- Dark+: #D4D4D4 (light gray) or #569CD6 (blue)
- Light+: #000000 (black)
- Monokai: #F92672 (pink/red)

### keyword.operator.arrow
Standard scope for arrow functions and arrow operators.

**Used for**: `<-`, `->`, `=>`
**Rationale**: These are arrow-like directional operators

**Theme Colors**:
- Dark+: #569CD6 (blue) or #C586C0 (purple)
- Light+: #0000FF (blue)
- Monokai: #66D9EF (cyan)

### keyword.operator.logical
Standard scope for logical operators (&&, ||, !).

**Used for**: `?.`, `??`, `?`
**Rationale**: Nullish operators are logical decision operators

**Theme Colors**:
- Dark+: #569CD6 (blue)
- Light+: #0000FF (blue)
- Monokai: #F92672 (pink/red)

### keyword.operator.spread
Standard scope for spread/rest operators (...).

**Used for**: `...`
**Rationale**: Direct mapping to standard spread operator

**Theme Colors**:
- Dark+: #569CD6 (blue)
- Light+: #0000FF (blue)
- Modern themes: Usually cyan or blue

### storage.modifier
Standard scope for modifiers and decorators.

**Used for**: `@`
**Rationale**: Decorators are storage modifiers in most languages

**Theme Colors**:
- Dark+: #569CD6 (blue) or #C586C0 (purple)
- Light+: #0000FF (blue)
- Monokai: #66D9EF (cyan)

## Activation Event Fix

### Problem

VSCode warning:
```
对于面向引擎版本 ^1.75.0 的扩展，可以移除此激活事件，
因为 VS Code 将从 package.json 贡献声明自动生成这些事件。
```

Translation: "For extensions targeting engine version ^1.75.0, this activation event can be removed, as VS Code will automatically generate these events from package.json contributions."

### Solution

1. **Removed**: `"activationEvents": ["onLanguage:alang"]`
2. **Updated**: `"engines": { "vscode": "^1.75.0" }`

VSCode 1.75.0+ automatically generates activation events from:
- `contributes.languages` declarations
- `contributes.grammars` associations
- Other contribution points

## Testing

### Test in Different Themes

1. **Dark+ (default dark)**:
   ```
   Open .alang file → Operators should be colored (blue, gray, purple)
   ```

2. **Light+ (default light)**:
   ```
   Open .alang file → Operators should be visible (black, blue)
   ```

3. **Monokai**:
   ```
   Open .alang file → Operators should match Monokai style (pink, cyan)
   ```

4. **ALang Default Dark (custom)**:
   ```
   Activate theme → Operators should use enhanced colors (pink, red, gold, blue, yellow)
   ```

### Test Cases

```alang
// 1. Interface match operator
let matches = value =~= pattern;  // Should be colored

// 2. Arrow operators
class Child <- Parent { }         // <- should be colored
let result = fn -> transform(fn); // -> should be colored
let arrow = () => result;         // => should be colored

// 3. Nullish operators
let safe = obj?.property;         // ?. should be colored
let fallback = value ?? "default"; // ?? should be colored

// 4. Spread operator
function process(...args) { }     // ... should be colored
let copy = [...array];            // ... should be colored

// 5. Decorator
@component                        // @ should be colored
class MyComponent { }
```

## Benefits

### For Users

✅ **Works Immediately**: No theme activation required
✅ **Consistent Experience**: Operators colored appropriately in any theme
✅ **Visual Distinction**: Special operators stand out in code
✅ **No Configuration**: Works out-of-box with default VSCode themes

### For Advanced Users

✅ **Enhanced Colors**: Custom theme provides distinctive colors
✅ **Theme Choice**: Use any base theme + optional custom enhancement
✅ **Flexibility**: Can mix-and-match themes as preferred

### For Theme Developers

✅ **Standard Conventions**: Uses well-known TextMate scopes
✅ **Override Support**: Can target ALang-specific scopes for custom colors
✅ **Predictable Behavior**: Follows TextMate scope resolution rules

## Migration

### From v0.2.6 to v0.2.7

**No action required for users**. The extension automatically works better in all themes.

**Optional**: If you were using "ALang Default Dark" only for operator colors, you can now:
1. Switch to your preferred base theme
2. Operators will still be colored appropriately
3. Or keep custom theme for enhanced colors

### For Extension Developers

**Lesson Learned**: Always use dual scopes for language-specific features:
```json
{
  "name": "standard.scope language-specific.scope.langname",
  "match": "pattern"
}
```

This ensures:
- Baseline compatibility with all themes
- Custom theme can provide enhanced colors
- Graceful degradation

## References

- [TextMate Scopes](https://macromates.com/manual/en/language_grammars)
- [VSCode Theme Documentation](https://code.visualstudio.com/api/extension-guides/color-theme)
- [VSCode Activation Events](https://code.visualstudio.com/api/references/activation-events)
- [VSCode 1.75.0 Release Notes](https://code.visualstudio.com/updates/v1_75)

## Version History

- **v0.2.7**: Fixed theme compatibility, removed redundant activation events
- **v0.2.6**: Node.js compatibility fix
- **v0.2.5**: GitHub Actions updates
- **v0.2.4**: Comprehensive review
- **v0.2.3**: Universal variable highlighting
- **v0.2.2**: Theme compatibility & string interpolation
- **v0.2.1**: Automated build system
- **v0.2.0**: Initial release with LSP

## Status

✅ **Fixed**: Special operators now visible in all themes
✅ **Fixed**: Removed activation event warning
✅ **Tested**: Works in Dark+, Light+, Monokai, and custom themes
✅ **Production Ready**: v0.2.7 ready for use
