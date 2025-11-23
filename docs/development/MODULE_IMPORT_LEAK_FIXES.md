# Hemlock Memory Leak Analysis

**Date:** 2025-11-23
**Analyzer:** Claude Code
**Status:** In Progress

## Executive Summary

This document provides a comprehensive analysis of Hemlock's internal memory management architecture, identified memory leaks, and proposed solutions.

---

## 1. Memory Management Architecture

### 1.1 Reference Counting System

Hemlock uses **manual reference counting** for heap-allocated types:

- **String**: `ref_count` field, managed by `string_retain()` / `string_release()`
- **Buffer**: `ref_count` field, managed by `buffer_retain()` / `buffer_release()`
- **Array**: `ref_count` field, managed by `array_retain()` / `array_release()`
- **Object**: `ref_count` field, managed by `object_retain()` / `object_release()`
- **Function**: `ref_count` field, managed by `function_retain()` / `function_release()`
- **Task**: `ref_count` field (atomic), managed by `task_retain()` / `task_release()`
- **Channel**: `ref_count` field (atomic), managed by `channel_retain()` / `channel_release()`
- **Environment**: `ref_count` field (atomic), managed by `env_retain()` / `env_release()`

### 1.2 Value Lifecycle

All `Value` operations follow these patterns:

1. **Creation**: `val_*()` constructors create values with `ref_count = 1`
2. **Retention**: `value_retain()` increments ref_count when storing/passing values
3. **Release**: `value_release()` decrements ref_count, frees when reaching 0
4. **Cleanup**: `value_free()` recursively frees value and nested structures

### 1.3 Environment Ownership

- Environments form a **parent-child hierarchy**
- Child environments **retain** their parent (prevents premature deallocation)
- Variables stored in environments are **retained** on define/set, **released** on env_free
- Closure functions **retain** their closure_env (captured environment)

### 1.4 Circular Reference Handling

**Problem**: Functions can capture environments containing themselves → circular reference → memory leak

**Solution**: `env_break_cycles()` breaks circular references by:
- Recursively finding all functions in environment values
- Setting `fn->closure_env = NULL` after releasing it
- Using a visited set to handle nested objects/arrays safely

**Critical**: Must call `env_break_cycles()` before final `env_release()` on top-level environments!

---

## 2. Identified Memory Leaks

### 2.1 **[CRITICAL] Module System: Double Retention on Import/Export**

**Location**: `src/module.c`, function `execute_module()`

**Bug Description**:
When importing values from modules, `env_get()` is called which **retains** the value. Then the value is passed to `env_define()` which **retains again**. This causes double-retention, and values are never freed.

**Affected Lines**:
- Line 360: Namespace import
  ```c
  Value val = env_get(imported->exports_env, export_name, ctx);  // +1 ref
  ns->field_values[ns->num_fields] = val;  // object stores it, but val is still retained
  ```

- Lines 378-379: Named imports
  ```c
  Value val = env_get(imported->exports_env, import_name, ctx);  // +1 ref
  env_define(module_env, bind_name, val, 1, ctx);  // +1 ref (DOUBLE!)
  ```

- Lines 413-414: Re-exports
  ```c
  Value val = env_get(reexported->exports_env, export_name, ctx);  // +1 ref
  env_define(module_env, final_name, val, 1, ctx);  // +1 ref (DOUBLE!)
  ```

**Impact**: Every imported value leaks memory. Modules with many imports will leak significantly.

**Fix**: Call `value_release(val)` after `env_define()` to compensate for double-retention:

```c
// Named imports - FIX
Value val = env_get(imported->exports_env, import_name, ctx);
env_define(module_env, bind_name, val, 1, ctx);
value_release(val);  // Release extra reference from env_get

// Re-exports - FIX
Value val = env_get(reexported->exports_env, export_name, ctx);
env_define(module_env, final_name, val, 1, ctx);
value_release(val);  // Release extra reference from env_get
```

For namespace imports, values stored in objects don't auto-retain, so we need to retain when storing:

```c
// Namespace imports - FIX
Value val = env_get(imported->exports_env, export_name, ctx);  // +1 ref
ns->field_names[ns->num_fields] = strdup(export_name);
ns->field_values[ns->num_fields] = val;  // Store it (val already retained by env_get)
ns->num_fields++;
// No value_release needed - object now owns the reference
```

Wait, actually checking the object code... when objects store values in `object_free_internal()`, it calls `value_release()` on field values. So the object DOES release. But we're getting the value with env_get which retains it, so we actually don't need to retain again when storing in the object.

Let me recheck: In namespace imports, we're getting values with env_get (+1), storing them in an object (no additional retain), then the object is stored in the environment with env_define (which will retain the object). When the object is freed, it releases the field values. So the flow is:
1. env_get -> val has ref_count from export_env + 1 from env_get call = 2 references
2. Store in object field (no retain)
3. Object stored in environment (object retained, not the val inside)
4. When object freed, it releases field values (-1 on val)
5. When export_env freed, it releases its values (-1 on val)
6. Val should be freed

