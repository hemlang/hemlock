#!/bin/bash

# Hemlock Name Resolution Test Runner
#
# The frontend name resolver (src/frontend/resolver/name_resolve.c) is shared
# by every tool, so these fixtures assert that hemlockc and the interpreter
# report an undefined name with the SAME text: same file, same line, same
# message. `hemlock check` runs the same pass and is covered by the fixtures in
# tests/check (names_*.hml), which assert its own diagnostic format.
#
# For each tests/names/<name>.hml there is a <name>.expected file holding the
# exact stderr both tools should produce. Both must also exit 1 - an undefined
# name is a hard error, not a warning.
#
# The tools are run from inside this directory so the path prefix in
# diagnostics is just "<name>.hml". A fixture whose undefined name lives in an
# imported module names that module instead, which is the point of
# names_in_imported_module.hml.
#
# Convention: fixtures named neg_*.hml have an empty .expected and must exit 0
# - they guard against false positives. Fixtures with no .expected at all are
# helper modules for other fixtures to import.

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0

echo "======================================"
echo "  Hemlock Name Resolution Test Suite"
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
TEST_DIR="$PROJECT_ROOT/tests/names"

for tool in "$HEMLOCK" "$HEMLOCKC"; do
    if [ ! -x "$tool" ]; then
        echo -e "${BLUE}Building...${NC}"
        if ! make -C "$PROJECT_ROOT" hemlock compiler > /tmp/name_build.log 2>&1; then
            echo -e "${RED}✗ Build failed${NC}"
            cat /tmp/name_build.log
            exit 1
        fi
        break
    fi
done

cd "$TEST_DIR" || exit 1
TMP_C=$(mktemp)
trap 'rm -f "$TMP_C"' EXIT

# report <test> <tool> <expected-exit> <actual-exit> <expected> <actual>
report_failure() {
    echo -e "${RED}✗${NC} $1 ($2)"
    echo "  Expected (exit $3):"
    echo "$5" | sed 's/^/    /'
    echo "  Actual (exit $4):"
    echo "$6" | sed 's/^/    /'
}

for test_file in *.hml; do
    [ -f "$test_file" ] || continue
    test_name="${test_file%.hml}"
    expected_file="$test_name.expected"

    # No .expected: a helper module imported by another fixture.
    [ -f "$expected_file" ] || continue

    expected=$(cat "$expected_file")
    if [ -z "$expected" ]; then
        expected_exit=0
    else
        expected_exit=1
    fi

    failed=0

    # hemlockc: stop at C emission, so a fixture that resolves cleanly does not
    # need a working C toolchain. Lint and borrow checks off so only name
    # diagnostics appear.
    c_actual=$("$HEMLOCKC" --no-lint --no-borrow-check -c --emit-c "$TMP_C" \
        "$test_file" 2>&1 1>/dev/null)
    c_exit=$?
    if [ "$c_actual" != "$expected" ] || [ "$c_exit" -ne "$expected_exit" ]; then
        report_failure "$test_name" hemlockc "$expected_exit" "$c_exit" \
            "$expected" "$c_actual"
        failed=1
    fi

    # Interpreter: the same pass runs at startup, before a line executes, so
    # stdout stays empty for a fixture with an undefined name.
    i_actual=$("$HEMLOCK" "$test_file" 2>&1 1>/dev/null)
    i_exit=$?
    if [ "$i_actual" != "$expected" ] || [ "$i_exit" -ne "$expected_exit" ]; then
        report_failure "$test_name" hemlock "$expected_exit" "$i_exit" \
            "$expected" "$i_actual"
        failed=1
    fi

    if [ "$failed" -eq 0 ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        ((PASS_COUNT++))
    else
        ((FAIL_COUNT++))
    fi
done

echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Passed:${NC}  $PASS_COUNT"
echo -e "${RED}Failed:${NC}  $FAIL_COUNT"
echo "======================================"

if [ "$FAIL_COUNT" -eq 0 ]; then
    echo -e "${GREEN}All name resolution tests passed! 🎉${NC}"
    exit 0
fi
echo -e "${RED}Some name resolution tests failed.${NC}"
exit 1
