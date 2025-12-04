#!/bin/bash

# Full Parity Test Suite for Hemlock
# Compiles ALL interpreter tests through the compiler and compares output
# This is more comprehensive than tests/parity/ which only has curated tests

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
HEMLOCK="$ROOT_DIR/hemlock"
HEMLOCKC="$ROOT_DIR/hemlockc"

# Configuration
TEST_TIMEOUT=10
SHOW_FAILURES=10  # How many failure details to show

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Counters
PARITY_PASS=0
PARITY_FAIL=0
INTERP_ONLY=0
COMPILER_ONLY=0
COMPILE_ERROR=0
GCC_ERROR=0
INTERP_TIMEOUT=0
COMPILED_TIMEOUT=0
SKIPPED=0
TOTAL=0

# Track failures for detailed reporting
declare -a FAILED_TESTS
declare -a FAILURE_DETAILS

# Temp directory
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Check binaries exist
if [ ! -f "$HEMLOCK" ]; then
    echo -e "${RED}Error: Interpreter not found at $HEMLOCK${NC}"
    echo "Run 'make' first."
    exit 1
fi

if [ ! -f "$HEMLOCKC" ]; then
    echo -e "${RED}Error: Compiler not found at $HEMLOCKC${NC}"
    echo "Run 'make compiler' first."
    exit 1
fi

# Check for optional libraries
ZLIB_FLAG=""
if echo 'int main(){return 0;}' | gcc -x c - -lz -o /dev/null 2>/dev/null; then
    ZLIB_FLAG="-lz"
fi

LWS_FLAG=""
if echo 'int main(){return 0;}' | gcc -x c - -lwebsockets -o /dev/null 2>/dev/null; then
    LWS_FLAG="-lwebsockets"
fi

echo "======================================"
echo "   Hemlock Full Parity Test Suite"
echo "======================================"
echo ""
echo "Interpreter: $HEMLOCK"
echo "Compiler:    $HEMLOCKC"
echo "Timeout:     ${TEST_TIMEOUT}s per test"
echo ""

# Categories to skip entirely (known incompatible or special tests)
SKIP_CATEGORIES="compiler parity ast_serialize lsp"

# Specific tests to skip (e.g., require user input, network, etc.)
SKIP_TESTS=""

should_skip_category() {
    local category="$1"
    for skip in $SKIP_CATEGORIES; do
        if [ "$category" = "$skip" ]; then
            return 0
        fi
    done
    return 1
}

