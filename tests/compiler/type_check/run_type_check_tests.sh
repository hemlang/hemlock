#!/bin/bash
# Run type checking tests for hemlockc

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEMLOCKC="$SCRIPT_DIR/../../../hemlockc"

PASSED=0
FAILED=0

# Test 1: Type mismatch should fail
echo "Test 1: Type mismatch (i32 = string)"
if $HEMLOCKC --check "$SCRIPT_DIR/type_mismatch.hml" 2>&1 | grep -q "cannot initialize"; then
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
if $HEMLOCKC --check /tmp/test_fn_arg.hml 2>&1 | grep -q "argument 1 to 'add'"; then
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
if $HEMLOCKC --check /tmp/test_too_many.hml 2>&1 | grep -q "too many arguments"; then
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
if $HEMLOCKC --check /tmp/test_operator.hml 2>&1 | grep -q "cannot subtract"; then
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
if $HEMLOCKC --check /tmp/test_const.hml 2>&1 | grep -q "cannot reassign const"; then
    echo "  PASSED: Caught const reassignment"
    ((PASSED++))
else
    echo "  FAILED: Did not catch const reassignment"
    ((FAILED++))
fi

# Test 6: Return type mismatch
echo "Test 6: Return type mismatch"
cat > /tmp/test_return.hml << 'EOF'
let getNum = fn(): i32 { return "hello"; };
EOF
if $HEMLOCKC --check /tmp/test_return.hml 2>&1 | grep -q "return type mismatch"; then
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
if $HEMLOCKC --check /tmp/test_bitwise.hml 2>&1 | grep -q "bitwise operation requires integer"; then
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
if $HEMLOCKC --check /tmp/test_valid.hml 2>&1 | grep -q "no type errors"; then
    echo "  PASSED: Valid code passed type checking"
    ((PASSED++))
else
    echo "  FAILED: Valid code reported as error"
    ((FAILED++))
fi

# Test 9: Too few arguments
echo "Test 9: Too few arguments"
cat > /tmp/test_too_few.hml << 'EOF'
let add = fn(a: i32, b: i32): i32 { return a + b; };
let x = add(1);
EOF
if $HEMLOCKC --check /tmp/test_too_few.hml 2>&1 | grep -q "too few arguments"; then
    echo "  PASSED: Caught too few arguments"
    ((PASSED++))
else
    echo "  FAILED: Did not catch too few arguments"
    ((FAILED++))
fi

# Test 10: Too few arguments with optional params should be fine
echo "Test 10: Optional params - fewer args allowed"
cat > /tmp/test_optional.hml << 'EOF'
let greet = fn(name: string, msg?: "Hello"): string { return msg + " " + name; };
let x = greet("world");
EOF
if $HEMLOCKC --check /tmp/test_optional.hml 2>&1 | grep -q "too few arguments"; then
    echo "  FAILED: Optional param incorrectly flagged"
    ((FAILED++))
else
    echo "  PASSED: Optional params work correctly"
    ((PASSED++))
fi

# Test 11: Array method type checking - push wrong type
echo "Test 11: Array.push() with wrong type"
cat > /tmp/test_array_push.hml << 'EOF'
let nums: array<i32> = [1, 2, 3];
nums.push("hello");
EOF
if $HEMLOCKC --check /tmp/test_array_push.hml 2>&1 | grep -q "cannot add"; then
    echo "  PASSED: Caught array.push() type mismatch"
    ((PASSED++))
else
    echo "  FAILED: Did not catch array.push() type mismatch"
    ((FAILED++))
fi

# Test 12: String method type checking - repeat with non-integer
echo "Test 12: String.repeat() with non-integer"
cat > /tmp/test_string_repeat.hml << 'EOF'
let s = "hi";
let r = s.repeat("3");
EOF
if $HEMLOCKC --check /tmp/test_string_repeat.hml 2>&1 | grep -q "count must be integer"; then
    echo "  PASSED: Caught string.repeat() type mismatch"
    ((PASSED++))
else
    echo "  FAILED: Did not catch string.repeat() type mismatch"
    ((FAILED++))
fi

# Test 13: Array.join() with non-string separator
echo "Test 13: Array.join() with non-string separator"
cat > /tmp/test_array_join.hml << 'EOF'
let arr = [1, 2, 3];
let s = arr.join(42);
EOF
if $HEMLOCKC --check /tmp/test_array_join.hml 2>&1 | grep -q "separator must be string"; then
    echo "  PASSED: Caught array.join() type mismatch"
    ((PASSED++))
else
    echo "  FAILED: Did not catch array.join() type mismatch"
    ((FAILED++))
fi

# Test 14: Property access on object type
echo "Test 14: Property type checking on set"
cat > /tmp/test_property.hml << 'EOF'
define Person { name: string, age: i32 }
let p: Person = { name: "Alice", age: 30 };
p.age = "thirty";
EOF
if $HEMLOCKC --check /tmp/test_property.hml 2>&1 | grep -q "cannot assign"; then
    echo "  PASSED: Caught property type mismatch"
    ((PASSED++))
else
    echo "  FAILED: Did not catch property type mismatch"
    ((FAILED++))
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
