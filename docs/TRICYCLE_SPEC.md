# Tricycle: Hemlock's Memory Safety Checker

> "Training wheels for the unsafe. Take them off when you're ready."

**Version:** 0.1.0 (Foundation Spec)
**Status:** Draft
**Author:** Claude + Human

---

## Overview

Tricycle is an **opt-in static analyzer** that proves memory safety properties for Hemlock programs. It piggybacks on Hemlock's existing infrastructure (lexer, parser, type checker, LSP) to provide:

- **Double-free detection**
- **Use-after-free detection**
- **Memory leak detection**
- **Null pointer safety**
- **Async lifetime safety**
- **Ownership tracking**

Tricycle does NOT change Hemlock's semantics. It's a separate tool that **rejects programs it can't prove safe** while letting unsafe programs compile normally with `hemlock`/`hemlockc`.

---

## Philosophy

### "Prove It Or Lose It"

Tricycle follows a simple principle: if it can't prove your code is memory-safe, it refuses to bless it. This is the opposite of Hemlock's default "trust the programmer" philosophy.

```
┌─────────────────────────────────────────────────────────────────┐
│                        HEMLOCK SPECTRUM                         │
│                                                                 │
│  hemlock (interpreter)     hemlockc (compiler)     tricycle    │
│  ──────────────────────────────────────────────────────────────│
│  "run it"                  "type check it"        "prove it"   │
│  No checks                 Type annotations       Memory safety │
│  Maximum freedom           Catch type errors      Zero UB       │
└─────────────────────────────────────────────────────────────────┘
```

### Not a Language Change

Tricycle is purely additive:
- No new syntax required (but annotations help)
- No runtime overhead (static analysis only)
- No changes to existing valid Hemlock programs
- Opt-in per-file or per-project

---

## Core Concepts

### 1. Allocation State

Every pointer-typed expression has an **allocation state**:

```
┌──────────────┬─────────────────────────────────────────────────┐
│ State        │ Meaning                                         │
├──────────────┼─────────────────────────────────────────────────┤
│ Unallocated  │ Never allocated, or after free()                │
│ Allocated    │ After alloc()/buffer(), valid to use            │
│ Moved        │ Ownership transferred, invalid to use           │
│ Borrowed     │ Reference to someone else's memory              │
│ Unknown      │ Came from external source, conservatively unsafe│
└──────────────┴─────────────────────────────────────────────────┘
```

### 2. Ownership

Every allocated pointer has exactly **one owner** responsible for freeing it:

```hemlock
let p = alloc(64);      // p owns the memory
let q = p;              // Ownership MOVED to q, p is now invalid
free(q);                // q frees it
// free(p);             // ERROR: p no longer owns anything
// ptr_write_i32(p, 1); // ERROR: use after move
```

### 3. Borrowing

Functions can **borrow** pointers without taking ownership:

```hemlock
fn print_buffer(borrow ptr: ptr, len: i32) {
    // Can read ptr, but doesn't own it
    // Cannot free(ptr) or store it beyond function scope
}

let p = alloc(64);
print_buffer(p, 64);    // p is borrowed, not moved
free(p);                // Still valid - we still own p
```

### 4. Lifetimes

Memory has a **lifetime** - the span during which it's valid:

```hemlock
fn dangerous(): ptr {
    let p = alloc(64);
    return p;           // OK: ownership transferred to caller
}

fn also_dangerous(): ptr {
    let local = 42;
    return &local;      // ERROR: returning pointer to stack memory
}
```

### 5. Async Safety

Spawned tasks must not reference memory that might be freed:

```hemlock
let p = alloc(64);
let task = spawn(worker, p);    // p's lifetime must exceed task
free(p);                        // ERROR: task might still use p
join(task);                     // Must join before free
free(p);                        // OK now
```

---

## New Annotations (Optional but Helpful)

Tricycle can infer ownership in many cases, but explicit annotations help:

### Ownership Modifiers

```hemlock
// Takes ownership - caller loses access
fn consume(own ptr: ptr) {
    // ...
    free(ptr);  // We own it, we free it
}

// Borrows - caller keeps ownership
fn inspect(borrow ptr: ptr): i32 {
    return ptr_read_i32(ptr);
    // Cannot free or store ptr
}

// Mutable borrow - can modify but not free
fn modify(borrow mut ptr: ptr) {
    ptr_write_i32(ptr, 42);
}
```

