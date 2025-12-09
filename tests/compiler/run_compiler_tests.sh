#!/bin/bash

# Hemlock Compiler Test Runner
# Compiles Hemlock source to C, then to native executable, and verifies output

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

echo "======================================"
echo "   Hemlock Compiler Test Suite"
echo "======================================"
echo ""

# Detect project root
if [ -f "../../Makefile" ]; then
    PROJECT_ROOT="../.."
    TEST_DIR="."
elif [ -f "Makefile" ]; then
    PROJECT_ROOT="."
    TEST_DIR="tests/compiler"
else
    echo -e "${RED}✗ Cannot find Makefile${NC}"
    exit 1
fi

cd "$PROJECT_ROOT"

# Build compiler and runtime
echo -e "${BLUE}Building compiler and runtime...${NC}"
if make compiler > /tmp/compiler_build.log 2>&1; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    cat /tmp/compiler_build.log
    exit 1
fi
echo ""

# Check that hemlockc exists
if [ ! -f "./hemlockc" ]; then
    echo -e "${RED}✗ hemlockc not found${NC}"
    exit 1
fi

# Check that runtime library exists
if [ ! -f "./libhemlock_runtime.a" ]; then
    echo -e "${RED}✗ libhemlock_runtime.a not found${NC}"
    exit 1
fi

echo -e "${BLUE}Running compiler tests...${NC}"
echo ""

# Create temp directory for compiled outputs
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Find all test files with .expected files
for test_file in "$TEST_DIR"/*.hml; do
    if [ ! -f "$test_file" ]; then
        continue
    fi

    test_name=$(basename "$test_file" .hml)
    expected_file="${test_file%.hml}.expected"

    # Check if expected file exists
    if [ ! -f "$expected_file" ]; then
        echo -e "${YELLOW}⊘${NC} $test_name ${YELLOW}(no .expected file)${NC}"
        ((SKIP_COUNT++))
        continue
    fi

    # Compile Hemlock to C
    c_file="$TEMP_DIR/${test_name}.c"
    if ! ./hemlockc "$test_file" -c --emit-c "$c_file" > /tmp/hemlockc_err.log 2>&1; then
        echo -e "${RED}✗${NC} $test_name ${RED}(compilation to C failed)${NC}"
        cat /tmp/hemlockc_err.log
        ((FAIL_COUNT++))
        continue
    fi

    # Compile C to executable
    # Check if zlib is available (same check as runtime Makefile)
    ZLIB_FLAG=""
    if echo 'int main(){return 0;}' | gcc -x c - -lz -o /dev/null 2>/dev/null; then
        ZLIB_FLAG="-lz"
    fi
    # Check if libwebsockets is available
    LWS_FLAG=""
    if echo 'int main(){return 0;}' | gcc -x c - -lwebsockets -o /dev/null 2>/dev/null; then
        LWS_FLAG="-lwebsockets"
    fi
    # libcrypto is always required for hash functions
    CRYPTO_FLAG="-lcrypto"
    exe_file="$TEMP_DIR/${test_name}"
    if ! gcc -o "$exe_file" "$c_file" -I./runtime/include -L. -lhemlock_runtime -lm -lpthread -lffi -ldl $ZLIB_FLAG $LWS_FLAG $CRYPTO_FLAG > /tmp/gcc_err.log 2>&1; then
        echo -e "${RED}✗${NC} $test_name ${RED}(C compilation failed)${NC}"
        cat /tmp/gcc_err.log
        ((FAIL_COUNT++))
        continue
    fi

    # Run executable and capture output (set LD_LIBRARY_PATH for shared library)
    actual_output=$(LD_LIBRARY_PATH="$PWD:$LD_LIBRARY_PATH" "$exe_file" 2>&1)
    expected_output=$(cat "$expected_file")

    # Compare output
    if [ "$actual_output" = "$expected_output" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        ((PASS_COUNT++))
    else
        echo -e "${RED}✗${NC} $test_name"
        echo "  Expected:"
        echo "$expected_output" | sed 's/^/    /'
        echo "  Actual:"
        echo "$actual_output" | sed 's/^/    /'
        ((FAIL_COUNT++))
    fi
done

echo ""
echo "======================================"
echo "              Summary"
echo "======================================"
echo -e "${GREEN}Passed:${NC}  $PASS_COUNT"
echo -e "${YELLOW}Skipped:${NC} $SKIP_COUNT"
echo -e "${RED}Failed:${NC}  $FAIL_COUNT"
echo "======================================"

TOTAL=$((PASS_COUNT + FAIL_COUNT))

echo ""
if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All $TOTAL compiler tests passed! 🎉${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed. Please review the output above.${NC}"
    exit 1
fi
