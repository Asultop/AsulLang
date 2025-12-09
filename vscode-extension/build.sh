#!/bin/bash

# ALang VSCode Extension - Automated Build and Package Script
# This script automatically builds and packages the extension to build/ directory

set -e

EXTENSION_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$EXTENSION_DIR/build"
cd "$EXTENSION_DIR"

echo "=========================================="
echo "ALang VSCode Extension Build Script"
echo "=========================================="
echo ""
echo "Build directory: $BUILD_DIR"
echo ""

# Function to install dependencies
install_dependencies() {
    echo "Installing dependencies..."
    
    # Check if npm is available
    if ! command_exists npm; then
        echo "  ✗ npm not found! Please install Node.js and npm."
        exit 1
    fi
    
    # Install root dependencies
    if [ -f "package.json" ]; then
        echo "  Installing root dependencies..."
        npm install --silent 2>&1 | tail -3
    fi
    
    # Install client dependencies
    if [ -f "client/package.json" ]; then
        echo "  Installing client dependencies..."
        cd client && npm install --silent 2>&1 | tail -3 && cd ..
    fi
    
    # Install server dependencies
    if [ -f "server/package.json" ]; then
        echo "  Installing server dependencies..."
        cd server && npm install --silent 2>&1 | tail -3 && cd ..
    fi
    
    echo "  ✓ Dependencies installed"
    echo ""
}

# Function to compile TypeScript
compile_typescript() {
    echo "Compiling TypeScript..."
    
    if ! command_exists tsc; then
        echo "  ⚠ tsc not found locally, using npx..."
        npx tsc -b
    else
        tsc -b
    fi
    
    if [ $? -eq 0 ]; then
        echo "  ✓ TypeScript compiled successfully"
        
        # Verify output files exist
        if [ -f "client/out/extension.js" ] && [ -f "server/out/server.js" ]; then
            echo "  ✓ Output files verified:"
            echo "    - client/out/extension.js"
            echo "    - server/out/server.js"
        else
            echo "  ✗ Output files not found!"
            exit 1
        fi
    else
        echo "  ✗ TypeScript compilation failed!"
        exit 1
    fi
    
    echo ""
}

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
    echo "Packaging extension to build directory..."
    
    # Create build directory if it doesn't exist
    mkdir -p "$BUILD_DIR"
    
    # Check if vsce is available
    if ! command_exists vsce; then
        echo "  ℹ vsce not found globally, installing locally..."
        npm install --no-save vsce@latest
        VSCE_CMD="npx vsce"
    else
        VSCE_CMD="vsce"
    fi
    
    # Get version from package.json
    VERSION=$(grep -o '"version":[[:space:]]*"[^"]*"' package.json | head -1 | cut -d'"' -f4)
    VSIX_NAME="alang-language-support-${VERSION}.vsix"
    
    # Package the extension
    echo "  Creating package: $VSIX_NAME"
    $VSCE_CMD package -o "$BUILD_DIR/$VSIX_NAME"
    
    if [ $? -eq 0 ]; then
        echo ""
        echo "  ✓ Extension packaged successfully!"
        echo "  📦 Package: build/$VSIX_NAME"
        echo "  📊 Size: $(du -h "$BUILD_DIR/$VSIX_NAME" | cut -f1)"
        echo ""
        
        # Create a symlink to latest
        cd "$BUILD_DIR"
        ln -sf "$VSIX_NAME" "alang-language-support-latest.vsix"
        cd "$EXTENSION_DIR"
        echo "  ✓ Created symlink: build/alang-language-support-latest.vsix"
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
    echo "The .vsix package has been created in: build/"
    echo ""
    echo "To install the extension:"
    echo ""
    echo "Method 1: Using VSCode UI"
    echo "  1. Open VSCode"
    echo "  2. Go to Extensions (Ctrl+Shift+X)"
    echo "  3. Click '...' menu → 'Install from VSIX...'"
    echo "  4. Navigate to build/ and select the .vsix file"
    echo ""
    echo "Method 2: Using command line"
    VERSION=$(grep -o '"version":[[:space:]]*"[^"]*"' package.json | head -1 | cut -d'"' -f4)
    echo "  code --install-extension build/alang-language-support-${VERSION}.vsix"
    echo ""
    echo "  Or use the latest symlink:"
    echo "  code --install-extension build/alang-language-support-latest.vsix"
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
            rm -rf "$BUILD_DIR"/*.vsix
            rm -rf client/out server/out
            rm -rf node_modules client/node_modules server/node_modules
            echo "  ✓ Cleaned build artifacts"
            echo ""
            ;;
        full|all)
            echo "=== Full Build Process ==="
            echo ""
            validate_json
            check_files
            show_info
            install_dependencies
            compile_typescript
            package_extension
            show_install_instructions
            ;;
        help|--help|-h)
            echo "Usage: $0 [command]"
            echo ""
            echo "Commands:"
            echo "  validate  - Validate JSON files and check required files"
            echo "  full|all  - Complete build: install deps, compile, package (default)"
            echo "  package   - Package extension to build/ directory"
            echo "  test      - Run tests (check example files)"
            echo "  clean     - Remove all build artifacts"
            echo "  help      - Show this help message"
            echo ""
            echo "Default (no command): full build"
            ;;
        *)
            # Default: full build
            echo "=== Full Build Process ==="
            echo ""
            validate_json
            check_files
            show_info
            install_dependencies
            compile_typescript
            package_extension
            show_install_instructions
            ;;
    esac
}

# Run main function
main "$@"
