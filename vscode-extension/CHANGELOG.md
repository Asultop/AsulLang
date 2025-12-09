# Change Log

All notable changes to the "alang-language-support" extension will be documented in this file.

## [0.2.0] - 2025-12-09

### Added
- **Color Theme**: Default dark theme with special highlighting for ALang operators
  - Interface match operator (=~=) in bold pink
  - Arrow operators (<-, ->, =>) in bold red
  - Nullish operators (?., ??) in bold gold
  - Spread operator (...) in bold blue
  - Decorator operator (@) in bold yellow
- **Language Server**: Full LSP implementation with:
  - Real-time syntax checking and diagnostics
  - Go to definition for functions, classes, interfaces, and variables
  - Auto-completion for keywords and symbols
  - Error highlighting for unclosed strings
- **Configuration**: Settings for language server behavior
- Documentation for language server features

### Changed
- Updated extension version to 0.2.0
- Enhanced README with new feature descriptions
- Added activation events for language server

## [0.1.0] - 2025-12-09

### Added
- Initial release of ALang Language Support extension
- Syntax highlighting for all ALang language features:
  - Keywords (let, var, const, function, class, interface, etc.)
  - Control flow statements (if, else, while, for, foreach, switch, etc.)
  - Async/await keywords (async, await, go)
  - All operators including special operators (=~=, ?., ??, etc.)
  - Literals (strings, numbers, booleans, null)
  - Comments (line and block comments)
  - String interpolation with template literals
- Language configuration:
  - Bracket matching
  - Auto-closing pairs
  - Comment toggling
  - Code folding
- File association for .alang files
- README with usage instructions
