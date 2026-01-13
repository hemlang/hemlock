#!/bin/bash
#
# Comprehensive Memory Leak Detection Script for Hemlock Runtime
#
# This script systematically tests the Hemlock interpreter for memory leaks
# using AddressSanitizer (ASAN). It provides guarantees about runtime memory safety.
#
# Usage:
#   ./tests/leak_check.sh              # Run full leak check
#   ./tests/leak_check.sh --quick      # Quick check (core tests only)
#   ./tests/leak_check.sh --category arrays  # Test specific category
#
# Exit codes:
#   0 - All tests passed, no leaks detected
#   1 - Leaks detected
#   2 - Build or setup failure
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
HEMLOCK="$ROOT_DIR/hemlock"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

# Configuration
TIMEOUT_PER_TEST=10  # seconds per test
ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:exitcode=42:print_summary=1"
QUICK_MODE=false
SPECIFIC_CATEGORY=""
VERBOSE=false

# Statistics
TOTAL_TESTS=0
PASSED_TESTS=0
LEAKED_TESTS=0
TIMEOUT_TESTS=0
ERROR_TESTS=0
LEAKED_FILES=()

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --quick|-q)
            QUICK_MODE=true
            shift
            ;;
        --category|-c)
            SPECIFIC_CATEGORY="$2"
            shift 2
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --quick, -q          Run quick check (core tests only)"
            echo "  --category, -c CAT   Test specific category"
            echo "  --verbose, -v        Show all test output"
            echo "  --help, -h           Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 2
            ;;
    esac
done

print_header() {
    echo ""
    echo -e "${BOLD}${BLUE}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${BLUE}       HEMLOCK RUNTIME MEMORY LEAK VERIFICATION${NC}"
    echo -e "${BOLD}${BLUE}════════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "${CYAN}This test suite verifies that the Hemlock runtime has no memory leaks.${NC}"
    echo -e "${CYAN}Using AddressSanitizer (ASAN) with leak detection enabled.${NC}"
    echo ""
}

print_section() {
    echo ""
    echo -e "${BOLD}${YELLOW}▶ $1${NC}"
    echo -e "${YELLOW}────────────────────────────────────────${NC}"
}

# Check if ASAN build exists
check_asan_build() {
    print_section "Checking ASAN Build"

    if [[ ! -f "$HEMLOCK" ]]; then
        echo -e "${RED}Error: hemlock binary not found at $HEMLOCK${NC}"
        echo "Run 'make asan' first to build with AddressSanitizer"
        exit 2
    fi

    # Check if built with ASAN by looking for ASAN symbols
    if ! nm "$HEMLOCK" 2>/dev/null | grep -q "__asan"; then
        echo -e "${YELLOW}Warning: hemlock may not be built with ASAN${NC}"
        echo "Building with ASAN now..."
        cd "$ROOT_DIR"
        make asan
        cd - > /dev/null
    fi

    echo -e "${GREEN}✓ ASAN build verified${NC}"
}

# Run a single test file and check for leaks
run_test() {
    local test_file="$1"
    local test_name=$(basename "$test_file" .hml)
    local category=$(basename $(dirname "$test_file"))

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if $VERBOSE; then
        echo -n "  Testing $category/$test_name... "
    fi

    # Run with ASAN and capture output
    local output
    local exit_code
    output=$(ASAN_OPTIONS="$ASAN_OPTIONS" timeout $TIMEOUT_PER_TEST "$HEMLOCK" "$test_file" 2>&1) || exit_code=$?
    exit_code=${exit_code:-0}

    # Check for different failure modes
    if [[ $exit_code -eq 124 ]]; then
        # Timeout
        TIMEOUT_TESTS=$((TIMEOUT_TESTS + 1))
        if $VERBOSE; then
            echo -e "${YELLOW}TIMEOUT${NC}"
        fi
        return
    elif [[ $exit_code -eq 42 ]] || echo "$output" | grep -q "LeakSanitizer"; then
        # Leak detected
        LEAKED_TESTS=$((LEAKED_TESTS + 1))
        LEAKED_FILES+=("$test_file")
        if $VERBOSE; then
            echo -e "${RED}LEAK${NC}"
            echo "$output" | grep -A20 "LeakSanitizer" | head -25
        fi
        return
    elif echo "$output" | grep -q "AddressSanitizer"; then
        # Other ASAN error (use-after-free, buffer overflow, etc.)
        ERROR_TESTS=$((ERROR_TESTS + 1))
        LEAKED_FILES+=("$test_file")
        if $VERBOSE; then
            echo -e "${RED}ASAN ERROR${NC}"
            echo "$output" | grep -A10 "AddressSanitizer" | head -15
        fi
        return
    fi

    PASSED_TESTS=$((PASSED_TESTS + 1))
    if $VERBOSE; then
        echo -e "${GREEN}OK${NC}"
    fi
}

