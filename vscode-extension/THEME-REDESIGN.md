# Theme Architecture Redesign - v0.3.0

## Overview

Version 0.3.0 introduces a completely redesigned theme architecture that extends VSCode's built-in Dark+ and Light+ themes rather than providing a standalone custom theme.

## The Problem with v0.2.x

### Previous Approach (Standalone Theme)
```
ALang Default Dark (standalone)
├── Defined all colors from scratch
├── Could miss colors for other languages
└── Risk of breaking non-ALang highlighting
```

**Issues**:
- If the theme didn't define a color for a specific token from another language, that token would be colorless
- Difficult to maintain compatibility with all possible language extensions
- Users lost their preferred theme appearance when editing ALang files

## The Solution in v0.3.0

### New Approach (Base Theme Extension)
```
ALang for Dark+ (extends vs-dark)
├── Inherits all Dark+ colors
├── Only overrides ALang-specific tokens
└── Other languages use Dark+ styling ✅

ALang for Light+ (extends vs)
├── Inherits all Light+ colors  
├── Only overrides ALang-specific tokens
└── Other languages use Light+ styling ✅
```

**Benefits**:
- ✅ Zero risk of breaking other languages
- ✅ Familiar UI and base colors
- ✅ ALang gets enhanced highlighting
- ✅ Users can choose dark or light
- ✅ Proper VSCode theme architecture

## Theme Details

### ALang for Dark+ (Dark Theme)

**Base**: VSCode Dark+ (vs-dark)

**ALang-Specific Colors**:
| Element | Color | Hex | Rationale |
|---------|-------|-----|-----------|
| Variables | Light Blue | #9CDCFE | Standard Dark+ variable color, highly visible |
| =~= | Bold Pink | #FF79C6 | Distinctive for interface matching |
| <-, ->, => | Bold Red | #FF6B6B | Arrow-like, directional |
| ?., ?? | Bold Gold | #FFD700 | Nullish operations stand out |
| ... | Bold Blue | #61AFEF | Spread operator recognition |
| @ | Bold Yellow | #E5C07B | Decorator emphasis |
| Keywords | Purple Bold | #C586C0 | Enhanced from Dark+ |
| Functions | Yellow | #DCDCAA | Dark+ function color |
| Classes | Teal | #4EC9B0 | Dark+ class color |
| Strings | Orange | #CE9178 | Dark+ string color |
| Comments | Green Italic | #6A9955 | Dark+ comment color |

### ALang for Light+ (Light Theme)

**Base**: VSCode Light+ (vs)

**ALang-Specific Colors**:
| Element | Color | Hex | Rationale |
|---------|-------|-----|-----------|
| Variables | Dark Blue | #001080 | Standard Light+ variable color, readable on white |
| =~= | Bold Magenta | #C800A8 | Distinctive, readable on light |
| <-, ->, => | Bold Dark Red | #A00000 | Strong contrast on white |
| ?., ?? | Bold Dark Orange | #CC8800 | Visible without glare |
| ... | Bold Blue | #0070C0 | Professional blue |
| @ | Bold Dark Yellow | #CC7700 | Warm, visible |
| Keywords | Purple Bold | #AF00DB | Enhanced from Light+ |
| Functions | Brown | #795E26 | Light+ function color |
| Classes | Dark Teal | #267F99 | Light+ class color |
| Strings | Dark Red | #A31515 | Light+ string color |
| Comments | Green Italic | #008000 | Light+ comment color |

## Visual Examples

### ALang for Dark+

```alang
// Variables in light blue - clearly visible
let userName = "Alice";
let userAge = 30;
let userData = { name: "Bob", age: 25 };

// String interpolation with enhanced delimiters
let greeting = `Hello, ${userName}!`;  // ${ } in bold blue

// Special operators in distinctive colors
let match = value =~= pattern;         // =~= in bold pink
class MyClass <- BaseClass {           // <- in bold red
    async function compute(...args) {  // ... in bold blue
        return args?.length ?? 0;      // ?. ?? in bold gold
    }
}

@decorator                             // @ in bold yellow
class Component { }
```

**Result**: All elements clearly visible with enhanced colors for ALang-specific syntax.

### ALang for Light+

```alang
// Variables in dark blue - readable on white
let userName = "Alice";
let userAge = 30;
let userData = { name: "Bob", age: 25 };

// String interpolation with visible delimiters
let greeting = `Hello, ${userName}!`;  // ${ } in bold blue

// Special operators in strong colors
let match = value =~= pattern;         // =~= in bold magenta
class MyClass <- BaseClass {           // <- in bold dark red
    async function compute(...args) {  // ... in bold blue
        return args?.length ?? 0;      // ?. ?? in bold dark orange
    }
}

@decorator                             // @ in bold dark yellow
class Component { }
```

