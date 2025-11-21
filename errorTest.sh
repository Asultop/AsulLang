#!/bin/bash

# ErrorExample Test Script
# This script runs all error example files in the ErrorExample directory

echo "================================"
echo "Running ALang Error Examples"
echo "================================"
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Path to alang executable (same directory as this script)
ALANG="$SCRIPT_DIR/alang"

# Error examples directory
ERROR_DIR="$SCRIPT_DIR/Example/ErrorExample"

# Array of error example files
error_files=(
    "assign_undefined.alang"
    "await_non_promise.alang"
    "call_non_function.alang"
    "expect_property_name.alang"
    "import_not_found.alang"
    "import_private_symbol.alang"
    "missing_import_math.alang"
    "index_assignment_non_array.alang"
    "index_non_array.alang"
    "index_out_of_range.alang"
    "interface_with_body.alang"
    "invalid_assignment_target.alang"
    "invalid_interpolation.alang"
    "missing_interface_method.alang"
    "missing_multiple_interface.alang"
    "property_access_non_object.alang"
    "spread_element_not_array.alang"
    "spread_value_not_object.alang"
    "undefined_variable.alang"
    "unterminated_string.alang"
)

# Counter for passed/failed tests
total=0
errors=0

# Run each error example
for file in "${error_files[@]}"; do
    echo "----------------------------------------"
    echo "Testing: $file"
    echo "----------------------------------------"
    
    # Run alang with the error example file
    "$ALANG" "$ERROR_DIR/$file"
    
    # Capture the exit code
    exit_code=$?
    
    total=$((total + 1))
    
    if [ $exit_code -ne 0 ]; then
        echo "✓ Expected error caught"
        errors=$((errors + 1))
    else
        echo "✗ No error (unexpected)"
    fi
    
    echo ""
done

echo "================================"
echo "Test Summary"
echo "================================"
echo "Total files tested: $total"
echo "Errors caught: $errors"
echo "No errors: $((total - errors))"
echo "================================"
