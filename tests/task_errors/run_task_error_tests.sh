#!/bin/bash

# Hemlock Spawned-Task Error Test Runner
#
# An exception that escapes a spawned task is handed to whoever join()s it.
# Nobody joins a detached task, or one whose handle was dropped, and the
# exception used to go with it: the task died, the process carried on, and
# nothing was written anywhere.
#
# For each tests/task_errors/<name>.hml there is a <name>.expected-stdout and a
# <name>.expected-stderr. Both backends must produce them: the summary line is
# byte-identical interpreter-side and compiled, which is why stderr is matched
# only up to the first line here - the interpreter adds the location and a
# stack trace below it, the same split that already applies to a top-level
# uncaught exception.
#
# A fixture with an empty .expected-stderr asserts silence, which is what a
# JOINED task must produce: its exception is the joiner's, and reporting it
# too would print on every handled failure.

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0

echo "======================================"
echo " Hemlock Spawned-Task Error Suite"
echo "======================================"
echo ""

if [ -f "../../Makefile" ]; then
    PROJECT_ROOT="$(cd ../.. && pwd)"
elif [ -f "Makefile" ]; then
    PROJECT_ROOT="$(pwd)"
else
    echo -e "${RED}✗ Cannot find Makefile${NC}"
    exit 1
fi

HEMLOCK="$PROJECT_ROOT/hemlock"
HEMLOCKC="$PROJECT_ROOT/hemlockc"
TEST_DIR="$PROJECT_ROOT/tests/task_errors"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cd "$TEST_DIR" || exit 1

# Is a C toolchain + runtime available to build the compiled half?
COMPILED=1
if [ ! -f "$PROJECT_ROOT/libhemlock_runtime.a" ] && \
   [ ! -f "$PROJECT_ROOT/runtime/build/libhemlock_runtime.a" ]; then
    echo -e "${BLUE}Building runtime...${NC}"
    make -C "$PROJECT_ROOT" runtime > "$WORK/runtime.log" 2>&1 || COMPILED=0
fi

# check <test> <tool> <expected-stdout> <expected-stderr-first-line> <stdout> <stderr>
compare() {
    local name="$1" tool="$2" exp_out="$3" exp_err="$4" out="$5" err="$6"
    local first_err
    first_err=$(printf '%s\n' "$err" | head -n 1)
    if [ "$out" = "$exp_out" ] && [ "$first_err" = "$exp_err" ]; then
        return 0
    fi
    echo -e "${RED}✗${NC} $name ($tool)"
    echo "  expected stdout: $exp_out"
    echo "  actual   stdout: $out"
    echo "  expected stderr: $exp_err"
    echo "  actual   stderr: $first_err"
    return 1
}

for test_file in *.hml; do
    [ -f "$test_file" ] || continue
    test_name="${test_file%.hml}"
    [ -f "$test_name.expected-stdout" ] || continue

    expected_out=$(cat "$test_name.expected-stdout")
    expected_err=$(head -n 1 "$test_name.expected-stderr")
    failed=0

    out=$("$HEMLOCK" "$test_file" 2>"$WORK/err")
    err=$(cat "$WORK/err")
    compare "$test_name" hemlock "$expected_out" "$expected_err" "$out" "$err" || failed=1

    if [ "$COMPILED" -eq 1 ]; then
        if "$HEMLOCKC" "$test_file" -o "$WORK/$test_name.bin" \
                > "$WORK/compile.log" 2>&1; then
            out=$(LD_LIBRARY_PATH="$PROJECT_ROOT:$LD_LIBRARY_PATH" \
                "$WORK/$test_name.bin" 2>"$WORK/err")
            err=$(cat "$WORK/err")
            compare "$test_name" hemlockc "$expected_out" "$expected_err" \
                "$out" "$err" || failed=1
        else
            echo -e "${RED}✗${NC} $test_name (hemlockc: compile failed)"
            cat "$WORK/compile.log" | sed 's/^/    /'
            failed=1
        fi
    fi

    if [ "$failed" -eq 0 ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        ((PASS_COUNT++))
    else
        ((FAIL_COUNT++))
    fi
done

if [ "$COMPILED" -eq 0 ]; then
    echo -e "${BLUE}note:${NC} compiled half skipped (runtime unavailable)"
fi

echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Passed:${NC}  $PASS_COUNT"
echo -e "${RED}Failed:${NC}  $FAIL_COUNT"
echo "======================================"

if [ "$FAIL_COUNT" -eq 0 ]; then
    echo -e "${GREEN}All spawned-task error tests passed! 🎉${NC}"
    exit 0
fi
echo -e "${RED}Some spawned-task error tests failed.${NC}"
exit 1
