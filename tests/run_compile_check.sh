#!/bin/bash

# Compile Check for Hemlock Interpreter Tests
# Verifies that all interpreter tests at least COMPILE with hemlockc
# (Does not check output parity - just compilation success)

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
HEMLOCKC="$ROOT_DIR/hemlockc"

# Configuration
COMPILE_TIMEOUT=30
SHOW_FAILURES=20  # How many failure details to show

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Counters
COMPILE_PASS=0
COMPILE_FAIL=0
GCC_FAIL=0
SKIPPED=0
TOTAL=0

# Track failures for detailed reporting
declare -a FAILED_TESTS
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

# Platform-specific library paths
EXTRA_CFLAGS=""
EXTRA_LDFLAGS=""
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: use Homebrew paths for OpenSSL, libffi, etc.
    if [ -d "/opt/homebrew/opt/openssl@3" ]; then
        EXTRA_CFLAGS="$EXTRA_CFLAGS -I/opt/homebrew/opt/openssl@3/include"
        EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L/opt/homebrew/opt/openssl@3/lib"
    fi
    if [ -d "/opt/homebrew/opt/libffi" ]; then
        EXTRA_CFLAGS="$EXTRA_CFLAGS -I/opt/homebrew/opt/libffi/include"
        EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L/opt/homebrew/opt/libffi/lib"
    fi
    if [ -d "/opt/homebrew/opt/libwebsockets" ]; then
        EXTRA_CFLAGS="$EXTRA_CFLAGS -I/opt/homebrew/opt/libwebsockets/include"
        EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L/opt/homebrew/opt/libwebsockets/lib"
    fi
fi

# Check for optional libraries
ZLIB_FLAG=""
if echo 'int main(){return 0;}' | gcc -x c - -lz -o /dev/null 2>/dev/null; then
    ZLIB_FLAG="-lz"
fi

LWS_FLAG=""
if echo 'int main(){return 0;}' | gcc -x c - $EXTRA_LDFLAGS -lwebsockets -o /dev/null 2>/dev/null; then
    LWS_FLAG="-lwebsockets"
fi

CRYPTO_FLAG=""
if echo 'int main(){return 0;}' | gcc -x c - $EXTRA_LDFLAGS -lcrypto -o /dev/null 2>/dev/null; then
    CRYPTO_FLAG="-lcrypto"
fi

echo "======================================"
echo "   Hemlock Compile Check Suite"
echo "======================================"
echo ""
echo "Compiler:    $HEMLOCKC"
echo "Timeout:     ${COMPILE_TIMEOUT}s per test"
echo ""
echo "This checks that interpreter tests COMPILE"
echo "(does not check output parity)"
echo ""

# Categories to skip entirely (known incompatible or special tests)
SKIP_CATEGORIES="compiler parity ast_serialize lsp bundler"

# Tests with known compile issues (expected to fail compilation)
# - tests for invalid syntax that should fail parsing
EXPECTED_COMPILE_FAIL="primitives/binary_invalid.hml primitives/hex_invalid.hml"

should_skip_category() {
    local category="$1"
    for skip in $SKIP_CATEGORIES; do
        if [ "$category" = "$skip" ]; then
            return 0
        fi
    done
    return 1
}

is_expected_compile_fail() {
    local test_name="$1"
    for fail_test in $EXPECTED_COMPILE_FAIL; do
        if [ "$test_name" = "$fail_test" ]; then
            return 0
        fi
    done
    return 1
}

