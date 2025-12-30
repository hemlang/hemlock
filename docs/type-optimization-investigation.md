# Type Optimization Investigation

## Current State

The Hemlock compiler already has significant type optimization infrastructure:

### Implemented Optimizations

1. **Type Inference Pass** (`src/backends/compiler/type_infer.c`)
   - Tracks variable types through control flow
   - Infers types from literals, binary ops, function returns
   - Supports type refinement and meet operations for branches

2. **Loop Counter Unboxing**
   - Detects `for (let i = 0; i < N; i++)` patterns
   - Uses native `int32_t`/`int64_t` instead of `HmlValue`
   - Bypasses boxing/unboxing for loop counter access
   - Already generates: `int32_t i = 0; while (i < bound) { ... i++; }`

3. **Escape Analysis**
   - Tracks if variables are passed to functions, stored in arrays, returned
   - Used to determine if a variable can be safely unboxed

4. **Strength Reduction & Constant Folding**
   - `x * 2` → `x << 1`, `x % 8` → `x & 7`
   - `2 + 3` → `5` at compile time
   - Identity elimination: `x + 0` → `x`, `x * 1` → `x`

5. **Type-Aware Binary Operations**
   - When both operands known to be i32: `hml_i32_add()` (skips dispatch)
   - Cascading fast paths: i32 → i64 → generic dispatch

---

## Unexploited Opportunities

### 1. Typed Variable Unboxing (High Impact)

**Problem:** Variables with explicit type annotations still use `HmlValue`:
```hemlock
let x: i32 = 42;      // Currently: HmlValue x = hml_val_i32(42);
let y: i32 = x + 1;   // Currently: HmlValue y = hml_i32_add(x, hml_val_i32(1));
```

**Optimization:** When a variable has a type annotation AND doesn't escape:
```c
int32_t x = 42;           // Direct native type
int32_t y = x + 1;        // Direct C arithmetic
```

**Benefits:**
- Eliminates boxing/unboxing overhead
- Enables C compiler optimizations (register allocation, loop unrolling)
- Reduces memory traffic (8 bytes vs 16 bytes per value)

**Implementation:**
1. In precompile pass, identify typed variables that don't escape
2. Add `is_unboxed` flag to variable declarations
3. Generate native C types for unboxed variables
4. Convert to `HmlValue` only at escape points (function calls, returns)

### 2. Accumulator Unboxing (Medium Impact)

**Problem:** The `type_is_accumulator()` function exists but isn't used in codegen:
```hemlock
let sum: i32 = 0;
for (let i = 0; i < arr.length; i++) {
    sum = sum + arr[i];  // sum is boxed, unboxed, reboxed each iteration
}
```

**Optimization:**
```c
int32_t sum = 0;
for (int32_t i = 0; i < arr.length; i++) {
    sum += hml_to_i32(hml_array_get_i32_fast(arr, i));
}
// Box only at the end if needed
```

### 3. Branch Type Narrowing (Medium Impact)

**Problem:** After type checks, the compiler doesn't use the narrowed type:
```hemlock
if (typeof(x) == "i32") {
    let y = x + 1;  // We KNOW x is i32 here, but still dispatch
}
```

**Optimization:** In the `then` branch, refine `x`'s type to `INFER_I32` and generate direct `hml_i32_add()`.

### 4. Typed Array Element Access (Medium Impact)

**Problem:** Typed arrays (`array<i32>`) still use generic element access:
```hemlock
let arr: array<i32> = [1, 2, 3];
let x = arr[0];  // Generic hml_array_get(), then type check
```

**Optimization:** Generate specialized element access:
```c
// When we know arr is array<i32>:
HmlValue x = hml_val_i32(hml_array_get_i32_unchecked(arr, 0));
```

### 5. Function Return Type Propagation (Low-Medium Impact)

**Problem:** Function return types aren't always propagated to call sites:
```hemlock
fn double(x: i32): i32 { return x * 2; }
let result = double(5);  // We know result is i32, but don't use it
```

**The registry exists** (`type_register_func_return`) but could be used more aggressively.

### 6. Inline Known-Type Conditions (Low Impact)

**Problem:** Boolean conditions always call `hml_to_bool()`:
```hemlock
let flag: bool = true;
if (flag) { ... }  // Generates: if (hml_to_bool(flag))
```

**Optimization:** When type is known bool:
```c
if (flag.as.boolean) { ... }  // Direct struct access
```

---

## Proposed Precompile Pass Architecture