But wait, the issue is that the value is in the exported environment AND in the namespace object. The export_env holds a reference, and when we env_get, it retains and returns. Then we store that retained value in the object. When we define the object in module_env, the OBJECT is retained, not the values inside.

Hmm, I need to trace this more carefully. Let me look at env_define again:
- env_define calls value_retain(value) on line 323 of environment.c

So:
1. val = env_get(...) -> val.ref_count goes from N to N+1 (caller owns this reference)
2. We store val in object field (no retain, just assignment)
3. env_define(module_env, ns_name, val_object(ns), ...) -> object is retained, val inside is NOT auto-retained
4. We never release the val from step 1!

So for namespace imports, we DO need to release after storing:

Actually wait, let me check object field assignment more carefully. When an object is freed, what happens to field values?

In values.c line 1133:
```c
for (int i = 0; i < obj->num_fields; i++) {
    free(obj->field_names[i]);
    // Release field values (decrements ref_counts)
    value_release(obj->field_values[i]);
}
```

So yes, when an object is freed, it releases its field values. But we're storing values that were obtained via env_get which already retained them. So:

1. val = env_get(...) -> +1 ref (caller owns)
2. Store in object field
3. Object freed later -> releases field value (-1 ref)

The problem is that between steps 2 and 3, the value has +1 extra reference that's never balanced until the object is freed. But the object might not be freed if there are other leaks.

Actually, I think the correct pattern for object field assignment is:
- When storing a value in an object field, the object takes ownership of a reference
- The caller should NOT release after storing (the object will release when freed)

So for namespace imports:
```c
Value val = env_get(imported->exports_env, export_name, ctx);  // +1 ref
ns->field_values[ns->num_fields] = val;  // Object takes ownership of this reference
```

This should be correct. The object will release when freed.

For named imports and re-exports:
```c
Value val = env_get(imported->exports_env, import_name, ctx);  // +1 ref
env_define(module_env, bind_name, val, 1, ctx);  // +1 ref (env takes ownership)
value_release(val);  // Release our temporary reference
```

This IS the leak - we need to release after env_define.

OK so the fixes are:
1. Named imports: Add value_release after env_define
2. Re-exports: Add value_release after env_define
3. Namespace imports: Keep as-is (object takes ownership)

Actually, I realize I need to verify the env_define semantics. Let me check line 323 of environment.c:
```c
value_retain(value);  // Retain the value
env->values[env->count] = value;
```

So env_define DOES retain. So the caller is expected to still own their reference. This means:
- Caller gets value (might be retained for them)
- Caller passes to env_define
- env_define retains the value (env now owns a reference)
- Caller still owns their reference and should release it when done

So yes, after env_define, we should release our reference from env_get.

### 2.2 **[HIGH] Type Registry Cleanup: Missing cleanup_enum_types() Call**

**Location**: `src/main.c:327`

**Bug Description**:
In the REPL-only execution path, `cleanup_enum_types()` is not called, but `cleanup_object_types()` is. This creates an inconsistency and leaks enum type definitions.

**Current Code** (line 323-327):
```c
// No file or command specified - start REPL
run_repl();

// Cleanup object type registry before exit
cleanup_object_types();
return 0;
```

**Fix**:
```c
// No file or command specified - start REPL
run_repl();

// Cleanup object and enum type registries before exit
cleanup_object_types();
cleanup_enum_types();
return 0;
```

**Impact**: Minor - only affects REPL sessions that define enums, but should be fixed for completeness.

---

## 3. Memory Management Best Practices

### 3.1 Reference Counting Rules

1. **Creation gives ownership**: `val_string("hello")` gives you a value with ref_count=1
2. **Storing retains**: `env_define()`, `array_push()`, `object.field = val` all retain
3. **Getting retains**: `env_get()` retains and returns - caller owns a reference
4. **Release when done**: Always release temporary references after use
5. **Balance retain/release**: Every retain needs a matching release

### 3.2 Environment Patterns

**Pattern 1: Define a new value**
```c
Value val = val_string("test");
env_define(env, "x", val, 0, ctx);  // env retains
value_release(val);  // Release our reference
```

**Pattern 2: Get and use a value**
```c
Value val = env_get(env, "x", ctx);  // +1 ref
print_value(val);
value_release(val);  // Release our reference
```

**Pattern 3: Get and re-define elsewhere**
```c
Value val = env_get(env1, "x", ctx);  // +1 ref
env_define(env2, "y", val, 0, ctx);  // +1 ref (env2 retains)
value_release(val);  // Release our reference (env2 still holds it)
```

### 3.3 Module System Patterns

**Import Pattern** (CORRECT):
```c
Value val = env_get(imported->exports_env, name, ctx);  // +1 ref from env_get
env_define(module_env, local_name, val, 1, ctx);       // +1 ref from env_define
value_release(val);  // Release temp reference (-1)
// Net result: value has 2 refs (one in export_env, one in module_env)
```

