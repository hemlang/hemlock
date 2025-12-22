#!/bin/bash
# Bundler Test Suite
# Tests the hemlock bundler functionality

HEMLOCK="./hemlock"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

PASSED=0
FAILED=0

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass() {
    echo -e "${GREEN}PASS${NC}: $1"
    ((PASSED++))
}

fail() {
    echo -e "${RED}FAIL${NC}: $1"
    echo "  $2"
    ((FAILED++))
}

echo "=== Hemlock Bundler Test Suite ==="
echo ""

# Test 1: Bundle single file
echo "Test 1: Bundle single file"
if $HEMLOCK --bundle tests/primitives/binary_literals.hml -o "$TMPDIR/single.hmlc" 2>/dev/null; then
    if [ -f "$TMPDIR/single.hmlc" ]; then
        pass "Single file bundle created"
    else
        fail "Single file bundle" "Output file not created"
    fi
else
    fail "Single file bundle" "Bundle command failed"
fi

# Test 2: Run bundled single file
echo "Test 2: Run bundled single file"
if $HEMLOCK "$TMPDIR/single.hmlc" >/dev/null 2>&1; then
    pass "Single file bundle runs"
else
    fail "Single file bundle runs" "Execution failed"
fi

# Test 3: Bundle with stdlib imports
echo "Test 3: Bundle with stdlib imports"
if $HEMLOCK --bundle tests/stdlib_collections/test_basic.hml -o "$TMPDIR/stdlib.hmlc" 2>/dev/null; then
    if [ -f "$TMPDIR/stdlib.hmlc" ]; then
        pass "Stdlib bundle created"
    else
        fail "Stdlib bundle" "Output file not created"
    fi
else
    fail "Stdlib bundle" "Bundle command failed"
fi

# Test 4: Run stdlib bundle
echo "Test 4: Run bundled stdlib file"
OUTPUT=$($HEMLOCK "$TMPDIR/stdlib.hmlc" 2>&1)
if echo "$OUTPUT" | grep -q "All basic collections tests passed"; then
    pass "Stdlib bundle runs correctly"
else
    fail "Stdlib bundle runs" "Expected output not found"
fi

# Test 5: Bundle multi-module example
echo "Test 5: Bundle multi-module example"
if $HEMLOCK --bundle examples/multi_module/main.hml -o "$TMPDIR/multi.hmlc" 2>/dev/null; then
    pass "Multi-module bundle created"
else
    fail "Multi-module bundle" "Bundle command failed"
fi

# Test 6: Run multi-module bundle
echo "Test 6: Run multi-module bundle"
OUTPUT=$($HEMLOCK "$TMPDIR/multi.hmlc" 2>&1)
if echo "$OUTPUT" | grep -q "Example Complete"; then
    pass "Multi-module bundle runs correctly"
else
    fail "Multi-module bundle runs" "Expected output not found"
fi

# Test 7: Compressed bundle
echo "Test 7: Compressed bundle"
if $HEMLOCK --bundle tests/stdlib_collections/test_basic.hml --compress -o "$TMPDIR/compressed.hmlb" 2>/dev/null; then
    UNCOMPRESSED_SIZE=$(stat -c%s "$TMPDIR/stdlib.hmlc" 2>/dev/null || stat -f%z "$TMPDIR/stdlib.hmlc")
    COMPRESSED_SIZE=$(stat -c%s "$TMPDIR/compressed.hmlb" 2>/dev/null || stat -f%z "$TMPDIR/compressed.hmlb")
    if [ "$COMPRESSED_SIZE" -lt "$UNCOMPRESSED_SIZE" ]; then
        pass "Compressed bundle is smaller ($COMPRESSED_SIZE < $UNCOMPRESSED_SIZE bytes)"
    else
        fail "Compression" "Compressed file not smaller"
    fi
else
    fail "Compressed bundle" "Bundle command failed"
fi

# Test 8: Verbose output
echo "Test 8: Verbose output"
OUTPUT=$($HEMLOCK --bundle examples/multi_module/main.hml --verbose -o "$TMPDIR/verbose.hmlc" 2>&1)
if echo "$OUTPUT" | grep -q "Bundle Summary" && echo "$OUTPUT" | grep -q "Flattened"; then
    pass "Verbose output shows summary"
else
    fail "Verbose output" "Expected verbose information not found"