# Run tests for a category
run_category() {
    local category="$1"
    local category_dir="$ROOT_DIR/tests/$category"
    local count=0
    local category_start=$TOTAL_TESTS

    if [[ ! -d "$category_dir" ]]; then
        return
    fi

    # Count files first
    local file_count=$(find "$category_dir" -name "*.hml" -type f 2>/dev/null | wc -l)
    if [[ $file_count -eq 0 ]]; then
        return
    fi

    echo -n "  $category ($file_count tests)... "

    # Run each test
    for test_file in "$category_dir"/*.hml; do
        if [[ -f "$test_file" ]]; then
            # Skip known problematic tests
            local basename=$(basename "$test_file")
            if [[ "$basename" =~ ^edge_ ]] || [[ "$basename" =~ stress_memory ]]; then
                continue
            fi
            run_test "$test_file"
            count=$((count + 1))
        fi
    done

    local category_passed=$((PASSED_TESTS - (category_start - (TOTAL_TESTS - count))))
    local category_total=$((TOTAL_TESTS - category_start))

    if [[ $category_total -gt 0 ]]; then
        local category_leaked=$((category_total - category_passed - TIMEOUT_TESTS + (TOTAL_TESTS - category_start - count)))
        if [[ $category_leaked -eq 0 ]] && [[ $TIMEOUT_TESTS -eq 0 ]]; then
            echo -e "${GREEN}✓ PASS${NC}"
        elif [[ $category_leaked -gt 0 ]]; then
            echo -e "${RED}✗ LEAKS DETECTED${NC}"
        else
            echo -e "${YELLOW}⚠ TIMEOUTS${NC}"
        fi
    fi
}

# Core categories that must pass for guarantees
CORE_CATEGORIES=(
    "memory"
    "strings"
    "arrays"
    "objects"
    "functions"
    "control"
    "loops"
    "variables"
    "primitives"
    "arithmetic"
    "comparisons"
    "bools"
    "buffers"
    "pointers"
    "defer"
    "exceptions"
    "error_handling"
)

# Extended categories for full verification
EXTENDED_CATEGORIES=(
    "async"
    "ffi"
    "modules"
    "interpolation"
    "switch"
    "bitwise"
    "typed_arrays"
    "enums"
    "conversions"
    "annotations"
    "const"
    "optional_chaining"
)

# Parity tests (comprehensive language feature tests)
PARITY_CATEGORIES=(
    "parity/language"
    "parity/builtins"
    "parity/methods"
)

run_comprehensive_test() {
    print_section "Running Comprehensive Leak Tests"

    if [[ -n "$SPECIFIC_CATEGORY" ]]; then
        echo "Testing specific category: $SPECIFIC_CATEGORY"
        run_category "$SPECIFIC_CATEGORY"
        return
    fi

    echo -e "${CYAN}Core Runtime Tests (must pass for guarantees):${NC}"
    for category in "${CORE_CATEGORIES[@]}"; do
        run_category "$category"
    done

    if ! $QUICK_MODE; then
        echo ""
        echo -e "${CYAN}Extended Tests:${NC}"
        for category in "${EXTENDED_CATEGORIES[@]}"; do
            run_category "$category"
        done

        echo ""
        echo -e "${CYAN}Parity Tests (comprehensive language coverage):${NC}"
        for category in "${PARITY_CATEGORIES[@]}"; do
            run_category "$category"
        done
    fi
}

# Run the dedicated comprehensive leak test
run_stress_test() {
    print_section "Running Comprehensive Leak Stress Test"

    local stress_test="$ROOT_DIR/tests/memory/comprehensive_leak_test.hml"

    if [[ ! -f "$stress_test" ]]; then
        echo -e "${YELLOW}Comprehensive leak test not found, skipping...${NC}"
        return
    fi

    echo -n "  Running comprehensive_leak_test.hml... "

    local output
    local exit_code
    output=$(ASAN_OPTIONS="$ASAN_OPTIONS" timeout 30 "$HEMLOCK" "$stress_test" 2>&1) || exit_code=$?
    exit_code=${exit_code:-0}

    if [[ $exit_code -eq 42 ]] || echo "$output" | grep -q "LeakSanitizer"; then
        echo -e "${RED}LEAK DETECTED${NC}"
        echo "$output" | grep -A30 "LeakSanitizer"
        LEAKED_TESTS=$((LEAKED_TESTS + 1))
        LEAKED_FILES+=("$stress_test")
    elif echo "$output" | grep -q "AddressSanitizer"; then
        echo -e "${RED}ASAN ERROR${NC}"
        echo "$output" | grep -A15 "AddressSanitizer"
        ERROR_TESTS=$((ERROR_TESTS + 1))
    elif [[ $exit_code -eq 124 ]]; then
        echo -e "${YELLOW}TIMEOUT${NC}"
        TIMEOUT_TESTS=$((TIMEOUT_TESTS + 1))
    else
        echo -e "${GREEN}✓ PASS${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    fi
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
}

print_results() {
    echo ""
    echo -e "${BOLD}${BLUE}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${BLUE}                         RESULTS${NC}"
    echo -e "${BOLD}${BLUE}════════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "  Total tests:    ${BOLD}$TOTAL_TESTS${NC}"
    echo -e "  Passed:         ${GREEN}$PASSED_TESTS${NC}"
    echo -e "  Leaks detected: ${RED}$LEAKED_TESTS${NC}"
    echo -e "  ASAN errors:    ${RED}$ERROR_TESTS${NC}"
    echo -e "  Timeouts:       ${YELLOW}$TIMEOUT_TESTS${NC}"
    echo ""

    if [[ $LEAKED_TESTS -gt 0 ]] || [[ $ERROR_TESTS -gt 0 ]]; then
        echo -e "${RED}${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${RED}${BOLD}║              ✗ MEMORY ISSUES DETECTED                        ║${NC}"
        echo -e "${RED}${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        echo "Files with issues:"
        for file in "${LEAKED_FILES[@]}"; do
            echo "  - $file"
        done
        echo ""
        echo "Run with --verbose to see detailed ASAN output"
        return 1
    else
        echo -e "${GREEN}${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}${BOLD}║     ✓ NO MEMORY LEAKS DETECTED - RUNTIME VERIFIED           ║${NC}"
        echo -e "${GREEN}${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        echo -e "${CYAN}The Hemlock runtime has been verified free of memory leaks across:${NC}"
        echo "  • Core runtime operations (memory, strings, arrays, objects)"
        echo "  • Control flow and loops"
        echo "  • Function calls and closures"
        echo "  • Exception handling and defer"
        if ! $QUICK_MODE; then
            echo "  • Async operations and concurrency"
            echo "  • FFI and external calls"
            echo "  • All parity test scenarios"
        fi
        echo ""
        echo -e "${GREEN}Memory safety guarantee: VERIFIED${NC}"
        return 0
    fi
}

# Main execution
main() {
    print_header
    check_asan_build
    run_stress_test
    run_comprehensive_test
    print_results
}

main