**Object Storage Pattern** (CORRECT):
```c
Value val = env_get(exported_env, name, ctx);  // +1 ref (caller owns)
object->field_values[i] = val;                 // Object takes ownership
// Don't release - object now owns the reference
```

---

## 4. Proposed Architectural Improvements

### 4.1 Automatic Leak Detection

**Proposal**: Add a debug mode that tracks all allocations/frees

```c
#ifdef HEMLOCK_DEBUG_MEMORY
static struct {
    void **pointers;
    int count;
    int capacity;
} allocation_tracker;

void track_alloc(void *ptr, const char *type, const char *file, int line);
void track_free(void *ptr);
void report_leaks(void);  // Call at program exit
#endif
```

**Usage**:
```bash
make clean
make DEBUG_MEMORY=1
./hemlock script.hml
# Prints leak report at exit
```

### 4.2 Ownership Documentation

**Proposal**: Add ownership annotations to function signatures

```c
// Returns a new reference (caller must release)
Value env_get(Environment *env, const char *name, ExecutionContext *ctx) RETURNS_RETAINED;

// Takes ownership of value (retains internally)
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx) TAKES_RETAINED;

// Borrows value (does not retain)
void print_value(Value val) BORROWS_VALUE;
```

Where:
- `RETURNS_RETAINED`: Caller owns a reference and must release
- `TAKES_RETAINED`: Function retains, caller should release their reference after call
- `BORROWS_VALUE`: Function doesn't retain, caller keeps ownership

### 4.3 Smart Pointer Helpers (Future)

**Proposal**: Add RAII-style helpers for common patterns

```c
// Auto-release wrapper
#define WITH_VALUE(var, expr) \
    Value var = (expr); \
    __attribute__((cleanup(value_release_cleanup))) Value *var##_guard = &var

// Usage:
WITH_VALUE(val, env_get(env, "x", ctx)) {
    print_value(val);
    // Auto-released on scope exit
}
```

### 4.4 Valgrind Integration

**Proposal**: Add Makefile target for leak checking

```makefile
.PHONY: valgrind
valgrind: hemlock
	@echo "Running valgrind leak check..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		./hemlock examples/hello.hml

.PHONY: valgrind-test
valgrind-test: hemlock
	@for test in tests/**/*.hml; do \
		echo "Checking $$test..."; \
		valgrind --leak-check=full --error-exitcode=1 -q ./hemlock $$test || exit 1; \
	done
```

---

## 5. Testing Strategy

### 5.1 Memory Leak Test Suite

Create `tests/memory_leaks/` with tests for:
- Module imports/exports (various patterns)
- Closure creation and cleanup
- Large object graphs
- Circular references
- Array/string operations

### 5.2 Continuous Integration

Add to CI pipeline:
1. Build with address sanitizer (`-fsanitize=address`)
2. Run test suite
3. Check for any memory leaks
4. Fail build if leaks detected

### 5.3 Manual Testing Protocol

Before each release:
1. Run `make valgrind-test`
2. Check REPL for leaks (run for 100+ commands)
3. Test large programs (stdlib usage, async, etc.)
4. Verify cleanup of all registries

---

## 6. Implementation Plan

### Phase 1: Critical Fixes (IMMEDIATE)
- [ ] Fix module system double-retention (module.c)
- [ ] Fix type registry cleanup (main.c)
- [ ] Test fixes with existing test suite

### Phase 2: Testing & Validation (THIS WEEK)
- [ ] Add valgrind Makefile targets
- [ ] Run valgrind on all tests
- [ ] Create memory leak test cases
- [ ] Document any remaining known leaks

### Phase 3: Architectural Improvements (NEXT SPRINT)
- [ ] Add ownership annotations to API
- [ ] Implement debug memory tracker
- [ ] Add CI memory leak checks
- [ ] Create memory management guide

---

## 7. Known Acceptable Leaks

Some "leaks" are intentional or acceptable:

1. **Global type registries**: Freed at program exit via cleanup_*_types()
2. **Builtin function names**: Registered once, never freed (static lifetime)
3. **FFI libraries**: dlopen'd libraries, freed via ffi_cleanup()
4. **pthread resources**: Some pthread internals may show as "still reachable"

These are documented and considered acceptable in long-running programs.

---

## 8. Conclusion

Hemlock's reference counting system is fundamentally sound, but has implementation bugs in the module system that cause value leaks. The proposed fixes are straightforward and low-risk. Additional architectural improvements will make future leak prevention easier.

**Priority Actions**:
1. Fix module.c double-retention (3 locations)
2. Fix main.c enum cleanup
3. Add valgrind testing
4. Document ownership patterns

**Success Criteria**:
- All tests pass under valgrind with no leaks
- Documentation clearly explains memory ownership
- CI pipeline catches future leaks
