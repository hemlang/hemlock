# Hemlock Compile-to-C Implementation Proposal

**Version:** 1.0
**Date:** 2025-11-23
**Status:** Draft for Review

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Goals and Non-Goals](#goals-and-non-goals)
3. [Compilation Pipeline Design](#compilation-pipeline-design)
4. [Runtime Library Architecture](#runtime-library-architecture)
5. [Code Generation Strategy](#code-generation-strategy)
6. [Type System Compilation](#type-system-compilation)
7. [Optimization Opportunities](#optimization-opportunities)
8. [Implementation Phases](#implementation-phases)
9. [Testing Strategy](#testing-strategy)
10. [Examples of Generated Code](#examples-of-generated-code)
11. [Performance Expectations](#performance-expectations)
12. [Open Questions](#open-questions)

---

## Executive Summary

This proposal outlines a plan to implement **compilation to C** for Hemlock, transforming it from a pure tree-walking interpreter into a compiled language that generates native executables. The key insight is to **split Hemlock into two parts**:

1. **Compiler frontend**: Hemlock source → C source code
2. **Runtime library**: Shared runtime for heap types, type system, builtins

**Design Philosophy**: Preserve Hemlock's semantics (manual memory management, runtime type checking, explicit control flow) while enabling C-level performance for static operations.

**Expected Benefits**:
- **10-100x performance improvement** for CPU-bound code (no AST traversal overhead)
- **Smaller memory footprint** (no AST in memory at runtime)
- **Native executables** (no interpreter dependency)
- **C toolchain integration** (debuggers, profilers, sanitizers)
- **Gradual optimization path** (start with 1:1 translation, optimize incrementally)

**Key Technical Challenges**:
- Preserving runtime type checking semantics
- Exception handling (try/catch/finally/throw)
- Closure capture and environment management
- Defer statement implementation
- Module system compilation and linking

---

## Goals and Non-Goals

### Goals

✅ **Performance**: Generate C code that compiles to fast native executables
✅ **Semantic Preservation**: 100% compatibility with current interpreter behavior
✅ **Incremental Compilation**: Support separate compilation of modules
✅ **Debuggability**: Generate readable C code with source mappings
✅ **Runtime Library**: Reusable runtime for heap types, builtins, stdlib
✅ **Optimization Hooks**: Design for future optimizations (type specialization, inlining)
✅ **Testing**: All 394 existing tests must pass with compiled code

### Non-Goals

❌ **Breaking Changes**: No changes to Hemlock semantics or syntax
❌ **JIT Compilation**: Not implementing JIT (future consideration)
❌ **Automatic Memory Management**: Still manual alloc/free
❌ **Static Type System**: Still dynamic with optional annotations
❌ **Bytecode VM**: Going directly to C, not through bytecode

---

## Compilation Pipeline Design

### High-Level Pipeline

```
┌─────────────────┐
│  Hemlock Source │
│   (example.hml) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Lexer       │  (Existing)
│  Source → Tokens│
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Parser      │  (Existing)
│  Tokens → AST   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   [NEW PHASE]   │
│  Semantic Pass  │  - Type inference (optional)
│                 │  - Constant folding
│                 │  - Dead code elimination
│                 │  - Closure analysis
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   [NEW PHASE]   │
│  C Code Gen     │  - AST → C code
│                 │  - Symbol mangling
│                 │  - Runtime library calls
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   C Compiler    │  (gcc/clang)
│  example.c →    │
│  example.o      │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Linker      │
│  Link runtime,  │
│  stdlib, libffi │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Executable    │
│   (example)     │
└─────────────────┘
```

### New Compiler Components

#### 1. Semantic Pass (`src/compiler/semantic.c`)

**Purpose**: Analyze AST before code generation to enable optimizations and catch errors early.

**Responsibilities**:
- **Type inference** (optional): Infer types from literals and operations where possible
- **Constant folding**: Evaluate `2 + 3` → `5` at compile time
- **Dead code elimination**: Remove unreachable code after `return`, `break`, etc.
- **Closure analysis**: Identify which variables are captured by closures
- **Escape analysis**: Determine if values escape function scope (stack vs. heap)
- **Call graph construction**: Identify recursive functions, inline candidates

**Output**: Annotated AST with metadata for code generation.

#### 2. C Code Generator (`src/compiler/codegen.c`)

**Purpose**: Translate annotated AST to C source code.

**Responsibilities**:
- **Expression translation**: AST expressions → C expressions
- **Statement translation**: AST statements → C statements
- **Symbol mangling**: Generate unique C identifiers
- **Runtime API calls**: Insert calls to runtime library
- **Header generation**: Generate `.h` files for modules
- **Source mapping**: Track line numbers for debugging

**Output**: C source files (`.c` and `.h`).

#### 3. Module Compiler (`src/compiler/module.c`)

**Purpose**: Handle multi-file compilation and imports.

**Responsibilities**:
- **Dependency resolution**: Determine compilation order
- **Separate compilation**: Compile modules independently
- **Export generation**: Generate C headers for exported symbols
- **Import translation**: Translate `import` to C `#include` or extern declarations

**Output**: Object files (`.o`) and headers (`.h`) per module.

---

## Runtime Library Architecture

The runtime library provides **shared functionality** needed by all compiled Hemlock programs.

### Directory Structure

```
hemlock/
├── runtime/
│   ├── include/
│   │   ├── hemlock_runtime.h    # Main runtime API
│   │   ├── hemlock_value.h      # Value type definition
│   │   ├── hemlock_types.h      # Type system API
│   │   ├── hemlock_memory.h     # Memory management
│   │   ├── hemlock_string.h     # String methods
│   │   ├── hemlock_array.h      # Array methods
│   │   ├── hemlock_object.h     # Object methods
│   │   ├── hemlock_exceptions.h # Exception handling
│   │   ├── hemlock_concurrency.h# Async/task/channel
│   │   └── hemlock_builtins.h   # Builtin functions
│   │
│   ├── src/
│   │   ├── value.c              # Value constructors, ref counting
│   │   ├── types.c              # Type checking, promotion
│   │   ├── memory.c             # alloc, free, talloc, memset, etc.
│   │   ├── string.c             # String methods (18 methods)
│   │   ├── array.c              # Array methods (18 methods)
│   │   ├── object.c             # Object methods, duck typing
│   │   ├── exceptions.c         # try/catch/finally implementation
│   │   ├── concurrency.c        # spawn/join/detach, channels
│   │   ├── builtins_core.c      # print, typeof, assert, panic
│   │   ├── builtins_io.c        # File I/O, exec
│   │   ├── builtins_math.c      # Math functions
│   │   ├── builtins_time.c      # Time functions
│   │   ├── builtins_env.c       # Environment variables
│   │   ├── builtins_fs.c        # Filesystem operations
│   │   ├── builtins_net.c       # Networking
│   │   ├── builtins_ffi.c       # FFI support
│   │   ├── builtins_signals.c   # Signal handling
│   │   └── utf8.c               # UTF-8 utilities
│   │
│   └── Makefile                 # Build runtime library
│
├── stdlib_compiled/             # Pre-compiled stdlib modules
│   ├── collections.c/h          # Compiled from stdlib/collections.hml
│   ├── math.c/h
│   ├── time.c/h
│   ├── env.c/h
│   ├── fs.c/h
│   ├── net.c/h
│   ├── regex.c/h
│   └── json.c/h
│
└── build/
    ├── libhemlock_runtime.a     # Static runtime library
    └── libhemlock_runtime.so    # Shared runtime library
```

### Runtime API Design

#### Core Value API (`hemlock_value.h`)

```c
// Value type (same as current interpreter)
typedef struct Value {
    ValueType type;
    union {
        int32_t as_i32;
        int64_t as_i64;
        double as_f64;
        String *as_string;
        Array *as_array;
        // ... (37 types total)
    } as;
} Value;

// Constructors
Value hml_val_i32(int32_t val);
Value hml_val_i64(int64_t val);
Value hml_val_f64(double val);
Value hml_val_string(const char *str);
Value hml_val_null(void);

// Reference counting
void hml_retain(Value *val);
void hml_release(Value *val);

// Type checking
int hml_is_i32(Value val);
int hml_is_string(Value val);
ValueType hml_typeof(Value val);
```

#### Type System API (`hemlock_types.h`)

```c
// Type promotion
Value hml_promote(Value left, Value right, ValueType *result_type);

// Type conversion
Value hml_convert_to_type(Value val, ValueType target_type);

// Type checking with annotations
void hml_check_type(Value val, ValueType expected, const char *var_name);

// Duck typing for objects
void hml_check_object_type(Value obj, const char *type_name);
```

#### Memory API (`hemlock_memory.h`)

```c
// Memory management (same as builtins)
Value hml_alloc(int32_t size);
void hml_free(Value ptr_or_buffer);
Value hml_talloc(ValueType type, int32_t count);
Value hml_realloc(Value ptr, int32_t new_size);
void hml_memset(Value ptr, uint8_t byte_val, int32_t size);
void hml_memcpy(Value dest, Value src, int32_t size);
int32_t hml_sizeof(ValueType type);
Value hml_buffer(int32_t size);
```

#### Exception API (`hemlock_exceptions.h`)

```c
// Exception context (uses setjmp/longjmp or C++ exceptions)
typedef struct ExceptionContext {
    jmp_buf *exception_buf;
    Value exception_value;
    int is_active;
} ExceptionContext;

// Exception handling
ExceptionContext *hml_exception_push(void);
void hml_exception_pop(void);
void hml_throw(Value exception_value);
Value hml_exception_get_value(void);

// Defer stack
typedef void (*DeferFn)(void *arg);
void hml_defer_push(DeferFn fn, void *arg);
void hml_defer_pop_and_execute(void);
void hml_defer_execute_all(void);
```

#### Concurrency API (`hemlock_concurrency.h`)

```c
// Task/async (same as current implementation)
Value hml_spawn(Value async_fn, Value *args, int num_args);
Value hml_join(Value task);
void hml_detach(Value task);

// Channels
Value hml_channel(int32_t capacity);
void hml_channel_send(Value channel, Value value);
Value hml_channel_recv(Value channel);
void hml_channel_close(Value channel);
```

#### Builtin Functions API (`hemlock_builtins.h`)

```c
// Core builtins
void hml_print(Value val);
void hml_eprint(Value val);
void hml_assert(Value condition, Value message);
void hml_panic(Value message);
const char *hml_typeof_str(Value val);

// I/O
Value hml_read_line(void);
Value hml_open(Value path, Value mode);
Value hml_exec(Value command);

// Math (wrappers for libm)
double hml_sin(double x);
double hml_cos(double x);
double hml_sqrt(double x);
// ... (all math builtins)

// Filesystem
int hml_exists(Value path);
Value hml_read_file(Value path);
void hml_write_file(Value path, Value content);
// ... (all fs builtins)

// Networking
Value hml_socket_create(Value type, Value host, Value port);
Value hml_dns_resolve(Value hostname);
// ... (all net builtins)

// Signals
void hml_signal(int32_t signum, Value handler_fn);
void hml_raise(int32_t signum);

// FFI
Value hml_import_ffi(const char *lib_path);
Value hml_extern_fn(/* ... */);
```

---

## Code Generation Strategy

### Translation Rules

#### 1. Primitives and Literals

**Hemlock:**
```hemlock
let x: i32 = 42;
let y: f64 = 3.14;
let s: string = "hello";
let b: bool = true;
let r: rune = 'A';
let n = null;
```

**Generated C:**
```c
Value x = hml_val_i32(42);
Value y = hml_val_f64(3.14);
Value s = hml_val_string("hello");
Value b = hml_val_bool(1);
Value r = hml_val_rune('A');
Value n = hml_val_null();
```

**Optimization (with type inference):**
```c
// If x is only used with i32 operations and never passed to dynamic code:
int32_t x = 42;  // Unboxed!
```

#### 2. Arithmetic Operations

**Hemlock:**
```hemlock
let a = 10;
let b = 20;
let c = a + b * 2;
```

**Generated C (basic):**
```c
Value a = hml_val_i32(10);
Value b = hml_val_i32(20);

// b * 2
Value tmp1 = hml_val_i32(2);
Value tmp2 = hml_binary_op(OP_MUL, b, tmp1);

// a + tmp2
Value c = hml_binary_op(OP_ADD, a, tmp2);

hml_release(&tmp1);
hml_release(&tmp2);
```

**Generated C (optimized):**
```c
// Constant folding + type inference
int32_t a = 10;
int32_t b = 20;
int32_t c = a + b * 2;  // Pure C arithmetic!
```

#### 3. Control Flow

**Hemlock:**
```hemlock
if (x > 10) {
    print("large");
} else {
    print("small");
}
```

**Generated C:**
```c
{
    Value cond_val = hml_binary_op(OP_GT, x, hml_val_i32(10));
    int cond = hml_value_to_bool(cond_val);
    hml_release(&cond_val);

    if (cond) {
        hml_print(hml_val_string("large"));
    } else {
        hml_print(hml_val_string("small"));
    }
}
```

**Optimized (if x is known i32):**
```c
if (x > 10) {
    hml_print(hml_val_string("large"));
} else {
    hml_print(hml_val_string("small"));
}
```

#### 4. Functions

**Hemlock:**
```hemlock
fn add(a: i32, b: i32): i32 {
    return a + b;
}

let result = add(5, 10);
```

**Generated C:**
```c
// Function signature
Value hml_fn_add(Value a, Value b) {
    // Type checks
    hml_check_type(a, VAL_I32, "a");
    hml_check_type(b, VAL_I32, "b");

    // Body
    Value result = hml_binary_op(OP_ADD, a, b);

    // Return type check
    hml_check_type(result, VAL_I32, "return value");
    return result;
}

// Call site
Value result = hml_fn_add(hml_val_i32(5), hml_val_i32(10));
```

**Optimized (with type specialization):**
```c
// Specialized version for i32 arguments
int32_t hml_fn_add_i32_i32(int32_t a, int32_t b) {
    return a + b;  // Pure C arithmetic!
}

// Call site
int32_t result = hml_fn_add_i32_i32(5, 10);
```

#### 5. Closures

**Hemlock:**
```hemlock
fn makeAdder(x: i32) {
    return fn(y: i32): i32 {
        return x + y;
    };
}

let add5 = makeAdder(5);
let result = add5(10);
```

**Generated C:**
```c
// Closure environment struct
typedef struct Closure_makeAdder_anon_0 {
    Value x;  // Captured variable
} Closure_makeAdder_anon_0;

// Inner function (accesses closure environment)
Value hml_fn_makeAdder_anon_0(void *env_ptr, Value y) {
    Closure_makeAdder_anon_0 *env = (Closure_makeAdder_anon_0 *)env_ptr;
    hml_check_type(y, VAL_I32, "y");

    Value result = hml_binary_op(OP_ADD, env->x, y);
    return result;
}

// Outer function
Value hml_fn_makeAdder(Value x) {
    hml_check_type(x, VAL_I32, "x");

    // Allocate closure environment
    Closure_makeAdder_anon_0 *env = malloc(sizeof(Closure_makeAdder_anon_0));
    env->x = x;
    hml_retain(&env->x);  // Retain captured value

    // Create function value with environment
    Value closure = hml_val_function_with_env(hml_fn_makeAdder_anon_0, env);
    return closure;
}

// Call sites
Value add5 = hml_fn_makeAdder(hml_val_i32(5));
Value result = hml_call_function(add5, (Value[]){hml_val_i32(10)}, 1);
```

#### 6. Exception Handling

**Hemlock:**
```hemlock
try {
    risky_operation();
    print("success");
} catch (e) {
    print("Error: " + e);
} finally {
    print("cleanup");
}
```

**Generated C (using setjmp/longjmp):**
```c
ExceptionContext *ex_ctx = hml_exception_push();
if (setjmp(ex_ctx->exception_buf) == 0) {
    // Try block
    hml_fn_risky_operation();
    hml_print(hml_val_string("success"));
} else {
    // Catch block
    Value e = hml_exception_get_value();
    Value msg = hml_string_concat(hml_val_string("Error: "), e);
    hml_print(msg);
    hml_release(&msg);
    hml_release(&e);
}

// Finally block (always executes)
hml_print(hml_val_string("cleanup"));
hml_exception_pop();
```

**Throw statement:**
```c
hml_throw(hml_val_string("error message"));
// Does not return (longjmps to nearest try block)
```

#### 7. Defer Statement

**Hemlock:**
```hemlock
fn process() {
    let f = open("file.txt", "r");
    defer f.close();

    let content = f.read();
    process(content);

    // f.close() executes here, even if process() throws
}
```

**Generated C:**
```c
void hml_fn_process(void) {
    Value f = hml_open(hml_val_string("file.txt"), hml_val_string("r"));

    // Push defer onto defer stack
    hml_defer_push(hml_defer_file_close, hml_value_to_ptr(f));

    Value content = hml_call_method(f, "read", NULL, 0);
    hml_fn_process_content(content);
    hml_release(&content);

    // Execute all defers before return
    hml_defer_execute_all();
    hml_release(&f);
}

// Defer callback
void hml_defer_file_close(void *arg) {
    Value f = hml_ptr_to_value(arg);
    hml_call_method(f, "close", NULL, 0);
}
```

#### 8. Async/Concurrency

**Hemlock:**
```hemlock
async fn compute(n: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}

let task = spawn(compute, 100);
let result = await task;
```

**Generated C:**
```c
// Async function (regular C function, can be spawned)
Value hml_async_fn_compute(Value n) {
    hml_check_type(n, VAL_I32, "n");

    Value sum = hml_val_i32(0);
    Value i = hml_val_i32(0);

    while (hml_value_to_bool(hml_binary_op(OP_LT, i, n))) {
        sum = hml_binary_op(OP_ADD, sum, i);
        i = hml_binary_op(OP_ADD, i, hml_val_i32(1));
    }

    return sum;
}

// Call sites
Value fn_val = hml_val_function(hml_async_fn_compute);
Value task = hml_spawn(fn_val, (Value[]){hml_val_i32(100)}, 1);
Value result = hml_join(task);  // await is syntactic sugar for join
```

#### 9. Arrays

**Hemlock:**
```hemlock
let arr = [1, 2, 3, 4, 5];
arr.push(6);
let doubled = arr.map(fn(x) { return x * 2; });
```

**Generated C:**
```c
// Array literal
Value arr = hml_val_array();
hml_array_push(arr, hml_val_i32(1));
hml_array_push(arr, hml_val_i32(2));
hml_array_push(arr, hml_val_i32(3));
hml_array_push(arr, hml_val_i32(4));
hml_array_push(arr, hml_val_i32(5));

// Method call
hml_array_push(arr, hml_val_i32(6));

// Map with closure
Value map_fn = hml_val_function(hml_fn_anon_map_callback);
Value doubled = hml_array_map(arr, map_fn);

// Callback
Value hml_fn_anon_map_callback(Value x) {
    return hml_binary_op(OP_MUL, x, hml_val_i32(2));
}
```

#### 10. Objects

**Hemlock:**
```hemlock
let person = { name: "Alice", age: 30 };
person.age = 31;
print(person.name);
```

**Generated C:**
```c
// Object literal
Value person = hml_val_object();
hml_object_set_field(person, "name", hml_val_string("Alice"));
hml_object_set_field(person, "age", hml_val_i32(30));

// Property set
hml_object_set_field(person, "age", hml_val_i32(31));

// Property get
Value name = hml_object_get_field(person, "name");
hml_print(name);
hml_release(&name);
```

#### 11. Modules

**Hemlock (`math_utils.hml`):**
```hemlock
export fn square(x: i32): i32 {
    return x * x;
}

export const PI = 3.14159;
```

**Generated C (`math_utils.h`):**
```c
#ifndef HEMLOCK_MODULE_MATH_UTILS_H
#define HEMLOCK_MODULE_MATH_UTILS_H

#include "hemlock_runtime.h"

// Exported functions
Value hml_mod_math_utils_square(Value x);

// Exported constants
extern const Value hml_mod_math_utils_PI;

#endif
```

**Generated C (`math_utils.c`):**
```c
#include "math_utils.h"

Value hml_mod_math_utils_square(Value x) {
    hml_check_type(x, VAL_I32, "x");
    return hml_binary_op(OP_MUL, x, x);
}

const Value hml_mod_math_utils_PI = {
    .type = VAL_F64,
    .as = { .as_f64 = 3.14159 }
};
```

**Import in another module:**
```hemlock
import { square, PI } from "./math_utils";

let area = PI * square(5);
```

**Generated C:**
```c
#include "math_utils.h"

// Usage
Value radius = hml_val_i32(5);
Value radius_squared = hml_mod_math_utils_square(radius);
Value area = hml_binary_op(OP_MUL, hml_mod_math_utils_PI, radius_squared);
```

---

## Type System Compilation

### Challenge: Preserving Dynamic Type Semantics

Hemlock's type system is **dynamic with optional annotations**. The compiler must preserve:
1. **Runtime type checking** for annotated variables/parameters
2. **Type promotion** for mixed arithmetic (e.g., `i32 + f64` → `f64`)
3. **typeof()** builtin
4. **Duck typing** for objects

### Strategy 1: Keep All Values Tagged (Conservative)

**Approach**: Every value is a `Value` struct, just like the interpreter.

**Pros**:
- 100% semantic preservation (no surprises)
- Simple code generation (direct translation)
- Easy to implement

**Cons**:
- Overhead of tagged union (16 bytes per value)
- Heap allocation for all strings, arrays, objects
- Type checking overhead at runtime

**When to use**: Initial implementation, dynamic code, code with `typeof()`.

### Strategy 2: Type Inference and Unboxing (Optimized)

**Approach**: Infer types through dataflow analysis, use unboxed C types where safe.

**Example:**
```hemlock
let x = 42;        // Infer: x is always i32
let y = x + 10;    // Infer: y is always i32
print(x);          // x doesn't escape as Value
```

**Generated C (unboxed):**
```c
int32_t x = 42;
int32_t y = x + 10;
hml_print(hml_val_i32(x));  // Box only when needed
```

**Algorithm**:
1. **Forward pass**: Propagate types from literals and annotations
2. **Backward pass**: Propagate type constraints from usage
3. **Escape analysis**: Mark values that escape (passed to dynamic functions, stored in arrays, etc.)
4. **Unboxing**: Use C primitive types for non-escaping values

**Pros**:
- Near-C performance for static code
- No tagged union overhead
- Stack allocation for primitives

**Cons**:
- Complex analysis required
- Potential for incorrect optimization (must be conservative)

**When to use**: Hot loops, numeric code, statically-typed functions.

### Strategy 3: Type Specialization (Advanced)

**Approach**: Generate specialized versions of functions for known argument types.

**Example:**
```hemlock
fn add(a, b) {
    return a + b;
}

let x = add(10, 20);     // Call with i32, i32
let y = add(3.14, 2.0);  // Call with f64, f64
```

**Generated C:**
```c
// Generic version (fallback)
Value hml_fn_add(Value a, Value b) {
    return hml_binary_op(OP_ADD, a, b);
}

// Specialized version for (i32, i32) → i32
int32_t hml_fn_add_i32_i32(int32_t a, int32_t b) {
    return a + b;
}

// Specialized version for (f64, f64) → f64
double hml_fn_add_f64_f64(double a, double b) {
    return a + b;
}

// Call sites
int32_t x = hml_fn_add_i32_i32(10, 20);        // Specialized
double y = hml_fn_add_f64_f64(3.14, 2.0);      // Specialized
Value z = hml_fn_add(hml_val_i32(10), hml_val_string("?")); // Generic
```

**Pros**:
- Best performance for statically-typed calls
- No boxing overhead
- Enables further optimizations (inlining)

**Cons**:
- Code size bloat (multiple versions of same function)
- Complex call site selection
- Requires whole-program analysis for best results

**When to use**: Performance-critical code, stdlib functions.

---

## Optimization Opportunities

### Phase 1: Low-Hanging Fruit

1. **Constant Folding**
   - Evaluate `2 + 3` → `5` at compile time
   - String concatenation: `"hello" + " " + "world"` → `"hello world"`
   - Boolean expressions: `true && x` → `x`

2. **Dead Code Elimination**
   - Remove code after `return`, `break`, `continue`
   - Remove `if (false) { ... }` branches
   - Remove unused variables

3. **Strength Reduction**
   - Replace `x * 2` with `x + x` or `x << 1`
   - Replace `x / 2` with `x >> 1` (for unsigned)

4. **Peephole Optimization**
   - Remove redundant loads/stores
   - Eliminate temporary variables
   - Simplify comparisons

### Phase 2: Type-Based Optimizations

1. **Type Inference and Unboxing**
   - Use `int32_t` instead of `Value` where safe
   - Stack allocation for non-escaping values

2. **Type Specialization**
   - Generate specialized functions for known types
   - Reduce boxing/unboxing overhead

3. **Inline Small Functions**
   - Inline functions with < 10 lines of C code
   - Especially effective with type specialization

### Phase 3: Advanced Optimizations

1. **Escape Analysis**
   - Allocate strings/arrays on stack if they don't escape
   - Elide reference counting for non-escaping values

2. **Loop Optimizations**
   - Loop invariant code motion
   - Loop unrolling for small constant bounds

3. **Tail Call Optimization**
   - Convert tail-recursive functions to loops

4. **Devirtualization**
   - Replace dynamic method calls with static calls where possible

### Optimization Flags

```bash
hemlockc --opt=none   example.hml   # No optimization (fastest compile)
hemlockc --opt=basic  example.hml   # Phase 1 optimizations (default)
hemlockc --opt=full   example.hml   # Phase 1 + 2 optimizations
hemlockc --opt=max    example.hml   # All optimizations (slowest compile)
```

---

## Implementation Phases

### Phase 1: MVP (Minimum Viable Compiler) - 4 weeks

**Goal**: Compile simple Hemlock programs to C with 100% semantic preservation.

**Scope**:
- Primitives, literals, variables
- Arithmetic, logical, comparison operators
- Control flow: if/else, while, for
- Functions (no closures yet)
- Print builtin
- All values as tagged `Value` (no optimization)

**Deliverables**:
- `hemlockc` CLI tool
- Basic C code generator
- Runtime library (core only)
- 50 basic tests passing

**Example program:**
```hemlock
fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

let result = factorial(10);
print(result);
```

### Phase 2: Full Language Support - 6 weeks

**Goal**: Compile all Hemlock features.

**Scope**:
- Closures and environments
- Arrays, strings, objects
- Exception handling (try/catch/finally/throw)
- Defer statement
- Type annotations and checking
- All builtins (memory, I/O, math, etc.)

**Deliverables**:
- Complete C code generator
- Full runtime library
- 200+ tests passing

### Phase 3: Module System - 3 weeks

**Goal**: Support multi-file compilation and stdlib.

**Scope**:
- Import/export statements
- Separate compilation
- Module headers generation
- Stdlib compilation (8 modules)

**Deliverables**:
- Module compiler
- Compiled stdlib modules
- Linker integration
- 300+ tests passing

### Phase 4: Async/Concurrency - 3 weeks

**Goal**: Compile async functions, tasks, channels.

**Scope**:
- `async fn` → regular C function (spawnable)
- spawn/join/detach → pthread wrappers
- Channels → thread-safe runtime library

**Deliverables**:
- Async code generation
- Concurrency runtime library
- All 394 tests passing

### Phase 5: Optimization - 4 weeks

**Goal**: Implement basic optimizations.

**Scope**:
- Constant folding
- Dead code elimination
- Type inference (simple cases)
- Unboxing for primitives

**Deliverables**:
- Semantic analyzer
- Optimization passes
- Performance benchmarks (vs. interpreter)

### Phase 6: Advanced Features - Ongoing

**Scope**:
- Type specialization
- Inline expansion
- Escape analysis
- Loop optimizations
- Debugger integration (DWARF symbols)
- FFI improvements

---

## Testing Strategy

### 1. Regression Testing

**Goal**: All 394 existing interpreter tests must pass with compiled code.

**Approach**:
```bash
# Run test suite in interpreter mode (baseline)
make test

# Run test suite in compiled mode
make test-compiled

# Compare outputs
diff test-results-interpreter.log test-results-compiled.log
```

**Test categories**:
- Primitives (50 tests)
- Control flow (40 tests)
- Functions (30 tests)
- Closures (20 tests)
- Arrays/strings (60 tests)
- Objects (30 tests)
- Exceptions (25 tests)
- Async/concurrency (40 tests)
- I/O (30 tests)
- Stdlib (69 tests)

### 2. Code Generation Tests

**Goal**: Verify generated C code is correct.

**Approach**:
- **Golden file testing**: Compare generated C against expected output
- **Compilation tests**: Ensure generated C compiles without warnings
- **Execution tests**: Run generated executable and check output

**Example test:**
```bash
tests/codegen/
├── simple_arithmetic.hml         # Input
├── simple_arithmetic.expected.c  # Expected C output
└── simple_arithmetic.expected.out # Expected program output
```

### 3. Performance Benchmarks

**Goal**: Measure speedup vs. interpreter.

**Benchmarks**:
1. **Fibonacci** (recursion)
2. **Array sum** (loops)
3. **String concatenation** (heap allocation)
4. **Factorial** (tail recursion)
5. **Parallel tasks** (concurrency)
6. **Object creation** (duck typing)

**Expected results**:
- Fibonacci: **50-100x** faster (no AST traversal)
- Array sum: **10-20x** faster (loop overhead eliminated)
- String concat: **2-5x** faster (reduced interpreter overhead)
- Parallel tasks: **Similar** (pthread overhead dominates)

### 4. Compatibility Tests

**Goal**: Ensure compiled code behaves identically to interpreter.

**Focus areas**:
- Exception propagation (try/catch/finally semantics)
- Defer execution order (LIFO)
- Type promotion rules (i32 + f64 → f64)
- Reference counting (no memory leaks)
- Closure capture (correct variable binding)
- Async task lifecycle (spawn/join/detach)

---

## Examples of Generated Code

### Example 1: Simple Function

**Input (`example1.hml`):**
```hemlock
fn greet(name: string): string {
    return "Hello, " + name + "!";
}

let msg = greet("World");
print(msg);
```

**Generated C (`example1.c`):**
```c
#include "hemlock_runtime.h"

// Function: greet
Value hml_fn_greet(Value name) {
    // Type check parameter
    hml_check_type(name, VAL_STRING, "name");

    // Body: "Hello, " + name + "!"
    Value tmp1 = hml_val_string("Hello, ");
    Value tmp2 = hml_string_concat(tmp1, name);
    Value tmp3 = hml_val_string("!");
    Value result = hml_string_concat(tmp2, tmp3);

    // Cleanup temporaries
    hml_release(&tmp1);
    hml_release(&tmp2);
    hml_release(&tmp3);

    // Type check return value
    hml_check_type(result, VAL_STRING, "return value");
    return result;
}

// Main program
int main(int argc, char **argv) {
    hml_runtime_init(argc, argv);

    // let msg = greet("World");
    Value msg = hml_fn_greet(hml_val_string("World"));

    // print(msg);
    hml_print(msg);

    // Cleanup
    hml_release(&msg);
    hml_runtime_cleanup();
    return 0;
}
```

**Compilation:**
```bash
hemlockc example1.hml -o example1
./example1
# Output: Hello, World!
```

### Example 2: Array Operations

**Input (`example2.hml`):**
```hemlock
let numbers = [1, 2, 3, 4, 5];
let doubled = numbers.map(fn(x) { return x * 2; });
print(doubled.join(", "));
```

**Generated C (`example2.c`):**
```c
#include "hemlock_runtime.h"

// Anonymous function for map
Value hml_fn_anon_0(Value x) {
    Value two = hml_val_i32(2);
    Value result = hml_binary_op(OP_MUL, x, two);
    hml_release(&two);
    return result;
}

int main(int argc, char **argv) {
    hml_runtime_init(argc, argv);

    // let numbers = [1, 2, 3, 4, 5];
    Value numbers = hml_val_array();
    hml_array_push(numbers, hml_val_i32(1));
    hml_array_push(numbers, hml_val_i32(2));
    hml_array_push(numbers, hml_val_i32(3));
    hml_array_push(numbers, hml_val_i32(4));
    hml_array_push(numbers, hml_val_i32(5));

    // let doubled = numbers.map(fn(x) { return x * 2; });
    Value map_fn = hml_val_function(hml_fn_anon_0);
    Value doubled = hml_array_map(numbers, map_fn);

    // print(doubled.join(", "));
    Value delimiter = hml_val_string(", ");
    Value joined = hml_array_join(doubled, delimiter);
    hml_print(joined);

    // Cleanup
    hml_release(&numbers);
    hml_release(&map_fn);
    hml_release(&doubled);
    hml_release(&delimiter);
    hml_release(&joined);

    hml_runtime_cleanup();
    return 0;
}
```

### Example 3: Exception Handling

**Input (`example3.hml`):**
```hemlock
try {
    throw "Something went wrong!";
} catch (e) {
    print("Caught: " + e);
} finally {
    print("Cleanup");
}
```

**Generated C (`example3.c`):**
```c
#include "hemlock_runtime.h"

int main(int argc, char **argv) {
    hml_runtime_init(argc, argv);

    ExceptionContext *ex_ctx = hml_exception_push();

    if (setjmp(ex_ctx->exception_buf) == 0) {
        // Try block
        Value exception = hml_val_string("Something went wrong!");
        hml_throw(exception);
        // Not reached
    } else {
        // Catch block
        Value e = hml_exception_get_value();
        Value prefix = hml_val_string("Caught: ");
        Value msg = hml_string_concat(prefix, e);
        hml_print(msg);
        hml_release(&e);
        hml_release(&prefix);
        hml_release(&msg);
    }

    // Finally block (always executes)
    hml_print(hml_val_string("Cleanup"));

    hml_exception_pop();
    hml_runtime_cleanup();
    return 0;
}
```

---

## Performance Expectations

### Theoretical Speedup Analysis

**Tree-walking interpreter overhead:**
- AST traversal: ~50-100 cycles per node
- Type checking: ~20-50 cycles per operation
- Environment lookup: ~50-200 cycles (hash table or chain traversal)
- Function call: ~500-1000 cycles (environment creation, argument copying)
- Reference counting: ~10-50 cycles per retain/release

**Compiled code (no optimization):**
- Direct C function calls: ~10-20 cycles
- Type checking: Same (~20-50 cycles)
- Variable access: ~1-5 cycles (stack/register)
- Function call: ~50-100 cycles (standard C calling convention)
- Reference counting: Same (~10-50 cycles)

**Expected speedup (conservative):**
- **Arithmetic-heavy code**: 10-50x (no AST traversal)
- **Function-heavy code**: 5-20x (cheaper calls)
- **String/array operations**: 2-5x (runtime library overhead dominates)
- **I/O-heavy code**: 1-2x (I/O overhead dominates)

### Benchmarks (Projected)

| Benchmark              | Interpreter | Compiled (no opt) | Compiled (opt) | Speedup |
|------------------------|-------------|-------------------|----------------|---------|
| Fibonacci (recursive)  | 2000ms      | 40ms              | 20ms           | 50-100x |
| Array sum (loop)       | 500ms       | 50ms              | 10ms           | 10-50x  |
| String concat (1000x)  | 300ms       | 100ms             | 60ms           | 3-5x    |
| Object creation (1000x)| 400ms       | 150ms             | 120ms          | 2-3x    |
| Parallel tasks (4)     | 800ms       | 700ms             | 700ms          | 1.1x    |

**Note**: These are projections based on similar interpreter → compiler transitions (Python → Cython, Ruby → Crystal, Lua → LuaJIT).

---

## Open Questions

### 1. Exception Handling Implementation

**Question**: Use setjmp/longjmp or C++ exceptions?

**Option A: setjmp/longjmp (C-compatible)**
- Pros: Pure C, portable, no C++ dependency
- Cons: Performance overhead, limited scope, non-local jumps

**Option B: C++ exceptions**
- Pros: Better performance, cleaner semantics, RAII-compatible
- Cons: Requires C++ compiler, larger binary size

**Recommendation**: Start with setjmp/longjmp (Phase 1), consider C++ exceptions later (Phase 6).

### 2. Runtime Library Distribution

**Question**: Static or dynamic linking?

**Option A: Static library (libhemlock_runtime.a)**
- Pros: Single executable, no runtime dependencies
- Cons: Larger binary size, no shared runtime across programs

**Option B: Dynamic library (libhemlock_runtime.so)**
- Pros: Smaller binaries, shared runtime, easier updates
- Cons: Requires runtime library installation

**Recommendation**: Support both, default to static linking (simpler deployment).

### 3. Type Specialization Strategy

**Question**: When to generate specialized functions?

**Option A: Lazy specialization (on-demand)**
- Generate specialized versions only when called with known types
- Pros: Minimal code bloat, faster compilation
- Cons: May miss optimization opportunities

**Option B: Eager specialization (whole-program)**
- Analyze all call sites, generate all needed specializations
- Pros: Maximum performance
- Cons: Slower compilation, code bloat

**Recommendation**: Start with Option A (Phase 5), consider Option B later (Phase 6).

### 4. Debugging Support

**Question**: How to support debugging compiled code?

**Options**:
- **Source maps**: Map C line numbers back to Hemlock source
- **DWARF symbols**: Embed debugging info for gdb/lldb
- **Custom debugger**: Build Hemlock-aware debugger

**Recommendation**: Emit DWARF symbols with Hemlock source file paths (Phase 6).

### 5. Interop with Interpreter

**Question**: Can compiled code call interpreted code (and vice versa)?

**Use case**: Gradual migration (compile hot paths, keep rest interpreted).

**Approach**:
- Compiled code → Runtime library → FFI callback to interpreter
- Complex, but possible for mixed-mode execution

**Recommendation**: Not for v0.2 (too complex), consider for v0.3.

---

## Next Steps

1. **Review this proposal** - Gather feedback from maintainers/users
2. **Prototype Phase 1** - Implement MVP compiler for simple programs
3. **Test against interpreter** - Ensure semantic equivalence
4. **Iterate** - Refine design based on implementation experience
5. **Expand** - Incrementally add features (Phases 2-4)
6. **Optimize** - Add optimization passes (Phase 5)
7. **Release v0.2** - Ship compiled Hemlock to users

---

## Conclusion

Compiling Hemlock to C is **feasible and valuable**. The current interpreter architecture is well-modularized, making it straightforward to extract the runtime library while generating C code for user programs.

**Key insights**:
- **Split runtime from compilation**: Keep heap types, builtins, stdlib as runtime library
- **Start simple**: Generate C code that calls runtime library (no optimization initially)
- **Preserve semantics**: All 394 tests must pass with compiled code
- **Optimize incrementally**: Add type inference, specialization, inlining over time

**Expected impact**:
- **10-100x performance improvement** for most code
- **Native executables** (no interpreter dependency)
- **C toolchain integration** (debuggers, profilers, sanitizers)
- **Foundation for future optimizations** (JIT, LLVM backend, etc.)

This proposal provides a **clear roadmap** from interpreter to compiler while maintaining Hemlock's core philosophy: explicit, unsafe, and honest about tradeoffs.

---

**Document Version:** 1.0
**Last Updated:** 2025-11-23
**Status:** Draft for Review
