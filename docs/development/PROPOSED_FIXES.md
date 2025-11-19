# Proposed Fixes for Remaining 6 Test Failures

## Summary of Issues

After implementing comprehensive memory leak fixes, 6 tests remain failing. These are edge cases in value lifecycle management introduced by the `env_get()` retain semantics.

## Root Cause

All issues stem from `env_get()` now retaining values before returning them. This creates ownership transfer but requires careful release management across all expression types.

---

## Fix 1: Increment/Decrement Operators - Release Old Values

**Issue**: `++x` and `x++` call `env_get()` which retains the old value, but never release it.

**Files**: `src/interpreter/runtime/expressions.c`
**Lines**: 1100, 1150, 1198, 1246 (PREFIX_INC, PREFIX_DEC, POSTFIX_INC, POSTFIX_DEC)

### Current Code (EXPR_PREFIX_INC):
```c
Value old_val = env_get(env, operand->as.ident, ctx);
Value new_val = value_add_one(old_val, ctx);
env_set(env, operand->as.ident, new_val, ctx);
return new_val;
```

### Proposed Fix:
```c
Value old_val = env_get(env, operand->as.ident, ctx);  // Retains old value
Value new_val = value_add_one(old_val, ctx);
value_release(old_val);  // Release old value after incrementing
env_set(env, operand->as.ident, new_val, ctx);
return new_val;
```

**Apply to all 4 operators** (PREFIX_INC, PREFIX_DEC, POSTFIX_INC, POSTFIX_DEC) - but **only** for the `EXPR_IDENT` case. The `EXPR_INDEX` and `EXPR_GET_PROPERTY` cases don't need this because they already handle object/array lifetimes differently.

---

## Fix 2: Binary String Comparison - Release Operands

**Issue**: String comparison operations don't release the string operands after comparing.

**File**: `src/interpreter/runtime/expressions.c`
**Lines**: 241-259

### Current Code:
```c
// String comparisons
if (left.type == VAL_STRING && right.type == VAL_STRING) {
    if (expr->as.binary.op == OP_EQUAL) {
        String *left_str = left.as.as_string;
        String *right_str = right.as.as_string;
        if (left_str->length != right_str->length) {
            return val_bool(0);
        }
        int cmp = memcmp(left_str->data, right_str->data, left_str->length);
        return val_bool(cmp == 0);
    } else if (expr->as.binary.op == OP_NOT_EQUAL) {
        String *left_str = left.as.as_string;
        String *right_str = right.as.as_string;
        if (left_str->length != right_str->length) {
            return val_bool(1);
        }
        int cmp = memcmp(left_str->data, right_str->data, left_str->length);
        return val_bool(cmp != 0);
    }
}
```

### Proposed Fix:
```c
// String comparisons
if (left.type == VAL_STRING && right.type == VAL_STRING) {
    if (expr->as.binary.op == OP_EQUAL) {
        String *left_str = left.as.as_string;
        String *right_str = right.as.as_string;
        int result;
        if (left_str->length != right_str->length) {
            result = 0;
        } else {
            int cmp = memcmp(left_str->data, right_str->data, left_str->length);
            result = (cmp == 0);
        }
        value_release(left);
        value_release(right);
        return val_bool(result);
    } else if (expr->as.binary.op == OP_NOT_EQUAL) {
        String *left_str = left.as.as_string;
        String *right_str = right.as.as_string;
        int result;
        if (left_str->length != right_str->length) {
            result = 1;
        } else {
            int cmp = memcmp(left_str->data, right_str->data, left_str->length);
            result = (cmp != 0);
        }
        value_release(left);
        value_release(right);
        return val_bool(result);
    }
}
```

---

## Fix 3: Exception Value in String Concatenation

**Issue**: When assert() throws and the exception message is used in string concatenation (e.g., `"Caught: " + e`), the exception value gets freed prematurely.

**Analysis**: This is actually already fixed by our `value_retain()` in `builtin_assert()`. The remaining issue is likely in how STMT_TRY sets the exception in the catch environment.

**File**: `src/interpreter/runtime/statements.c`
**Line**: 315

