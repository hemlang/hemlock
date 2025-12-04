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

echo ""
echo "=== Results ==="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