fi

# Test 9: Output matches original execution
echo "Test 9: Output matches original"
ORIGINAL=$($HEMLOCK examples/multi_module/main.hml 2>&1)
BUNDLED=$($HEMLOCK "$TMPDIR/multi.hmlc" 2>&1)
if [ "$ORIGINAL" = "$BUNDLED" ]; then
    pass "Bundled output matches original"
else
    fail "Output match" "Bundled output differs from original"
fi

# Test 10: Multiple stdlib imports
echo "Test 10: Bundle with multiple stdlib imports"
cat > "$TMPDIR/multi_stdlib.hml" << 'EOF'
import { HashMap } from "@stdlib/collections";
import { now } from "@stdlib/time";

let map = HashMap();
map.set("test", 42);
assert(map.get("test") == 42, "HashMap should work");
print("Multi-stdlib test passed!");
EOF

if $HEMLOCK --bundle "$TMPDIR/multi_stdlib.hml" -o "$TMPDIR/multi_stdlib.hmlc" 2>/dev/null; then
    OUTPUT=$($HEMLOCK "$TMPDIR/multi_stdlib.hmlc" 2>&1)
    if echo "$OUTPUT" | grep -q "Multi-stdlib test passed"; then
        pass "Multiple stdlib imports bundle works"
    else
        fail "Multiple stdlib imports" "Execution failed"
    fi
else
    fail "Multiple stdlib imports bundle" "Bundle command failed"
fi

# Test 11: Info command on hmlc
echo "Test 11: Info command on .hmlc"
OUTPUT=$($HEMLOCK --info "$TMPDIR/stdlib.hmlc" 2>&1)
if echo "$OUTPUT" | grep -q "Format: HMLC" && echo "$OUTPUT" | grep -q "Statements:"; then
    pass "Info command shows .hmlc details"
else
    fail "Info command on .hmlc" "Expected output not found"
fi

# Test 12: Info command on hmlb
echo "Test 12: Info command on .hmlb"
OUTPUT=$($HEMLOCK --info "$TMPDIR/compressed.hmlb" 2>&1)
if echo "$OUTPUT" | grep -q "Format: HMLB" && echo "$OUTPUT" | grep -q "Ratio:"; then
    pass "Info command shows .hmlb compression ratio"
else
    fail "Info command on .hmlb" "Expected output not found"
fi

# Test 13: Package single file (compressed)
echo "Test 13: Package single file (compressed)"
if $HEMLOCK --package tests/primitives/binary_literals.hml -o "$TMPDIR/pkg_single" 2>/dev/null; then
    if [ -x "$TMPDIR/pkg_single" ]; then
        pass "Packaged single file created and is executable"
    else
        fail "Package single file" "Output not executable"
    fi
else
    fail "Package single file" "Package command failed"
fi

# Test 14: Run packaged single file
echo "Test 14: Run packaged single file"
if "$TMPDIR/pkg_single" >/dev/null 2>&1; then
    pass "Packaged single file runs"
else
    fail "Packaged single file runs" "Execution failed"
fi

# Test 15: Package multi-module example (compressed)
echo "Test 15: Package multi-module example"
if $HEMLOCK --package examples/multi_module/main.hml -o "$TMPDIR/pkg_multi" 2>/dev/null; then
    pass "Packaged multi-module example created"
else
    fail "Package multi-module" "Package command failed"
fi

# Test 16: Run packaged multi-module and verify output
echo "Test 16: Run packaged multi-module"
OUTPUT=$("$TMPDIR/pkg_multi" 2>&1)
if echo "$OUTPUT" | grep -q "Example Complete"; then
    pass "Packaged multi-module runs correctly"
else
    fail "Packaged multi-module runs" "Expected output not found"
fi

# Test 17: Packaged output matches original
echo "Test 17: Packaged output matches original"
ORIGINAL=$($HEMLOCK examples/multi_module/main.hml 2>&1)
PACKAGED=$("$TMPDIR/pkg_multi" 2>&1)
if [ "$ORIGINAL" = "$PACKAGED" ]; then
    pass "Packaged output matches original"
else
    fail "Packaged output match" "Output differs from original"
fi

