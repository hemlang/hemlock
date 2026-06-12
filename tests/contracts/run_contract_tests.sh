#!/bin/bash

# Contract Test Suite for Hemlock
#
# Contract tests pin down DOCUMENTED behavior — type promotion rules, literal
# inference, error messages, evaluation semantics — so that user-visible
# guarantees can never change silently. Every test runs through BOTH the
# interpreter and the compiler: a contract that only one backend honors is a
# parity bug, not a contract.
#
# Test kinds:
#   - Normal tests:  <name>.hml + <name>.expected
#       Both backends must produce the .expected output byte-for-byte.
#   - Error tests:   filename contains error|invalid|overflow|negative
#       Both backends must REJECT the program: the interpreter must exit
#       non-zero, and the compiler must either refuse to compile or produce
#       a binary that exits non-zero. (Backends may reject at different
#       stages; the contract is rejection, not the stage.)
#       Optional <name>.expected_error: every non-empty line must appear as
#       a substring of the interpreter's output, pinning the stable part of
#       the error message without its [file:line] location prefix.
#
# See docs/contributing/contract-testing.md for the practices behind this
# suite and how to add new contracts.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
HEMLOCK="$ROOT_DIR/hemlock"
HEMLOCKC="$ROOT_DIR/hemlockc"

TEST_TIMEOUT=${TEST_TIMEOUT:-10}
COMPILE_TIMEOUT=${COMPILE_TIMEOUT:-30}

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

PASSED=0
FAILED=0
SKIPPED=0

TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

if [ ! -f "$HEMLOCK" ]; then
    echo -e "${RED}Error: Interpreter not found at $HEMLOCK${NC}"
    echo "Run 'make' to build the interpreter first."
    exit 1
fi

if [ ! -f "$HEMLOCKC" ]; then
    echo -e "${RED}Error: Compiler not found at $HEMLOCKC${NC}"
    echo "Run 'make compiler' to build the compiler first."
    exit 1
fi

echo "======================================"
echo "   Hemlock Contract Test Suite"
echo "======================================"
echo ""
echo "Pinning documented behavior on both backends"

# Optional filter: only run tests whose path matches the first argument
FILTER="${1:-}"
if [ -n "$FILTER" ]; then
    echo "Filter: $FILTER"
fi
echo ""

print_diff() {
    local expected="$1"
    local actual="$2"
    diff <(echo "$expected") <(echo "$actual") 2>/dev/null | head -20 | sed 's/^/    /'
}

# Error tests: filename declares the program must be rejected by both backends.
is_error_test() {
    local name="$1"
    [[ "$name" =~ (error|invalid|overflow|negative) ]]
}

run_error_test() {
    local test_file="$1"
    local test_name="$2"

    local interp_output
    local interp_exit=0
    interp_output=$(timeout "$TEST_TIMEOUT" "$HEMLOCK" "$test_file" 2>&1) || interp_exit=$?

    if [ $interp_exit -eq 0 ]; then
        echo -e "${RED}✗${NC} $test_name (interpreter accepted a program the contract forbids)"
        ((FAILED++))
        return
    fi
    if [ $interp_exit -eq 124 ]; then
        echo -e "${RED}✗${NC} $test_name (interpreter timed out instead of rejecting)"
        ((FAILED++))
        return
    fi

    # Pin the stable part of the interpreter's error message, if declared
    local subst_file="${test_file%.hml}.expected_error"
    if [ -f "$subst_file" ]; then
        local line
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            if [[ "$interp_output" != *"$line"* ]]; then
                echo -e "${RED}✗${NC} $test_name (error message contract broken)"
                echo -e "    ${RED}Expected substring:${NC} $line"
                echo -e "    ${RED}Interpreter output:${NC}"
                echo "$interp_output" | head -5 | sed 's/^/    /'
                ((FAILED++))
                return
            fi
        done < "$subst_file"
    fi

    # Compiler must reject too: at compile time or at run time
    local compile_exit=0
    timeout "$COMPILE_TIMEOUT" "$HEMLOCKC" "$test_file" -o "$TEMP_DIR/$test_name" 2>/dev/null || compile_exit=$?
    if [ $compile_exit -eq 0 ]; then
        local run_exit=0
        timeout "$TEST_TIMEOUT" env LD_LIBRARY_PATH="$ROOT_DIR" "$TEMP_DIR/$test_name" >/dev/null 2>&1 || run_exit=$?
        if [ $run_exit -eq 0 ]; then
            echo -e "${RED}✗${NC} $test_name (compiled binary accepted a program the contract forbids)"
            ((FAILED++))
            return
        fi
        if [ $run_exit -eq 124 ]; then
            echo -e "${RED}✗${NC} $test_name (compiled binary timed out instead of rejecting)"
            ((FAILED++))
            return
        fi
    fi

    echo -e "${GREEN}✓${NC} $test_name (rejected by both backends)"
    ((PASSED++))
}

