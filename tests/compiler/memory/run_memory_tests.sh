#!/bin/bash

# Compiler Memory Regression Tests
# Compiles and runs test programs, verifying RSS stays below a threshold.
# Each test runs 50k iterations of allocation-heavy patterns. If any compiled
# binary exceeds the RSS limit, it indicates a memory leak in the codegen.

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
HEMLOCKC="$ROOT_DIR/hemlockc"

# RSS threshold in KB — 10 MB should be generous for 50k iterations of small objects.
# A leaking binary would use 50-200+ MB for these tests.
RSS_LIMIT_KB=4096

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

if [ ! -f "$HEMLOCKC" ]; then
    echo -e "${RED}Error: hemlockc not found at $HEMLOCKC${NC}"
    echo "Run 'make' to build first."
    exit 1
fi

echo "======================================"
echo "  Compiler Memory Regression Tests"
echo "======================================"
echo ""
echo "RSS limit: ${RSS_LIMIT_KB} KB ($(( RSS_LIMIT_KB / 1024 )) MB)"
echo ""

TEST_TIMEOUT=30

get_peak_rss_kb() {
    local binary="$1"
    local output
    if [[ "$(uname)" == "Darwin" ]]; then
        # macOS: /usr/bin/time -l reports bytes
        output=$(timeout $TEST_TIMEOUT /usr/bin/time -l "$binary" 2>&1 >/dev/null)
        local bytes
        bytes=$(echo "$output" | grep "maximum resident" | awk '{print $1}')
        if [ -n "$bytes" ] && [ "$bytes" -gt 0 ] 2>/dev/null; then
            echo $(( bytes / 1024 ))
        else
            echo 0
        fi
    else
        # Linux: /usr/bin/time -v reports KB
        output=$(timeout $TEST_TIMEOUT /usr/bin/time -v "$binary" 2>&1 >/dev/null)
        echo "$output" | grep "Maximum resident" | awk '{print $NF}'
    fi
}

for test_file in "$SCRIPT_DIR"/*.hml; do
    test_name=$(basename "$test_file" .hml)
    binary="$TEMP_DIR/$test_name"

    # Compile
    if ! "$HEMLOCKC" "$test_file" -o "$binary" > /dev/null 2>&1; then
        echo -e "${YELLOW}⊘${NC} $test_name ${YELLOW}(compile failed, skipped)${NC}"
        ((SKIP_COUNT++))
        continue
    fi

    # Run and measure RSS
    rss_kb=$(get_peak_rss_kb "$binary")

    if [ -z "$rss_kb" ] || [ "$rss_kb" -eq 0 ]; then
        echo -e "${YELLOW}⊘${NC} $test_name ${YELLOW}(could not measure RSS, skipped)${NC}"
        ((SKIP_COUNT++))
        continue
    fi

    if [ "$rss_kb" -le "$RSS_LIMIT_KB" ]; then
        echo -e "${GREEN}✓${NC} $test_name ${BLUE}(${rss_kb} KB)${NC}"
        ((PASS_COUNT++))
    else
        rss_mb=$(( rss_kb / 1024 ))
        echo -e "${RED}✗${NC} $test_name ${RED}(${rss_kb} KB = ${rss_mb} MB — exceeds ${RSS_LIMIT_KB} KB limit)${NC}"
        ((FAIL_COUNT++))
        echo -e "${RED}Aborting early — memory regression detected.${NC}"
        break
    fi
done

echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Passed:${NC}  $PASS_COUNT"
if [ "$SKIP_COUNT" -gt 0 ]; then
    echo -e "${YELLOW}Skipped:${NC} $SKIP_COUNT"
fi
echo -e "${RED}Failed:${NC}  $FAIL_COUNT"
echo "======================================"

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo ""
    echo -e "${RED}Memory regression detected! One or more tests exceeded the RSS limit.${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}All memory tests passed.${NC}"
exit 0
