# Build Output Directory

This directory contains the packaged `.vsix` files for the ALang VSCode extension.

## Files

After running `./build.sh` or `npm run build`, you'll find:

- `alang-language-support-<version>.vsix` - Versioned package
- `alang-language-support-latest.vsix` - Symlink to latest version

## Installation

Install the extension using:

```bash
code --install-extension alang-language-support-latest.vsix
```

Or through VSCode UI:
1. Extensions panel (`Ctrl+Shift+X`)
2. `...` menu → `Install from VSIX...`
3. Select the `.vsix` file

## Build

To generate a new .vsix package:

```bash
cd ..
./build.sh
```

See the parent directory's README for more details.