### Lifetime Annotations (Advanced)

```hemlock
// Return value lives as long as input
fn get_field<'a>(borrow obj: ptr<'a>): ptr<'a> {
    return ptr_offset(obj, 8);
}

// Multiple lifetimes
fn combine<'a, 'b>(borrow a: ptr<'a>, borrow b: ptr<'b>): ptr<'a> {
    // Return tied to 'a lifetime
}
```

**Note:** Lifetime annotations are Phase 2. Foundation focuses on ownership without explicit lifetimes.

---

## Architecture

### Integration Points

```
┌─────────────────────────────────────────────────────────────────┐
│                     HEMLOCK PIPELINE                            │
│                                                                 │
│  Source → Lexer → Parser → Resolver → Optimizer → TypeChecker  │
│                                                      ↓          │
│                                              ┌──────────────┐   │
│                                              │  TRICYCLE    │   │
│                                              │              │   │
│                                              │ OwnershipCtx │   │
│                                              │ BorrowCheck  │   │
│                                              │ LifetimeInfer│   │
│                                              │ AsyncSafety  │   │
│                                              └──────┬───────┘   │
│                                                     ↓           │
│                                              ┌──────────────┐   │
│                                              │ Diagnostics  │───┼→ LSP
│                                              │ (Errors/     │   │
│                                              │  Warnings)   │   │
│                                              └──────────────┘   │
│                                                     ↓           │
│                                                 CodeGen         │
└─────────────────────────────────────────────────────────────────┘
```

### Directory Structure

```
hemlock/
├── src/
│   ├── frontend/           # Existing (lexer, parser, resolver)
│   ├── backends/
│   │   ├── compiler/
│   │   │   ├── type_check.c    # Existing type checker
│   │   │   └── ...
│   │   └── tricycle/       # NEW
│   │       ├── tricycle.h          # Public API
│   │       ├── tricycle.c          # Main entry point
│   │       ├── ownership.h/c       # Ownership tracking
│   │       ├── borrow_check.h/c    # Borrow validation
│   │       ├── lifetime.h/c        # Lifetime inference
│   │       ├── async_safety.h/c    # Async boundary checks
│   │       ├── memory_state.h/c    # Allocation state tracking
│   │       └── diagnostics.h/c     # Error reporting
│   └── lsp/
│       └── handlers.c      # Add tricycle integration
├── include/
│   └── tricycle.h          # Public header
└── tests/
    └── tricycle/           # Memory safety tests
        ├── ownership/
        ├── borrowing/
        ├── lifetimes/
        └── async/
```

### Core Data Structures

#### TricycleContext

```c
typedef struct TricycleContext {
    // Input
    Stmt **program;                 // AST from parser
    int stmt_count;
    TypeCheckContext *type_ctx;     // Reuse type information

    // Analysis state
    OwnershipEnv *ownership_env;    // Current ownership scope
    BorrowSet *active_borrows;      // Currently borrowed pointers
    LifetimeGraph *lifetimes;       // Lifetime relationships

    // Output
    TricycleDiagnostic *diagnostics;
    int diagnostic_count;
    int diagnostic_capacity;

    // Configuration
    int strict_mode;                // Reject all warnings as errors
    int infer_ownership;            // Auto-infer vs require annotations
} TricycleContext;
```

#### MemoryState

```c
typedef enum {
    MEM_UNALLOCATED,    // Not allocated (or freed)
    MEM_ALLOCATED,      // Valid, owned
    MEM_MOVED,          // Ownership transferred elsewhere
    MEM_BORROWED,       // Reference to owned memory
    MEM_UNKNOWN,        // External/unknown origin
} MemoryStateKind;

typedef struct MemoryState {
    MemoryStateKind kind;
    int allocation_line;        // Where allocated (for diagnostics)
    int free_line;              // Where freed (if applicable)
    char *owner_name;           // Variable that owns this
    int borrow_count;           // Number of active borrows
    LifetimeId lifetime;        // Lifetime identifier
} MemoryState;
```

#### OwnershipEnv

