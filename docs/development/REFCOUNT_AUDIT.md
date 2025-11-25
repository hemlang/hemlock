# Reference Counting Audit - Current State

## Summary

The reference counting infrastructure **exists and is mostly working**, but has critical gaps that cause memory leaks. This audit identifies all issues and proposes fixes.

## ✅ What's Working

### 1. Core Infrastructure
- All heap types have `ref_count` fields (String, Buffer, Array, Object, Function, Task, Channel, Environment)
- `value_retain()` and `value_release()` functions exist and work correctly
- Reference counting is **thread-safe** for Tasks, Channels, and Environments (atomic operations)

### 2. Proper Refcounting in These Areas

**Environment Management:**
- ✅ `env_define()` retains values when storing (environment.c:323)
- ✅ `env_set()` releases old value, retains new value (environment.c:344-345)
- ✅ `env_get()` retains for caller (environment.c:397)
- ✅ `env_free()` releases all stored values (environment.c:244)

**Array/Object Operations:**
- ✅ Array element assignment releases old value, retains new (values.c:349-350)
- ✅ Object field assignment releases old value, retains new (expressions.c:1220-1222)
- ✅ Array methods (push, insert) properly retain values

**Expression Evaluation:**
- ✅ Binary operators release operands after use (expressions.c:699-700)
- ✅ Function call arguments are released after call (expressions.c:730, 754, 778, etc.)
- ✅ Increment/decrement release old values (expressions.c:1454, 1505)
- ✅ Conditionals release condition values (statements.c:36, 39, 42)

**Control Flow:**
- ✅ If/while/for statements release condition values (statements.c:36-42, 53-57, 101-108)
- ✅ For loop increment expressions released (statements.c:132)
- ✅ Switch case values released after comparison (statements.c:495, 499, 528)
- ✅ For-in iterables released after loop (statements.c:296)

## ❌ Critical Gaps (Memory Leaks)

### 1. **STMT_EXPR Leaks Everything** (CRITICAL)

**Location:** `src/interpreter/runtime/statements.c:27-30`

```c
case STMT_EXPR: {
    eval_expr(stmt->as.expr, env, ctx);  // ❌ Result is NEVER released
    break;
}
```

**Impact:** Every standalone expression leaks:
- REPL: Every line typed leaks memory
- Scripts: Every expression statement leaks

**Examples of leaking code:**
```hemlock
"hello";        // String leaked
[1, 2, 3];      // Array leaked
{ x: 10 };      // Object leaked
x + y;          // Result leaked
foo();          // Return value leaked (unless assigned)
```

**Fix:** Release the expression result:
```c
case STMT_EXPR: {
    Value result = eval_expr(stmt->as.expr, env, ctx);
    value_release(result);  // Release unused result
    break;
}
```

### 2. **Let/Const Double Ownership** (MODERATE)

**Location:** `src/interpreter/runtime/statements.c:7-14, 17-24`

```c
case STMT_LET: {
    Value value = eval_expr(stmt->as.let.value, env, ctx);  // ref_count = 1
    // ... type conversion ...
    env_define(env, stmt->as.let.name, value, 0, ctx);  // ❌ ref_count = 2!
    // ❌ Original reference NEVER released
    break;
}
```

**Issue:** `eval_expr()` returns a value with ref_count=1, then `env_define()` retains it again (ref_count=2), but we never release the first reference.

**Impact:** Variables leak their values when defined:
```hemlock
let s = "hello";  // String has ref_count=2, should be 1
// When variable goes out of scope, only decrements to 1, never freed
```

**Fix:** Release after defining:
```c
case STMT_LET: {
    Value value = eval_expr(stmt->as.let.value, env, ctx);
    if (stmt->as.let.type_annotation != NULL) {
        value = convert_to_type(value, stmt->as.let.type_annotation, env, ctx);
    }
    env_define(env, stmt->as.let.name, value, 0, ctx);
    value_release(value);  // ✅ Release original reference
    break;
}
```

### 3. **Assignment Expression Leaks** (MINOR)

**Location:** `src/interpreter/runtime/expressions.c:138-145`

```c
case EXPR_ASSIGN: {
    Value value = eval_expr(expr->as.assign.value, env, ctx);  // ref_count = 1
    env_set(env, expr->as.assign.name, value, ctx);  // Retains (ref_count = 2)
    return value;  // ❌ Return value with ref_count=2, caller gets it with ref_count=3
}
```

**Issue:** Assignment returns the value, but that value already has 2 references (one from eval_expr, one from env_set). The caller retains again.

