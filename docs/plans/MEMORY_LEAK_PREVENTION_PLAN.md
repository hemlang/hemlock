# Memory Leak Prevention Plan

> Ensuring the Hemlock runtime is free of memory leaks and upholds its contract with the programmer.

**Date:** 2026-01-16
**Status:** Proposed
**Version:** 1.0

---

## Executive Summary

Hemlock's design philosophy states: *"We give you the tools to be safe, but we don't force you to use them."* This means the **runtime itself** must be leak-free even when user code uses unsafe features. The programmer's contract is:

1. **User allocations** (`alloc`, `buffer`) are the programmer's responsibility to `free`
2. **Runtime-internal allocations** (strings, arrays, objects, closures) are automatically managed via reference counting
3. **Errors and exceptions** must not leak memory
4. **Async tasks** have clear ownership semantics
5. **The runtime never hides allocations** from the programmer

This plan identifies gaps in the current infrastructure and proposes systematic improvements.

---

## Table of Contents

1. [Current State Assessment](#current-state-assessment)
2. [Identified Gaps](#identified-gaps)
3. [Proposed Improvements](#proposed-improvements)
4. [Testing Strategy](#testing-strategy)
5. [Documentation Requirements](#documentation-requirements)
6. [Implementation Phases](#implementation-phases)
7. [Success Criteria](#success-criteria)

---

## Current State Assessment

### Strengths

| Component | Implementation | Location |
|-----------|---------------|----------|
| Reference counting | Atomic ops with `__ATOMIC_SEQ_CST` | `src/backends/interpreter/values.c:413-550` |
| Cycle detection | VisitedSet for graph traversal | `src/backends/interpreter/values.c:1345-1480` |
| Thread isolation | Deep copy on spawn | `src/backends/interpreter/values.c:1687-1859` |
| Profiler with leak detection | AllocSite tracking | `src/backends/interpreter/profiler/` |
| ASAN integration | CI pipeline with leak detection | `.github/workflows/tests.yml` |
| Valgrind support | Multiple Makefile targets | `Makefile:189-327` |
| Comprehensive test script | Category-based testing | `tests/leak_check.sh` |

### Current Memory Ownership Model

```
┌─────────────────────────────────────────────────────────────────┐
│                    PROGRAMMER RESPONSIBILITY                     │
├─────────────────────────────────────────────────────────────────┤
│  alloc(size)  ────────────────────────────────►  free(ptr)      │
│  buffer(size) ────────────────────────────────►  free(buf)      │
│  ptr arithmetic ──────────────────────────────►  bounds safety  │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    RUNTIME RESPONSIBILITY                        │
├─────────────────────────────────────────────────────────────────┤
│  String literals/operations ──────► refcount + auto-release     │
│  Array literals/operations ───────► refcount + auto-release     │
│  Object literals/operations ──────► refcount + auto-release     │
│  Function closures ───────────────► refcount + env release      │
│  Task results ────────────────────► released after join()       │
│  Channel buffers ─────────────────► released on close()         │
└─────────────────────────────────────────────────────────────────┘
```

---

## Identified Gaps

### Gap 1: Error Path Cleanup (HIGH PRIORITY)

**Issue:** When exceptions occur mid-execution, allocated temporaries may leak.

**Example Scenario:**
```hemlock
fn process_data() {
    let arr = [1, 2, 3];           // Array allocated
    let transformed = arr.map(fn(x) {
        if (x == 2) { throw "error"; }  // Exception thrown
        return x * 2;
    });
    // 'transformed' partially allocated, 'arr' may not be released
}
```

**Current State:** The interpreter's exception handling unwinds the call stack but may not release all temporaries created during expression evaluation.

**Files Affected:**
- `src/backends/interpreter/runtime/evaluator.c` (expression evaluation)
- `src/backends/interpreter/runtime/context.c` (exception handling)

### Gap 2: Detached Task Result Ownership (MEDIUM PRIORITY)

**Issue:** `detach(task)` allows fire-and-forget execution, but the task's result may never be collected.

**Current Behavior:**
```hemlock
let task = spawn(compute_something);
detach(task);  // Task runs in background
// What happens to the return value when task completes?
```

**Files Affected:**
- `src/backends/interpreter/builtins/concurrency.c:148-165` (task completion)
- `src/backends/interpreter/values.c:745-780` (task_free)

### Gap 3: Channel Close vs. Drain Semantics (MEDIUM PRIORITY)

**Issue:** When a channel is closed with buffered values remaining, are those values properly released?

**Scenario:**
```hemlock
let ch = channel(10);
ch.send("a");
ch.send("b");
ch.close();  // Are "a" and "b" released?
```

**Files Affected:**
- `src/backends/interpreter/values.c:850-915` (channel_close, channel_free)

### Gap 4: Null Coalescing AST Leak (FIXED)

**Issue:** The optimizer was optimizing away null coalescing expressions when the result was known at compile time, but not freeing the discarded AST nodes.

**Root Cause:** In `optimizer.c`, when `??` was optimized (e.g., `"value" ?? "default"` → `"value"`), the optimizer returned the kept child without freeing the parent `EXPR_NULL_COALESCE` node or the discarded child.

**Fix:** Added proper cleanup in the optimizer to free discarded nodes:
- Save the result child
- Free the unused child with `expr_free()`
- Free the parent node structure
- Return the saved result

**Files Modified:**
- `src/frontend/optimizer/optimizer.c` (null coalescing optimization cleanup)

### Gap 5: Closure Capture List Granularity (LOW PRIORITY)

**Issue:** Closures capture the entire environment chain rather than only referenced variables, potentially extending lifetimes unnecessarily.

**Example:**
```hemlock
fn outer() {
    let large_data = buffer(1000000);  // 1MB
    let counter = 0;

    return fn() {
        return counter;  // Only uses 'counter', but 'large_data' is also captured
    };
}
let f = outer();  // 'large_data' kept alive until 'f' is released
```

**Files Affected:**
- `src/backends/interpreter/values.c` (function_new, closure creation)
- `src/frontend/parser/` (variable capture analysis)

### Gap 6: Cyclic Reference in Async Coordination (LOW PRIORITY)

**Issue:** Tasks referencing channels that reference tasks could create cycles.

**Scenario:**
```hemlock
let ch = channel(1);
let task = spawn(fn() {
    ch.send(task);  // Task sends itself through channel
});
```

**Current mitigation:** Deep copy on send prevents this specific case, but object cycles are possible.

### Gap 7: FFI Memory Boundary Documentation (DOCUMENTATION)

**Issue:** Ownership transfer across FFI boundary is not formally documented.

**Questions to clarify:**
- Who owns memory returned by extern functions?
- What happens to strings passed to C functions?
- How should callbacks handle memory?

---

## Proposed Improvements

### Phase 1: Critical Fixes (Weeks 1-2)

#### 1.1 Exception-Safe Expression Evaluation

**Approach:** Implement a "temporary value stack" that tracks allocations during expression evaluation.

```c
// In evaluator.c
typedef struct {
    Value *temps;
    int count;
    int capacity;
} TempStack;

// Push temporary before returning from sub-expression
Value eval_binary_op(Evaluator *e, BinaryExpr *expr) {
    Value left = eval_expr(e, expr->left);
    temp_stack_push(e->temps, left);  // Track

    Value right = eval_expr(e, expr->right);
    temp_stack_push(e->temps, right);  // Track

    Value result = perform_op(left, right);

    temp_stack_pop(e->temps, 2);  // Release on success
    return result;
}

// On exception, cleanup releases all tracked temps
void exception_cleanup(Evaluator *e) {
    while (e->temps->count > 0) {
        Value v = temp_stack_pop(e->temps, 1);
        value_release(v);
    }
}
```

**Testing:**
- Add tests in `tests/memory/exception_cleanup.hml`
- ASAN verification of exception paths

#### 1.2 Detached Task Result Cleanup

**Approach:** Detached tasks release their own result when complete.

```c
// In concurrency.c - task completion handler
void task_complete(Task *task, Value result) {
    pthread_mutex_lock(task->task_mutex);
    task->result = result;
    value_retain(task->result);  // Task owns result
    task->state = TASK_COMPLETED;

    if (task->detached) {
        // No one will join(), so release result now
        value_release(task->result);
        task->result = VAL_NULL;
    }
    pthread_mutex_unlock(task->task_mutex);
}
```

**Testing:**
- Extend `tests/manual/stress_memory_leak.hml` with detached task stress
- Verify no growth in ASAN leak report

#### 1.3 Channel Drain on Close

**Approach:** `channel_close()` and `channel_free()` must drain remaining values.

```c
// In values.c
void channel_free(Channel *ch) {
    pthread_mutex_lock(ch->mutex);

    // Drain buffered values
    while (ch->count > 0) {
        Value v = ch->buffer[ch->head];
        value_release(v);
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
    }

    pthread_mutex_unlock(ch->mutex);

    // Free synchronization primitives
    pthread_mutex_destroy(ch->mutex);
    pthread_cond_destroy(ch->not_empty);
    pthread_cond_destroy(ch->not_full);
    pthread_cond_destroy(ch->rendezvous);

    free(ch->buffer);
    free(ch);
}
```

**Testing:**
- Add `tests/memory/channel_drain.hml`

### Phase 2: Known Issue Fixes (Weeks 3-4)

#### 2.1 Null Coalescing AST Fix

**Approach:** Ensure AST nodes for short-circuited expressions are still visited for cleanup, or use value-based representation instead of AST references at evaluation time.

**Investigation needed:** Determine if AST nodes should be owned by parser or copied during evaluation.

#### 2.2 Closure Capture Optimization (Optional)

**Approach:** Analyze variable references in function body and create minimal capture list.

```c
// During function parsing
typedef struct {
    char **captured_names;
    int count;
} CaptureList;

CaptureList *analyze_captures(FunctionExpr *fn, Environment *env) {
    CaptureList *list = capture_list_new();
    visit_expr(fn->body, fn->params, env, list);  // Collect referenced free vars
    return list;
}
```

**Note:** This is an optimization, not a correctness fix. May be deferred.

### Phase 3: Testing Infrastructure Hardening (Weeks 5-6)

#### 3.1 Leak Regression Suite

Create a dedicated leak regression test suite that specifically targets each gap:

```
tests/memory/
├── regression/
│   ├── exception_in_map.hml
│   ├── exception_in_filter.hml
│   ├── exception_in_reduce.hml
│   ├── exception_in_nested_call.hml
│   ├── detached_task_result.hml
│   ├── detached_task_spawn_loop.hml
│   ├── channel_close_with_values.hml
│   ├── channel_gc_stress.hml
│   ├── null_coalesce_literal.hml
│   ├── closure_large_capture.hml
│   └── cyclic_object_channel.hml
```

#### 3.2 Continuous Leak Monitoring

**Enhancement to `tests/leak_check.sh`:**

```bash
# Add baseline comparison
BASELINE_FILE="tests/memory/baseline_leaks.txt"

check_regression() {
    local current_leaks=$(count_leaks)
    local baseline_leaks=$(cat "$BASELINE_FILE" 2>/dev/null || echo "0")

    if [ "$current_leaks" -gt "$baseline_leaks" ]; then
        echo "LEAK REGRESSION: $current_leaks > $baseline_leaks"
        exit 1
    fi
}
```

#### 3.3 Fuzz Testing for Memory Safety

Integrate libFuzzer or AFL for memory safety fuzzing:

```c
// fuzz_evaluator.c
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *source = malloc(size + 1);
    memcpy(source, data, size);
    source[size] = '\0';

    // Parse and evaluate with ASAN active
    AST *ast = parse(source);
    if (ast) {
        ExecutionContext *ctx = ctx_new();
        evaluate(ctx, ast);  // ASAN will catch leaks/UAF
        ctx_free(ctx);
        ast_free(ast);
    }

    free(source);
    return 0;
}
```

### Phase 4: Documentation & Contract (Week 7)

#### 4.1 Memory Ownership Documentation

Create `docs/advanced/memory-ownership.md`:

```markdown
# Memory Ownership in Hemlock

## The Contract

1. **You allocate, you free**: `alloc()` and `buffer()` return memory you own.
2. **Runtime manages values**: Strings, arrays, objects are reference-counted.
3. **Exceptions clean up**: Throwing doesn't leak (after Phase 1 fix).
4. **Tasks copy arguments**: Spawned tasks get their own copy of data.
5. **Channels transfer ownership**: `send()` transfers, `recv()` receives.

## Ownership Transfer Points

| Operation | From | To |
|-----------|------|----|
| `let x = expr` | expr evaluation | variable binding |
| `return val` | function | caller |
| `ch.send(val)` | sender | channel buffer |
| `ch.recv()` | channel buffer | receiver |
| `spawn(fn, args)` | caller (copies) | task |
| `join(task)` | task | caller |

## FFI Ownership Rules

1. **Passing to C**: Hemlock retains ownership unless `move` qualifier used
2. **Receiving from C**: Hemlock takes ownership, will free when refcount→0
3. **Callbacks**: Arguments owned by C, return value owned by Hemlock
```

---

## Testing Strategy

### Test Categories

| Category | Description | Tool |
|----------|-------------|------|
| Unit | Individual function leak tests | ASAN |
| Integration | Multi-component scenarios | ASAN + Valgrind |
| Stress | High-volume allocation/free cycles | ASAN (leak-check=no) |
| Fuzz | Random input memory safety | libFuzzer + ASAN |
| Regression | Known-fixed leak scenarios | ASAN + baseline |

### CI Pipeline Enhancement

```yaml
# .github/workflows/memory.yml
memory-safety:
  runs-on: ubuntu-latest
  steps:
    - name: Build with ASAN
      run: make asan

    - name: Run leak regression suite
      run: make leak-regression

    - name: Compare to baseline
      run: |
        ./tests/leak_check.sh --baseline
        if [ $? -ne 0 ]; then
          echo "::error::Leak regression detected"
          exit 1
        fi

    - name: Fuzz test (5 minutes)
      run: make fuzz-test FUZZ_TIME=300
```

---

## Implementation Phases

| Phase | Focus | Duration | Priority |
|-------|-------|----------|----------|
| 1 | Critical fixes (exception, detach, channel) | 2 weeks | HIGH |
| 2 | Known issue fixes (null coalesce, captures) | 2 weeks | MEDIUM |
| 3 | Testing infrastructure | 2 weeks | HIGH |
| 4 | Documentation | 1 week | MEDIUM |

### Dependencies

```
Phase 1 ──────► Phase 3 (tests verify fixes)
    │
    └──────► Phase 4 (document new guarantees)

Phase 2 ──────► Phase 3 (add regression tests)
```

---

## Success Criteria

### Quantitative

- [ ] Zero leaks reported by ASAN on full test suite
- [ ] Zero leaks reported by Valgrind on full test suite
- [ ] Leak baseline established and enforced in CI
- [ ] 100% of identified gaps addressed or documented as acceptable

### Qualitative

- [ ] Memory ownership documented in `docs/advanced/memory-ownership.md`
- [ ] FFI ownership rules documented
- [ ] Regression test for each fixed leak
- [ ] Fuzz testing integrated into CI

### Runtime Contract Verification

The following guarantees must hold after implementation:

1. **No leak on normal execution**: Running any valid program and exiting normally leaks no memory (runtime-internal).

2. **No leak on exception**: Throwing and catching exceptions leaks no memory.

3. **No leak on task completion**: Completed tasks (joined or detached) leak no memory.

4. **No leak on channel close**: Closing channels releases all buffered values.

5. **Deterministic cleanup**: Order of destructor calls is predictable (LIFO for defer, topological for objects).

---

## Appendix: Files Requiring Modification

| File | Changes |
|------|---------|
| `src/backends/interpreter/runtime/evaluator.c` | Add TempStack for exception-safe evaluation |
| `src/backends/interpreter/runtime/context.c` | Exception cleanup integration |
| `src/backends/interpreter/builtins/concurrency.c` | Detached task result cleanup |
| `src/backends/interpreter/values.c` | Channel drain, capture optimization |
| `tests/leak_check.sh` | Baseline comparison |
| `.github/workflows/tests.yml` | Add memory regression job |
| `docs/advanced/memory-ownership.md` | New documentation |
| `CLAUDE.md` | Update with ownership guarantees |

---

## References

- Current profiler: `src/backends/interpreter/profiler/profiler.c`
- Reference counting: `src/backends/interpreter/values.c:413-550`
- Task management: `src/backends/interpreter/builtins/concurrency.c`
- ASAN documentation: https://clang.llvm.org/docs/AddressSanitizer.html
- Valgrind memcheck: https://valgrind.org/docs/manual/mc-manual.html