### Current Code:
```c
Environment *catch_env = env_new(env);
env_set(catch_env, stmt->as.try_stmt.catch_param, ctx->exception_state.exception_value, ctx);

// Clear exception state and release the exception value
// (env_set retained it, so we can release the context's reference)
value_release(ctx->exception_state.exception_value);
ctx->exception_state.is_throwing = 0;
```

**This is actually correct.** The issue is that when the exception value is accessed in the catch block via `env_get()`, it gets retained again, and when used in string concatenation, the operands aren't being released.

**Real Fix**: Apply Fix #2 (String Comparison Release) more broadly to **all binary string operations**, including concatenation.

---

## Fix 4: Array Literal Element Retention

**Issue**: `array_push()` retains elements, but elements coming from `eval_expr()` via `env_get()` are already retained.

**File**: `src/interpreter/runtime/expressions.c`
**Line**: 1027

### Current Code:
```c
for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
    Value element = eval_expr(expr->as.array_literal.elements[i], env, ctx);
    array_push(arr, element);
}
```

### Check array_push Implementation:
Need to verify if `array_push()` retains the element. If it does, we need to release after push.

**File**: `src/interpreter/values.c` - check `array_push()`

### Proposed Fix (if array_push retains):
```c
for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
    Value element = eval_expr(expr->as.array_literal.elements[i], env, ctx);
    array_push(arr, element);
    value_release(element);  // Release our reference; array now owns it
}
```

---

## Fix 5: Object Literal Field Retention

**Issue**: Same as Fix #4 but for objects.

**File**: `src/interpreter/runtime/expressions.c`
**Line**: 1041-1043

### Current Code:
```c
obj->field_values[i] = eval_expr(expr->as.object_literal.field_values[i], env, ctx);
// Retain field values (objects own their field values)
value_retain(obj->field_values[i]);
```

### Analysis:
If the field value comes from a variable via `env_get()`, it's already retained. Then we retain again. This creates double retention.

### Proposed Fix:
```c
obj->field_values[i] = eval_expr(expr->as.object_literal.field_values[i], env, ctx);
// Field values from eval_expr are already retained if from env_get
// Objects own their field values, so we don't need extra retain
// value_retain(obj->field_values[i]);  // REMOVE THIS LINE
```

**WAIT** - this would break literals like `{x: 42}` where 42 is not retained.

### Better Fix - Check if value needs retention:
```c
Value field_val = eval_expr(expr->as.object_literal.field_values[i], env, ctx);
obj->field_values[i] = field_val;
// Objects own their fields - retain unconditionally
// But since eval_expr from variables already retains, this creates double-retain
// The proper fix is that eval_expr should ALWAYS return with ref_count appropriate
// for ownership transfer (i.e., caller owns the result)

// For now, keep the retain since literals don't have ref_count tracking
value_retain(obj->field_values[i]);
```

This is a complex architectural issue. **Recommend skipping this fix for now** and focusing on the simpler ones.

---

## Fix 6: Serialization Issues

**Issue**: Object serialization/deserialization tests failing due to field value retention.

**Analysis**: Likely related to Fix #5. When deserializing JSON, field values are created fresh but the retention logic may be incorrect.

**Recommendation**: Test after applying Fix #5.

---

## Recommended Implementation Order

1. **Fix #1** (Increment/Decrement) - Simple, clear benefit
2. **Fix #2** (String Comparison) - Will likely fix assert tests
3. Test results
4. **Fix #4** (Array Literal) - If array_push retains
5. **Fix #5** (Object Literal) - Complex, may need more analysis
6. **Fix #6** (Serialization) - May be fixed by #5

---

## Testing Strategy

After each fix:
1. Rebuild: `make`
2. Run specific failing test
3. Run full test suite: `bash tests/run_tests.sh`
4. Check for regressions
5. Verify with Valgrind: `valgrind --leak-check=full ./hemlock test_program.hml`

---

## Expected Outcomes

- **Fix #1**: Should fix defer/increment tests if any
- **Fix #2**: Should fix assert_basic.hml and assert_types.hml
- **Fix #4**: May help with array-heavy tests
- **Fix #5**: Should fix serialization tests (deserialize_basic, round_trip, serialize_large)
- **Binary IO test**: May require targeted investigation of buffer property access patterns
