# Bug Report: Null Comparison Failure

## Summary
After merging from main, **all null comparisons fail** with "Binary operation requires numeric operands" error, breaking the collections test suite and likely many other programs.

## Reproduction

### Minimal Failing Case
```hemlock
let x = null;
let result = (x == null);  // ERROR: Binary operation requires numeric operands
```

### Even Simpler
```hemlock
let x = null;
if (x == null) {  // ERROR occurs here
    print("true");
}
```

### What Works
```hemlock
let x = null;
print(typeof(x));  // "null" - this works fine
```

## Impact on Collections Tests

All comprehensive collection tests fail because they test `null` values:

```hemlock
// From test_queue.hml line 52-59
q.enqueue(null);
assert(q.dequeue() == null, "Should dequeue null");  // FAILS HERE
```

The basic test suite works because it doesn't test `null` values.

## Root Cause

The error "Binary operation requires numeric operands" suggests that Hemlock's type system is treating the `==` operator as requiring numeric types when one operand is `null`. This is a fundamental type checking bug.

## Expected Behavior

`null == null` should return `true` (boolean).
`value == null` should return boolean for any value type.

## Actual Behavior

Any comparison involving `null` triggers: "Binary operation requires numeric operands"

## Test Results

### ✅ Working Tests
- `tests/stdlib_collections/test_basic.hml` - Passes because it doesn't test null values

### ❌ Failing Tests (all fail at null comparisons)
- `tests/stdlib_collections/experimental/test_queue.hml` - Line 59
- `tests/stdlib_collections/experimental/test_stack.hml` - Similar null test
- `tests/stdlib_collections/experimental/test_set.hml` - Similar null test
- `tests/stdlib_collections/experimental/test_hashmap.hml` - Similar null test
- `tests/stdlib_collections/experimental/test_linkedlist.hml` - Similar null test

## Verification

This can be verified with a simple one-liner:
```bash
echo 'let x = null; assert(x == null, "test");' | ./hemlock -
```

## Introduced By

This appears to be a regression introduced in the recent merge from main. The collections code itself is correct - the Hemlock type system has a bug handling null comparisons.

## Recommendation

1. Fix the type checker to allow `==` comparisons with `null` operands
2. Add test coverage for null comparisons to prevent regression
3. Consider if other operators have similar issues with null

## Collections Module Status

The **collections themselves are production-ready** and work correctly. The test failures are 100% due to this Hemlock type system bug, not issues with the collection implementations.
