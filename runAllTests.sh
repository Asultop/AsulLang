#!/bin/bash

# All Tests Runner
# This script runs both error tests and functional tests

echo "========================================"
echo "   Running All ALang Tests"
echo "========================================"
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Run error tests
echo ">>> Running Error Tests..."
echo ""
"$SCRIPT_DIR/errorTest.sh"
error_result=$?
echo ""

# Run functional tests
echo ">>> Running Functional Tests..."
echo ""
"$SCRIPT_DIR/funcTest.sh"
func_result=$?
echo ""

# Summary
echo "========================================"
echo "   Overall Test Summary"
echo "========================================"
if [ $error_result -eq 0 ]; then
    echo "Error Tests: ✓ PASSED"
else
    echo "Error Tests: ✗ FAILED"
fi

if [ $func_result -eq 0 ]; then
    echo "Functional Tests: ✓ PASSED"
else
    echo "Functional Tests: ✗ FAILED (some tests may have known issues)"
fi
echo "========================================"

# Exit with error if any test suite failed
if [ $error_result -ne 0 ] || [ $func_result -ne 0 ]; then
    exit 1
fi
