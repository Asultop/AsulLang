# ALang VSCode Extension - Quick Reference

## Overview

This VSCode extension provides comprehensive syntax highlighting for the ALang scripting language. It recognizes all language constructs defined in the ALang Token Provider (AsulLexer).

## Supported Token Types

### Keywords

#### Variable Declaration
- `let` - Block-scoped variable
- `var` - Function-scoped variable
- `const` - Constant (immutable binding)

#### Function Declaration
- `function` - Full function keyword
- `fn` - Short function keyword
- `return` - Return statement

#### Control Flow
- `if`, `else` - Conditional statements
- `while`, `do` - While loops
- `for` - For loops
- `foreach`, `in` - Foreach iteration
- `break`, `continue` - Loop control
- `switch`, `case`, `default` - Switch statements

#### Object-Oriented Programming
- `class` - Class definition
- `interface` - Interface definition
- `extends` - Class extension (inheritance)
- `new` - Object instantiation
- `static` - Static members

#### Async Programming
- `async` - Async function modifier
- `await` - Await expression
- `go` - Fire-and-forget async execution

#### Error Handling
- `try` - Try block
- `catch` - Catch block
- `finally` - Finally block
- `throw` - Throw exception

#### Module System
- `import` - Import statement
- `from` - From import
- `as` - Import alias
- `export` - Export declaration

#### Advanced Features
- `match` - Pattern matching
- `yield` - Generator yield

### Literals

#### Boolean
- `true` - Boolean true
- `false` - Boolean false

#### Null
- `null` - Null value

#### Numbers
- Integers: `42`, `100`
- Floats: `3.14`, `2.718`
- Scientific notation: `1.5e-10`, `6.022e23`
- Hexadecimal: `0xFF`, `0x1A2B`
- Binary: `0b1010`, `0b1111`
- Octal: `0o777`, `0o123`

#### Strings
- Single quotes: `'hello'`
- Double quotes: `"world"`
- Template literals: `` `Hello ${name}` ``
- Escape sequences: `\n`, `\t`, `\r`, `\\`, `\"`, `\'`, `\0`

### Special Constructs

#### this
- `this` - Current object reference

### Operators

#### Arithmetic
- `+` - Addition
- `-` - Subtraction
- `*` - Multiplication
- `/` - Division
- `%` - Modulo

#### Comparison
- `<` - Less than
- `<=` - Less than or equal
- `>` - Greater than
- `>=` - Greater than or equal
- `==` - Equal
- `!=` - Not equal
- `===` - Strict equal
- `!==` - Strict not equal

#### Logical
- `&&` - Logical AND
- `||` - Logical OR
- `!` - Logical NOT

#### Bitwise
- `&` - Bitwise AND
- `|` - Bitwise OR
- `^` - Bitwise XOR
- `~` - Bitwise NOT
- `<<` - Left shift
- `>>` - Right shift

#### Assignment
- `=` - Assignment
- `+=` - Add and assign
- `-=` - Subtract and assign
- `*=` - Multiply and assign
- `/=` - Divide and assign
- `%=` - Modulo and assign
- `<<=` - Left shift and assign
- `>>=` - Right shift and assign
- `&=` - Bitwise AND and assign
- `|=` - Bitwise OR and assign
- `^=` - Bitwise XOR and assign

#### Increment/Decrement
- `++` - Increment
- `--` - Decrement

#### Special Operators
- `=~=` - Interface/class descriptor matching
- `?.` - Optional chaining
- `??` - Nullish coalescing
- `??=` - Nullish coalescing assignment
- `&&=` - Logical AND assignment
- `||=` - Logical OR assignment
- `<-` - Inheritance operator
- `->` - Arrow operator
- `=>` - Lambda arrow
- `...` - Spread/rest operator
- `@` - Decorator operator
- `?` - Ternary conditional

### Comments

#### Line Comments
```alang
// C-style single-line comment
# Python-style single-line comment
```

#### Block Comments
```alang
/* C-style block comment */

"""
Pure triple-double-quote block comment
"""

'''
Pure triple-single-quote block comment
'''

#"""
Python-style triple-double-quote block comment
"""

#'''
Python-style triple-single-quote block comment
'''
```

### Punctuation

- `(` `)` - Parentheses
- `[` `]` - Brackets
- `{` `}` - Braces
- `,` - Comma
- `;` - Semicolon
- `:` - Colon
- `.` - Dot (member access)

## Editor Features

### Bracket Matching
Automatically highlights matching brackets, parentheses, and braces.

### Auto-Closing Pairs
Automatically closes:
- `{` with `}`
- `[` with `]`
- `(` with `)`
- `"` with `"`
- `'` with `'`
- `` ` `` with `` ` ``

### Comment Toggling
- Line comment: `Ctrl+/` (or `Cmd+/` on macOS)
- Block comment: `Shift+Alt+A` (or `Shift+Option+A` on macOS)

### Code Folding
Supports folding for:
- Code blocks (`{...}`)
- Arrays (`[...]`)
- Objects (`{...}`)
- Regions (`// #region` ... `// #endregion`)

## Color Scopes

The extension uses standard TextMate scopes for syntax highlighting:

- **Keywords**: `keyword.control.alang`, `storage.type.alang`
- **Strings**: `string.quoted.*.alang`, `string.template.alang`
- **Numbers**: `constant.numeric.alang`
- **Comments**: `comment.line.*.alang`, `comment.block.alang`
- **Functions**: `entity.name.function.alang`, `storage.type.function.alang`
- **Classes**: `entity.name.type.class.alang`, `storage.type.class.alang`
- **Operators**: `keyword.operator.*.alang`
- **Constants**: `constant.language.*.alang`
- **Variables**: `variable.language.*.alang`

These scopes are compatible with most VSCode color themes.

## Examples

See the `examples/` directory for comprehensive syntax highlighting demonstrations:
- `syntax-demo.alang` - Complete feature showcase
- `basic-test.alang` - Basic language features

## Customization

To customize syntax highlighting colors, add theme overrides to your VS Code `settings.json`:

```json
{
  "editor.tokenColorCustomizations": {
    "textMateRules": [
      {
        "scope": "keyword.operator.interface-match.alang",
        "settings": {
          "foreground": "#FF6B6B",
          "fontStyle": "bold"
        }
      }
    ]
  }
}
```

## Technical Details

### File Association
- **Language ID**: `alang`
- **File Extension**: `.alang`
- **MIME Type**: `text/x-alang`

### Grammar Details
- **Scope Name**: `source.alang`
- **Grammar Type**: TextMate JSON
- **Grammar File**: `syntaxes/alang.tmLanguage.json`

### Configuration
- **Language Configuration**: `language-configuration.json`
- **Package Manifest**: `package.json`

## Troubleshooting

### Colors not matching expectations?
Make sure your color theme supports the TextMate scopes used by the extension. Try switching to a different theme or customize the colors manually.

### Syntax highlighting incorrect?
Please report an issue at https://github.com/Asultop/AsulLang/issues with:
- A code sample
- Screenshot of the incorrect highlighting
- Your VSCode version and theme

## Contributing

To contribute improvements to the syntax highlighting:

1. Edit `syntaxes/alang.tmLanguage.json`
2. Test with the example files
3. Submit a pull request

For more information about TextMate grammars:
- https://macromates.com/manual/en/language_grammars
- https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide
