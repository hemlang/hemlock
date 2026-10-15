#!/bin/bash

# Hemlock Static Checker (`hemlock check`) Test Runner
#
# For each tests/check/<name>.hml there is a <name>.expected file holding the
# exact stdout `hemlock check` should produce (diagnostics in source order
# plus the trailing summary line, or the full --json object). An optional
# <name>.flags file supplies extra flags (e.g. --json, --lint-strict). An optional
# <name>.exit file states the expected exit code when it is not implied by the
# diagnostics (--deny-warnings makes a warning-only run exit 1).
#
# The checker is run from inside this directory so the path prefix in
# diagnostics is just "<name>.hml". The expected exit code is derived from
# the expected output: if it reports one or more errors the exit code must
# be 1, otherwise 0. stderr must stay empty — every diagnostic belongs on
# stdout where tools can consume it.
#
# Convention: fixtures named neg_*.hml assert a clean report (no diagnostics,
# exit 0) and guard against false positives.

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0

echo "======================================"
echo "  Hemlock Static Checker Test Suite"
echo "======================================"
echo ""

# Detect project root
if [ -f "../../Makefile" ]; then
    PROJECT_ROOT="$(cd ../.. && pwd)"
elif [ -f "Makefile" ]; then
    PROJECT_ROOT="$(pwd)"
else
    echo -e "${RED}✗ Cannot find Makefile${NC}"
    exit 1
fi

HEMLOCK="$PROJECT_ROOT/hemlock"
TEST_DIR="$PROJECT_ROOT/tests/check"

if [ ! -x "$HEMLOCK" ]; then
    echo -e "${BLUE}Building interpreter...${NC}"
    if ! make -C "$PROJECT_ROOT" hemlock > /tmp/check_build.log 2>&1; then
        echo -e "${RED}✗ Build failed${NC}"
        cat /tmp/check_build.log
        exit 1
    fi
fi

cd "$TEST_DIR" || exit 1

for test_file in *.hml; do
    [ -f "$test_file" ] || continue
    test_name="${test_file%.hml}"
    expected_file="$test_name.expected"
    flags_file="$test_name.flags"

    if [ ! -f "$expected_file" ]; then
        continue  # helper/library fixture, not a test case
    fi

    FLAGS=""
    if [ -f "$flags_file" ]; then
        FLAGS=$(cat "$flags_file")
    fi

    actual_out=$("$HEMLOCK" check $FLAGS "$test_file" 2>/tmp/check_stderr.txt)
    actual_exit=$?
    actual_err=$(cat /tmp/check_stderr.txt)
    expected_out=$(cat "$expected_file")

    # Expected exit code: 1 when the expected output reports errors, else 0.
    # An optional <name>.exit overrides that derivation, for cases where the
    # exit code is the thing under test rather than a consequence of the
    # diagnostics — --deny-warnings turns a warning-only run into exit 1.
    if [ -f "$test_name.exit" ]; then
        expected_exit=$(cat "$test_name.exit")
    elif echo "$expected_out" | grep -qE '(: error: |"errors": [1-9])'; then
        expected_exit=1
    else
        expected_exit=0
    fi

    if [ "$actual_out" == "$expected_out" ] && [ "$actual_exit" -eq "$expected_exit" ] && [ -z "$actual_err" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗${NC} $test_name"
        if [ "$actual_out" != "$expected_out" ]; then
            echo "  --- expected stdout ---"
            echo "$expected_out" | sed 's/^/  /'
            echo "  --- actual stdout ---"
            echo "$actual_out" | sed 's/^/  /'
        fi
        if [ "$actual_exit" -ne "$expected_exit" ]; then
            echo "  expected exit $expected_exit, got $actual_exit"
        fi
        if [ -n "$actual_err" ]; then
            echo "  --- unexpected stderr ---"
            echo "$actual_err" | sed 's/^/  /'
        fi
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

echo ""
echo "======================================"
echo -e "  ${GREEN}Passed: $PASS_COUNT${NC}  ${RED}Failed: $FAIL_COUNT${NC}"
echo "======================================"

[ "$FAIL_COUNT" -eq 0 ]
