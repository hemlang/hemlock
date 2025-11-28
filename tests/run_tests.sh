#!/bin/bash

# Hemlock Test Runner
# Runs all tests and reports results

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
PASS_COUNT=0
FAIL_COUNT=0
ERROR_TEST_COUNT=0
UNEXPECTED_FAIL_COUNT=0

echo "======================================"
echo "   Hemlock Interpreter Test Suite"
echo "======================================"
echo ""

# Build the project
echo -e "${BLUE}Building hemlock...${NC}"
# Detect if we're in the tests directory or project root
if [ -f "../Makefile" ]; then
    # We're in tests directory - change to project root
    cd ..
    PROJECT_ROOT="."
    TEST_DIR="tests"
elif [ -f "Makefile" ]; then
    # We're in project root
    PROJECT_ROOT="."
    TEST_DIR="tests"
else
    echo -e "${RED}✗ Cannot find Makefile${NC}"
    exit 1
fi

# Build from project root
if make clean > /dev/null 2>&1 && make > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

# Build stdlib modules (if dependencies available)
if make stdlib > /dev/null 2>&1; then
    echo -e "${GREEN}✓ stdlib modules built${NC}"
else
    echo -e "${YELLOW}⊘ stdlib modules skipped (dependencies not installed)${NC}"
fi
echo ""

# Function to check if a test is expected to fail (error test)
is_error_test() {
    local test_file="$1"
    # Tests with these keywords in their name are expected to fail
    if [[ "$test_file" =~ (overflow|negative|invalid|error) ]]; then
        return 0
    fi
    return 1
}

echo -e "${BLUE}Running tests...${NC}"
echo ""

# Find all test files (excluding compiler and parity directories which have their own test runners)
TEST_FILES=$(find "$TEST_DIR" -name "*.hml" -not -path "*/compiler/*" -not -path "*/parity/*" | sort)

CURRENT_CATEGORY=""
for test_file in $TEST_FILES; do
    # Extract category from path (always relative to tests/)
    category=$(dirname "$test_file" | cut -d'/' -f2)
    test_name="${test_file#tests/}"

    # Skip HTTP/WebSocket tests if lws_wrapper.so doesn't exist
    if [[ "$category" == "stdlib_http" || "$category" == "stdlib_websocket" ]]; then
        if [ ! -f "$PROJECT_ROOT/stdlib/c/lws_wrapper.so" ]; then
            # Only print the skip message once per category
            if [ "$category" != "$CURRENT_CATEGORY" ]; then
                if [ -n "$CURRENT_CATEGORY" ]; then
                    echo ""
                fi
                echo -e "${BLUE}[$category]${NC}"
                echo -e "${YELLOW}⊘${NC} Skipping $category tests (libwebsockets not installed)"
                echo "  Run 'sudo apt-get install libwebsockets-dev && make stdlib' to enable"
                CURRENT_CATEGORY="$category"
            fi
            continue
        fi
    fi

    # Print category header if changed
    if [ "$category" != "$CURRENT_CATEGORY" ]; then
        if [ -n "$CURRENT_CATEGORY" ]; then
            echo ""
        fi
        echo -e "${BLUE}[$category]${NC}"
        CURRENT_CATEGORY="$category"
    fi

    # Run the test with timeout and capture output and exit code
    output=$(timeout 20 "$PROJECT_ROOT/hemlock" "$test_file" 2>&1)
    exit_code=$?

    # Check if timeout occurred
    if [ $exit_code -eq 124 ]; then
        echo -e "${RED}✗${NC} $test_name ${RED}(timeout)${NC}"
        ((FAIL_COUNT++))
        continue
    fi

    if is_error_test "$test_file"; then
        # This is an error test - we expect it to fail
        if [ $exit_code -ne 0 ]; then
            echo -e "${GREEN}✓${NC} $test_name ${YELLOW}(expected error)${NC}"
            ((ERROR_TEST_COUNT++))
        else
            echo -e "${RED}✗${NC} $test_name ${RED}(should have failed!)${NC}"
            echo "  Output: $output"
            ((UNEXPECTED_FAIL_COUNT++))
        fi
    else
        # Regular test - we expect it to pass
        if [ $exit_code -eq 0 ]; then
            echo -e "${GREEN}✓${NC} $test_name"
            ((PASS_COUNT++))
        else
            echo -e "${RED}✗${NC} $test_name"
            echo "  Error: $output"
            ((FAIL_COUNT++))
        fi
    fi
done

echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Passed:${NC}           $PASS_COUNT"
echo -e "${YELLOW}Error tests:${NC}      $ERROR_TEST_COUNT ${YELLOW}(expected failures)${NC}"
echo -e "${RED}Failed:${NC}           $FAIL_COUNT"
if [ $UNEXPECTED_FAIL_COUNT -gt 0 ]; then
    echo -e "${RED}Unexpected:${NC}       $UNEXPECTED_FAIL_COUNT ${RED}(error tests that passed!)${NC}"
fi
echo "======================================"

# Calculate total
TOTAL=$((PASS_COUNT + ERROR_TEST_COUNT))
TOTAL_TESTS=$((TOTAL + FAIL_COUNT + UNEXPECTED_FAIL_COUNT))

echo ""
if [ $FAIL_COUNT -eq 0 ] && [ $UNEXPECTED_FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All $TOTAL_TESTS tests behaved as expected! 🎉${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed. Please review the output above.${NC}"
    exit 1
fi
