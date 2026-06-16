# Developer Guide - ALang VSCode Extension

This guide is for developers who want to contribute to, modify, or understand the internals of the ALang VSCode extension.

## Architecture Overview

The extension consists of several key components:

```
vscode-extension/
├── package.json                    # Extension manifest
├── language-configuration.json     # Editor behavior configuration
├── syntaxes/
│   └── alang.tmLanguage.json      # TextMate grammar for syntax highlighting
├── images/
│   └── icon.png                   # Extension icon
├── examples/                      # Test files and examples
│   ├── syntax-demo.alang
│   └── basic-test.alang
├── README.md                      # User-facing documentation
├── INSTALL.md                     # Installation instructions
├── CHANGELOG.md                   # Version history
└── SYNTAX-REFERENCE.md            # Syntax reference
```

## Component Details

### 1. package.json

The extension manifest defines:

- **Metadata**: name, version, publisher, description
- **Engine compatibility**: minimum VSCode version required
- **Language contribution**: associates `.alang` files with the language
- **Grammar contribution**: links to the TextMate grammar file

Key sections:
```json
{
  "contributes": {
    "languages": [{
      "id": "alang",
      "extensions": [".alang"],
      "configuration": "./language-configuration.json"
    }],
    "grammars": [{
      "language": "alang",
      "scopeName": "source.alang",
      "path": "./syntaxes/alang.tmLanguage.json"
    }]
  }
}
```

### 2. language-configuration.json

Configures editor behavior:

- **Comment syntax**: Line (`//`) and block (`/* */`) comments
- **Bracket pairs**: `()`, `[]`, `{}`
- **Auto-closing pairs**: Brackets, quotes, backticks
- **Surrounding pairs**: For selection wrapping
- **Indentation rules**: When to increase/decrease indent
- **Word pattern**: What constitutes a "word" for selection
- **Folding markers**: Region markers for code folding

### 3. syntaxes/alang.tmLanguage.json

The heart of syntax highlighting. This is a TextMate grammar that defines:

#### Structure
```json
{
  "name": "ALang",
  "scopeName": "source.alang",
  "patterns": [
    { "include": "#comments" },
    { "include": "#strings" },
    // ... other top-level patterns
  ],
  "repository": {
    "comments": { /* comment patterns */ },
    "strings": { /* string patterns */ },
    // ... other pattern definitions
  }
}
```

#### Pattern Types

**1. Match Patterns** - Simple regex matches:
```json
{
  "name": "keyword.control.alang",
  "match": "\\b(if|else|while)\\b"
}
```

**2. Begin-End Patterns** - Multi-line constructs:
```json
{
  "name": "string.quoted.double.alang",
  "begin": "\"",
  "end": "\"",
  "patterns": [
    { "include": "#string-escapes" }
  ]
}
```

**3. Capture Groups** - Extract and name parts of matches:
```json
{
  "match": "\\b(class)\\s+([a-zA-Z_][a-zA-Z0-9_]*)",
  "captures": {
    "1": { "name": "storage.type.class.alang" },
    "2": { "name": "entity.name.type.class.alang" }
  }
}
```

## Mapping Token Types to Scopes

The extension maps ALang token types (from `AsulLexer.h`) to TextMate scopes:

| Token Type | TextMate Scope | Description |
|------------|----------------|-------------|
| `Let`, `Var`, `Const` | `storage.type.alang` | Variable declarations |
| `Function`, `Fn` | `storage.type.function.alang` | Function keyword |
| `If`, `Else`, `While`, etc. | `keyword.control.alang` | Control flow |
| `Async`, `Await`, `Go` | `keyword.control.async.alang` | Async keywords |
| `Class`, `Interface` | `storage.type.class.alang` | OOP keywords |
| `Import`, `From`, `Export` | `keyword.other.alang` | Module keywords |
| `True`, `False` | `constant.language.boolean.alang` | Boolean literals |
| `Null` | `constant.language.null.alang` | Null literal |
| `Number` | `constant.numeric.alang` | Numeric literals |
| `String` | `string.quoted.*.alang` | String literals |
| `Identifier` | `entity.name.function.alang` (context) | Identifiers |

### Special Operators

ALang has several unique operators that require special handling:

```json
{
  "name": "keyword.operator.interface-match.alang",
  "match": "=~="
},
{
  "name": "keyword.operator.nullish.alang",
  "match": "(\\?\\?=?|\\?\\.|\\?)"
},
{
  "name": "keyword.operator.arrow.alang",
  "match": "(<-|->|=>)"
}
```

## Adding New Token Types

When new tokens are added to `AsulLexer.h`, update the grammar:

1. **Add the keyword** to the appropriate pattern in `keywords`:
   ```json
   {
     "name": "keyword.control.alang",
     "match": "\\b(if|else|...|newkeyword)\\b"
   }
   ```

2. **Choose the appropriate scope** based on the token's purpose:
   - Control flow → `keyword.control.*`
   - Storage/types → `storage.type.*`
   - Constants → `constant.*`
   - Operators → `keyword.operator.*`

3. **Test** with examples to ensure highlighting works correctly.

## Testing the Grammar

### Manual Testing

1. **Install the extension** (see INSTALL.md)