```c
typedef struct OwnershipBinding {
    char *name;                 // Variable name
    MemoryState state;          // Current state
    int is_parameter;           // Came from function parameter?
    OwnershipKind param_kind;   // OWN, BORROW, BORROW_MUT
} OwnershipBinding;

typedef struct OwnershipEnv {
    OwnershipBinding *bindings;
    int count;
    int capacity;
    struct OwnershipEnv *parent;    // Enclosing scope
    ScopeKind scope_kind;           // FUNCTION, BLOCK, LOOP, ASYNC
} OwnershipEnv;
```

#### Diagnostics

```c
typedef enum {
    TRIC_ERROR,         // Definite memory error
    TRIC_WARNING,       // Potential issue
    TRIC_NOTE,          // Additional context
} TricycleSeverity;

typedef enum {
    TRIC_DOUBLE_FREE,
    TRIC_USE_AFTER_FREE,
    TRIC_USE_AFTER_MOVE,
    TRIC_MEMORY_LEAK,
    TRIC_NULL_DEREF,
    TRIC_DANGLING_ASYNC,
    TRIC_BORROW_OUTLIVES_OWNER,
    TRIC_MOVE_OF_BORROWED,
    TRIC_FREE_OF_BORROWED,
    TRIC_UNKNOWN_LIFETIME,
} TricycleErrorKind;

typedef struct TricycleDiagnostic {
    TricycleSeverity severity;
    TricycleErrorKind kind;
    int line;
    int column;
    char *message;
    char *hint;                 // Suggested fix
    // Related locations (for "allocated here", "freed here" notes)
    int *related_lines;
    char **related_messages;
    int related_count;
} TricycleDiagnostic;
```

---

## Analysis Algorithms

### Phase 1: Ownership Analysis

Walk the AST and track ownership state for each pointer variable:

```
OWNERSHIP_ANALYZE(stmt):
    match stmt:
        LET(name, init):
            if init is CALL("alloc", _) or CALL("buffer", _):
                bind(name, ALLOCATED, owner=name)
            else if init is IDENT(other):
                if lookup(other).kind == ALLOCATED:
                    bind(name, ALLOCATED, owner=name)
                    update(other, MOVED)  // Transfer ownership
            else if init has ptr type:
                bind(name, UNKNOWN)

        EXPR_STMT(CALL("free", [arg])):
            if arg is IDENT(name):
                state = lookup(name)
                if state.kind == UNALLOCATED:
                    error(DOUBLE_FREE, name)
                else if state.kind == MOVED:
                    error(USE_AFTER_MOVE, name)
                else if state.kind == BORROWED:
                    error(FREE_OF_BORROWED, name)
                else:
                    update(name, UNALLOCATED)

        RETURN(expr):
            if expr is IDENT(name) with ptr type:
                // Ownership transfers to caller
                update(name, MOVED)

        FUNCTION_END:
            for binding in current_scope:
                if binding.state.kind == ALLOCATED:
                    warning(MEMORY_LEAK, binding.name)
```

### Phase 2: Use-After-Free Detection

Track all uses of pointer variables:

```
CHECK_USE(expr):
    match expr:
        IDENT(name) where type is ptr:
            state = lookup(name)
            if state.kind == UNALLOCATED:
                error(USE_AFTER_FREE, name)
            else if state.kind == MOVED:
                error(USE_AFTER_MOVE, name)

        CALL(fn, args):
            for arg in args:
                CHECK_USE(arg)
            // Check if function takes ownership
            if fn_takes_ownership(fn, arg_index):
                update(arg_name, MOVED)

        GET_PROPERTY(obj, prop):
            CHECK_USE(obj)

        // etc.
```

### Phase 3: Borrow Checking

Ensure borrows don't outlive owners or conflict:

```
CHECK_BORROW(expr, context):
    match expr:
        CALL(fn, args):
            for (param, arg) in zip(fn.params, args):
                if param.modifier == BORROW:
                    state = lookup(arg)
                    if state.kind != ALLOCATED:
                        error(BORROW_OF_INVALID, arg)
                    add_borrow(arg, context.scope)

        ASSIGN(name, value) where name is borrowed:
            error(MUTATE_WHILE_BORROWED, name)

        SCOPE_EXIT:
            for borrow in active_borrows_in_scope:
                remove_borrow(borrow)
```

