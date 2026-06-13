#!/bin/bash

# Parity Test Suite for Hemlock
# Runs each test through both interpreter and compiler, ensuring identical output

# Don't use set -e as it conflicts with arithmetic expressions and error handling

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
HEMLOCK="$ROOT_DIR/hemlock"
HEMLOCKC="$ROOT_DIR/hemlockc"

# Timeout for each test execution (in seconds).  Compilation can legitimately
# take longer than running the produced binary for large parity fixtures (for
# example the stdlib_jinja audit exercises many imported helpers), so keep a
# separate compile timeout to avoid reporting slow-but-valid codegen as an
# interpreter-only parity failure.
TEST_TIMEOUT=${TEST_TIMEOUT:-10}
COMPILE_TIMEOUT=${COMPILE_TIMEOUT:-30}

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
PASSED=0
FAILED=0
SKIPPED=0
INTERP_ONLY=0
COMPILER_ONLY=0

# Temp directory for compiled binaries
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Check if binaries exist
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
echo "    Hemlock Parity Test Suite"
echo "======================================"
echo ""
echo "Testing interpreter ($HEMLOCK) vs compiler ($HEMLOCKC)"

# Optional filter: only run tests whose path matches the first argument
FILTER="${1:-}"
if [ -n "$FILTER" ]; then
    echo "Filter: $FILTER"
fi
echo ""

# ===== Capability detection =====
# Tests can declare environment requirements with a directive comment:
#     // REQUIRES: http
# Tests whose requirements aren't met are skipped instead of being
# reported as parity failures.

# http: libwebsockets compiled into the interpreter/runtime. Stub builds
# throw "... not available (libwebsockets not installed)" from every HTTP
# builtin; probe one and inspect the exception text.
HAS_HTTP=0
HTTP_PROBE=$(mktemp --suffix=.hml)
cat > "$HTTP_PROBE" <<'EOF'
try {
    __lws_http_stream_read("probe", 0);
    print("yes");
} catch (e) {
    if (e.contains("not installed")) { print("no"); } else { print("yes"); }
}
EOF
if [ "$(timeout 10 "$HEMLOCK" "$HTTP_PROBE" 2>/dev/null)" = "yes" ]; then
    HAS_HTTP=1
fi
rm -f "$HTTP_PROBE"
if [ "$HAS_HTTP" = "0" ]; then
    echo "Capability 'http' unavailable (libwebsockets not installed) - HTTP tests will be skipped"
    echo ""
fi

# Returns 0 if all REQUIRES directives in the test file are satisfied;
# prints the first unmet requirement otherwise.
check_requirements() {
    local test_file="$1"
    local req
    for req in $(sed -n 's|^//[[:space:]]*REQUIRES:[[:space:]]*||p' "$test_file"); do
        case "$req" in
            http)
                if [ "$HAS_HTTP" = "0" ]; then echo "http"; return 1; fi
                ;;
            *)
                echo "$req (unknown)"; return 1
                ;;
        esac
    done
    return 0
}

