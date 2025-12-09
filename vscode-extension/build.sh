#!/bin/bash

# ALang VSCode Extension - Build and Package Script
# This script helps build, validate, and package the VSCode extension

set -e

EXTENSION_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$EXTENSION_DIR"

echo "=========================================="
echo "ALang VSCode Extension Build Script"
echo "=========================================="
echo ""

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to validate JSON files
validate_json() {
    echo "Validating JSON files..."
    
    for file in package.json language-configuration.json syntaxes/alang.tmLanguage.json; do
        if [ -f "$file" ]; then
            if python3 -m json.tool "$file" > /dev/null 2>&1; then
                echo "  ✓ $file is valid"
            else
                echo "  ✗ $file is INVALID!"
                exit 1
            fi
        else
            echo "  ✗ $file not found!"
            exit 1
        fi
    done
    
    echo ""
}

# Function to check required files
check_files() {
    echo "Checking required files..."
    
    required_files=(
        "package.json"
        "language-configuration.json"
        "syntaxes/alang.tmLanguage.json"
        "README.md"
        "CHANGELOG.md"
        "images/icon.png"
    )
    
    for file in "${required_files[@]}"; do
        if [ -f "$file" ]; then
            echo "  ✓ $file exists"
        else
            echo "  ✗ $file is missing!"
            exit 1
        fi
    done
    
    echo ""
}

# Function to display extension info
show_info() {
    echo "Extension Information:"
    echo "  Name: $(grep -o '"name":[[:space:]]*"[^"]*"' package.json | head -1 | cut -d'"' -f4)"
    echo "  Display Name: $(grep -o '"displayName":[[:space:]]*"[^"]*"' package.json | head -1 | cut -d'"' -f4)"
    echo "  Version: $(grep -o '"version":[[:space:]]*"[^"]*"' package.json | head -1 | cut -d'"' -f4)"
    echo "  Publisher: $(grep -o '"publisher":[[:space:]]*"[^"]*"' package.json | head -1 | cut -d'"' -f4)"
    echo ""
}

# Function to package the extension
package_extension() {
    echo "Packaging extension..."
    
    if ! command_exists vsce; then
        echo "  ⚠ vsce not found. Install it with: npm install -g vsce"
        echo ""
        echo "Skipping packaging step."
        echo "To package manually, run: vsce package"
        return 1
    fi
    
    vsce package
    
    if [ $? -eq 0 ]; then
        echo ""
        echo "  ✓ Extension packaged successfully!"
        VSIX_FILE=$(ls -t *.vsix 2>/dev/null | head -1)
        if [ -n "$VSIX_FILE" ]; then
            echo "  Package: $VSIX_FILE"
            echo "  Size: $(du -h "$VSIX_FILE" | cut -f1)"
        fi
    else
        echo "  ✗ Packaging failed!"
        exit 1
    fi
    
    echo ""
}

# Function to show installation instructions
show_install_instructions() {
    echo "=========================================="
    echo "Installation Instructions"
    echo "=========================================="
    echo ""
    echo "To install the extension:"
    echo ""
    echo "Method 1: Using VSCode UI"
    echo "  1. Open VSCode"
    echo "  2. Go to Extensions (Ctrl+Shift+X)"
    echo "  3. Click '...' menu → 'Install from VSIX...'"
    echo "  4. Select the .vsix file"
    echo ""
    echo "Method 2: Using command line"
    VSIX_FILE=$(ls -t *.vsix 2>/dev/null | head -1)
    if [ -n "$VSIX_FILE" ]; then
        echo "  code --install-extension $VSIX_FILE"
    else
        echo "  code --install-extension <extension.vsix>"
    fi
    echo ""
    echo "Method 3: Manual installation (development)"
    echo "  See INSTALL.md for detailed instructions"
    echo ""
}

# Function to run tests
run_tests() {
    echo "Running tests..."
    
    # Check if test examples exist
    if [ -d "examples" ]; then
        echo "  Found example files:"
        ls examples/*.alang 2>/dev/null | while read file; do
            echo "    - $(basename "$file")"
        done
    else
        echo "  ⚠ No examples directory found"
    fi
    
    echo ""
}

# Main execution
main() {
    # Parse command line arguments
    case "${1:-}" in
        validate|check)
            validate_json
            check_files
            show_info
            ;;
        package|build)
            validate_json
            check_files
            show_info
            package_extension
            show_install_instructions
            ;;
        test)
            run_tests
            ;;
        clean)
            echo "Cleaning build artifacts..."
            rm -f *.vsix
            echo "  ✓ Cleaned"
            echo ""
            ;;
        help|--help|-h)
            echo "Usage: $0 [command]"
            echo ""
            echo "Commands:"
            echo "  validate  - Validate JSON files and check required files"
            echo "  package   - Validate, package, and show install instructions"
            echo "  test      - Run tests (check example files)"
            echo "  clean     - Remove build artifacts (.vsix files)"
            echo "  help      - Show this help message"
            echo ""
            echo "Default (no command): validate"
            ;;
        *)
            validate_json
            check_files
            show_info
            ;;
    esac
}

# Run main function
main "$@"
