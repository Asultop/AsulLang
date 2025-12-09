#!/bin/bash

# Function Test Script
# This script runs all functional example files in the Example directory

echo "================================"
echo "Running ALang Function Examples"
echo "================================"
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Path to alang executable (from build directory)
ALANG="$SCRIPT_DIR/build/alang"

# Example directory
EXAMPLE_DIR="$SCRIPT_DIR/Example"

# Array of functional example files
func_files=(
    "builtins_test.alang"
    "comment_examples.alang"
    "computedProps.alang"
    "defaultParamsExample.alang"
    "doWhileExample.alang"
    "emptySemicolons.alang"
    "array_methods_test.alang"
    "evalExample.alang"
    "example.alang"
    "export_test.alang"
    "fileImportExample.alang"
    "foreachExample.alang"
    "foreachAdvanced.alang"
    "goExample.alang"
    "importExample.alang"
    "mathExample.alang"
    "networkExample.alang"
    "incrementExample.alang"
    "interfaceExample.alang"
    "interfaceValidationTest.alang"
    "interpolationExample.alang"
    "lambdaExample.alang"
    "overloadTest.alang"
    "overrideTest.alang"
    "quoteExample.alang"
    "quote_complex.alang"
    "quote_edit_apply.alang"
    "reflection_test.alang"
    "restParamsExample.alang"
    "restParamsAdvanced.alang"
    "spread_examples.alang"
    "switchExample.alang"
    "switchAdvanced.alang"
    "ternaryExample.alang"
    "try_catchExample.alang",
    "type_and_match_example.alang",
    "uuid_url_example.alang",
    "type_comparison.alang"
    "map_example.alang"
    "containers_example.alang"
    "STLExample.alang"
    "staticMethodExample.alang"
    "bitwiseExample.alang"
    "fileIOExample.alang"
    "fileIOClassExample.alang"
    "fileIOAdvancedExample.alang"
    "dateTimeExample.alang"
    "timezone_test.alang"
    "jsonExample.alang"
    "OSExample.alang"
    "io_os_test.alang"
    "signal_test.alang"
    "stringExample.alang"
    "test_lazy.alang"
    "test_wildcard.alang"
    "test_wildcard_std.alang"
    "setExample.alang"
    "stackExample.alang"
    "priorityQueueExample.alang"
    "binarySearchExample.alang"
    "string_methods_extended.alang"
    "encoding_test.alang"
    "socket_test.alang"
    "xml_yaml_example.alang"
    "http_test.alang"
    "http_sendfail_test.alang"
    "crypto_example.alang"
    "crypto_hash_demo.alang"
    "stream_example.alang"
    "csvExample.alang"
    "array_select_methods.alang"
    "string_methods_test.alang"
    "math_methods_test.alang"
    "object_methods_test.alang"
    "path_enhancements_test.alang"
    "encoding_enhancements_test.alang"
    "promise_utilities_test.alang"
    "log_test.alang"
    "test_framework_test.alang"
    "crypto_enhancements_test.alang"
    "language_runtime_test.alang"
    "type_system_iterator_test.alang"
    "operator_overload_test.alang"
    "ffi_test.alang"
)

# Counter for passed/failed tests
total=0
passed=0
failed=0

# Run each functional example
for file in "${func_files[@]}"; do
    echo "----------------------------------------"
    echo "Testing: $file"
    echo "----------------------------------------"
    
    # Run alang with the functional example file
    "$ALANG" "$EXAMPLE_DIR/$file"
    
    # Capture the exit code
    exit_code=$?
    
    total=$((total + 1))
    
    if [ $exit_code -eq 0 ]; then
        echo "✓ Test passed"
        passed=$((passed + 1))
    else
        echo "✗ Test failed (exit code: $exit_code)"
        failed=$((failed + 1))
    fi
    
    echo ""
done

echo "================================"
echo "Test Summary"
echo "================================"
echo "Total files tested: $total"
echo "Passed: $passed"
echo "Failed: $failed"
echo "================================"

# Exit with error if any test failed
if [ $failed -gt 0 ]; then
    exit 1
fi