run_test() {
    local test_file="$1"
    local test_name=$(basename "$test_file" .hml)
    local expected_file="${test_file%.hml}.expected"
    local test_dir=$(dirname "$test_file")

    # Apply filter if given
    if [ -n "$FILTER" ] && [[ "$test_file" != *"$FILTER"* ]]; then
        return
    fi

    # Check for expected output file. Files imported by other tests in the
    # same directory are module fixtures, not forgotten tests.
    if [ ! -f "$expected_file" ]; then
        if grep -rlsE "(import|from).*[\"/]$test_name(\.hml)?\"" "$test_dir" --include="*.hml" >/dev/null 2>&1; then
            echo -e "${YELLOW}⊘${NC} $test_name (module fixture)"
        else
            echo -e "${YELLOW}⊘${NC} $test_name (no .expected file)"
        fi
        ((SKIPPED++))
        return
    fi

    local expected=$(cat "$expected_file")

    # Skip tests whose environment requirements aren't met
    local unmet
    if ! unmet=$(check_requirements "$test_file"); then
        echo -e "${YELLOW}⊘${NC} $test_name (requires $unmet)"
        ((SKIPPED++))
        return
    fi

    # Run interpreter with timeout
    local interp_output
    local interp_exit=0
    interp_output=$(timeout "$TEST_TIMEOUT" "$HEMLOCK" "$test_file" 2>&1) || interp_exit=$?

    # Check if interpreter timed out (exit code 124)
    if [ $interp_exit -eq 124 ]; then
        interp_output="[TIMEOUT after ${TEST_TIMEOUT}s]"
    fi

    # Compile and run with timeout
    local compile_exit=0
    local compiler_output
    local run_exit=0

    timeout "$COMPILE_TIMEOUT" "$HEMLOCKC" "$test_file" -o "$TEMP_DIR/$test_name" 2>/dev/null || compile_exit=$?

    # Check if compilation timed out
    if [ $compile_exit -eq 124 ]; then
        compile_exit=1  # Treat timeout as compile failure
        compiler_output="[COMPILE TIMEOUT after ${COMPILE_TIMEOUT}s]"
    fi

    if [ $compile_exit -eq 0 ]; then
        compiler_output=$(timeout "$TEST_TIMEOUT" env LD_LIBRARY_PATH="$ROOT_DIR" "$TEMP_DIR/$test_name" 2>&1) || run_exit=$?
        # Check if runtime timed out
        if [ $run_exit -eq 124 ]; then
            compiler_output="[TIMEOUT after ${TEST_TIMEOUT}s]"
        fi
    fi

    # Compare results
    local interp_match=false
    local compiler_match=false

    if [ "$interp_output" = "$expected" ]; then
        interp_match=true
    fi

    if [ $compile_exit -eq 0 ] && [ "$compiler_output" = "$expected" ]; then
        compiler_match=true
    fi

    # Determine test result
    if [ "$interp_match" = true ] && [ "$compiler_match" = true ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        ((PASSED++))
    elif [ "$interp_match" = true ] && [ "$compiler_match" = false ]; then
        echo -e "${YELLOW}◐${NC} $test_name (interpreter only)"
        if [ $compile_exit -ne 0 ]; then
            if [ -n "$compiler_output" ]; then
                echo -e "    ${RED}$compiler_output${NC}"
            else
                echo -e "    ${RED}Compiler failed to compile${NC}"
            fi
        else
            echo -e "    ${RED}Compiler output differs (expected vs compiled):${NC}"
            print_diff "$expected" "$compiler_output"
        fi
        ((INTERP_ONLY++))
    elif [ "$interp_match" = false ] && [ "$compiler_match" = true ]; then
        echo -e "${YELLOW}◑${NC} $test_name (compiler only)"
        echo -e "    ${RED}Interpreter output differs (expected vs interpreter):${NC}"
        print_diff "$expected" "$interp_output"
        ((COMPILER_ONLY++))
    else
        echo -e "${RED}✗${NC} $test_name (both fail)"
        ((FAILED++))
        echo -e "    ${RED}Expected vs interpreter:${NC}"
        print_diff "$expected" "$interp_output"
        if [ $compile_exit -eq 0 ]; then
            echo -e "    ${RED}Expected vs compiled:${NC}"
            print_diff "$expected" "$compiler_output"
        elif [ -n "$compiler_output" ]; then
            echo -e "    ${RED}Compile error: $compiler_output${NC}"
        else
            echo -e "    ${RED}Compiler failed to compile${NC}"
        fi
    fi
}

# Print an indented unified diff between expected and actual output,
# truncated to keep failure reports readable.
print_diff() {
    local expected="$1"
    local actual="$2"
    diff <(echo "$expected") <(echo "$actual") 2>/dev/null | head -20 | sed 's/^/    /'
}

# Find and run all tests
find_tests() {
    local dir="$1"
    find "$dir" -name "*.hml" -type f | sort
}

# Run tests in each category
for category in language builtins methods modules annotations; do
    category_dir="$SCRIPT_DIR/$category"
    if [ -d "$category_dir" ]; then
        test_files=$(find_tests "$category_dir")
        if [ -n "$test_files" ]; then
            echo "--- $category ---"
            for test_file in $test_files; do
                run_test "$test_file"
            done
            echo ""
        fi
    fi
done

# Also run any tests directly in the parity directory
root_tests=$(find "$SCRIPT_DIR" -maxdepth 1 -name "*.hml" -type f | sort)
if [ -n "$root_tests" ]; then
    echo "--- other ---"
    for test_file in $root_tests; do
        run_test "$test_file"
    done
    echo ""
fi

# Summary
echo "======================================"
echo "              Summary"
echo "======================================"
TOTAL=$((PASSED + FAILED + SKIPPED + INTERP_ONLY + COMPILER_ONLY))
echo -e "${GREEN}Full Parity:${NC}     $PASSED"
echo -e "${YELLOW}Interpreter Only:${NC} $INTERP_ONLY"
echo -e "${YELLOW}Compiler Only:${NC}   $COMPILER_ONLY"
echo -e "${RED}Both Failed:${NC}     $FAILED"
echo -e "${BLUE}Skipped:${NC}         $SKIPPED"
echo "--------------------------------------"
echo "Total:           $TOTAL"
echo ""

# Calculate parity percentage
if [ $((PASSED + INTERP_ONLY + COMPILER_ONLY)) -gt 0 ]; then
    PARITY_PCT=$((PASSED * 100 / (PASSED + INTERP_ONLY + COMPILER_ONLY + FAILED)))
    echo -e "Parity Score: ${GREEN}$PARITY_PCT%${NC} ($PASSED/$((PASSED + INTERP_ONLY + COMPILER_ONLY + FAILED)) tests)"
fi
echo "======================================"

# Exit with error if any tests failed completely
if [ $FAILED -gt 0 ]; then
    exit 1
fi

# Exit with warning code if there's incomplete parity
if [ $INTERP_ONLY -gt 0 ] || [ $COMPILER_ONLY -gt 0 ]; then
    exit 2
fi

echo -e "${GREEN}Full parity achieved! 🎉${NC}"
exit 0
