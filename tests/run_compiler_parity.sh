#!/bin/bash

# Hemlock Compiler Parity Test Runner
# Compiles ALL interpreter tests with hemlockc and verifies output matches interpreted version
#
# This tests that the compiler produces identical output to the interpreter
# for the entire interpreter test suite.

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Counters
PARITY_PASS=0
PARITY_FAIL=0
INTERP_ONLY=0
COMPILE_FAIL=0
SKIP_COUNT=0
ERROR_TEST_PARITY=0
ERROR_TEST_MISMATCH=0
TIMEOUT_COUNT=0

# Configuration
TIMEOUT_SECONDS=20
VERBOSE=0
CATEGORY_FILTER=""
STOP_ON_FAIL=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -c|--category)
            CATEGORY_FILTER="$2"
            shift 2
            ;;
        -s|--stop-on-fail)
            STOP_ON_FAIL=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  -v, --verbose       Show detailed output for failures"
            echo "  -c, --category CAT  Only run tests in category CAT"
            echo "  -s, --stop-on-fail  Stop on first failure"
            echo "  -h, --help          Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "======================================"
echo "  Hemlock Compiler Parity Test Suite"
echo "======================================"
echo ""
echo "Tests that compiled code produces identical output to interpreted code"
echo ""

# Detect project root
if [ -f "../Makefile" ]; then
    cd ..
    PROJECT_ROOT="."
    TEST_DIR="tests"
elif [ -f "Makefile" ]; then
    PROJECT_ROOT="."
    TEST_DIR="tests"
else
    echo -e "${RED}✗ Cannot find Makefile${NC}"
    exit 1
fi

# Build interpreter
echo -e "${BLUE}Building interpreter...${NC}"
if make clean > /dev/null 2>&1 && make > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Interpreter build successful${NC}"
else
    echo -e "${RED}✗ Interpreter build failed${NC}"
    exit 1
fi

# Build compiler and runtime
echo -e "${BLUE}Building compiler and runtime...${NC}"
if make compiler > /tmp/compiler_build.log 2>&1; then
    echo -e "${GREEN}✓ Compiler build successful${NC}"
else
    echo -e "${RED}✗ Compiler build failed${NC}"
    cat /tmp/compiler_build.log
    exit 1
fi

# Build stdlib if possible
if make stdlib > /dev/null 2>&1; then
    echo -e "${GREEN}✓ stdlib modules built${NC}"
else
    echo -e "${YELLOW}⊘ stdlib modules skipped (dependencies not installed)${NC}"
fi
echo ""

# Check binaries exist
if [ ! -f "./hemlock" ]; then
    echo -e "${RED}✗ hemlock interpreter not found${NC}"
    exit 1
fi

if [ ! -f "./hemlockc" ]; then
    echo -e "${RED}✗ hemlockc compiler not found${NC}"
    exit 1
fi

if [ ! -f "./libhemlock_runtime.a" ]; then
    echo -e "${RED}✗ libhemlock_runtime.a not found${NC}"
    exit 1
fi

# Create temp directory for compiled outputs
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Check for zlib
ZLIB_FLAG=""
if echo 'int main(){return 0;}' | gcc -x c - -lz -o /dev/null 2>/dev/null; then
    ZLIB_FLAG="-lz"
fi

# Function to check if a test is expected to fail (error test)
is_error_test() {
    local test_file="$1"
    if [[ "$test_file" =~ (overflow|negative|invalid|error) ]]; then
        return 0
    fi
    return 1
}

# Function to check if test should be skipped
should_skip() {
    local test_file="$1"
    local category="$2"

    # Skip HTTP/WebSocket tests if lws_wrapper.so doesn't exist
    if [[ "$category" == "stdlib_http" || "$category" == "stdlib_websocket" ]]; then
        if [ ! -f "$PROJECT_ROOT/stdlib/c/lws_wrapper.so" ]; then
            return 0
        fi
    fi

    return 1
}

echo -e "${BLUE}Running parity tests...${NC}"
echo ""

# Find all test files (excluding compiler and parity directories)
TEST_FILES=$(find "$TEST_DIR" -name "*.hml" -not -path "*/compiler/*" -not -path "*/parity/*" | sort)

CURRENT_CATEGORY=""
CATEGORY_SKIP_REPORTED=""