# Test 18: Package with --no-compress (uncompressed)
echo "Test 18: Package with --no-compress"
if $HEMLOCK --package examples/multi_module/main.hml --no-compress -o "$TMPDIR/pkg_nocompress" 2>/dev/null; then
    # Uncompressed should be slightly larger
    COMPRESSED_SIZE=$(stat -c%s "$TMPDIR/pkg_multi" 2>/dev/null || stat -f%z "$TMPDIR/pkg_multi")
    UNCOMPRESSED_SIZE=$(stat -c%s "$TMPDIR/pkg_nocompress" 2>/dev/null || stat -f%z "$TMPDIR/pkg_nocompress")
    if [ "$UNCOMPRESSED_SIZE" -ge "$COMPRESSED_SIZE" ]; then
        pass "Uncompressed package created (size: $UNCOMPRESSED_SIZE >= $COMPRESSED_SIZE)"
    else
        fail "Uncompressed package" "Expected uncompressed to be >= compressed"
    fi
else
    fail "Uncompressed package" "Package command failed"
fi

# Test 19: Run uncompressed package
echo "Test 19: Run uncompressed package"
OUTPUT=$("$TMPDIR/pkg_nocompress" 2>&1)
if echo "$OUTPUT" | grep -q "Example Complete"; then
    pass "Uncompressed package runs correctly"
else
    fail "Uncompressed package runs" "Expected output not found"
fi

# Test 20: Package with stdlib imports
echo "Test 20: Package with stdlib imports"
if $HEMLOCK --package tests/stdlib_collections/test_basic.hml -o "$TMPDIR/pkg_stdlib" 2>/dev/null; then
    OUTPUT=$("$TMPDIR/pkg_stdlib" 2>&1)
    if echo "$OUTPUT" | grep -q "All basic collections tests passed"; then
        pass "Packaged stdlib works correctly"
    else
        fail "Packaged stdlib" "Execution failed"
    fi
else
    fail "Package with stdlib" "Package command failed"
fi

# Test 21: Stdlib deduplication (multiple files importing same stdlib module)
echo "Test 21: Stdlib deduplication"
mkdir -p "$TMPDIR/dedup_test"
cat > "$TMPDIR/dedup_test/module_a.hml" << 'EOF'
import { getenv } from "@stdlib/env";
export fn get_home() { return getenv("HOME"); }
EOF
cat > "$TMPDIR/dedup_test/module_b.hml" << 'EOF'
import { getenv } from "@stdlib/env";
export fn get_path() { return getenv("PATH"); }
EOF
cat > "$TMPDIR/dedup_test/main.hml" << 'EOF'
import { get_home } from "./module_a";
import { get_path } from "./module_b";
let home = get_home();
let path = get_path();
// Verify both modules can access the shared @stdlib/env import
if (home != null && path != null) {
    print("Dedup test passed!");
} else {
    print("Failed: home=" + home + " path=" + path);
}
EOF

if $HEMLOCK --bundle "$TMPDIR/dedup_test/main.hml" -o "$TMPDIR/dedup.hmlc" 2>/dev/null; then
    OUTPUT=$($HEMLOCK "$TMPDIR/dedup.hmlc" 2>&1)
    if echo "$OUTPUT" | grep -q "Dedup test passed"; then
        pass "Stdlib deduplication works correctly"
    else
        fail "Stdlib deduplication" "Execution failed: $OUTPUT"
    fi
else
    fail "Stdlib deduplication bundle" "Bundle command failed"
fi

# Test 22: Multiple stdlib modules with shared dependencies
echo "Test 22: Multiple stdlib modules"
cat > "$TMPDIR/multi_stdlib_shared.hml" << 'EOF'
import { getenv, setenv } from "@stdlib/env";
import { sin, cos, PI } from "@stdlib/math";
import { now } from "@stdlib/time";

setenv("TEST_VAR", "hello");
assert(getenv("TEST_VAR") == "hello", "setenv/getenv should work");
assert(sin(0) == 0, "sin(0) should be 0");
assert(cos(0) == 1, "cos(0) should be 1");
assert(PI > 3.14, "PI should be > 3.14");
let t = now();
assert(t > 0, "now() should return positive timestamp");
print("Multi-stdlib shared test passed!");
EOF