run_test() {
    local test_file="$1"
    local test_name="${test_file#$SCRIPT_DIR/}"
    local base_name=$(basename "$test_file" .hml)
    local category=$(echo "$test_name" | cut -d'/' -f1)

    TOTAL=$((TOTAL + 1))

    # Skip certain categories
    if should_skip_category "$category"; then
        SKIPPED=$((SKIPPED + 1))
        return
    fi

    # Run interpreter
    local interp_output interp_exit
    interp_output=$(timeout "$TEST_TIMEOUT" "$HEMLOCK" "$test_file" 2>&1)
    interp_exit=$?

    if [ $interp_exit -eq 124 ]; then
        echo -e "${YELLOW}⊘${NC} $test_name (interpreter timeout)"
        INTERP_TIMEOUT=$((INTERP_TIMEOUT + 1))
        return
    fi

    # Compile to C
    local c_file="$TEMP_DIR/${base_name}_$$.c"
    local exe_file="$TEMP_DIR/${base_name}_$$"

    if ! timeout "$TEST_TIMEOUT" "$HEMLOCKC" "$test_file" -c --emit-c "$c_file" 2>/dev/null; then
        echo -e "${YELLOW}◐${NC} $test_name (hemlockc failed)"
        COMPILE_ERROR=$((COMPILE_ERROR + 1))
        return
    fi

    # Compile C to executable
    if ! gcc -o "$exe_file" "$c_file" -I"$ROOT_DIR/runtime/include" -L"$ROOT_DIR" \
         -lhemlock_runtime -lm -lpthread -lffi -ldl $ZLIB_FLAG $LWS_FLAG 2>/dev/null; then
        echo -e "${YELLOW}◐${NC} $test_name (gcc failed)"
        GCC_ERROR=$((GCC_ERROR + 1))
        rm -f "$c_file"
        return
    fi

    # Run compiled version
    local compiled_output compiled_exit
    compiled_output=$(timeout "$TEST_TIMEOUT" env LD_LIBRARY_PATH="$ROOT_DIR:$LD_LIBRARY_PATH" "$exe_file" 2>&1)
    compiled_exit=$?

    if [ $compiled_exit -eq 124 ]; then
        echo -e "${YELLOW}⊘${NC} $test_name (compiled timeout)"
        COMPILED_TIMEOUT=$((COMPILED_TIMEOUT + 1))
        rm -f "$c_file" "$exe_file"
        return
    fi

    # Compare outputs
    if [ "$interp_output" = "$compiled_output" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        PARITY_PASS=$((PARITY_PASS + 1))
    else
        echo -e "${RED}✗${NC} $test_name"
        PARITY_FAIL=$((PARITY_FAIL + 1))

        # Store failure details (limit stored failures)
        if [ ${#FAILED_TESTS[@]} -lt $SHOW_FAILURES ]; then
            FAILED_TESTS+=("$test_name")
            FAILURE_DETAILS+=("$(printf "Interpreter:\n%s\n\nCompiled:\n%s" "$interp_output" "$compiled_output")")
        fi
    fi

    # Cleanup
    rm -f "$c_file" "$exe_file"
}

# Find and run all tests
echo "Running tests..."
echo ""

current_category=""
for test_file in $(find "$SCRIPT_DIR" -name "*.hml" -type f | sort); do
    category=$(dirname "$test_file" | xargs basename)

    # Print category header when it changes
    if [ "$category" != "$current_category" ]; then
        if [ -n "$current_category" ]; then
            echo ""
        fi
        if ! should_skip_category "$category"; then
            echo -e "${CYAN}--- $category ---${NC}"
        fi
        current_category="$category"
    fi

    run_test "$test_file"
done

# Summary
echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Parity Pass:${NC}      $PARITY_PASS"
echo -e "${RED}Parity Fail:${NC}      $PARITY_FAIL"
echo -e "${YELLOW}Compile Error:${NC}    $COMPILE_ERROR (hemlockc)"
echo -e "${YELLOW}GCC Error:${NC}        $GCC_ERROR"
echo -e "${YELLOW}Interp Timeout:${NC}   $INTERP_TIMEOUT"
echo -e "${YELLOW}Compiled Timeout:${NC} $COMPILED_TIMEOUT"
echo -e "${BLUE}Skipped:${NC}          $SKIPPED"
echo "--------------------------------------"
echo "Total:            $TOTAL"
echo ""

# Calculate parity rate
TESTED=$((PARITY_PASS + PARITY_FAIL))
if [ $TESTED -gt 0 ]; then
    PARITY_RATE=$((PARITY_PASS * 100 / TESTED))
    echo -e "Parity Rate: ${GREEN}${PARITY_RATE}%${NC} ($PARITY_PASS/$TESTED tests with matching output)"
fi

# Show failure details
if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo ""
    echo "======================================"
    echo "        Failure Details"
    echo "======================================"
    for i in "${!FAILED_TESTS[@]}"; do
        echo ""
        echo -e "${RED}--- ${FAILED_TESTS[$i]} ---${NC}"
        echo "${FAILURE_DETAILS[$i]}" | head -20
        if [ $(echo "${FAILURE_DETAILS[$i]}" | wc -l) -gt 20 ]; then
            echo "  ... (truncated)"
        fi
    done

    if [ $PARITY_FAIL -gt $SHOW_FAILURES ]; then
        echo ""
        echo "(Showing first $SHOW_FAILURES of $PARITY_FAIL failures)"
    fi
fi

echo ""
echo "======================================"

# Exit code based on parity rate
if [ $PARITY_FAIL -eq 0 ] && [ $PARITY_PASS -gt 0 ]; then
    echo -e "${GREEN}Full parity achieved! 🎉${NC}"
    exit 0
elif [ $PARITY_RATE -ge 90 ]; then
    echo -e "${GREEN}Parity rate >= 90% ✓${NC}"
    exit 0
elif [ $PARITY_RATE -ge 75 ]; then
    echo -e "${YELLOW}Parity rate >= 75% (acceptable)${NC}"
    exit 0
else
    echo -e "${RED}Parity rate below 75%${NC}"
    exit 1
fi
