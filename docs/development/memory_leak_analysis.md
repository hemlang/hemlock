# Memory Leak Root Cause Analysis

## The Problem

ALL reference-counted types (String, Array, Object, Function, Buffer, Task) leak memory because of a mismatch in the reference counting pattern.

## Current Flow

1. Create object: `object_new()` sets `ref_count = 1`
2. Store in environment: `env_define()` calls `value_retain()` → `ref_count = 2`
3. Expression returns (no release of temporary)
4. Environment freed: `env_free()` calls `value_release()` → `ref_count = 1`
5. **Object NOT freed** because ref_count > 0

## The Bug

When `eval_expr()` creates and returns a value, the caller receives a value with ref_count=1 (the "temporary" reference). When this value is stored in the environment via `env_define()`, the environment retains it (ref_count=2). But the temporary reference is never released!

## Solution Options

### Option A: Release temporary after storing (COMPLEX)
After every `env_define()`, call `value_release()` on the value to release the temporary:
```c
Value value = eval_expr(...);
env_define(env, name, value, 0, ctx);
value_release(value);  // Release temporary
```

**Pros:** Follows strict ownership rules
**Cons:** Requires changes throughout the codebase, error-prone

### Option B: Start with ref_count=0 (SIMPLER)
Initialize ref_count=0 instead of 1. The first `value_retain()` brings it to 1.

**Pros:** Simple, one-line change per type
**Cons:** Breaks intuition that created objects start with ref_count=1

### Option C: Don't retain in env_define() (INCORRECT)
Remove `value_retain()` from `env_define()`.

**Pros:** Simple
**Cons:** Breaks when same value stored in multiple places or returned from functions

## Recommended Fix: **Option A** (Release temporary references)

We need to release temporary references after storing them. The pattern is:
- If you create a value and store it: release your temporary reference
- If you create a value and return it: keep the reference (caller owns it)

This matches standard reference counting semantics.

## Implementation Plan

1. After `env_define()` in STMT_LET/STMT_CONST, add `value_release(value)`
2. After array_push() for temporary values, add `value_release(value)`
3. After object field assignment for temporaries, add `value_release(value)`
4. Review all eval_expr() call sites

This is the correct solution but requires careful review of the entire codebase.