if $HEMLOCK --bundle "$TMPDIR/multi_stdlib_shared.hml" -o "$TMPDIR/multi_stdlib_shared.hmlc" 2>/dev/null; then
    OUTPUT=$($HEMLOCK "$TMPDIR/multi_stdlib_shared.hmlc" 2>&1)
    if echo "$OUTPUT" | grep -q "Multi-stdlib shared test passed"; then
        pass "Multiple stdlib modules work correctly"
    else
        fail "Multiple stdlib modules" "Execution failed: $OUTPUT"
    fi
else
    fail "Multiple stdlib modules bundle" "Bundle command failed"
fi

# ========== TREE SHAKING TESTS ==========

# Test 23: Tree shaking eliminates unused exports
echo "Test 23: Tree shaking eliminates unused exports"
mkdir -p "$TMPDIR/treeshake_test"
cat > "$TMPDIR/treeshake_test/utils.hml" << 'EOF'
export fn used_fn() {
    return "I am used";
}

export fn unused_fn() {
    return "I am NOT used";
}

export let used_const = 42;
export let unused_const = 999;
EOF
cat > "$TMPDIR/treeshake_test/main.hml" << 'EOF'
import { used_fn, used_const } from "./utils";
print("used_fn: " + used_fn());
print("used_const: " + used_const);
print("Tree shake test passed!");
EOF

# Bundle without tree shaking
$HEMLOCK --bundle "$TMPDIR/treeshake_test/main.hml" -o "$TMPDIR/no_shake.hmlc" 2>/dev/null
NO_SHAKE_SIZE=$(stat -c%s "$TMPDIR/no_shake.hmlc" 2>/dev/null || stat -f%z "$TMPDIR/no_shake.hmlc")

# Bundle with tree shaking
$HEMLOCK --bundle "$TMPDIR/treeshake_test/main.hml" --tree-shake -o "$TMPDIR/with_shake.hmlc" 2>/dev/null
WITH_SHAKE_SIZE=$(stat -c%s "$TMPDIR/with_shake.hmlc" 2>/dev/null || stat -f%z "$TMPDIR/with_shake.hmlc")

if [ "$WITH_SHAKE_SIZE" -lt "$NO_SHAKE_SIZE" ]; then
    # Verify the shaken bundle still runs correctly
    OUTPUT=$($HEMLOCK "$TMPDIR/with_shake.hmlc" 2>&1)
    if echo "$OUTPUT" | grep -q "Tree shake test passed"; then
        pass "Tree shaking reduces bundle size ($WITH_SHAKE_SIZE < $NO_SHAKE_SIZE bytes)"
    else
        fail "Tree shaking" "Bundle runs but output incorrect: $OUTPUT"
    fi
else
    fail "Tree shaking" "Shaken bundle not smaller ($WITH_SHAKE_SIZE >= $NO_SHAKE_SIZE)"
fi

# Test 24: Tree shaking preserves dependencies
echo "Test 24: Tree shaking preserves dependencies"
mkdir -p "$TMPDIR/treeshake_deps"
cat > "$TMPDIR/treeshake_deps/helpers.hml" << 'EOF'
export fn helper() {
    return "helper called";
}

export fn unused_helper() {
    return "unused";
}
EOF
cat > "$TMPDIR/treeshake_deps/main_utils.hml" << 'EOF'
import { helper } from "./helpers";

export fn main_function() {
    return helper();
}

export fn unused_function() {
    return "not used";
}
EOF
cat > "$TMPDIR/treeshake_deps/main.hml" << 'EOF'
import { main_function } from "./main_utils";
let result = main_function();
print("Result: " + result);
if (result == "helper called") {
    print("Dependency test passed!");
}
EOF

if $HEMLOCK --bundle "$TMPDIR/treeshake_deps/main.hml" --tree-shake -o "$TMPDIR/deps_shake.hmlc" 2>/dev/null; then
    OUTPUT=$($HEMLOCK "$TMPDIR/deps_shake.hmlc" 2>&1)
    if echo "$OUTPUT" | grep -q "Dependency test passed"; then
        pass "Tree shaking preserves transitive dependencies"
    else
        fail "Tree shaking dependencies" "Execution failed: $OUTPUT"
    fi
else
    fail "Tree shaking dependencies" "Bundle command failed"
fi