**Result**: Professional appearance with excellent readability on light backgrounds.

## Theme Selection Guide

### Choose "ALang for Dark+"
- ✅ You prefer dark themes
- ✅ You use Dark+ as your default theme
- ✅ You work in low-light environments
- ✅ You want light blue variables
- ✅ You like vibrant colors

### Choose "ALang for Light+"
- ✅ You prefer light themes
- ✅ You use Light+ as your default theme  
- ✅ You work in bright environments
- ✅ You want dark blue variables
- ✅ You like professional, subdued colors

## Migration from v0.2.x

### What Changed
- Old theme: "ALang Default Dark" → **Removed**
- New themes: "ALang for Dark+" and "ALang for Light+" → **Added**

### Action Required
1. Open VSCode
2. Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on Mac)
3. Type "Color Theme"
4. Select "Preferences: Color Theme"
5. Choose "ALang for Dark+" or "ALang for Light+"

### No Functionality Lost
- All ALang syntax highlighting works the same
- Special operators still enhanced
- Variables still highlighted in blue
- Only the theme selection changed

## Technical Implementation

### Theme Structure

**ALang for Dark+** (`themes/alang-dark-plus.json`):
```json
{
  "name": "ALang for Dark+",
  "type": "dark",
  "tokenColors": [
    // Only ALang-specific scopes defined
    // Base theme provides all other colors
  ]
}
```

**ALang for Light+** (`themes/alang-light-plus.json`):
```json
{
  "name": "ALang for Light+",
  "type": "light",
  "tokenColors": [
    // Only ALang-specific scopes defined
    // Base theme provides all other colors
  ]
}
```

### Scopes Defined

Both themes define colors for:
- `variable.other.readwrite` - Variables
- `variable.other.property` - Properties
- `comment.*.alang` - ALang comments
- `string.*.alang` - ALang strings
- `keyword.*.alang` - ALang keywords
- `entity.name.*.alang` - ALang functions/classes
- `keyword.operator.*.alang` - ALang operators (with dual scopes for compatibility)
- `punctuation.*.alang` - ALang punctuation

### What's NOT Defined

These use base theme colors:
- JavaScript tokens
- Python tokens
- HTML tokens
- CSS tokens
- Markdown tokens
- JSON tokens
- YAML tokens
- All other language tokens

## Compatibility Guarantee

### Other Languages ✅
```javascript
// JavaScript - uses Dark+/Light+ colors
const myVar = 42;
function myFunc() { }
class MyClass { }
```

```python
# Python - uses Dark+/Light+ colors
my_var = 42
def my_func():
    pass
class MyClass:
    pass
```

```markdown
<!-- Markdown - uses Dark+/Light+ colors -->
# Heading
**Bold** *Italic*
```

**All work perfectly!** No color conflicts, no missing highlighting.

### ALang ✅
```alang
// ALang - uses enhanced colors
let myVar = 42;
function myFunc() { }
class MyClass <- BaseClass { }
let result = obj?.prop ?? "default";
```

**Gets special treatment!** Variables in blue, special operators enhanced.

## Testing

Both themes have been tested with:
- ✅ ALang files (.alang)
- ✅ JavaScript files (.js, .jsx, .ts, .tsx)
- ✅ Python files (.py)
- ✅ HTML files (.html)
- ✅ CSS files (.css, .scss)
- ✅ Markdown files (.md)
- ✅ JSON files (.json)
- ✅ YAML files (.yml, .yaml)

**Result**: All languages display correctly with appropriate syntax highlighting.

## Future Considerations

### Adding More Themes

If users request themes based on other popular themes:
- Monokai
- Solarized Dark/Light
- Dracula
- One Dark Pro
- Material Theme

We can easily create them using the same extension pattern:
1. Set appropriate `uiTheme` (vs-dark or vs for UI)
2. Define only ALang-specific token colors
3. Let base theme handle everything else

### Customization

Users can further customize by:
1. Using VSCode's settings.json
2. Creating their own theme that extends ours
3. Using theme extensions alongside ours

## Summary

Version 0.3.0 provides:
- ✅ Two professionally-designed themes
- ✅ Dark and light options
- ✅ Light blue/dark blue variable rendering as requested
- ✅ No conflicts with other languages
- ✅ Extends familiar Dark+ and Light+ themes
- ✅ Proper VSCode theme architecture
- ✅ Future-proof and maintainable

**Recommendation**: Use "ALang for Dark+" for dark theme users, "ALang for Light+" for light theme users.
