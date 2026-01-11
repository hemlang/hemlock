#!/bin/bash

# Repo-wide Compile Check for Hemlock
# Verifies that all .hml files in the repository compile with hemlockc

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
HEMLOCKC="$ROOT_DIR/hemlockc"

# Configuration
COMPILE_TIMEOUT=30
SHOW_FAILURES=20

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Counters
COMPILE_PASS=0
COMPILE_FAIL=0
EXPECTED_FAIL_PASS=0
EXPECTED_FAIL_MISMATCH=0
TOTAL=0

# Track failures for detailed reporting
declare -a FAILED_FILES
declare -a FAILURE_REASONS

# Temp directory
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Check compiler exists
if [ ! -f "$HEMLOCKC" ]; then
    echo -e "${RED}Error: Compiler not found at $HEMLOCKC${NC}"
    echo "Run 'make compiler' first."
    exit 1
fi

echo "======================================"
echo " Hemlock Repo Compile Check Suite"
echo "======================================"
echo ""
echo "Compiler:    $HEMLOCKC"
echo "Timeout:     ${COMPILE_TIMEOUT}s per file"
echo ""
echo "This checks that all .hml files COMPILE"
echo ""

is_expected_fail() {
    local file_name="$1"
    if [[ "$file_name" == *"invalid"* ]]; then
        return 0
    fi
    return 1
}

run_compile_check() {
    local test_file="$1"
    local test_name="${test_file#$ROOT_DIR/}"
    local base_name=$(basename "$test_file" .hml)

    TOTAL=$((TOTAL + 1))

    local c_file="$TEMP_DIR/${base_name}_$$.c"

    if is_expected_fail "$test_name"; then
        if ! timeout "$COMPILE_TIMEOUT" "$HEMLOCKC" "$test_file" -c --emit-c "$c_file" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} $test_name (expected compile failure)"
            EXPECTED_FAIL_PASS=$((EXPECTED_FAIL_PASS + 1))
        else
            echo -e "${YELLOW}!${NC} $test_name (expected to fail but compiled)"
            EXPECTED_FAIL_MISMATCH=$((EXPECTED_FAIL_MISMATCH + 1))
        fi
        rm -f "$c_file"
        return
    fi

    local compile_output
    local compile_exit

    compile_output=$(timeout "$COMPILE_TIMEOUT" "$HEMLOCKC" "$test_file" -c --emit-c "$c_file" 2>&1)
    compile_exit=$?

    if [ $compile_exit -eq 124 ]; then
        echo -e "${RED}✗${NC} $test_name (hemlockc timeout)"
        COMPILE_FAIL=$((COMPILE_FAIL + 1))
        if [ ${#FAILED_FILES[@]} -lt $SHOW_FAILURES ]; then
            FAILED_FILES+=("$test_name")
            FAILURE_REASONS+=("hemlockc timed out after ${COMPILE_TIMEOUT}s")
        fi
        return
    fi

    if [ $compile_exit -ne 0 ]; then
        echo -e "${RED}✗${NC} $test_name (hemlockc failed)"
        COMPILE_FAIL=$((COMPILE_FAIL + 1))
        if [ ${#FAILED_FILES[@]} -lt $SHOW_FAILURES ]; then
            FAILED_FILES+=("$test_name")
            FAILURE_REASONS+=("hemlockc error: $(echo "$compile_output" | head -3)")
        fi
        rm -f "$c_file"
        return
    fi

    echo -e "${GREEN}✓${NC} $test_name"
    COMPILE_PASS=$((COMPILE_PASS + 1))

    rm -f "$c_file"
}

echo "Checking compilation..."
echo ""

for test_file in $(find "$ROOT_DIR" -name "*.hml" -type f | sort); do
    run_compile_check "$test_file"
done

# Summary
echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Compile Pass:${NC}       $COMPILE_PASS"
echo -e "${GREEN}Expected Fail:${NC}      $EXPECTED_FAIL_PASS"
echo -e "${RED}Compile Fail:${NC}       $COMPILE_FAIL"
echo -e "${RED}Expected Mismatch:${NC}  $EXPECTED_FAIL_MISMATCH"
echo "--------------------------------------"
echo "Total:              $TOTAL"

echo ""

# Show failure details
if [ ${#FAILED_FILES[@]} -gt 0 ]; then
    echo "======================================"
    echo "          Failure Details"
    echo "======================================"
    for i in "${!FAILED_FILES[@]}"; do
        echo ""
        echo -e "${RED}--- ${FAILED_FILES[$i]} ---${NC}"
        echo "${FAILURE_REASONS[$i]}"
    done

    TOTAL_FAILURES=$((COMPILE_FAIL + EXPECTED_FAIL_MISMATCH))
    if [ $TOTAL_FAILURES -gt $SHOW_FAILURES ]; then
        echo ""
        echo "(Showing first $SHOW_FAILURES of $TOTAL_FAILURES failures)"
    fi
fi

echo ""
echo "======================================"

TOTAL_FAILURES=$((COMPILE_FAIL + EXPECTED_FAIL_MISMATCH))
if [ $TOTAL_FAILURES -eq 0 ] && [ $COMPILE_PASS -gt 0 ]; then
    echo -e "${GREEN}All .hml files compile!${NC}"
    exit 0
else
    echo -e "${RED}Some .hml files failed to compile.${NC}"
    exit 1
fi
