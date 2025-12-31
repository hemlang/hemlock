#!/bin/bash
# Run type checking tests for hemlockc

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEMLOCKC="$SCRIPT_DIR/../../../hemlockc"

PASSED=0
FAILED=0

# Test 1: Type mismatch should fail
echo "Test 1: Type mismatch (i32 = string)"
if $HEMLOCKC --type-check "$SCRIPT_DIR/type_mismatch.hml" 2>&1 | grep -q "cannot initialize"; then
    echo "  PASSED: Caught type mismatch"
    ((PASSED++))
else
    echo "  FAILED: Did not catch type mismatch"
    ((FAILED++))
fi

# Test 2: Function argument type mismatch
echo "Test 2: Function argument type mismatch"
cat > /tmp/test_fn_arg.hml << 'EOF'
let add = fn(a: i32, b: i32): i32 { return a + b; };
let x = add("hello", 2);
EOF
if $HEMLOCKC --type-check /tmp/test_fn_arg.hml 2>&1 | grep -q "argument 1 to 'add'"; then
    echo "  PASSED: Caught function argument type mismatch"
    ((PASSED++))
else
    echo "  FAILED: Did not catch function argument type mismatch"
    ((FAILED++))
fi

# Test 3: Too many arguments
echo "Test 3: Too many arguments"
cat > /tmp/test_too_many.hml << 'EOF'
let add = fn(a: i32, b: i32): i32 { return a + b; };
let x = add(1, 2, 3);
EOF
if $HEMLOCKC --type-check /tmp/test_too_many.hml 2>&1 | grep -q "too many arguments"; then
    echo "  PASSED: Caught too many arguments"
    ((PASSED++))
else
    echo "  FAILED: Did not catch too many arguments"
    ((FAILED++))
fi

# Test 4: Invalid operator usage
echo "Test 4: Invalid operator usage (string - number)"
cat > /tmp/test_operator.hml << 'EOF'
let x = "hello" - 5;
EOF
if $HEMLOCKC --type-check /tmp/test_operator.hml 2>&1 | grep -q "cannot subtract"; then
    echo "  PASSED: Caught invalid operator usage"
    ((PASSED++))
else
    echo "  FAILED: Did not catch invalid operator usage"
    ((FAILED++))
fi

# Test 5: Const reassignment
echo "Test 5: Const reassignment"
cat > /tmp/test_const.hml << 'EOF'
const PI = 3.14;
PI = 4;
EOF
if $HEMLOCKC --type-check /tmp/test_const.hml 2>&1 | grep -q "cannot reassign const"; then
    echo "  PASSED: Caught const reassignment"
    ((PASSED++))
else
    echo "  FAILED: Did not catch const reassignment"
    ((FAILED++))
fi

# Test 6: Return type mismatch
echo "Test 6: Return type mismatch"
cat > /tmp/test_return.hml << 'EOF'
let fn getNum(): i32 { return "hello"; };
EOF
if $HEMLOCKC --type-check /tmp/test_return.hml 2>&1 | grep -q "return type mismatch"; then
    echo "  PASSED: Caught return type mismatch"
    ((PASSED++))
else
    echo "  FAILED: Did not catch return type mismatch"
    ((FAILED++))
fi

# Test 7: Bitwise operator with non-integer
echo "Test 7: Bitwise operator with non-integer"
cat > /tmp/test_bitwise.hml << 'EOF'
let x = 3.14 & 5;
EOF
if $HEMLOCKC --type-check /tmp/test_bitwise.hml 2>&1 | grep -q "bitwise operation requires integer"; then
    echo "  PASSED: Caught bitwise with non-integer"
    ((PASSED++))
else
    echo "  FAILED: Did not catch bitwise with non-integer"
    ((FAILED++))
fi

# Test 8: Valid code should pass
echo "Test 8: Valid typed code should pass"
cat > /tmp/test_valid.hml << 'EOF'
let x: i32 = 42;
let y: i32 = x + 10;
let name: string = "hello";
let add = fn(a: i32, b: i32): i32 { return a + b; };
let result = add(x, y);
EOF
if $HEMLOCKC --type-check /tmp/test_valid.hml 2>&1 | grep -q "type error"; then
    echo "  FAILED: Valid code reported as error"
    ((FAILED++))
else
    echo "  PASSED: Valid code passed type checking"
    ((PASSED++))
fi

echo ""
echo "=== Type Check Test Results ==="
echo "Passed: $PASSED"
echo "Failed: $FAILED"

if [ $FAILED -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi
