# Change Log

All notable changes to the "alang-language-support" extension will be documented in this file.

## [0.2.6] - 2025-12-12

### Fixed
- **CI/CD Build Failure**: Fixed Node.js compatibility issue
  - Updated GitHub Actions workflow from Node.js v18 to v20 LTS
  - Resolved `ReferenceError: File is not defined` error in undici library
  - Build process now stable and reliable

## [0.2.5] - 2025-12-12

### Changed
- **GitHub Actions Workflow**: Updated to latest action versions
  - `actions/upload-artifact@v3` → `actions/upload-artifact@v4`
  - Removed deprecation warnings
  - All actions now using latest stable versions

## [0.2.4] - 2025-12-12

### Added
- **Theme Compatibility Review**: Comprehensive review document
  - Complete analysis of theme compatibility across all VSCode themes
  - String interpolation rendering verification
  - LSP parsing support evaluation
  - Testing recommendations and status matrix
  - Identified improvement areas and future enhancements
- **Comprehensive Test File**: New `examples/comprehensive-test.alang`
  - Tests all features in one file
  - Variable highlighting across themes
  - String interpolation with all patterns
  - LSP go-to-definition for functions, classes, variables
  - All special operators and keywords
  - Complete testing instructions

### Verified
- ✅ Theme compatibility: Works in Dark+, Light+, Monokai, Solarized, and all themes
- ✅ String interpolation: Delimiters and expressions properly highlighted
- ✅ LSP features: Go-to-definition, auto-completion, syntax checking functional
- ✅ Variable highlighting: Universal support using standard scopes
- ✅ Special operators: Custom colors in "ALang Default Dark" theme

### Status
- **Theme Compatibility**: ✅ Excellent - Works in all themes
- **String Interpolation**: ✅ Excellent - Full syntax support with delimiter highlighting
- **LSP Support**: ✅ Good - Basic features working, advanced features identified for future

## [0.2.3] - 2025-12-12

### Added
- **Standard Scope Variable Highlighting**: Variables now highlighted in all themes
  - Added `variable.other.readwrite` scope for variables (light blue in most themes)
  - Added `variable.other.property` scope for object properties
  - Changed `this` keyword to use standard `variable.language.this` scope
  - **Works in ANY VSCode theme** without requiring custom theme activation
  - Variables appear in light blue (#9CDCFE) in Dark+ theme
  - Variables appear in dark blue (#001080) in Light+ theme
- **Variable Highlighting Demo**: New `examples/variable-highlighting-demo.alang`
  - Demonstrates all variable patterns and highlighting
  - Shows declarations, properties, method chaining, etc.
- **Standard Scopes Documentation**: New `STANDARD-SCOPES.md`
  - Explains standard vs. language-specific scope strategy
  - Lists all standard scopes used for ALang
  - Migration guide for users and developers

### Changed
- Variables and properties now use standard TextMate scopes instead of `.alang` suffix
- Core language elements (variables, this) work universally across all themes
- ALang-specific features (special operators) still use `.alang` suffix for custom theming

### Benefits
- ✅ Variables highlighted automatically in any theme
- ✅ No need to activate "ALang Default Dark" theme for basic highlighting
- ✅ Consistent with how other languages display variables
- ✅ Custom theme still available for enhanced operator highlighting

## [0.2.2] - 2025-12-12

### Fixed
- **Color Theme Compatibility**: Fixed theme causing other languages to lose rendering
  - Removed editor-wide color overrides (`editor.background`, `editor.foreground`)
  - Theme now only defines ALang-specific token colors
  - Compatible with all base VSCode themes
- **String Interpolation Highlighting**: Enhanced template literal interpolation
  - Added distinct colors for `${}` delimiters (bold blue)
  - Proper syntax highlighting inside interpolation expressions
  - All language features work correctly inside `${...}`

### Added
- **String Interpolation Demo**: New `examples/string-interpolation-demo.alang`
  - Comprehensive examples of all interpolation patterns
  - Shows nested expressions, function calls, operators in interpolation
  - Demonstrates complex interpolation scenarios

### Changed
- Enhanced TextMate grammar for better interpolation delimiter capture
- Updated color theme with interpolation-specific scopes

## [0.2.1] - 2025-12-09

### Added
- **Automated Build System**: Complete build automation with `build.sh`
  - Automatic dependency installation
  - TypeScript compilation
  - .vsix package generation in `build/` directory
  - No manual intervention required
- **Build Output**: `.vsix` files now generated in `vscode-extension/build/`
  - Versioned package (e.g., `alang-language-support-0.2.1.vsix`)
  - Symlink to latest version (`alang-language-support-latest.vsix`)
- **NPM Scripts**: Added convenience scripts
  - `npm run build` - Full automated build
  - `npm run build:quick` - Quick build (assumes deps installed)
  - `npm run package` - Package to build/ directory
- **Documentation**: Added `build/README.md` with build instructions
- **Dev Dependency**: Added `@vscode/vsce` for packaging

### Changed
- Updated build process to output to `build/` directory
- Enhanced `build.sh` with full automation
- Updated README with automated build instructions
- Modified .gitignore to track build directory structure

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
