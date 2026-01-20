#!/bin/bash

# Tricycle Memory Safety Test Runner
#
# This script runs all Tricycle tests and reports results.

# Note: Don't use 'set -e' because (( PASSED++ )) returns 1 when PASSED=0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEMLOCK_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TRICYCLE="$HEMLOCK_ROOT/tricycle"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
PASSED=0
FAILED=0
SKIPPED=0

# Test a single file
run_test() {
    local test_file="$1"
    local expected_file="${test_file%.hml}.expected"
    local test_name=$(basename "$test_file" .hml)

    # Check if tricycle exists
    if [ ! -x "$TRICYCLE" ]; then
        echo -e "${YELLOW}SKIP${NC} $test_name (tricycle not built)"
        ((SKIPPED++))
        return
    fi

    # Run tricycle and capture output
    local output
    local exit_code
    output=$("$TRICYCLE" check --all "$test_file" 2>&1) || exit_code=$?
    exit_code=${exit_code:-0}

    # Check if expected file exists
    if [ -f "$expected_file" ]; then
        local expected
        expected=$(cat "$expected_file")

        # Check for expected error codes in output
        local all_found=true
        while IFS= read -r line; do
            # Skip empty lines and comments
            [[ -z "$line" || "$line" =~ ^# ]] && continue

            # Check if the expected pattern is in the output
            if ! echo "$output" | grep -q "$line"; then
                all_found=false
                break
            fi
        done < "$expected_file"

        if $all_found; then
            echo -e "${GREEN}PASS${NC} $test_name"
            ((PASSED++))
        else
            echo -e "${RED}FAIL${NC} $test_name"
            echo "  Expected patterns from: $expected_file"
            echo "  Got output:"
            echo "$output" | sed 's/^/    /'
            ((FAILED++))
        fi
    else
        # No expected file - just check if it runs without crashing
        if [ $exit_code -eq 2 ]; then
            echo -e "${RED}FAIL${NC} $test_name (crashed or file not found)"
            ((FAILED++))
        else
            echo -e "${GREEN}PASS${NC} $test_name (no expected file, ran successfully)"
            ((PASSED++))
        fi
    fi
}

# Main
echo "=== Tricycle Memory Safety Tests ==="
echo ""

# Find all test files
TEST_DIRS=(
    "$SCRIPT_DIR/ownership"
)

for dir in "${TEST_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        category=$(basename "$dir")
        echo "--- $category ---"

        for test_file in "$dir"/*.hml; do
            [ -f "$test_file" ] && run_test "$test_file"
        done
        echo ""
    fi
done

# Summary
echo "=== Summary ==="
echo -e "${GREEN}Passed:${NC} $PASSED"
echo -e "${RED}Failed:${NC} $FAILED"
echo -e "${YELLOW}Skipped:${NC} $SKIPPED"

# Exit with failure if any tests failed
if [ $FAILED -gt 0 ]; then
    exit 1
fi

exit 0