### Phase 4: Async Safety

Special handling for spawn/join:

```
CHECK_ASYNC(stmt):
    match stmt:
        SPAWN(fn, args):
            for arg in args where arg has ptr type:
                state = lookup(arg)
                if state.kind != ALLOCATED:
                    error(SPAWN_WITH_INVALID_PTR, arg)
                // Mark as "async borrowed" - can't free until join
                add_async_borrow(arg, task_id)

        JOIN(task):
            // Release async borrows for this task
            release_async_borrows(task_id)

        FREE(ptr) where has_async_borrow(ptr):
            error(FREE_BEFORE_JOIN, ptr)
```

### Phase 5: Control Flow Sensitivity

Handle branches and loops:

```
CHECK_IF(cond, then_branch, else_branch):
    // Save state
    state_before = snapshot(ownership_env)

    // Analyze then branch
    analyze(then_branch)
    state_after_then = snapshot(ownership_env)

    // Restore and analyze else branch
    restore(state_before)
    analyze(else_branch)
    state_after_else = snapshot(ownership_env)

    // Merge: if different states, mark as UNKNOWN
    merge(state_after_then, state_after_else)

CHECK_LOOP(body):
    // Fixed-point iteration until states stabilize
    repeat:
        state_before = snapshot(ownership_env)
        analyze(body)
        state_after = snapshot(ownership_env)
    until state_before == state_after or iteration_limit
```

---

## Tracked Memory Operations

### Allocation Functions

| Function | Effect |
|----------|--------|
| `alloc(size)` | Creates ALLOCATED state with ownership |
| `buffer(size)` | Creates ALLOCATED state with ownership |
| `realloc(ptr, size)` | Frees old, allocates new (old becomes UNALLOCATED) |

### Deallocation Functions

| Function | Effect |
|----------|--------|
| `free(ptr)` | Sets state to UNALLOCATED |

### Pointer Operations (Require ALLOCATED state)

| Function | Requirement |
|----------|-------------|
| `ptr_read_i8/i16/i32/i64(ptr)` | ptr must be ALLOCATED |
| `ptr_write_i8/i16/i32/i64(ptr, val)` | ptr must be ALLOCATED |
| `ptr_offset(ptr, n)` | ptr must be ALLOCATED |
| `memcpy(dst, src, n)` | Both must be ALLOCATED |
| `memset(ptr, val, n)` | ptr must be ALLOCATED |

### Ownership Transfer

| Pattern | Effect |
|---------|--------|
| `let q = p` (ptr type) | p becomes MOVED, q becomes ALLOCATED |
| `return p` | p becomes MOVED (ownership to caller) |
| `fn(own p)` | p becomes MOVED (ownership to callee) |
| `array.push(p)` | p becomes MOVED (ownership to array) |
| `obj.field = p` | p becomes MOVED (ownership to object) |

---

## Error Messages

### Double Free

```
error[TRIC001]: double free detected
  --> src/example.hml:15:5
   |
10 |     let p = alloc(64);
   |             --------- first allocated here
...
12 |     free(p);
   |     ------- first freed here
...
15 |     free(p);
   |     ^^^^^^^ second free here
   |
   = help: memory was already freed at line 12
```

### Use After Free

```
error[TRIC002]: use of freed memory
  --> src/example.hml:18:5
   |
10 |     let p = alloc(64);
   |             --------- allocated here
...
15 |     free(p);
   |     ------- freed here
...
18 |     ptr_write_i32(p, 42);
   |                   ^ used after free
   |
   = help: consider removing the free at line 15, or don't use p after
```

### Memory Leak

```
warning[TRIC003]: memory leak detected
  --> src/example.hml:20:1
   |
10 |     let p = alloc(64);
   |             --------- allocated here
...
20 | }
   | ^ function returns without freeing p
   |
   = help: add `free(p);` before returning, or return p to transfer ownership
```

### Use After Move

```
error[TRIC004]: use of moved value
  --> src/example.hml:14:5
   |
10 |     let p = alloc(64);
   |         - allocated here
11 |     let q = p;
   |             - value moved here
...
14 |     free(p);
   |          ^ value used after move
   |
   = help: use q instead, which now owns the memory
```

### Async Dangling Pointer