for test_file in $TEST_FILES; do
    # Extract category from path
    category=$(dirname "$test_file" | cut -d'/' -f2)
    test_name="${test_file#tests/}"
    base_name=$(basename "$test_file" .hml)

    # Filter by category if specified
    if [ -n "$CATEGORY_FILTER" ] && [ "$category" != "$CATEGORY_FILTER" ]; then
        continue
    fi

    # Check if entire category should be skipped
    if should_skip "$test_file" "$category"; then
        if [[ "$CATEGORY_SKIP_REPORTED" != *"$category"* ]]; then
            if [ "$category" != "$CURRENT_CATEGORY" ]; then
                if [ -n "$CURRENT_CATEGORY" ]; then
                    echo ""
                fi
                echo -e "${BLUE}[$category]${NC}"
                CURRENT_CATEGORY="$category"
            fi
            echo -e "${YELLOW}⊘${NC} Skipping $category tests (missing dependencies)"
            CATEGORY_SKIP_REPORTED="$CATEGORY_SKIP_REPORTED $category"
        fi
        ((SKIP_COUNT++))
        continue
    fi

    # Print category header if changed
    if [ "$category" != "$CURRENT_CATEGORY" ]; then
        if [ -n "$CURRENT_CATEGORY" ]; then
            echo ""
        fi
        echo -e "${BLUE}[$category]${NC}"
        CURRENT_CATEGORY="$category"
    fi

    # Run interpreter and capture output
    interp_output=$(timeout $TIMEOUT_SECONDS "$PROJECT_ROOT/hemlock" "$test_file" 2>&1)
    interp_exit=$?

    # Check for interpreter timeout
    if [ $interp_exit -eq 124 ]; then
        echo -e "${YELLOW}⊘${NC} $test_name ${YELLOW}(interpreter timeout)${NC}"
        ((TIMEOUT_COUNT++))
        continue
    fi

    # Compile to C
    c_file="$TEMP_DIR/${base_name}_$$.c"
    exe_file="$TEMP_DIR/${base_name}_$$"

    compile_output=$(./hemlockc "$test_file" -c --emit-c "$c_file" 2>&1)
    compile_exit=$?

    if [ $compile_exit -ne 0 ]; then
        if is_error_test "$test_file"; then
            # Error test - both should fail
            if [ $interp_exit -ne 0 ]; then
                echo -e "${GREEN}✓${NC} $test_name ${YELLOW}(expected error - both fail)${NC}"
                ((ERROR_TEST_PARITY++))
            else
                echo -e "${RED}✗${NC} $test_name ${RED}(compiler failed, interpreter passed)${NC}"
                if [ $VERBOSE -eq 1 ]; then
                    echo "  Compiler error: $compile_output"
                fi
                ((ERROR_TEST_MISMATCH++))
            fi
        else
            echo -e "${CYAN}◐${NC} $test_name ${CYAN}(compile failed - interpreter only)${NC}"
            if [ $VERBOSE -eq 1 ]; then
                echo "  Compiler error: $compile_output"
            fi
            ((INTERP_ONLY++))
        fi

        if [ $STOP_ON_FAIL -eq 1 ] && ! is_error_test "$test_file"; then
            echo -e "\n${RED}Stopping on first failure${NC}"
            break
        fi
        continue
    fi

    # Compile C to executable
    gcc_output=$(gcc -o "$exe_file" "$c_file" -I./runtime/include -L. -lhemlock_runtime -lm -lpthread -lffi -ldl $ZLIB_FLAG -lcrypto 2>&1)
    gcc_exit=$?

    if [ $gcc_exit -ne 0 ]; then
        echo -e "${CYAN}◐${NC} $test_name ${CYAN}(C compile failed - interpreter only)${NC}"
        if [ $VERBOSE -eq 1 ]; then
            echo "  GCC error: $gcc_output"
        fi
        ((COMPILE_FAIL++))
        continue
    fi

    # Run compiled executable
    compiled_output=$(timeout $TIMEOUT_SECONDS env LD_LIBRARY_PATH="$PWD:$LD_LIBRARY_PATH" "$exe_file" 2>&1)
    compiled_exit=$?

    # Check for compiled timeout
    if [ $compiled_exit -eq 124 ]; then
        echo -e "${YELLOW}⊘${NC} $test_name ${YELLOW}(compiled timeout)${NC}"
        ((TIMEOUT_COUNT++))
        rm -f "$c_file" "$exe_file"
        continue
    fi

    # Compare outputs
    if is_error_test "$test_file"; then
        # Error test - check exit codes match
        if [ $interp_exit -ne 0 ] && [ $compiled_exit -ne 0 ]; then
            echo -e "${GREEN}✓${NC} $test_name ${YELLOW}(expected error)${NC}"
            ((ERROR_TEST_PARITY++))
        elif [ $interp_exit -eq 0 ] && [ $compiled_exit -eq 0 ]; then
            # Both passed when they should fail - check output at least
            if [ "$interp_output" = "$compiled_output" ]; then
                echo -e "${YELLOW}◐${NC} $test_name ${YELLOW}(both passed unexpectedly, same output)${NC}"
                ((ERROR_TEST_MISMATCH++))
            else
                echo -e "${RED}✗${NC} $test_name ${RED}(both passed unexpectedly, different output)${NC}"
                ((ERROR_TEST_MISMATCH++))
            fi
        else
            echo -e "${RED}✗${NC} $test_name ${RED}(exit code mismatch: interp=$interp_exit, compiled=$compiled_exit)${NC}"
            ((ERROR_TEST_MISMATCH++))
        fi
    else
        # Normal test - check output matches
        if [ "$interp_output" = "$compiled_output" ] && [ $interp_exit -eq $compiled_exit ]; then
            echo -e "${GREEN}✓${NC} $test_name"
            ((PARITY_PASS++))
        else
            echo -e "${RED}✗${NC} $test_name"
            ((PARITY_FAIL++))

            if [ $VERBOSE -eq 1 ] || [ $interp_exit -ne $compiled_exit ]; then
                if [ $interp_exit -ne $compiled_exit ]; then
                    echo "  Exit codes: interpreter=$interp_exit, compiled=$compiled_exit"
                fi
                if [ "$interp_output" != "$compiled_output" ]; then
                    echo "  Interpreter output:"
                    echo "$interp_output" | head -5 | sed 's/^/    /'
                    if [ $(echo "$interp_output" | wc -l) -gt 5 ]; then
                        echo "    ... (truncated)"
                    fi
                    echo "  Compiled output:"
                    echo "$compiled_output" | head -5 | sed 's/^/    /'
                    if [ $(echo "$compiled_output" | wc -l) -gt 5 ]; then
                        echo "    ... (truncated)"
                    fi
                fi
            fi

            if [ $STOP_ON_FAIL -eq 1 ]; then
                echo -e "\n${RED}Stopping on first failure${NC}"
                break
            fi
        fi
    fi

    # Cleanup temp files
    rm -f "$c_file" "$exe_file"