```
Source (.hml)
     ↓
┌─────────────────────────────┐
│  FRONTEND (existing)        │
│  - Lexer, Parser, AST       │
└─────────────────────────────┘
     ↓
┌─────────────────────────────┐
│  NEW: TYPE ANALYSIS PASS    │  ← Precompile step
│  1. Collect typed variables │
│  2. Escape analysis         │
│  3. Mark unboxable vars     │
│  4. Type narrowing in CFG   │
└─────────────────────────────┘
     ↓
┌─────────────────────────────┐
│  CODEGEN (enhanced)         │
│  - Use analysis results     │
│  - Generate optimized code  │
└─────────────────────────────┘
```

---

## Implementation Priority

| Optimization | Impact | Effort | Priority |
|-------------|--------|--------|----------|
| Typed Variable Unboxing | High | Medium | 1 |
| Accumulator Unboxing | Medium | Low | 2 |
| Branch Type Narrowing | Medium | High | 3 |
| Typed Array Access | Medium | Medium | 4 |
| Function Return Propagation | Low | Low | 5 |
| Inline Bool Conditions | Low | Low | 6 |

---

## Safety Considerations

All optimizations must maintain parity with the interpreter:

1. **Only optimize when type is PROVEN** - never assume
2. **Handle null carefully** - `x?: i32` means x could be null
3. **Escape points require boxing** - any value passed to unknown code
4. **Division always f64** - `/` operator returns float even on integers
5. **Overflow behavior** - Hemlock integers wrap like C

---

---

## Concrete Example: Before/After

**Source code:**
```hemlock
fn sum_to_n(n: i32): i32 {
    let total: i32 = 0;
    for (let i = 0; i < n; i = i + 1) {
        total = total + i;
    }
    return total;
}
```

**Current Generated C (simplified):**
```c
HmlValue hml_fn_sum_to_n(HmlClosureEnv *env, HmlValue n) {
    HmlValue total = hml_val_i32(0);   // Boxed in 16-byte struct
    {
        int32_t i = 0;                 // ✓ Loop counter is unboxed (recent opt)
        int32_t _bound = hml_to_i32(n);
        while (i < _bound) {
            HmlValue _tmp4 = total;
            hml_retain_if_needed(&_tmp4);
            HmlValue _tmp5 = hml_val_i32(i);      // RE-BOX i just to add!

            // RUNTIME TYPE CHECK even though we KNOW both are i32!
            HmlValue _tmp3 = hml_both_i32(_tmp4, _tmp5)
                ? hml_i32_add(_tmp4, _tmp5)
                : hml_binary_op(HML_OP_ADD, _tmp4, _tmp5);

            hml_release_if_needed(&_tmp4);
            hml_release_if_needed(&_tmp5);
            hml_release(&total);
            total = _tmp3;
            hml_retain(&total);
            i += 1;
        }
    }
    return total;
}
```

**Optimal Generated C (with typed variable unboxing):**
```c
HmlValue hml_fn_sum_to_n(HmlClosureEnv *env, HmlValue n) {
    int32_t total = 0;                 // Native C type!
    int32_t _bound = n.as.i32;         // Direct struct access
    for (int32_t i = 0; i < _bound; i++) {
        total = total + i;             // Pure C arithmetic - no boxing!
    }
    return hml_val_i32(total);         // Box only at function return
}
```

**Performance Impact:**
- Current: ~15 operations per loop iteration (box, type check, unbox, refcount, ...)
- Optimized: ~2 operations per loop iteration (add, increment)
- Estimated speedup: **5-10x for tight numeric loops**

---

## Implementation Status

### ✓ Implemented: Typed Variable Unboxing

Variables with explicit type annotations (`:i32`, `:i64`, `:f64`, `:bool`) that don't escape
are now stored as native C types instead of boxed `HmlValue`.

**Files changed:**
- `type_infer.c`: Added `type_analyze_typed_variables()` and escape analysis
- `codegen_stmt.c`: Generate native C type declarations for unboxed vars
- `codegen_expr.c`: Handle assignments to unboxed variables
- `codegen_expr_ident.c`: Box unboxed variables when accessed

**Parity test:** `tests/parity/language/typed_variable_unboxing.hml`

### Remaining Optimizations

1. **Direct C arithmetic for unboxed operands** - When both operands of a binary operation
   are unboxed, generate `a + b` instead of boxing/unboxing.
2. **Branch type narrowing** - After `typeof(x) == "i32"`, know x is i32 in that branch.
3. **Typed array element access** - For `array<i32>`, generate specialized element access.
4. **Accumulator optimization** - Use `type_is_accumulator()` (infrastructure exists).

## Next Steps

1. Implement direct C arithmetic for unboxed binary operations
2. Add `--dump-types` flag for debugging type inference
3. Measure performance impact with benchmarks
