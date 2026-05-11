#!/bin/bash
# Memory Leak Regression Test Suite
# Runs all memory leak tests with AddressSanitizer leak detection
#
# Usage: ./run_leak_tests.sh [--quick]
#   --quick: Skip comprehensive test (faster for CI)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEMLOCK_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
HEMLOCK="$HEMLOCK_DIR/hemlock"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Parse arguments
QUICK_MODE=0
if [ "$1" == "--quick" ]; then
    QUICK_MODE=1
fi

# Check if hemlock exists
if [ ! -f "$HEMLOCK" ]; then
    echo -e "${RED}Error: hemlock not found at $HEMLOCK${NC}"
    echo "Please build with: make asan"
    exit 1
fi

# Check if built with ASAN
if ! nm "$HEMLOCK" 2>/dev/null | grep -q "__asan"; then
    echo -e "${YELLOW}Warning: hemlock may not be built with ASAN${NC}"
    echo "For accurate leak detection, build with: make asan"
fi

echo "════════════════════════════════════════════════════════════════"
echo "       MEMORY LEAK REGRESSION TEST SUITE"
echo "════════════════════════════════════════════════════════════════"
echo ""

PASSED=0
FAILED=0
SKIPPED=0
FAILED_TESTS=""

# Test function
run_test() {
    local test_file="$1"
    local test_name="$(basename "$test_file" .hml)"
    local timeout_sec="${2:-30}"

    printf "  %-45s " "$test_name"

    # Run with ASAN leak detection
    output=$(ASAN_OPTIONS=detect_leaks=1:exitcode=42 timeout "$timeout_sec" "$HEMLOCK" "$test_file" 2>&1) || exit_code=$?
    exit_code=${exit_code:-0}

    if [ $exit_code -eq 42 ]; then
        echo -e "${RED}LEAK${NC}"
        FAILED=$((FAILED + 1))
        FAILED_TESTS="$FAILED_TESTS\n  - $test_name (memory leak detected)"
        return 1
    elif [ $exit_code -eq 124 ]; then
        echo -e "${YELLOW}TIMEOUT${NC}"
        FAILED=$((FAILED + 1))
        FAILED_TESTS="$FAILED_TESTS\n  - $test_name (timeout after ${timeout_sec}s)"
        return 1
    elif [ $exit_code -ne 0 ]; then
        echo -e "${RED}ERROR${NC}"
        FAILED=$((FAILED + 1))
        FAILED_TESTS="$FAILED_TESTS\n  - $test_name (exit code: $exit_code)"
        return 1
    else
        echo -e "${GREEN}PASS${NC}"
        PASSED=$((PASSED + 1))
        return 0
    fi
}

# Phase 1 Regression Tests: Exception Safety
echo "▶ Phase 1: Exception Safety"
run_test "$HEMLOCK_DIR/tests/memory/exception_in_array.hml"
run_test "$HEMLOCK_DIR/tests/memory/exception_in_object.hml"
run_test "$HEMLOCK_DIR/tests/memory/exception_in_call_args.hml"
run_test "$HEMLOCK_DIR/tests/memory/exception_override_cleanup.hml"
echo ""

# Phase 1 Regression Tests: Task/Channel Cleanup
echo "▶ Phase 1: Task & Channel Cleanup"
run_test "$HEMLOCK_DIR/tests/memory/detached_task_result.hml"
run_test "$HEMLOCK_DIR/tests/memory/channel_drain.hml"
echo ""

# Phase 2 Regression Tests: Optimizer Cleanup
echo "▶ Phase 2: Optimizer Cleanup"
run_test "$HEMLOCK_DIR/tests/memory/null_coalesce_leak.hml"
echo ""

# Core Memory Operations
echo "▶ Core Memory Operations"
run_test "$HEMLOCK_DIR/tests/memory/refcount_basic.hml"
run_test "$HEMLOCK_DIR/tests/memory/talloc.hml"
run_test "$HEMLOCK_DIR/tests/memory/realloc.hml"
run_test "$HEMLOCK_DIR/tests/memory/sizeof.hml"
echo ""

# Edge Cases
echo "▶ Edge Cases"
run_test "$HEMLOCK_DIR/tests/memory/edge_free_null.hml"
run_test "$HEMLOCK_DIR/tests/memory/edge_memset_zero.hml"
run_test "$HEMLOCK_DIR/tests/memory/edge_memcpy_zero.hml"
echo ""

# Comprehensive Test (skip in quick mode)
if [ $QUICK_MODE -eq 0 ]; then
    echo "▶ Comprehensive Test"
    run_test "$HEMLOCK_DIR/tests/memory/comprehensive_leak_test.hml" 60
    echo ""
else
    echo "▶ Comprehensive Test (skipped in quick mode)"
    SKIPPED=$((SKIPPED + 1))
    echo ""
fi

# Summary
echo "════════════════════════════════════════════════════════════════"
echo "                      SUMMARY"
echo "════════════════════════════════════════════════════════════════"
echo -e "  Passed:  ${GREEN}$PASSED${NC}"
echo -e "  Failed:  ${RED}$FAILED${NC}"
if [ $SKIPPED -gt 0 ]; then
    echo -e "  Skipped: ${YELLOW}$SKIPPED${NC}"
fi
echo ""

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Failed tests:${NC}"
    echo -e "$FAILED_TESTS"
    echo ""
    exit 1
else
    echo -e "${GREEN}All memory leak regression tests passed!${NC}"
    exit 0
fi