```
error[TRIC005]: pointer may dangle across async boundary
  --> src/example.hml:12:5
   |
10 |     let p = alloc(64);
   |             --------- allocated here
11 |     let task = spawn(worker, p);
   |                      ------ pointer passed to spawned task
12 |     free(p);
   |     ^^^^^^^ freed while task may still use it
   |
   = help: call `join(task)` before `free(p)` to ensure task completes first
```

---

## CLI Interface

### Basic Usage

```bash
# Check a single file
tricycle check program.hml

# Check with strict mode (warnings are errors)
tricycle check --strict program.hml

# Check and show all diagnostics (not just first error)
tricycle check --all program.hml

# Output diagnostics as JSON (for IDE integration)
tricycle check --format=json program.hml

# Verbose mode (show analysis progress)
tricycle check -v program.hml
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | No errors (warnings may exist) |
| 1 | Memory safety errors found |
| 2 | Invalid arguments or file not found |

### Integration with hemlockc

```bash
# Compile with tricycle pre-check
hemlockc --tricycle program.hml -o program

# Same as:
tricycle check program.hml && hemlockc program.hml -o program
```

---

## LSP Integration

Tricycle diagnostics appear in real-time via the LSP:

```json
{
  "uri": "file:///path/to/program.hml",
  "diagnostics": [
    {
      "range": { "start": {"line": 14, "character": 5}, "end": {"line": 14, "character": 12} },
      "severity": 1,
      "code": "TRIC002",
      "source": "tricycle",
      "message": "use of freed memory",
      "relatedInformation": [
        {
          "location": { "uri": "file:///path/to/program.hml", "range": {"line": 10, "character": 12} },
          "message": "allocated here"
        },
        {
          "location": { "uri": "file:///path/to/program.hml", "range": {"line": 12, "character": 5} },
          "message": "freed here"
        }
      ]
    }
  ]
}
```

---

## Implementation Phases

### Phase 1: Foundation (This Spec)

**Goal:** Basic ownership tracking and double-free/use-after-free detection

**Scope:**
- [x] Spec document
- [ ] `TricycleContext` and core data structures
- [ ] `OwnershipEnv` for tracking allocation state
- [ ] Basic AST walker that tracks alloc/free
- [ ] Double-free detection
- [ ] Use-after-free detection
- [ ] CLI tool (`tricycle check`)
- [ ] Basic test suite

**Limitations:**
- No borrow annotations (everything is ownership transfer)
- No lifetime inference
- No async safety (just warns on spawn with pointers)
- Intra-procedural only (doesn't cross function boundaries)

### Phase 2: Borrow Checking

**Goal:** Add borrowing semantics for non-owning references

**Scope:**
- [ ] `borrow` parameter modifier
- [ ] `borrow mut` for mutable borrows
- [ ] Borrow validity tracking
- [ ] Conflict detection (multiple mutable borrows)
- [ ] Borrow-outlives-owner detection

### Phase 3: Lifetime Inference

**Goal:** Automatically infer lifetimes without annotations

**Scope:**
- [ ] Lifetime variables and constraints
- [ ] Constraint solving
- [ ] Function signature lifetime inference
- [ ] Lifetime annotation syntax (for complex cases)

### Phase 4: Async Safety

**Goal:** Prove memory safety across spawn/join boundaries

**Scope:**
- [ ] Task lifetime tracking
- [ ] Spawn argument validation
- [ ] Join-before-free enforcement
- [ ] Channel safety (ownership through channels)

### Phase 5: Inter-procedural Analysis

**Goal:** Track ownership across function calls

**Scope:**
- [ ] Function summary generation
- [ ] Call-site ownership validation
- [ ] Module-level analysis
- [ ] Incremental analysis for LSP performance

---

## Test Plan

### Unit Tests

```
tests/tricycle/
├── ownership/
│   ├── basic_alloc_free.hml          # Simple alloc/free
│   ├── double_free.hml               # Should error
│   ├── use_after_free.hml            # Should error
│   ├── ownership_transfer.hml        # let q = p
│   ├── return_ownership.hml          # return p
│   ├── leak_detection.hml            # Should warn
│   └── conditional_free.hml          # if (x) free(p)
├── borrowing/                        # Phase 2
├── lifetimes/                        # Phase 3
└── async/                            # Phase 4
```

### Expected Output Format

```
// tests/tricycle/ownership/double_free.hml
let p = alloc(64);
free(p);
free(p);  // ERROR: double free