# Test 25: Tree shaking verbose output
echo "Test 25: Tree shaking verbose output"
OUTPUT=$($HEMLOCK --bundle "$TMPDIR/treeshake_test/main.hml" --tree-shake --verbose -o "$TMPDIR/verbose_shake.hmlc" 2>&1)
if echo "$OUTPUT" | grep -q "Tree Shaking" && echo "$OUTPUT" | grep -q "eliminated"; then
    pass "Tree shaking verbose output shows statistics"
else
    fail "Tree shaking verbose" "Expected tree shaking statistics not found"
fi

# Test 26: Tree shaking with multi-module example
echo "Test 26: Tree shaking multi-module example"
# The multi-module example has unused exports (E, is_odd, count_chars)
$HEMLOCK --bundle examples/multi_module/main.hml -o "$TMPDIR/multi_no_shake.hmlc" 2>/dev/null
MULTI_NO_SHAKE=$(stat -c%s "$TMPDIR/multi_no_shake.hmlc" 2>/dev/null || stat -f%z "$TMPDIR/multi_no_shake.hmlc")

$HEMLOCK --bundle examples/multi_module/main.hml --tree-shake -o "$TMPDIR/multi_with_shake.hmlc" 2>/dev/null
MULTI_WITH_SHAKE=$(stat -c%s "$TMPDIR/multi_with_shake.hmlc" 2>/dev/null || stat -f%z "$TMPDIR/multi_with_shake.hmlc")

if [ "$MULTI_WITH_SHAKE" -lt "$MULTI_NO_SHAKE" ]; then
    # Verify it still runs
    ORIGINAL=$($HEMLOCK examples/multi_module/main.hml 2>&1)
    SHAKEN=$($HEMLOCK "$TMPDIR/multi_with_shake.hmlc" 2>&1)
    if [ "$ORIGINAL" = "$SHAKEN" ]; then
        pass "Tree shaking multi-module ($MULTI_WITH_SHAKE < $MULTI_NO_SHAKE bytes, output matches)"
    else
        fail "Tree shaking multi-module" "Output differs after shaking"
    fi
else
    fail "Tree shaking multi-module" "Shaken bundle not smaller"
fi

# Test 27: Tree shaking with package command
echo "Test 27: Tree shaking with package"
$HEMLOCK --package "$TMPDIR/treeshake_test/main.hml" -o "$TMPDIR/pkg_no_shake" 2>/dev/null
PKG_NO_SHAKE=$(stat -c%s "$TMPDIR/pkg_no_shake" 2>/dev/null || stat -f%z "$TMPDIR/pkg_no_shake")

$HEMLOCK --package "$TMPDIR/treeshake_test/main.hml" --tree-shake -o "$TMPDIR/pkg_with_shake" 2>/dev/null
PKG_WITH_SHAKE=$(stat -c%s "$TMPDIR/pkg_with_shake" 2>/dev/null || stat -f%z "$TMPDIR/pkg_with_shake")

if [ "$PKG_WITH_SHAKE" -lt "$PKG_NO_SHAKE" ]; then
    OUTPUT=$("$TMPDIR/pkg_with_shake" 2>&1)
    if echo "$OUTPUT" | grep -q "Tree shake test passed"; then
        pass "Tree shaking with package command ($PKG_WITH_SHAKE < $PKG_NO_SHAKE bytes)"
    else
        fail "Tree shaking package" "Packaged bundle failed: $OUTPUT"
    fi
else
    fail "Tree shaking package" "Shaken package not smaller"
fi

# Test 28: Tree shaking preserves side effects
echo "Test 28: Tree shaking preserves side effects"
cat > "$TMPDIR/side_effect.hml" << 'EOF'
let counter = 0;

fn increment() {
    counter = counter + 1;
}

// Side effect - should be preserved
increment();
increment();
increment();

print("counter: " + counter);
if (counter == 3) {
    print("Side effects preserved!");
}
EOF

if $HEMLOCK --bundle "$TMPDIR/side_effect.hml" --tree-shake -o "$TMPDIR/side_effect.hmlc" 2>/dev/null; then
    OUTPUT=$($HEMLOCK "$TMPDIR/side_effect.hmlc" 2>&1)
    if echo "$OUTPUT" | grep -q "Side effects preserved"; then
        pass "Tree shaking preserves side effects"
    else
        fail "Tree shaking side effects" "Side effects not preserved: $OUTPUT"
    fi
else
    fail "Tree shaking side effects" "Bundle command failed"
fi

echo ""
echo "=== Results ==="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
