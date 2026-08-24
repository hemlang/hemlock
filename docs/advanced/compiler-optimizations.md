# Compiler Optimizations

The Hemlock compiler (`hemlockc`) applies several optimization passes when generating C code. These optimizations are automatic and require no user intervention, but understanding them helps explain performance characteristics.

---

## Overview

```
Source (.hml)
    ↓
  Parse → AST
    ↓
  Type Check (optional)
    ↓
  AST Optimization Pass
    ↓
  C Code Generation (with inlining + unboxing)
    ↓
  GCC/Clang Compilation
```

---

## Expression-Level Unboxing

Hemlock's runtime represents all values as tagged `HmlValue` structs. In the interpreter, every arithmetic operation boxes and unboxes values through runtime dispatch. The compiler eliminates this overhead for expressions with known primitive types.

**Before (naive codegen):**
```c
// x + 1 where x is i32
hml_i32_add(hml_val_i32(x), hml_val_i32(1))  // 2 boxing calls + runtime dispatch
```

**After (with expression unboxing):**
```c
// x + 1 where x is i32
hml_val_i32((x + 1))  // Pure C arithmetic, single box at the end
```

### What Gets Unboxed

- Binary arithmetic: `+`, `-`, `*`, `%`
- Bitwise operations: `&`, `|`, `^`, `<<`, `>>`
- Comparisons: `<`, `>`, `<=`, `>=`, `==`, `!=`
- Unary operations: `-`, `~`, `!`
- Type-annotated variables and loop counters

### What Falls Back to HmlValue

- Function calls (return type may be dynamic)
- Array/object access (element type unknown at compile time)
- Variables without type annotations and no inferred type

### Tip

Adding type annotations to hot-path variables helps the compiler apply unboxing:

```hemlock
// The compiler can unbox this entire expression
fn dot(a: i32, b: i32, c: i32, d: i32): i32 {
    return a * c + b * d;
}
```

To see exactly which variables the compiler unboxes and why the rest stay
boxed, run `hemlockc --coverage file.hml` — see
[Specialization Coverage](specialization-coverage.md).

---

## Multi-Level Function Inlining

The compiler inlines small functions at call sites, replacing function call overhead with direct code. Hemlock supports multi-level inlining up to depth 3, meaning nested helper calls are also inlined.

### How It Works

```hemlock
fn rotr(x: u32, n: i32): u32 => (x >> n) | (x << (32 - n));

fn ep0(x: u32): u32 => rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);

fn sha256_round(a: u32, ...): u32 {
    let s0 = ep0(a);  // Both ep0 AND rotr get inlined here
    // ...
}
```

At depth 1, `ep0()` is inlined into `sha256_round()`. At depth 2, the `rotr()` calls inside `ep0()` are also inlined. The result is a single block of native arithmetic with no function call overhead.

### Inlining Criteria

Functions are inlined when:
- The function body is small (single expression or a few statements)
- The function is not recursive
- The current inline depth is less than 3

### Controlling Inlining with Annotations

```hemlock
@inline
fn always_inline(x: i32): i32 => x * 2;

@noinline
fn never_inline(x: i32): i32 {
    // Complex function that should not be duplicated
    return x;
}
```

---

## While-Loop Accumulator Unboxing

For top-level while loops, the compiler detects counter and accumulator variables and shadows them with native C locals, eliminating boxing/unboxing overhead on every iteration.

### What Gets Optimized

```hemlock
let sum = 0;
let i = 0;
while (i < 1000000) {
    sum += i;
    i++;
}
print(sum);
```

The compiler detects that `sum` and `i` are integer accumulators used only within the loop, and generates native `int32_t` locals instead of `HmlValue` operations. This eliminates retain/release overhead and type dispatch on every iteration.

### Performance Impact

Benchmark improvements from these optimizations (measured on typical workloads):

| Benchmark | Before | After | Improvement |
|-----------|--------|-------|-------------|
| primes_sieve | 10ms | 6ms | -40% |
| binary_tree | 11ms | 8ms | -27% |
| json_serialize | 8ms | 5ms | -37% |
| json_deserialize | 10ms | 7ms | -30% |
| fibonacci | 29ms | 24ms | -17% |
| array_sum | 41ms | 36ms | -12% |

---

## Helper Annotations

### Function annotations

The compiler supports 10 function-level optimization annotations that map to GCC/Clang attributes:

| Annotation | Effect |
|------------|--------|
| `@inline` | Encourage function inlining |
| `@noinline` | Prevent function inlining |
| `@hot` | Mark as frequently executed (branch prediction) |
| `@cold` | Mark as rarely executed |
| `@pure` | Function has no side effects (reads external state) |
| `@const` | Function depends only on arguments (no external state) |
| `@flatten` | Inline all calls within the function |
| `@optimize(level)` | Per-function optimization level ("0"-"3", "s", "fast") |
| `@warn_unused` | Warn if return value is ignored |
| `@section(name)` | Place function in a custom linker section (ELF/COFF; mapped into `__TEXT` on macOS) |

### Example

```hemlock
@hot @inline
fn fast_hash(key: string): u32 {
    // Hot-path hashing function
    let h: u32 = 5381;
    for (ch in key.chars()) {
        h = ((h << 5) + h) + ch;
    }
    return h;
}

@cold
fn handle_error(msg: string) {
    eprint("Error: " + msg);
    panic(msg);
}
```

### Loop and branch annotations

Annotations may also be placed directly on loop statements (`for`, `while`,
`loop`, `for`-in) and on `if` statements. These are hints for the C optimizer:
the compiler emits the matching `#pragma` / `__builtin_expect` ahead of the
generated loop or branch, while the interpreter ignores them entirely. Because
they never change control flow, both backends always produce identical output.

| Annotation | Target | Generated C | Effect |
|------------|--------|-------------|--------|
| `@unroll(n)` | loops | `#pragma GCC unroll n` | Unroll the loop by a factor of `n` (1–256) |
| `@nounroll` | loops | `#pragma GCC unroll 1` | Disable unrolling of the loop |
| `@simd` | loops | `#pragma GCC ivdep` (Clang: `#pragma clang loop vectorize(enable)`) | Assert no loop-carried dependencies so the compiler may vectorize |
| `@likely` | `if` | `__builtin_expect(cond, 1)` | Hint the condition is usually true |
| `@unlikely` | `if` | `__builtin_expect(cond, 0)` | Hint the condition is usually false |

```hemlock
// Unroll a tight numeric kernel four ways.
@unroll(4)
for (let i = 0; i < n; i++) {
    out[i] = a[i] * b[i];
}

// Tell GCC this reduction has no aliasing between iterations.
@simd
for (let i = 0; i < n; i++) {
    sum = sum + data[i];
}

// Steer branch prediction on a hot path.
@likely
if (cache.has(key)) {
    return cache.get(key);
}

@unlikely
if (err != null) {
    handle_error(err);
}
```

Annotations are validated against their target: `@unroll` / `@nounroll` /
`@simd` are only accepted on loops, and `@likely` / `@unlikely` only on `if`
statements. Misplacing one (for example `@likely` on a `for` loop, or `@unroll`
without its factor) is a compile-time error in both backends. These hints are
advisory — GCC is free to ignore a pragma it cannot honor for a given loop.

---

## Allocation Pools

The runtime uses pre-allocated object pools to avoid `malloc`/`free` overhead for frequently created short-lived objects:

| Pool | Slots | Description |
|------|-------|-------------|
| Environment pool | 1024 | Closure/function scope environments (up to 16 variables each) |
| Object pool | 512 | Anonymous objects with up to 8 fields |
| Function pool | 512 | Closure structs for captured functions |

Pools use free-list stacks for O(1) allocation and deallocation. When a pool is exhausted, the runtime falls back to `malloc`. Objects that outgrow their pool slot (e.g., an object gaining a 9th field) are transparently migrated to heap storage.

### AST-Borrowed Parameters

Closures borrow parameter metadata directly from the AST instead of deep-copying, eliminating approximately 6 `malloc` + N `strdup` calls per closure creation. Parameter name hashes are lazily computed and cached on the AST node.

---

## Type Checking

The compiler includes compile-time type checking (enabled by default):

```bash
hemlockc program.hml -o program       # Type check + compile
hemlockc --check program.hml          # Type check only
hemlockc --no-type-check program.hml  # Skip type checking
hemlockc --strict-types program.hml   # Warn on implicit 'any' types
```

Untyped code is treated as dynamic (`any` type) and always passes type checking. Type annotations provide optimization hints that enable unboxing.

---

## See Also

- [Helper Annotations Proposal](../proposals/compiler-helper-annotations.md) - Detailed annotation reference
- [Memory API](../reference/memory-api.md) - Buffer and pointer operations
- [Functions](../language-guide/functions.md) - Type annotations and expression-bodied functions