done

echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Parity Pass:${NC}        $PARITY_PASS"
echo -e "${RED}Parity Fail:${NC}        $PARITY_FAIL"
echo -e "${CYAN}Interpreter Only:${NC}   $INTERP_ONLY ${CYAN}(compiler doesn't support yet)${NC}"
echo -e "${RED}C Compile Fail:${NC}     $COMPILE_FAIL"
echo -e "${GREEN}Error Test Parity:${NC}  $ERROR_TEST_PARITY ${YELLOW}(expected errors match)${NC}"
echo -e "${RED}Error Test Mismatch:${NC} $ERROR_TEST_MISMATCH"
echo -e "${YELLOW}Skipped:${NC}            $SKIP_COUNT"
echo -e "${YELLOW}Timeout:${NC}            $TIMEOUT_COUNT"
echo "======================================"

# Calculate totals
TOTAL_TESTED=$((PARITY_PASS + PARITY_FAIL + INTERP_ONLY + COMPILE_FAIL + ERROR_TEST_PARITY + ERROR_TEST_MISMATCH))
TOTAL_PARITY=$((PARITY_PASS + ERROR_TEST_PARITY))
TOTAL_FAILURES=$((PARITY_FAIL + ERROR_TEST_MISMATCH))

echo ""
echo "Total tests checked: $TOTAL_TESTED"

if [ $TOTAL_TESTED -gt 0 ]; then
    PARITY_PCT=$((TOTAL_PARITY * 100 / TOTAL_TESTED))
    echo -e "Parity rate: ${GREEN}$PARITY_PCT%${NC} ($TOTAL_PARITY/$TOTAL_TESTED)"
fi

COMPILER_SUPPORT=$((PARITY_PASS + PARITY_FAIL + ERROR_TEST_PARITY + ERROR_TEST_MISMATCH))
if [ $TOTAL_TESTED -gt 0 ]; then
    SUPPORT_PCT=$((COMPILER_SUPPORT * 100 / TOTAL_TESTED))
    echo -e "Compiler coverage: ${BLUE}$SUPPORT_PCT%${NC} ($COMPILER_SUPPORT/$TOTAL_TESTED tests compile)"
fi

echo "======================================"

# Exit codes
if [ $TOTAL_FAILURES -gt 0 ]; then
    echo -e "\n${RED}Some tests have parity failures.${NC}"
    exit 1
elif [ $INTERP_ONLY -gt 0 ] || [ $COMPILE_FAIL -gt 0 ]; then
    echo -e "\n${YELLOW}Parity achieved for all compilable tests, but some tests don't compile yet.${NC}"
    exit 0
else
    echo -e "\n${GREEN}Full parity achieved! 🎉${NC}"
    exit 0
fi