2. **Create test files** with various language constructs:
   ```alang
   // Test file
   let x = 42;
   function test() { return x; }
   ```

3. **Verify highlighting** matches expectations

4. **Use VSCode's scope inspector**:
   - Open Command Palette (`Ctrl+Shift+P`)
   - Run "Developer: Inspect Editor Tokens and Scopes"
   - Click on any token to see its scopes

### Automated Testing

For TextMate grammars, use the `vscode-tmgrammar-test` tool:

```bash
npm install -g vscode-tmgrammar-test

# Run tests
vscode-tmgrammar-test -s source.alang \
  -g syntaxes/alang.tmLanguage.json \
  examples/syntax-demo.alang
```

## Best Practices

### 1. Scope Naming

Follow TextMate scope naming conventions:
- Use hierarchical naming: `keyword.control.conditional.alang`
- Use standard categories: `keyword`, `string`, `comment`, `constant`, etc.
- Add `.alang` suffix for language-specific scopes

### 2. Regex Patterns

- **Escape special characters**: `\\(`, `\\)`, `\\[`, `\\]`
- **Use word boundaries**: `\\b` for keywords
- **Be specific**: Avoid overly broad patterns
- **Consider performance**: Complex patterns can slow down highlighting

### 3. Pattern Order

Patterns are matched in order. Place more specific patterns first:

```json
"patterns": [
  { "include": "#comments" },      // First: comments can contain anything
  { "include": "#strings" },       // Second: strings can contain keywords
  { "include": "#keywords" },      // Third: keywords
  { "include": "#operators" }      // Last: general operators
]
```

### 4. Testing Edge Cases

Test with:
- **Nested constructs**: Strings with interpolation, nested blocks
- **Edge cases**: Empty strings, multiline comments, escape sequences
- **Invalid syntax**: Grammar should be robust to incomplete code

## Debugging

### Enable Grammar Debugging

1. Open VSCode settings
2. Search for "textmate"
3. Enable "Developer: Log Grammar Token"

### Common Issues

**Highlighting doesn't work:**
- Check JSON syntax is valid
- Verify regex patterns are correctly escaped
- Ensure scopes are properly named

**Highlighting is incorrect:**
- Use scope inspector to see actual scopes
- Check pattern order in the grammar
- Verify regex matches expected text

**Performance issues:**
- Simplify complex regex patterns
- Avoid backtracking in patterns
- Use `begin`/`end` instead of large `match` patterns

## Contributing

### Workflow

1. **Fork** the repository
2. **Create a branch** for your changes
3. **Edit** the grammar files
4. **Test** with example files
5. **Commit** with descriptive messages
6. **Submit** a pull request

### Commit Message Format

```
Add support for [feature]

- Added pattern for [specific construct]
- Updated scope for [token type]
- Tests: [what was tested]
```

### Code Review Checklist

Before submitting:
- [ ] All JSON files are valid
- [ ] Regex patterns are properly escaped
- [ ] New scopes follow naming conventions
- [ ] Changes tested with example files
- [ ] Documentation updated if needed
- [ ] CHANGELOG.md updated

## Resources

### TextMate Grammar
- [TextMate Language Grammar Guide](https://macromates.com/manual/en/language_grammars)
- [Scope Naming Conventions](https://www.sublimetext.com/docs/scope_naming.html)
- [VSCode Syntax Highlighting Guide](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide)

### Regular Expressions
- [Oniguruma Regex](https://github.com/kkos/oniguruma/blob/master/doc/RE) - TextMate uses Oniguruma
- [RegExr](https://regexr.com/) - Regex tester (note: uses JavaScript regex, not Oniguruma)

### VSCode Extension Development
- [VSCode Extension API](https://code.visualstudio.com/api)
- [Language Extension Overview](https://code.visualstudio.com/api/language-extensions/overview)
- [Publishing Extensions](https://code.visualstudio.com/api/working-with-extensions/publishing-extension)

## Release Process

### Version Numbering

Follow [Semantic Versioning](https://semver.org/):
- MAJOR.MINOR.PATCH (e.g., 0.1.0)
- Increment MAJOR for breaking changes
- Increment MINOR for new features
- Increment PATCH for bug fixes

### Publishing Steps

1. **Update version** in `package.json`
2. **Update CHANGELOG.md** with changes
3. **Commit** version bump
4. **Tag** the release: `git tag v0.1.0`
5. **Build** the package: `vsce package`
6. **Test** the .vsix file
7. **Publish**: `vsce publish`
8. **Push** tags: `git push --tags`

## Future Enhancements

Potential improvements for the extension:

### Language Features
- [ ] IntelliSense/autocomplete support
- [ ] Go to definition
- [ ] Find all references
- [ ] Rename symbol
- [ ] Hover documentation

### Syntax Highlighting
- [ ] Semantic highlighting (using language server)
- [ ] Better template literal interpolation
- [ ] Syntax-aware folding
- [ ] Custom color themes

### Tooling
- [ ] Snippets for common patterns
- [ ] Linter integration
- [ ] Formatter integration
- [ ] Debugger support

## Contact

For questions or discussions about the extension:
- GitHub Issues: https://github.com/Asultop/AsulLang/issues
- Repository: https://github.com/Asultop/AsulLang
