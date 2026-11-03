#!/bin/bash

# Hemlock stdin / exit Test Runner
#
# Covers the deadlock that made `exit()` never return, and the read_line()
# semantics the fix has to preserve.
#
# getline(stdin) holds stdin's stdio lock for as long as it is blocked, and
# glibc >= 2.39 takes every stream's lock during the exit-time flush, so a
# program with one task blocked in read_line() and another calling exit()
# hung forever (glibc 2.35 did not lock there, so it only showed on some
# hosts). read_line() now reads through src/shared/stdin_lines.c instead.
#
# Two kinds of fixture, both run against the interpreter AND a compiled
# binary, since the runtime had the same bug:
#
#   <name>.hml + <name>.exit      - must exit with that status, not hang.
#                                   stdin is held open by a sleeping writer,
#                                   which is what kept the reader blocked.
#   <name>.hml + <name>.expected  - stdout must match exactly, with
#                                   <name>.stdin fed in (if present).

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0
TIMEOUT=10

echo "======================================"
echo "    Hemlock stdin / exit Suite"
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
TEST_DIR="$PROJECT_ROOT/tests/stdin_exit"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cd "$TEST_DIR" || exit 1

COMPILED=1
if [ ! -f "$PROJECT_ROOT/libhemlock_runtime.a" ] && \
   [ ! -f "$PROJECT_ROOT/runtime/build/libhemlock_runtime.a" ]; then
    echo -e "${BLUE}Building runtime...${NC}"
    make -C "$PROJECT_ROOT" runtime > "$WORK/runtime.log" 2>&1 || COMPILED=0
fi

# Run one program with stdin held open by a writer that never writes, so a
# read_line() in it stays blocked. Process substitution keeps the shell from
# waiting on that writer, so the status is the program's own.
run_blocked() {
    timeout "$TIMEOUT" "$@" < <(sleep 60) > "$WORK/out" 2> "$WORK/err"
    echo $?
}

for test_file in *.hml; do
    [ -f "$test_file" ] || continue
    test_name="${test_file%.hml}"
    exit_file="$test_name.exit"
    expected_file="$test_name.expected"
    failed=0

    binary=""
    if [ "$COMPILED" -eq 1 ]; then
        if "$HEMLOCKC" "$test_file" -o "$WORK/$test_name.bin" > "$WORK/c.log" 2>&1; then
            binary="$WORK/$test_name.bin"
        else
            echo -e "${RED}✗${NC} $test_name (hemlockc: compile failed)"
            sed 's/^/    /' "$WORK/c.log"
            failed=1
        fi
    fi

    if [ -f "$exit_file" ]; then
        want=$(cat "$exit_file")
        for tool in interpreter compiled; do
            [ "$tool" = "compiled" ] && [ -z "$binary" ] && continue
            if [ "$tool" = "interpreter" ]; then
                got=$(run_blocked "$HEMLOCK" "$test_file")
            else
                got=$(LD_LIBRARY_PATH="$PROJECT_ROOT:$LD_LIBRARY_PATH" run_blocked "$binary")
            fi
            if [ "$got" != "$want" ]; then
                if [ "$got" = "124" ]; then
                    echo -e "${RED}✗${NC} $test_name ($tool): hung (timed out after ${TIMEOUT}s), wanted exit $want"
                else
                    echo -e "${RED}✗${NC} $test_name ($tool): exit $got, wanted $want"
                fi
                failed=1
            fi
        done
    fi

    if [ -f "$expected_file" ]; then
        want=$(cat "$expected_file")
        stdin_file="$test_name.stdin"
        [ -f "$stdin_file" ] || stdin_file=/dev/null
        for tool in interpreter compiled; do
            [ "$tool" = "compiled" ] && [ -z "$binary" ] && continue
            if [ "$tool" = "interpreter" ]; then
                got=$(timeout "$TIMEOUT" "$HEMLOCK" "$test_file" < "$stdin_file" 2>&1)
            else
                got=$(LD_LIBRARY_PATH="$PROJECT_ROOT:$LD_LIBRARY_PATH" \
                    timeout "$TIMEOUT" "$binary" < "$stdin_file" 2>&1)
            fi
            if [ "$got" != "$want" ]; then
                echo -e "${RED}✗${NC} $test_name ($tool) output:"
                diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") | sed 's/^/    /'
                failed=1
            fi
        done
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
    echo -e "${GREEN}All stdin/exit tests passed! 🎉${NC}"
    exit 0
fi
echo -e "${RED}Some stdin/exit tests failed.${NC}"
exit 1
