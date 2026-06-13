#!/bin/bash

# Hemlock Borrow Checker Test Runner
#
# For each tests/borrow/<name>.hml there is a <name>.expected file holding the
# borrow-check diagnostics the compiler should emit (without the leading path,
# i.e. lines of the form "<name>.hml:LINE: warning: ...", in order). An optional
# <name>.flags file supplies extra hemlockc flags (e.g. --borrow-strict).
#
# The compiler is invoked so it only emits C (no linking); stdout is discarded
# and stderr (the diagnostics) is captured and compared. The compiler is run
# from inside this directory so the path prefix is just "<name>.hml".

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0

echo "======================================"
echo "   Hemlock Borrow Checker Test Suite"
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

HEMLOCKC="$PROJECT_ROOT/hemlockc"
TEST_DIR="$PROJECT_ROOT/tests/borrow"

if [ ! -x "$HEMLOCKC" ]; then
    echo -e "${BLUE}Building compiler...${NC}"
    if ! make -C "$PROJECT_ROOT" compiler > /tmp/borrow_build.log 2>&1; then
        echo -e "${RED}✗ Build failed${NC}"
        cat /tmp/borrow_build.log
        exit 1
    fi
fi

cd "$TEST_DIR" || exit 1
TMP_C=$(mktemp)
trap "rm -f $TMP_C" EXIT

for test_file in *.hml; do
    [ -f "$test_file" ] || continue
    test_name="${test_file%.hml}"
    expected_file="${test_name}.expected"
    flags_file="${test_name}.flags"

    [ -f "$expected_file" ] || continue

    extra_flags=""
    [ -f "$flags_file" ] && extra_flags="$(cat "$flags_file")"

    # Compile to C only; capture stderr diagnostics, discard stdout/C output.
    actual=$("$HEMLOCKC" $extra_flags -c --emit-c "$TMP_C" "$test_file" 2>&1 1>/dev/null)
    expected=$(cat "$expected_file")

    if [ "$actual" = "$expected" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        ((PASS_COUNT++))
    else
        echo -e "${RED}✗${NC} $test_name"
        echo "  Expected:"
        echo "$expected" | sed 's/^/    /'
        echo "  Actual:"
        echo "$actual" | sed 's/^/    /'
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

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All borrow checker tests passed! 🎉${NC}"
    exit 0
else
    echo -e "${RED}Some borrow checker tests failed.${NC}"
    exit 1
fi
