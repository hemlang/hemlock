# Hemlock Language Design Philosophy

> "A small, unsafe language for writing unsafe things safely."

This document captures the core design principles and philosophy of Hemlock. Read this first before making any changes or additions to the language.

---

## Table of Contents

- [Core Identity](#core-identity)
- [Design Principles](#design-principles)
- [Philosophy on Safety](#philosophy-on-safety)
- [What NOT to Add](#what-not-to-add)
- [Future Considerations](#future-considerations)
- [Final Thoughts](#final-thoughts)

---

## Core Identity

Hemlock is a **systems scripting language** that embraces manual memory management and explicit control. It's designed for programmers who want:

- The power of C
- The ergonomics of modern scripting languages
- Structured async concurrency built-in
- No hidden behavior or magic

### What Hemlock IS NOT

- **Memory-safe** (dangling pointers are your responsibility)
- **A replacement for Rust, Go, or Lua**
- **A language that hides complexity from you**

### What Hemlock IS

- **Explicit over implicit, always**
- **Educational and experimental**
- **A "C scripting layer" for systems work**
- **Honest about tradeoffs**

---

## Design Principles

### 1. Explicit Over Implicit

Hemlock favors explicitness in all language constructs. There should be no surprises, no magic, and no hidden behavior.

**Bad (implicit):**
```hemlock
let x = 5  // Missing semicolon - should error
```

**Good (explicit):**
```hemlock
let x = 5;
free(ptr);  // You allocated it, you free it
```

**Key aspects:**
- Semicolons are mandatory (no automatic semicolon insertion)
- No garbage collection
- Manual memory management (alloc/free)
- Type annotations are optional but checked at runtime
- No automatic resource cleanup (no RAII), but `defer` provides explicit cleanup

### 2. Dynamic by Default, Typed by Choice

Every value has a runtime type tag, but the system is designed to be flexible while still catching errors.

**Type inference:**
- Small integers (fits in i32): `42` → `i32`
- Large integers (> i32 range): `9223372036854775807` → `i64`
- Floats: `3.14` → `f64`

**Explicit typing when needed:**
```hemlock
let x = 42;              // i32 inferred (small value)
let y: u8 = 255;         // explicit u8
let z = x + y;           // promotes to i32
let big = 5000000000;    // i64 inferred (> i32 max)
```

**Type promotion rules** follow a clear hierarchy from smallest to largest, with floats always winning over integers.

### 3. Unsafe is a Feature, Not a Bug

Hemlock doesn't try to prevent all errors. Instead, it gives you the tools to be safe while allowing you to opt into unsafe behavior when needed.

**Examples of intentional unsafety:**
- Pointer arithmetic can overflow (user's responsibility)
- No bounds checking on raw `ptr` (use `buffer` if you want safety)
- Double-free crashes are allowed (manual memory management)
- Type system prevents accidents but allows footguns when needed

```hemlock
let p = alloc(10);
let q = p + 100;  // Way past allocation - allowed but dangerous
```

**The philosophy:** The type system should prevent *accidents* but allow *intentional* unsafe operations.

### 4. Structured Concurrency First-Class

Concurrency is not an afterthought in Hemlock. It's built into the language from the ground up.

**Key features:**
- `async`/`await` built into the language
- Channels for communication
- `spawn`/`join`/`detach` for task management
- No raw threads, no locks - structured only
- True multi-threaded parallelism using POSIX threads

**Not an event loop or green threads** - Hemlock uses real OS threads for true parallelism across multiple CPU cores.

### 5. C-like Syntax, Low Ceremony

Hemlock should feel familiar to systems programmers while reducing boilerplate.

**Design choices:**
- `{}` blocks always, no optional braces
- Operators match C: `+`, `-`, `*`, `/`, `&&`, `||`, `!`
- Type syntax matches Rust/TypeScript: `let x: type = value;`
- Functions are first-class values
- Minimal keywords and special forms

---

## Philosophy on Safety

**Hemlock's take on safety:**

> "We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).
>
> The default should guide toward safety, but the escape hatch should always be available."

### Safety Tools Provided

**1. Safe buffer type:**
```hemlock
let b: buffer = buffer(64);
b[0] = 65;              // bounds checked
print(b.length);        // 64
free(b);                // still manual
```

**2. Unsafe raw pointers:**
```hemlock
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);  // You must remember to free
```

**3. Type annotations:**
```hemlock
let x: u8 = 255;   // OK
let y: u8 = 256;   // ERROR: out of range
```

**4. Runtime type checking:**
```hemlock
let val = some_function();
if (typeof(val) == "i32") {
    // Safe to use as integer
}
```

### Guiding Principles

1. **Default to safe patterns in documentation** - Show `buffer` before `ptr`, encourage type annotations
2. **Make unsafe operations obvious** - Raw pointer arithmetic should look intentional
3. **Provide escape hatches** - Don't prevent experienced users from doing low-level work
4. **Be honest about tradeoffs** - Document what can go wrong

### Examples of Safety vs. Unsafety

| Safe Pattern | Unsafe Pattern | When to Use Unsafe |
|-------------|----------------|-------------------|
| `buffer` type | `ptr` type | FFI, performance-critical code |
| Type annotations | No annotations | External interfaces, validation |
| Bounds-checked access | Pointer arithmetic | Low-level memory operations |
| Exception handling | Returning null/error codes | When exceptions are too heavyweight |

---

## What NOT to Add

Understanding what **not** to add is as important as knowing what to add.

### ❌ Don't Add Implicit Behavior

**Bad examples:**

```hemlock
// BAD: Automatic semicolon insertion
let x = 5
let y = 10

// BAD: Implicit type conversions that lose precision
let x: i32 = 3.14  // Should truncate or error?
```

**Why:** Implicit behavior creates surprises and makes code harder to reason about.

### ❌ Don't Hide Complexity

**Bad examples:**

```hemlock
// BAD: Magic behind-the-scenes optimization
let arr = [1, 2, 3]  // Is this stack or heap? User should know! (Heap, refcounted)

// BAD: Raw pointer auto-freed
let p = alloc(100)  // Does this auto-free? NO! Raw ptrs always need free()
```

**Note on refcounting:** Hemlock uses internal refcounting for strings, arrays, objects, and buffers - these ARE auto-freed when scope exits. This is explicit and predictable (deterministic cleanup when ref hits 0, no GC pauses). Raw pointers (`ptr` from `alloc()`) are NOT refcounted and always require manual `free()`.

**Why:** Hidden complexity makes it impossible to predict performance and debug issues.

### ❌ Don't Break Existing Semantics

**Never change these core decisions:**
- Semicolons are mandatory - don't make them optional
- Manual memory management - don't add GC
- Mutable strings - don't make them immutable
- Runtime type checking - don't remove it

**Why:** Consistency and stability are more important than trendy features.

### ❌ Don't Add "Convenient" Features That Reduce Explicitness

**Examples of features to avoid:**
- Operator overloading (maybe for user types, but carefully)
- Implicit type coercion that loses information
- Automatic resource cleanup (RAII)
- Method chaining that hides complexity
- DSLs and magic syntax

**Exception:** Convenience features are OK if they're **explicit sugar** over simple operations:
- `else if` is fine (it's just nested if statements)
- String interpolation might be OK if it's clearly syntactic sugar
- Method syntax for objects is fine (it's explicit what it does)

---

## Future Considerations

### Maybe Add (Under Discussion)

These features align with Hemlock's philosophy but need careful design:

**1. Pattern matching**
```hemlock
match (value) {
    case i32: print("integer");
    case string: print("text");
    case _: print("other");
}
```
- Explicit type checking
- No hidden costs
- Compile-time exhaustiveness checking possible

**2. Error types (`Result<T, E>`)**
```hemlock
fn divide(a: i32, b: i32): Result<i32, string> {
    if (b == 0) {
        return Err("division by zero");
    }
    return Ok(a / b);
}
```
- Explicit error handling
- Forces users to think about errors
- Alternative to exceptions

**3. Array/slice types**
- Already have dynamic arrays
- Could add fixed-size arrays for stack allocation
- Would need to be explicit about stack vs. heap

**4. Improved memory safety tools**
- Optional bounds checking flag
- Memory leak detection in debug builds
- Sanitizer integration

### Probably Never Add

These features violate core principles:

**1. Garbage collection**
- Hides memory management complexity
- Unpredictable performance
- Against explicit control principle

**2. Automatic memory management**
- Same reasons as GC
- Reference counting might be OK if explicit

**3. Implicit type conversions that lose data**
- Goes against "explicit over implicit"
- Source of subtle bugs

**4. Macros (complex ones)**
- Too much power, too much complexity
- Simple macro system might be OK
- Prefer code generation or functions

**5. Class-based OOP with inheritance**
- Too much implicit behavior
- Duck typing and objects are sufficient
- Composition over inheritance

**6. Module system with complex resolution**
- Keep imports simple and explicit
- No magic search paths
- No version resolution (use OS package manager)

---

## Final Thoughts

### Trust and Responsibility

Hemlock is about **trust and responsibility**. We trust the programmer to:

- Manage memory correctly
- Use types appropriately
- Handle errors properly
- Understand the tradeoffs

In return, Hemlock provides:

- No hidden costs
- No surprise behavior
- Full control when needed
- Safety tools when wanted

### The Guiding Question

**When considering a new feature, ask:**

> "Does this give the programmer more explicit control, or does it hide something?"

- If it **adds explicit control** → probably fits Hemlock
- If it **hides complexity** → probably doesn't belong
- If it's **optional sugar** that's clearly documented → might be OK

### Examples of Good Additions

✅ **Switch statements** - Explicit control flow, no magic, clear semantics

✅ **Async/await with pthreads** - Explicit concurrency, true parallelism, user controls spawning

✅ **Buffer type alongside ptr** - Gives choice between safe and unsafe

✅ **Optional type annotations** - Helps catch bugs without forcing strictness

✅ **Try/catch/finally** - Explicit error handling with clear control flow

### Examples of Bad Additions

❌ **Automatic semicolon insertion** - Hides syntax errors, makes code ambiguous

❌ **RAII/destructors** - Automatic cleanup hides when resources are released

❌ **Implicit null coalescing** - Hides null checks, makes code harder to reason about

❌ **Auto-growing strings** - Hides memory allocation, unpredictable performance

---

## Conclusion

Hemlock is not trying to be the safest language, the fastest language, or the most feature-rich language.

**Hemlock is trying to be the most *honest* language.**

It tells you exactly what it's doing, gives you control when you need it, and doesn't hide the sharp edges. It's a language for people who want to understand their code at a low level while still enjoying modern ergonomics.

If you're not sure whether a feature belongs in Hemlock, remember:

> **Explicit over implicit, always.**
> **Unsafe is a feature, not a bug.**
> **The user is responsible, and that's OK.**
