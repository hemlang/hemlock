# Compiler Helper Annotations: Analysis and Proposal

**Author:** Claude
**Date:** 2026-01-08
**Status:** Partially Implemented (Phase 1-2 completed in v1.9.0; Phase 3-5 remain proposals)
**Related:** Issue #TBD

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Current State Analysis](#current-state-analysis)
3. [Proposed Annotations](#proposed-annotations)
4. [Implementation Plan](#implementation-plan)
5. [Examples](#examples)
6. [Testing Strategy](#testing-strategy)
7. [Future Considerations](#future-considerations)

---

## Executive Summary

Hemlock's annotation system provides a robust foundation for adding compiler hints and directives. This proposal extends the current annotation infrastructure with **15 new compiler helper annotations** organized into five categories:

- **Optimization Hints** (7 annotations)
- **Memory Management** (3 annotations)
- **Code Generation Control** (2 annotations)
- **Error Checking** (2 annotations)
- **FFI/Interop** (1 annotation)

These annotations will enable developers to provide explicit guidance to the compiler (`hemlockc`) while maintaining backward compatibility with the interpreter.

---

## Current State Analysis

### 1. Annotation Infrastructure

The annotation system is fully implemented with three main components:

**Parser** (`src/frontend/parser/statements.c`):
- Parses `@name` and `@name(args...)` syntax
- Supports positional and named arguments
- Attaches annotations to declarations (let, const, define, enum)

**Validator** (`src/frontend/annotations.c`):
- Validates annotation targets (function, type, variable, etc.)
- Checks argument counts and types
- Warns on unknown or duplicate annotations

**Resolver** (`src/frontend/resolver.c`):
- Stores annotations alongside variable definitions
- Enables annotation lookup during semantic analysis
- Powers `@deprecated` warnings on variable use

### 2. Currently Implemented Annotations

```c
// Safety annotations (for Tricycle memory checker)
@safe       // Function is memory-safe
@unsafe     // Function contains unsafe operations
@trusted    // Function is trusted despite unsafe operations

// Compiler optimization hints (IMPLEMENTED in v1.9.0)
@inline     // Suggest inlining this function
@noinline   // Prevent inlining this function
@cold       // Function is rarely executed
@hot        // Function is frequently executed
@pure       // Function has no side effects

// Other annotations
@deprecated      // Mark as deprecated with optional message
@test, @skip     // Testing framework annotations
@author, @since  // Documentation annotations
```

### 3. Current Limitations

**Update (v1.9.0):** The core function-level annotations (`@inline`, `@noinline`, `@hot`, `@cold`, `@pure`, `@const`, `@flatten`, `@optimize`, `@warn_unused`, `@section`) are now fully implemented in the compiler backend. The remaining proposals (loop annotations, memory annotations) in Phases 3-5 below are still unimplemented.

---

## Proposed Annotations

### Category 1: Optimization Hints

#### `@unroll(count?: number)`
**Target:** Loops (for, while)
**Arguments:** Optional unroll factor (default: compiler decides)

Suggests loop unrolling for performance-critical tight loops.

```hemlock
@unroll(4)
for (let i = 0; i < 1024; i++) {
    buffer[i] = buffer[i] * 2;
}
```

**Compiler codegen:**
```c
// Generate: #pragma GCC unroll 4
// Or: #pragma clang loop unroll_count(4)
```

---

#### `@simd` / `@nosimd`
**Target:** Functions, loops
**Arguments:** None

Enable or disable SIMD vectorization.

```hemlock
@simd
fn vector_add(a: buffer, b: buffer, n: i32) {
    for (let i = 0; i < n; i++) {
        ptr_write_f64(a, i, ptr_read_f64(a, i) + ptr_read_f64(b, i));
    }
}
```

**Compiler codegen:**
```c
// Function level: __attribute__((target("avx2")))
// Loop level: #pragma omp simd
```

---

#### `@likely` / `@unlikely`
**Target:** If statements, conditionals
**Arguments:** None

Branch prediction hints for hot paths.

```hemlock
@likely
if (cache.has(key)) {
    return cache.get(key);
}

@unlikely
if (error) {
    handle_error(error);
}
```

**Compiler codegen:**
```c
if (__builtin_expect(!!(condition), 1))  // @likely
if (__builtin_expect(!!(condition), 0))  // @unlikely
```

---

#### `@const`
**Target:** Functions
**Arguments:** None

Function always returns the same result for the same inputs (stronger than `@pure`).

```hemlock
@const
fn square(x: i32): i32 => x * x;

@const
fn add(a: i32, b: i32): i32 => a + b;
```

**Compiler codegen:**
```c
__attribute__((const))
```

**Difference from `@pure`:**
- `@pure`: Can read global memory, but doesn't modify it
- `@const`: Cannot even read global memory, only uses parameters

---

#### `@tail_call`
**Target:** Function calls
**Arguments:** None

Requests tail call optimization (TCO).

```hemlock
fn factorial_helper(n: i32, acc: i32): i32 {
    if (n <= 1) { return acc; }
    @tail_call
    return factorial_helper(n - 1, n * acc);
}
```

**Compiler codegen:**
```c
// Generate tail-recursive loop instead of recursive call
// Or use: __attribute__((musttail)) on Clang
```

---

#### `@flatten`
**Target:** Functions
**Arguments:** None

Inline all calls within this function.

```hemlock
@flatten
fn compute_hash(data: buffer, len: i32): u64 {
    let hash = init_hash();
    hash = process_block(hash, data, len);
    return finalize_hash(hash);
}
```

**Compiler codegen:**
```c
__attribute__((flatten))
```

---

#### `@optimize(level: string)`
**Target:** Functions
**Arguments:** Optimization level ("0", "1", "2", "3", "s", "fast")

Override global optimization level for specific function.

```hemlock
@optimize("3")
fn matrix_multiply(a: buffer, b: buffer, n: i32) {
    // Performance-critical inner loop
}

@optimize("s")
fn rarely_called_error_handler() {
    // Optimize for size, not speed
}
```

**Compiler codegen:**
```c
__attribute__((optimize("O3")))
__attribute__((optimize("Os")))
```

---

### Category 2: Memory Management

#### `@stack`
**Target:** Variables (arrays, buffers)
**Arguments:** None

Allocate on stack instead of heap (where possible).

```hemlock
@stack
let temp_buffer = buffer(1024);  // Stack allocation

fn process_small_data() {
    @stack
    let workspace: array<i32> = [0, 0, 0, 0];  // Stack array
}
```

**Compiler codegen:**
```c
// Instead of: HmlValue temp = hml_buffer_new(1024);
// Generate: uint8_t temp_buf[1024]; HmlValue temp = hml_buffer_from_stack(temp_buf, 1024);
```

---

#### `@noalias`
**Target:** Function parameters (pointers/buffers)
**Arguments:** None

Promise that pointer does not alias with other pointers.

```hemlock
fn memcpy_fast(@noalias dest: ptr, @noalias src: ptr, n: i32) {
    // Compiler knows dest and src don't overlap
    memcpy(dest, src, n);
}
```

**Compiler codegen:**
```c
void fn(HmlValue dest __attribute__((noalias)),
        HmlValue src __attribute__((noalias)), ...)
```

---

#### `@aligned(bytes: number)`
**Target:** Variables (pointers, buffers), function returns
**Arguments:** Alignment in bytes (must be power of 2)

Specify memory alignment requirements.

```hemlock
@aligned(64)  // Cache line aligned
let cache_line_buffer = buffer(64);

fn get_aligned_buffer(): @aligned(32) ptr {
    return alloc_aligned(1024, 32);
}
```

**Compiler codegen:**
```c
__attribute__((aligned(64)))
__attribute__((returns_aligned(32)))
```

---

### Category 3: Code Generation Control

#### `@extern(name?: string, abi?: string)`
**Target:** Functions
**Arguments:**
- `name`: External symbol name (default: function name)
- `abi`: Calling convention ("C", "stdcall", "fastcall")

Mark function for external linkage or FFI export.

```hemlock
// Export with C linkage
@extern
fn hemlock_init() {
    print("Library initialized");
}

// Custom symbol name
@extern("_ZN3foo3barEv")
fn mangled_cpp_function() { }

// Windows calling convention
@extern(abi: "stdcall")
fn windows_callback(msg: i32) { }
```

**Compiler codegen:**
```c
extern "C" void hemlock_init();
__attribute__((stdcall))
```

---

#### `@section(name: string)`
**Target:** Functions, global variables
**Arguments:** Section name

Place symbol in specific ELF/Mach-O section.

```hemlock
@section(".text.hot")
@hot
fn critical_path() { }

@section(".rodata.secure")
const SECRET_KEY = "...";
```

**Compiler codegen:**
```c
__attribute__((section(".text.hot")))
```

---

### Category 4: Error Checking

#### `@bounds_check` / `@no_bounds_check`
**Target:** Array/buffer operations, loops
**Arguments:** None

Override global bounds checking policy.

```hemlock
// Force bounds checks even with --no-bounds-check
@bounds_check
fn safe_array_access(arr: array, idx: i32) {
    return arr[idx];
}

// Disable for performance (use with caution!)
@no_bounds_check
fn trusted_hot_loop(data: buffer, n: i32) {
    for (let i = 0; i < n; i++) {
        // Compiler skips bounds checks
        data[i] = 0;
    }
}
```

**Compiler codegen:**
```c
// Insert/omit: if (idx < 0 || idx >= len) abort();
```

---

#### `@warn_unused`
**Target:** Function return values
**Arguments:** None

Warn if caller ignores return value.

```hemlock
@warn_unused
fn allocate_memory(size: i32): ptr {
    return alloc(size);
}

let p = allocate_memory(1024);  // OK
allocate_memory(1024);          // Warning: unused return value
```

**Compiler codegen:**
```c
__attribute__((warn_unused_result))
```

---

### Category 5: FFI/Interop

#### `@packed`
**Target:** Type definitions (define)
**Arguments:** None

Create packed struct with no padding (for C interop).

```hemlock
@packed
define NetworkHeader {
    magic: u32,
    version: u8,
    flags: u8,
    length: u16
}  // Total: 8 bytes, no padding
```

**Compiler codegen:**
```c
struct NetworkHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t flags;
    uint16_t length;
} __attribute__((packed));
```

---

## Implementation Plan

### Phase 1: Core Infrastructure (Week 1)

**Goal:** Enable compiler to query and use annotations

1. **Add annotation query functions** (`src/frontend/annotations.c`):
   ```c
   // Optimization hints
   int annotation_has_unroll(Annotation **annotations, int count, int *factor);
   int annotation_has_simd(Annotation **annotations, int count);
   int annotation_has_nosimd(Annotation **annotations, int count);
   int annotation_has_likely(Annotation **annotations, int count);
   int annotation_has_unlikely(Annotation **annotations, int count);
   // ... etc for all new annotations
   ```

2. **Update annotation specs** (`src/frontend/annotations.c`):
   ```c
   static const AnnotationSpec known_annotations[] = {
       // ... existing annotations ...
       {"unroll",      ANNOT_TARGET_FN, 1, 0, 1},  // 0-1 args
       {"simd",        ANNOT_TARGET_FN, 0, 0, 0},
       {"nosimd",      ANNOT_TARGET_FN, 0, 0, 0},
       {"likely",      ANNOT_TARGET_FN, 0, 0, 0},
       {"unlikely",    ANNOT_TARGET_FN, 0, 0, 0},
       {"const",       ANNOT_TARGET_FN, 0, 0, 0},
       {"tail_call",   ANNOT_TARGET_FN, 0, 0, 0},
       {"flatten",     ANNOT_TARGET_FN, 0, 0, 0},
       {"optimize",    ANNOT_TARGET_FN, 1, 1, 1},  // 1 arg required
       {"stack",       ANNOT_TARGET_LET, 0, 0, 0},
       {"noalias",     ANNOT_TARGET_FN, 0, 0, 0},  // For parameters
       {"aligned",     ANNOT_TARGET_LET | ANNOT_TARGET_FN, 1, 1, 1},
       {"extern",      ANNOT_TARGET_FN, 1, 0, 2},  // 0-2 named args
       {"section",     ANNOT_TARGET_FN | ANNOT_TARGET_LET, 1, 1, 1},
       {"bounds_check", ANNOT_TARGET_FN, 0, 0, 0},
       {"no_bounds_check", ANNOT_TARGET_FN, 0, 0, 0},
       {"warn_unused", ANNOT_TARGET_FN, 0, 0, 0},
       {"packed",      ANNOT_TARGET_DEFINE, 0, 0, 0},
       {NULL, 0, 0, 0, 0}
   };
   ```

3. **Add compiler context annotation support**:
   ```c
   // src/backends/compiler/codegen.h
   typedef struct {
       int current_optimization_level;  // Track active optimization
       int bounds_checking_enabled;     // Global bounds check setting
       // ... existing fields ...
   } CodegenContext;
   ```

### Phase 2: Function Annotations (Week 2)

**Goal:** Implement function-level optimization hints

1. **Update function codegen** (`src/backends/compiler/codegen_stmt.c`):
   ```c
   static void codegen_function_attributes(CodegenContext *ctx, Stmt *stmt) {
       Annotation **annotations = stmt->as.let.annotations;
       int count = stmt->as.let.annotation_count;

       // Generate GCC/Clang attributes
       if (annotation_has(annotations, count, "inline")) {
           codegen_write(ctx, "__attribute__((always_inline)) ");
       }
       if (annotation_has(annotations, count, "noinline")) {
           codegen_write(ctx, "__attribute__((noinline)) ");
       }
       if (annotation_has(annotations, count, "cold")) {
           codegen_write(ctx, "__attribute__((cold)) ");
       }
       if (annotation_has(annotations, count, "hot")) {
           codegen_write(ctx, "__attribute__((hot)) ");
       }
       if (annotation_has(annotations, count, "pure")) {
           codegen_write(ctx, "__attribute__((pure)) ");
       }
       if (annotation_has(annotations, count, "const")) {
           codegen_write(ctx, "__attribute__((const)) ");
       }
       if (annotation_has(annotations, count, "flatten")) {
           codegen_write(ctx, "__attribute__((flatten)) ");
       }
       if (annotation_has(annotations, count, "warn_unused")) {
           codegen_write(ctx, "__attribute__((warn_unused_result)) ");
       }

       // Handle @optimize(level)
       int opt_level;
       const char *opt_str = annotation_get_optimize_level(annotations, count);
       if (opt_str) {
           codegen_write(ctx, "__attribute__((optimize(\"O%s\"))) ", opt_str);
       }

       // Handle @extern
       if (annotation_has(annotations, count, "extern")) {
           codegen_write(ctx, "extern ");
       }
   }
   ```

2. **Implement in codegen:** Call `codegen_function_attributes()` before function declaration

### Phase 3: Loop Annotations (Week 3)

**Goal:** Support loop-level hints (@unroll, @simd, @likely/@unlikely)

This requires **statement-level annotations**, which is a new feature:

1. **Extend parser** to allow annotations on statements:
   ```hemlock
   @unroll(4)
   for (let i = 0; i < n; i++) { }

   @likely
   if (condition) { }
   ```

2. **Update AST** to store annotations on all statement types:
   ```c
   struct Stmt {
       StmtType type;
       Annotation **annotations;  // Add to base Stmt struct
       int annotation_count;
       // ... existing fields ...
   };
   ```

3. **Generate pragmas** in codegen:
   ```c
   // For @unroll(4) on for loop:
   codegen_writeln(ctx, "#pragma GCC unroll 4");
   codegen_writeln(ctx, "for (...) {");

   // For @simd on loop:
   codegen_writeln(ctx, "#pragma omp simd");
   ```

### Phase 4: Memory Annotations (Week 4)

**Goal:** Implement @stack, @noalias, @aligned

1. **Stack allocation** (`@stack`):
   ```c
   // Instead of heap allocation:
   if (annotation_has(stmt->as.let.annotations, stmt->as.let.annotation_count, "stack")) {
       // Generate: uint8_t buf[size]; HmlValue var = hml_buffer_from_stack(buf, size);
   } else {
       // Generate: HmlValue var = hml_buffer_new(size);
   }
   ```

2. **Pointer aliasing** (`@noalias`):
   - Add to parameter declarations as `__attribute__((noalias))`

3. **Alignment** (`@aligned(N)`):
   - Apply to variable declarations and return types

### Phase 5: Testing & Documentation (Week 5)

**Goal:** Comprehensive test coverage and docs

1. **Parity tests** (`tests/parity/annotations/`):
   - All annotations should work identically in interpreter (ignore hints) and compiler (apply optimizations)
   - Test that programs produce same output with/without annotations

2. **Compiler-specific tests** (`tests/compiler/annotations/`):
   - Verify generated C code contains correct attributes/pragmas
   - Test with different optimization levels
   - Benchmark performance improvements

3. **Documentation**:
   - Add to `CLAUDE.md` under "Annotations" section
   - Create `docs/annotations.md` with full reference
   - Update `docs/compiler.md` with optimization guide

---

## Examples

### Example 1: High-Performance Vector Math

```hemlock
// Vector addition with SIMD and unrolling
@simd
@flatten
fn vector_add(a: buffer, b: buffer, result: buffer, n: i32) {
    @unroll(8)
    for (let i = 0; i < n; i++) {
        let av = ptr_read_f64(a, i * 8);
        let bv = ptr_read_f64(b, i * 8);
        ptr_write_f64(result, i * 8, av + bv);
    }
}
```

**Generated C (pseudocode):**
```c
__attribute__((target("avx2")))
__attribute__((flatten))
void vector_add(...) {
    #pragma GCC unroll 8
    #pragma omp simd
    for (int i = 0; i < n; i++) {
        // ... vectorized loop body ...
    }
}
```

---

### Example 2: Cache-Optimized Data Structure

```hemlock
@packed
define CacheLineNode {
    next: ptr,
    data: i64,
    timestamp: u64,
    flags: u32,
    padding: u32  // Explicit padding to 64 bytes
}

@hot
@inline
fn cache_lookup(@aligned(64) cache: ptr, key: u64): ptr {
    @likely
    if (cache == null) {
        return null;
    }

    // Hot path optimization
    let node = ptr_read_ptr(cache);
    @unroll(4)
    while (node != null) {
        let node_key = ptr_read_u64(node, 8);
        if (node_key == key) {
            return node;
        }
        node = ptr_read_ptr(node);
    }

    return null;
}
```

---

### Example 3: Recursive Tail Call Optimization

```hemlock
@tail_call
fn sum_range(start: i32, end: i32, acc: i32): i32 {
    if (start > end) {
        return acc;
    }
    @tail_call
    return sum_range(start + 1, end, acc + start);
}

// Compiler transforms to iterative loop:
// while (start <= end) { acc += start; start++; }
```

---

### Example 4: FFI Export with Custom ABI

```hemlock
import "libmath.so";

extern fn sqrt(x: f64): f64;

// Export function for embedding Hemlock in C application
@extern(name: "hemlock_compute_distance")
@warn_unused
fn compute_distance(x1: f64, y1: f64, x2: f64, y2: f64): f64 {
    let dx = x2 - x1;
    let dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}
```

**Generated C:**
```c
extern "C" __attribute__((warn_unused_result))
HmlValue hemlock_compute_distance(HmlValue x1, HmlValue y1, HmlValue x2, HmlValue y2);
```

---

### Example 5: Performance-Critical Path with Mixed Hints

```hemlock
@optimize("3")
@hot
fn process_frame(@noalias frame_data: buffer, width: i32, height: i32) {
    @stack
    let temp_row = buffer(width * 4);

    @no_bounds_check
    for (let y = 0; y < height; y++) {
        // Compiler knows this is hot, optimizes aggressively
        @unroll(4)
        for (let x = 0; x < width; x++) {
            let offset = (y * width + x) * 4;
            // Process pixel at offset...
        }
    }
}

@optimize("s")
@cold
fn handle_error(code: i32, msg: string) {
    // Rare error path - optimize for size, not speed
    eprint("Error ", code, ": ", msg);
    signal(SIGABRT, fn(sig) { panic("Fatal error"); });
}
```

---

## Testing Strategy

### 1. Validation Tests (`tests/annotations/validation_*.hml`)

Test that new annotations are validated correctly:

- `validation_unroll.hml` - Test @unroll argument validation
- `validation_optimize.hml` - Test @optimize with invalid levels
- `validation_aligned.hml` - Test @aligned with non-power-of-2
- `validation_mixed.hml` - Test conflicting annotations (@inline + @noinline)

### 2. Parity Tests (`tests/parity/annotations/`)

Ensure annotations don't change program behavior:

```hemlock
// tests/parity/annotations/optimization_hints.hml
@inline
fn add(a: i32, b: i32): i32 => a + b;

@noinline
fn sub(a: i32, b: i32): i32 => a - b;

print(add(10, 20));  // Output: 30
print(sub(100, 50)); // Output: 50
```

Expected: Identical output from interpreter and compiler.

### 3. Compiler-Specific Tests (`tests/compiler/annotations/`)

Verify generated C code:

```bash
# Test inline attribute generation
hemlockc --emit-c tests/compiler/annotations/inline.hml -o inline.c
grep '__attribute__((always_inline))' inline.c

# Test optimization level override
hemlockc --emit-c tests/compiler/annotations/optimize.hml -o opt.c
grep '__attribute__((optimize("O3")))' opt.c
```

### 4. Performance Benchmarks (`benchmarks/annotations/`)

Measure actual performance improvements:

```hemlock
// benchmarks/annotations/vector_math.hml
import { time_ms } from "@stdlib/time";

@simd
fn vector_add_optimized(a: buffer, b: buffer, n: i32): buffer {
    let result = buffer(n * 8);
    @unroll(8)
    for (let i = 0; i < n; i++) {
        ptr_write_f64(result, i * 8,
            ptr_read_f64(a, i * 8) + ptr_read_f64(b, i * 8));
    }
    return result;
}

fn vector_add_baseline(a: buffer, b: buffer, n: i32): buffer {
    let result = buffer(n * 8);
    for (let i = 0; i < n; i++) {
        ptr_write_f64(result, i * 8,
            ptr_read_f64(a, i * 8) + ptr_read_f64(b, i * 8));
    }
    return result;
}

// Benchmark both versions
let n = 1000000;
let a = buffer(n * 8);
let b = buffer(n * 8);

let start = time_ms();
for (let i = 0; i < 100; i++) {
    vector_add_baseline(a, b, n);
}
let baseline_time = time_ms() - start;

start = time_ms();
for (let i = 0; i < 100; i++) {
    vector_add_optimized(a, b, n);
}
let optimized_time = time_ms() - start;

print("Baseline: ", baseline_time, "ms");
print("Optimized: ", optimized_time, "ms");
print("Speedup: ", baseline_time / optimized_time, "x");
```

Expected results:
- 2-4x speedup with @simd on AVX2-capable systems
- 1.5-2x speedup with @unroll alone

---

## Future Considerations

### 1. Annotation Inheritance

Should annotations on types apply to all instances?

```hemlock
@aligned(64)
define CacheLine { data: array<u8> }

let cache: CacheLine = { data: [0; 64] };
// Should 'cache' be automatically aligned to 64 bytes?
```

**Recommendation:** Yes for @aligned, @packed. No for @deprecated, @inline.

### 2. Annotation Composition

Allow creating custom annotations from others?

```hemlock
// Define a "hot path" annotation bundle
@define_annotation("hotpath", @hot, @inline, @optimize("3"))

@hotpath
fn critical_function() { }
```

**Recommendation:** Defer to v1.8+. Keep v1.7 focused on core annotations.

### 3. Compiler Flags Integration

Should annotations override compiler flags?

```bash
hemlockc --no-inline program.hml  # Global: no inlining
```

```hemlock
@inline
fn force_inline() { }  // Should this override --no-inline?
```

**Recommendation:** Annotations win. Developer's explicit intent should be respected.

### 4. Static Analysis Integration

Annotations could power static analysis tools:

```hemlock
@noalias
fn memcpy_wrapper(dest: ptr, src: ptr, n: i32) {
    memcpy(dest, src, n);
}

// Static analyzer could verify that calls to memcpy_wrapper
// never pass overlapping pointers
memcpy_wrapper(buf, buf + 10, 100);  // Error: may alias!
```

**Recommendation:** Consider for Tricycle integration in future.

### 5. Runtime Annotation Access

Should annotations be queryable at runtime?

```hemlock
fn get_annotations(fn_name: string): array<string> {
    // Returns ["inline", "hot", "pure"]
}
```

**Recommendation:** Not for v1.7. Increases binary size and complexity.

---

## Conclusion

This proposal adds **15 new compiler helper annotations** to Hemlock, enabling developers to provide explicit optimization hints while maintaining the language's "explicit over implicit" philosophy. The implementation is straightforward given the existing annotation infrastructure, and parity between interpreter and compiler can be maintained (interpreter ignores optimization hints).

**Key Benefits:**

1. **Performance:** 2-10x speedups for critical paths with SIMD, unrolling, inlining
2. **Control:** Developers can override default compiler heuristics
3. **Interop:** Better FFI support with @extern, @packed, @aligned
4. **Safety:** Explicit @bounds_check/@no_bounds_check makes safety trade-offs visible
5. **Explicit:** Fits Hemlock's philosophy - no magic, just clear directives

**Implementation Effort:**

- Phase 1-2: ~2 weeks (core infrastructure + function annotations)
- Phase 3: ~1 week (loop annotations - requires statement-level annotations)
- Phase 4: ~1 week (memory annotations)
- Phase 5: ~1 week (testing & docs)

**Total: ~5 weeks for full implementation**

---

## Appendix: Full Annotation Reference Table

| Annotation | Target | Args | Description | C Attribute |
|------------|--------|------|-------------|-------------|
| `@inline` | fn | 0 | Force inlining | `always_inline` |
| `@noinline` | fn | 0 | Prevent inlining | `noinline` |
| `@cold` | fn | 0 | Rarely executed | `cold` |
| `@hot` | fn | 0 | Frequently executed | `hot` |
| `@pure` | fn | 0 | No side effects, can read globals | `pure` |
| `@const` | fn | 0 | No side effects, no global reads | `const` |
| `@flatten` | fn | 0 | Inline all calls within function | `flatten` |
| `@tail_call` | fn | 0 | Request tail call optimization | Custom |
| `@optimize(level)` | fn | 1 | Override optimization level | `optimize("OX")` |
| `@unroll(factor?)` | loop | 0-1 | Loop unrolling hint | `#pragma unroll` |
| `@simd` | fn, loop | 0 | Enable SIMD vectorization | `#pragma omp simd` |
| `@nosimd` | fn, loop | 0 | Disable SIMD | Custom |
| `@likely` | if | 0 | Branch likely taken | `__builtin_expect` |
| `@unlikely` | if | 0 | Branch unlikely taken | `__builtin_expect` |
| `@stack` | let | 0 | Stack allocation | Custom |
| `@noalias` | param | 0 | No pointer aliasing | `noalias` |
| `@aligned(N)` | let, fn | 1 | Memory alignment | `aligned(N)` |
| `@extern(name?, abi?)` | fn | 0-2 | External linkage | `extern "C"` |
| `@section(name)` | fn, let | 1 | Place in specific section | `section("X")` |
| `@bounds_check` | fn | 0 | Force bounds checking | Custom |
| `@no_bounds_check` | fn | 0 | Disable bounds checking | Custom |
| `@warn_unused` | fn | 0 | Warn on unused return | `warn_unused_result` |
| `@packed` | define | 0 | No struct padding | `packed` |

**Existing annotations (not covered in this proposal):**
- `@safe`, `@unsafe`, `@trusted` (for Tricycle)
- `@deprecated` (already implemented)
- `@test`, `@skip`, `@timeout` (testing framework)
- `@author`, `@since`, `@see` (documentation)