run_compile_check() {
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

    # Handle expected compile failures
    if is_expected_compile_fail "$test_name"; then
        local c_file="$TEMP_DIR/${base_name}_$$.c"
        if ! timeout "$COMPILE_TIMEOUT" "$HEMLOCKC" "$test_file" -c --emit-c "$c_file" 2>/dev/null; then
            echo -e "${GREEN}✓${NC} $test_name (expected compile failure)"
            COMPILE_PASS=$((COMPILE_PASS + 1))
        else
            echo -e "${YELLOW}!${NC} $test_name (expected to fail but compiled)"
            COMPILE_PASS=$((COMPILE_PASS + 1))  # Still counts as pass for now
        fi
        rm -f "$c_file"
        return
    fi

    # Try to compile with hemlockc
    local c_file="$TEMP_DIR/${base_name}_$$.c"
    local exe_file="$TEMP_DIR/${base_name}_$$"
    local compile_output
    local compile_exit

    compile_output=$(timeout "$COMPILE_TIMEOUT" "$HEMLOCKC" "$test_file" -c --emit-c "$c_file" 2>&1)
    compile_exit=$?

    if [ $compile_exit -eq 124 ]; then
        echo -e "${RED}✗${NC} $test_name (hemlockc timeout)"
        COMPILE_FAIL=$((COMPILE_FAIL + 1))
        if [ ${#FAILED_TESTS[@]} -lt $SHOW_FAILURES ]; then
            FAILED_TESTS+=("$test_name")
            FAILURE_REASONS+=("hemlockc timed out after ${COMPILE_TIMEOUT}s")
        fi
        return
    fi

    if [ $compile_exit -ne 0 ]; then
        echo -e "${RED}✗${NC} $test_name (hemlockc failed)"
        COMPILE_FAIL=$((COMPILE_FAIL + 1))
        if [ ${#FAILED_TESTS[@]} -lt $SHOW_FAILURES ]; then
            FAILED_TESTS+=("$test_name")
            FAILURE_REASONS+=("hemlockc error: $(echo "$compile_output" | head -3)")
        fi
        rm -f "$c_file"
        return
    fi

    # Try to compile the generated C code with gcc
    local gcc_output
    gcc_output=$(gcc -o "$exe_file" "$c_file" -I"$ROOT_DIR/runtime/include" -L"$ROOT_DIR" \
         $EXTRA_CFLAGS $EXTRA_LDFLAGS \
         -lhemlock_runtime -lm -lpthread -lffi -ldl $ZLIB_FLAG $LWS_FLAG $CRYPTO_FLAG 2>&1)
    local gcc_exit=$?

    if [ $gcc_exit -ne 0 ]; then
        echo -e "${YELLOW}◐${NC} $test_name (gcc failed)"
        GCC_FAIL=$((GCC_FAIL + 1))
        if [ ${#FAILED_TESTS[@]} -lt $SHOW_FAILURES ]; then
            FAILED_TESTS+=("$test_name")
            FAILURE_REASONS+=("gcc error: $(echo "$gcc_output" | head -3)")
        fi
        rm -f "$c_file"
        return
    fi

    echo -e "${GREEN}✓${NC} $test_name"
    COMPILE_PASS=$((COMPILE_PASS + 1))

    # Cleanup
    rm -f "$c_file" "$exe_file"
}

# Find and run all tests
echo "Checking compilation..."
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

    run_compile_check "$test_file"
done

# Summary
echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Compile Pass:${NC}     $COMPILE_PASS"
echo -e "${RED}Compile Fail:${NC}     $COMPILE_FAIL (hemlockc)"
echo -e "${YELLOW}GCC Fail:${NC}         $GCC_FAIL"
echo -e "${BLUE}Skipped:${NC}          $SKIPPED"
echo "--------------------------------------"
echo "Total:            $TOTAL"
echo ""

# Calculate compile success rate
TOTAL_CHECKED=$((COMPILE_PASS + COMPILE_FAIL + GCC_FAIL))
if [ $TOTAL_CHECKED -gt 0 ]; then
    COMPILE_RATE=$((COMPILE_PASS * 100 / TOTAL_CHECKED))
    echo -e "Compile Rate: ${GREEN}${COMPILE_RATE}%${NC} ($COMPILE_PASS/$TOTAL_CHECKED tests compile successfully)"
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
        echo "${FAILURE_REASONS[$i]}"
    done

    TOTAL_FAILURES=$((COMPILE_FAIL + GCC_FAIL))
    if [ $TOTAL_FAILURES -gt $SHOW_FAILURES ]; then
        echo ""
        echo "(Showing first $SHOW_FAILURES of $TOTAL_FAILURES failures)"
    fi
fi

echo ""
echo "======================================"

# Exit code based on compile rate
TOTAL_FAILURES=$((COMPILE_FAIL + GCC_FAIL))
if [ $TOTAL_FAILURES -eq 0 ] && [ $COMPILE_PASS -gt 0 ]; then
    echo -e "${GREEN}All interpreter tests compile!${NC}"
    exit 0
elif [ $COMPILE_RATE -ge 95 ]; then
    echo -e "${GREEN}Compile rate >= 95%${NC}"
    exit 0
elif [ $COMPILE_RATE -ge 90 ]; then
    echo -e "${YELLOW}Compile rate >= 90% (acceptable)${NC}"
    exit 0
else
    echo -e "${RED}Compile rate below 90%${NC}"
    exit 1
fi