run_output_test() {
    local test_file="$1"
    local test_name="$2"
    local expected_file="${test_file%.hml}.expected"

    if [ ! -f "$expected_file" ]; then
        echo -e "${YELLOW}⊘${NC} $test_name (no .expected file)"
        ((SKIPPED++))
        return
    fi

    local expected=$(cat "$expected_file")

    local interp_output
    local interp_exit=0
    interp_output=$(timeout "$TEST_TIMEOUT" "$HEMLOCK" "$test_file" 2>&1) || interp_exit=$?
    if [ $interp_exit -eq 124 ]; then
        interp_output="[TIMEOUT after ${TEST_TIMEOUT}s]"
    fi

    local compile_exit=0
    local compiler_output=""
    timeout "$COMPILE_TIMEOUT" "$HEMLOCKC" "$test_file" -o "$TEMP_DIR/$test_name" 2>/dev/null || compile_exit=$?
    if [ $compile_exit -eq 124 ]; then
        compile_exit=1
        compiler_output="[COMPILE TIMEOUT after ${COMPILE_TIMEOUT}s]"
    fi
    if [ $compile_exit -eq 0 ]; then
        local run_exit=0
        compiler_output=$(timeout "$TEST_TIMEOUT" env LD_LIBRARY_PATH="$ROOT_DIR" "$TEMP_DIR/$test_name" 2>&1) || run_exit=$?
        if [ $run_exit -eq 124 ]; then
            compiler_output="[TIMEOUT after ${TEST_TIMEOUT}s]"
        fi
    fi

    local interp_match=false
    local compiler_match=false
    [ "$interp_output" = "$expected" ] && interp_match=true
    [ $compile_exit -eq 0 ] && [ "$compiler_output" = "$expected" ] && compiler_match=true

    if [ "$interp_match" = true ] && [ "$compiler_match" = true ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        ((PASSED++))
    else
        echo -e "${RED}✗${NC} $test_name"
        ((FAILED++))
        if [ "$interp_match" = false ]; then
            echo -e "    ${RED}Interpreter broke the contract (expected vs interpreter):${NC}"
            print_diff "$expected" "$interp_output"
        fi
        if [ "$compiler_match" = false ]; then
            if [ $compile_exit -ne 0 ]; then
                echo -e "    ${RED}Compiler failed to compile a contracted program${NC}"
                [ -n "$compiler_output" ] && echo "$compiler_output" | head -5 | sed 's/^/    /'
            else
                echo -e "    ${RED}Compiler broke the contract (expected vs compiled):${NC}"
                print_diff "$expected" "$compiler_output"
            fi
        fi
    fi
}

run_test() {
    local test_file="$1"
    local test_name=$(basename "$test_file" .hml)

    if [ -n "$FILTER" ] && [[ "$test_file" != *"$FILTER"* ]]; then
        return
    fi

    if is_error_test "$test_name"; then
        run_error_test "$test_file" "$test_name"
    else
        run_output_test "$test_file" "$test_name"
    fi
}

for category_dir in "$SCRIPT_DIR"/*/; do
    [ -d "$category_dir" ] || continue
    category=$(basename "$category_dir")
    test_files=$(find "$category_dir" -name "*.hml" -type f | sort)
    if [ -n "$test_files" ]; then
        echo "--- $category ---"
        for test_file in $test_files; do
            run_test "$test_file"
        done
        echo ""
    fi
done

echo "======================================"
echo "              Summary"
echo "======================================"
TOTAL=$((PASSED + FAILED + SKIPPED))
echo -e "${GREEN}Passed:${NC}  $PASSED"
echo -e "${RED}Failed:${NC}  $FAILED"
echo -e "${BLUE}Skipped:${NC} $SKIPPED"
echo "--------------------------------------"
echo "Total:   $TOTAL"
echo ""

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Contract violations detected.${NC}"
    echo "A failing contract test means documented behavior changed."
    echo "Either the change is a bug, or the docs and the contract test"
    echo "must be updated together in the same commit."
    exit 1
fi

echo -e "${GREEN}All contracts hold on both backends.${NC}"
exit 0