**Impact:** Assignments used in expressions leak:
```hemlock
let x = (y = "hello");  // "hello" has ref_count too high
```

**Fix:** Release before returning:
```c
case EXPR_ASSIGN: {
    Value value = eval_expr(expr->as.assign.value, env, ctx);
    env_set(env, expr->as.assign.name, value, ctx);
    value_release(value);  // Release env_set's reference
    return value;  // Caller gets the env_set reference
}
```

Actually, better fix - don't retain in env_set if we're about to return it. Need to review this more carefully.

### 4. **Return Statement Ownership** (COMPLEX)

**Location:** Multiple places where return values are handled

**Issue:** When a function returns, who owns the reference?
- Currently: Return value is retained by caller
- Problem: Some callers release, some don't

**Examples:**
```c
// In function call evaluation:
Value result = eval_expr(fn->body, fn_env, ctx);  // ref_count = 1
return result;  // Caller gets it with ref_count = 1 ✅ (GOOD)

// But in some method calls:
Value result = builtin_fn(args, num_args, ctx);  // ref_count = 1
value_retain(result);  // ❌ Why? (expressions.c:1021)
```

**Fix:** Establish clear ownership convention:
- **Rule:** Functions return values with ref_count=1 (caller owns the reference)
- Remove unnecessary retains after function calls

## 📊 Leak Severity Classification

| Leak Type | Severity | Frequency | Impact |
|-----------|----------|-----------|--------|
| STMT_EXPR | **CRITICAL** | Every expression statement | Major memory leak in REPL and scripts |
| Let/Const double ownership | **MODERATE** | Every variable declaration | Accumulated leak over program lifetime |
| Assignment expression | **MINOR** | Only chained assignments | Rare in practice |
| Return statement | **LOW** | Inconsistent, may be correct | Need more investigation |

## 🔧 Proposed Fixes

### Phase 1: Critical Leaks (Immediate)

1. **Fix STMT_EXPR** - Release expression results
2. **Fix Let/Const** - Release after env_define
3. **Fix Const** - Same as Let

**Estimated changes:** ~10 lines

### Phase 2: Cleanup and Consistency (Short-term)

1. **Audit all eval_expr call sites** - Ensure results are released
2. **Establish ownership conventions** - Document in code comments
3. **Remove unnecessary retains** - Clean up expressions.c

**Estimated changes:** ~50 lines

### Phase 3: Scope-Based Cleanup (Future)

1. **Track local variables** - Maintain cleanup list per scope
2. **Release on scope exit** - Automatic cleanup on return/exception/block-end
3. **Remove manual free() for composite types** - Let refcounting handle it

**Estimated changes:** ~200 lines

## 🧪 Testing Strategy

### 1. Leak Detection Tests

Create tests that:
- Allocate strings/arrays/objects
- Check ref_counts at specific points
- Verify cleanup after scope exit

### 2. REPL Leak Tests

Test REPL memory usage:
- Run expression 1000 times
- Check memory doesn't grow
- Use `valgrind` for verification

### 3. Stress Tests

- Create and destroy 10000 objects
- Verify all memory is freed
- Test with nested objects/arrays

## 📝 Implementation Plan

### Step 1: Fix Critical Leaks (This PR)
- [ ] Fix STMT_EXPR
- [ ] Fix STMT_LET
- [ ] Fix STMT_CONST
- [ ] Add basic leak tests

### Step 2: Comprehensive Audit (Next PR)
- [ ] Audit all eval_expr call sites
- [ ] Fix any remaining leaks
- [ ] Add comprehensive tests
- [ ] Document ownership conventions

### Step 3: Future Enhancement (v0.2)
- [ ] Implement scope-based cleanup
- [ ] Remove manual free() requirement for composite types
- [ ] Add cycle detection (already exists, but verify it works)

## 🎯 Success Criteria

After fixes:
1. ✅ REPL can run 1000+ expressions without memory growth
2. ✅ All heap allocations are freed by program end
3. ✅ Valgrind shows 0 leaks in test suite
4. ✅ ref_count is always 1 for newly created values
5. ✅ ref_count reaches 0 when last reference is released

## 💡 Key Insights

1. **Infrastructure is solid** - Refcounting works, just incomplete
2. **Ownership is unclear** - Need clear conventions for who owns references
3. **Easy to fix** - Most leaks are simple missing `value_release()` calls
4. **Foundation for GC-free** - With scope cleanup, no GC needed

---

**Conclusion:** The refcounting system is 80% complete. The remaining 20% is adding missing `value_release()` calls in strategic places. No major refactoring needed, just surgical fixes.
