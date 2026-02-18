#!/bin/bash
#
# HemlockScript WASM Test Runner
#
# Compiles Hemlock test programs to WASM and runs them with Node.js,
# comparing output against expected files.
#
# Usage: bash tests/wasm/run_wasm_tests.sh
#
# Requirements:
#   - hemlockc built (make compiler)
#   - libhemlock_runtime_wasm.a built (make runtime-wasm)
#   - emcc (Emscripten SDK) in PATH
#   - node (Node.js) in PATH

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_DIR="$SCRIPT_DIR"
BUILD_DIR="$TEST_DIR/build"
HEMLOCKC="$PROJECT_DIR/hemlockc"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Check prerequisites
if ! command -v emcc &> /dev/null; then
    echo -e "${YELLOW}WARNING: emcc not found. Install Emscripten SDK to run WASM tests.${NC}"
    echo "  See: https://emscripten.org/docs/getting_started/downloads.html"
    exit 0  # Don't fail CI if emscripten isn't installed
fi

if ! command -v node &> /dev/null; then
    echo -e "${RED}ERROR: node not found. Install Node.js to run WASM tests.${NC}"
    exit 1
fi

if [ ! -f "$HEMLOCKC" ]; then
    echo -e "${RED}ERROR: hemlockc not found. Run 'make compiler' first.${NC}"
    exit 1
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Find test files
TESTS=$(find "$TEST_DIR" -name "*.hml" -maxdepth 1 | sort)
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

echo "=========================================="
echo "  HemlockScript WASM Test Suite"
echo "=========================================="
echo ""

for test_file in $TESTS; do
    test_name=$(basename "$test_file" .hml)
    expected_file="$TEST_DIR/$test_name.expected"
    output_js="$BUILD_DIR/$test_name.js"

    TOTAL=$((TOTAL + 1))

    # Check for expected file
    if [ ! -f "$expected_file" ]; then
        echo -e "  ${YELLOW}SKIP${NC} $test_name (no .expected file)"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    # Compile to WASM
    printf "  %-30s " "$test_name"

    if ! "$HEMLOCKC" --target wasm -o "$output_js" "$test_file" 2>"$BUILD_DIR/$test_name.compile_err"; then
        echo -e "${RED}COMPILE FAIL${NC}"
        cat "$BUILD_DIR/$test_name.compile_err" | head -5
        FAILED=$((FAILED + 1))
        continue
    fi

    # Run with Node.js
    actual_output=$(timeout 10 node "$output_js" 2>&1) || true

    # Compare output
    expected_output=$(cat "$expected_file")

    if [ "$actual_output" = "$expected_output" ]; then
        echo -e "${GREEN}PASSED${NC}"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}FAILED${NC}"
        echo "    Expected:"
        echo "$expected_output" | head -5 | sed 's/^/      /'
        echo "    Got:"
        echo "$actual_output" | head -5 | sed 's/^/      /'
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "=========================================="
echo "  Results: $PASSED passed, $FAILED failed, $SKIPPED skipped ($TOTAL total)"
echo "=========================================="

# Cleanup
rm -rf "$BUILD_DIR"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