// tests/tricycle/ownership/double_free.expected
error[TRIC001]: double free detected
```

### Integration Tests

- Run tricycle on all existing `tests/parity/` that use memory
- Ensure no false positives on valid code
- Benchmark analysis time on large files

---

## Open Questions

### Q1: Ownership Transfer Syntax

Should ownership transfer be explicit?

**Option A: Implicit (Rust-like)**
```hemlock
let q = p;      // p implicitly moved
```

**Option B: Explicit**
```hemlock
let q = move p; // Explicit move
let q = p;      // Error: ambiguous
```

**Recommendation:** Start with Option A (implicit), add optional `move` keyword later.

### Q2: Function Parameter Defaults

What's the default for pointer parameters?

**Option A: Borrow by default**
```hemlock
fn foo(p: ptr) { }  // Implicitly: borrow p
fn bar(own p: ptr) { } // Explicit ownership
```

**Option B: Own by default**
```hemlock
fn foo(p: ptr) { }  // Implicitly: own p (caller loses access)
fn bar(borrow p: ptr) { } // Explicit borrow
```

**Option C: Require annotation**
```hemlock
fn foo(p: ptr) { }  // Warning: specify own/borrow
```

**Recommendation:** Option A (borrow by default) - safer, matches most use cases.

### Q3: Escape Hatches

Should there be an `unsafe` block to bypass tricycle?

```hemlock
unsafe {
    // Tricycle doesn't analyze this block
    let p = get_ptr_from_somewhere();
    do_sketchy_things(p);
}
```

**Recommendation:** Yes, but save for Phase 2. Foundation should be strict.

### Q4: Null Handling

How to handle nullable pointers?

**Option A: Separate nullable type**
```hemlock
let p: ptr = alloc(64);      // Non-null
let q: ptr? = maybe_alloc(); // Nullable
if (q != null) { use(q); }   // OK in this branch
```

**Option B: Track null state**
```hemlock
let p = alloc(64);           // Known non-null
let q = get_ptr();           // Unknown
if (q != null) { use(q); }   // OK: guarded
use(q);                      // Warning: might be null
```

**Recommendation:** Option B (track null state) - more flexible, leverages existing `??` operator.

---

## References

- [Rust Ownership Model](https://doc.rust-lang.org/book/ch04-00-understanding-ownership.html)
- [Linear Types](https://en.wikipedia.org/wiki/Substructural_type_system#Linear_type_systems)
- [Cyclone Region-Based Memory](https://cyclone.thelanguage.org/wiki/Memory%20Management/)
- [Hemlock Type Checker](src/backends/compiler/type_check.c)

---

## Appendix A: Full Error Code Reference

| Code | Name | Description |
|------|------|-------------|
| TRIC001 | DOUBLE_FREE | Memory freed twice |
| TRIC002 | USE_AFTER_FREE | Use of freed memory |
| TRIC003 | MEMORY_LEAK | Allocated memory not freed |
| TRIC004 | USE_AFTER_MOVE | Use of moved value |
| TRIC005 | DANGLING_ASYNC | Pointer freed while task running |
| TRIC006 | BORROW_OUTLIVES | Borrow outlives owner |
| TRIC007 | MOVE_OF_BORROWED | Moving borrowed value |
| TRIC008 | FREE_OF_BORROWED | Freeing borrowed value |
| TRIC009 | NULL_DEREF | Possible null dereference |
| TRIC010 | UNKNOWN_LIFETIME | Cannot determine lifetime |

---

## Appendix B: Grammar Extensions

```ebnf
(* Parameter modifiers - Phase 2 *)
param_modifier ::= "own" | "borrow" | "borrow" "mut"

(* Lifetime annotations - Phase 3 *)
lifetime ::= "'" IDENT
lifetime_bounds ::= "<" lifetime ("," lifetime)* ">"
ptr_type ::= "ptr" ("<" lifetime ">")?

(* Unsafe block - Phase 2 *)
unsafe_block ::= "unsafe" block
```

---

*End of Spec*
