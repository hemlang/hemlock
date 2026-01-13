# Memory Management

Hemlock embraces **manual memory management** with explicit control over allocation and deallocation. This guide covers Hemlock's memory model, the two pointer types, and the complete memory API.

---

## Memory 101: The Basics

**New to programming?** Start here. If you already understand memory management, skip to [Philosophy](#philosophy).

### What is Memory Management?

When your program needs to store data (text, numbers, lists), it needs space to put it. That space comes from your computer's memory (RAM). Memory management is about:

1. **Getting space** - asking for memory when you need it
2. **Using space** - reading and writing your data
3. **Giving it back** - returning memory when you're done

### Why Does It Matter?

Imagine a library with limited books:
- If you keep checking out books and never return them, eventually there are none left
- If you try to read a book you already returned, you'll get confused or cause problems

Memory works the same way. If you forget to return memory, your program slowly uses more and more (a "memory leak"). If you try to use memory after returning it, bad things happen.

### The Good News

**Most of the time, you don't need to think about this!**

Hemlock automatically cleans up most common types:

```hemlock
fn example() {
    let name = "Alice";       // Hemlock manages this
    let numbers = [1, 2, 3];  // And this
    let person = { age: 30 }; // And this too

    // When the function ends, all of this is cleaned up automatically!
}
```

### When You DO Need to Think About It

You only need manual memory management when using:

1. **`alloc()`** - raw memory allocation (returns `ptr`)
2. **`buffer()`** - when you want to free early (optional - it auto-frees at scope end)

```hemlock
// This needs manual cleanup:
let raw = alloc(100);   // Raw memory - YOU must free it
// ... use raw ...
free(raw);              // Required! Or you have a memory leak

// This cleans up automatically (but you CAN free early):
let buf = buffer(100);  // Safe buffer
// ... use buf ...
// free(buf);           // Optional - will auto-free when scope ends
```

### The Simple Rule

> **If you call `alloc()`, you must call `free()`.**
>
> Everything else is handled for you.

### Which Should You Use?

| Situation | Use This | Why |
|-----------|----------|-----|
| **Just starting out** | `buffer()` | Safe, bounds-checked, auto-cleanup |
| **Need byte storage** | `buffer()` | Safe and easy |
| **Working with C libraries (FFI)** | `alloc()` / `ptr` | Required for C interop |
| **Maximum performance** | `alloc()` / `ptr` | No bounds checking overhead |
| **Not sure** | `buffer()` | Always the safer choice |

### Quick Example: Safe vs Raw

```hemlock
// RECOMMENDED: Safe buffer
fn safe_example() {
    let data = buffer(10);
    data[0] = 65;           // OK
    data[5] = 66;           // OK
    // data[100] = 67;      // ERROR - Hemlock stops you (bounds check)
    free(data);             // Clean up
}

// ADVANCED: Raw pointer (only when you need it)
fn raw_example() {
    let data = alloc(10);
    *data = 65;             // OK
    *(data + 5) = 66;       // OK
    *(data + 100) = 67;     // DANGER - No bounds check, corrupts memory!
    free(data);             // Clean up
}
```

**Start with `buffer()`. Only use `alloc()` when you specifically need raw pointers.**

---

## Philosophy

Hemlock follows the principle of explicit memory management with sensible defaults:
- No garbage collection (no unpredictable pauses)
- Internal refcounting for common types (string, array, object, buffer)
- Raw pointers (`ptr`) require manual `free()`

This hybrid approach gives you complete control when needed (raw pointers) while preventing common bugs for typical use cases (refcounted types auto-freed on scope exit).

## Internal Reference Counting

The runtime uses **internal reference counting** to manage object lifetimes. For most local variables of refcounted types, cleanup is automatic and deterministic.

### What Reference Counting Handles

The runtime automatically manages reference counts when:

1. **Variables are reassigned** - the old value is released:
   ```hemlock
   let x = "first";   // ref_count = 1
   x = "second";      // "first" released internally, "second" ref_count = 1
   ```

2. **Scopes exit** - local variables are released:
   ```hemlock
   fn example() {
       let arr = [1, 2, 3];  // ref_count = 1
   }  // arr released when function returns
   ```

3. **Containers are freed** - elements are released:
   ```hemlock
   let arr = [obj1, obj2];
   free(arr);  // obj1 and obj2 get their ref_counts decremented
   ```

### When You Need `free()` vs When It's Automatic

**Automatic (no `free()` needed):** Local variables of refcounted types are freed when scope exits:

```hemlock
fn process_data() {
    let arr = [1, 2, 3];
    let obj = { name: "test" };
    let buf = buffer(64);
    // ... use them ...
}  // All automatically freed when function returns - no free() needed
```

**Manual `free()` required:**

1. **Raw pointers** - `alloc()` has no refcounting:
   ```hemlock
   let p = alloc(64);
   // ... use p ...
   free(p);  // Always required - will leak otherwise
   ```

2. **Early cleanup** - free before scope ends to release memory sooner:
   ```hemlock
   fn long_running() {
       let big = buffer(10000000);  // 10MB
       // ... done with big ...
       free(big);  // Free now, don't wait for function to return
       // ... more work that doesn't need big ...
   }
   ```

3. **Long-lived data** - globals or data stored in persistent structures:
   ```hemlock
   let cache = {};  // Module-level, lives until program exit unless freed

   fn cleanup() {
       free(cache);  // Manual cleanup for long-lived data
   }
   ```

### Refcounting vs Garbage Collection

| Aspect | Hemlock Refcounting | Garbage Collection |
|--------|---------------------|-------------------|
| Cleanup timing | Deterministic (immediate when ref hits 0) | Non-deterministic (GC decides when) |
| User responsibility | Must call `free()` | Fully automatic |
| Runtime pauses | None | "Stop the world" pauses |
| Visibility | Hidden implementation detail | Usually invisible |
| Cycles | Handled with visited-set tracking | Handled by tracing |

### Which Types Have Refcounting

| Type | Refcounted | Notes |
|------|------------|-------|
| `ptr` | ❌ No | Always requires manual `free()` |
| `buffer` | ✅ Yes | Auto-freed on scope exit; manual `free()` for early cleanup |
| `array` | ✅ Yes | Auto-freed on scope exit; manual `free()` for early cleanup |
| `object` | ✅ Yes | Auto-freed on scope exit; manual `free()` for early cleanup |
| `string` | ✅ Yes | Fully automatic, no `free()` needed |
| `function` | ✅ Yes | Fully automatic (closure environments) |
| `task` | ✅ Yes | Thread-safe atomic refcounting |
| `channel` | ✅ Yes | Thread-safe atomic refcounting |
| Primitives | ❌ No | Stack-allocated, no heap allocation |

### Why This Design?

This hybrid approach gives you:
- **Explicit control** - You decide when to deallocate
- **Safety from scope bugs** - Reassignment doesn't leak
- **Predictable performance** - No GC pauses
- **Closure support** - Functions can safely capture variables

The philosophy remains: you're in control, but the runtime helps prevent common bugs like leaking on reassignment or double-freeing in containers.

## The Two Pointer Types

Hemlock provides two distinct pointer types, each with different safety characteristics:

### `ptr` - Raw Pointer (Dangerous)

Raw pointers are **just addresses** with minimal safety guarantees:

```hemlock
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);  // You must remember to free
```

**Characteristics:**
- Just an 8-byte address
- No bounds checking
- No length tracking
- User manages lifetime entirely
- For experts and FFI

**Use cases:**
- Low-level system programming
- Foreign Function Interface (FFI)
- Performance-critical code
- When you need complete control

**Dangers:**
```hemlock
let p = alloc(10);
let q = p + 100;  // Way past allocation - allowed but dangerous
free(p);
let x = *p;       // Dangling pointer - undefined behavior
free(p);          // Double-free - will crash
```

### `buffer` - Safe Wrapper (Recommended)

Buffers provide **bounds-checked access** while still requiring manual deallocation:

```hemlock
let b: buffer = buffer(64);
b[0] = 65;              // bounds checked
print(b.length);        // 64
free(b);                // still manual
```

**Characteristics:**
- Pointer + length + capacity
- Bounds checked on access
- Still requires manual `free()`
- Better default for most code

**Properties:**
```hemlock
let buf = buffer(100);
print(buf.length);      // 100 (current size)
print(buf.capacity);    // 100 (allocated capacity)
```

**Bounds checking:**
```hemlock
let buf = buffer(10);
buf[5] = 42;      // OK
buf[100] = 42;    // ERROR: Index out of bounds
```

## Memory API

### Core Allocation

**`alloc(bytes)` - Allocate raw memory**
```hemlock
let p = alloc(1024);  // Allocate 1KB, returns ptr
// ... use memory
free(p);
```

**`buffer(size)` - Allocate safe buffer**
```hemlock
let buf = buffer(256);  // Allocate 256-byte buffer
buf[0] = 65;            // 'A'
buf[1] = 66;            // 'B'
free(buf);
```

**`free(ptr)` - Free memory**
```hemlock
let p = alloc(100);
free(p);  // Must free to avoid memory leak

let buf = buffer(100);
free(buf);  // Works on both ptr and buffer
```

**Important:** `free()` works on both `ptr` and `buffer` types.

### Memory Operations

**`memset(ptr, byte, size)` - Fill memory**
```hemlock
let p = alloc(100);
memset(p, 0, 100);     // Zero out 100 bytes
memset(p, 65, 10);     // Fill first 10 bytes with 'A'
free(p);
```

**`memcpy(dest, src, size)` - Copy memory**
```hemlock
let src = alloc(50);
let dst = alloc(50);
memset(src, 42, 50);
memcpy(dst, src, 50);  // Copy 50 bytes from src to dst
free(src);
free(dst);
```

**`realloc(ptr, size)` - Resize allocation**
```hemlock
let p = alloc(100);
// ... use 100 bytes
p = realloc(p, 200);   // Resize to 200 bytes
// ... use 200 bytes
free(p);
```

**Note:** After `realloc()`, the old pointer may be invalid. Always use the returned pointer.

### Typed Allocation

Hemlock provides typed allocation helpers for convenience:

```hemlock
let arr = talloc(i32, 100);  // Allocate 100 i32 values (400 bytes)
let size = sizeof(i32);      // Returns 4 (bytes)
```

**`sizeof(type)`** returns the size in bytes of a type:
- `sizeof(i8)` / `sizeof(u8)` → 1
- `sizeof(i16)` / `sizeof(u16)` → 2
- `sizeof(i32)` / `sizeof(u32)` / `sizeof(f32)` → 4
- `sizeof(i64)` / `sizeof(u64)` / `sizeof(f64)` → 8
- `sizeof(ptr)` → 8 (on 64-bit systems)

**`talloc(type, count)`** allocates `count` elements of `type`:

```hemlock
let ints = talloc(i32, 10);   // 40 bytes for 10 i32 values
let floats = talloc(f64, 5);  // 40 bytes for 5 f64 values
free(ints);
free(floats);
```

## Common Patterns

### Pattern: Allocate, Use, Free

The basic pattern for memory management:

```hemlock
// 1. Allocate
let data = alloc(1024);

// 2. Use
memset(data, 0, 1024);
// ... do work

// 3. Free
free(data);
```

### Pattern: Safe Buffer Usage

Prefer buffers for bounds-checked access:

```hemlock
let buf = buffer(256);

// Safe iteration
let i = 0;
while (i < buf.length) {
    buf[i] = i;
    i = i + 1;
}

free(buf);
```

### Pattern: Resource Management with try/finally

Ensure cleanup even on errors:

```hemlock
let data = alloc(1024);
try {
    // ... risky operations
    process(data);
} finally {
    free(data);  // Always freed, even on error
}
```

## Memory Safety Considerations

### Double-Free

**Allowed but will crash:**
```hemlock
let p = alloc(100);
free(p);
free(p);  // CRASH: Double-free detected
```

**Prevention:**
```hemlock
let p = alloc(100);
free(p);
p = null;  // Set to null after freeing

if (p != null) {
    free(p);  // Won't execute
}
```

### Dangling Pointers

**Allowed but undefined behavior:**
```hemlock
let p = alloc(100);
*p = 42;      // OK
free(p);
let x = *p;   // UNDEFINED: Reading freed memory
```

**Prevention:** Don't access memory after freeing.

### Memory Leaks

**Easy to create, hard to debug:**
```hemlock
fn leak_memory() {
    let p = alloc(1000);
    // Forgot to free!
    return;  // Memory leaked
}
```

**Prevention:** Always pair `alloc()` with `free()`:
```hemlock
fn safe_function() {
    let p = alloc(1000);
    try {
        // ... use p
    } finally {
        free(p);  // Always freed
    }
}
```

### Pointer Arithmetic

**Allowed but dangerous:**
```hemlock
let p = alloc(10);
let q = p + 100;  // Way past allocation boundary
*q = 42;          // UNDEFINED: Out of bounds write
free(p);
```

**Use buffers for bounds checking:**
```hemlock
let buf = buffer(10);
buf[100] = 42;  // ERROR: Bounds check prevents overflow
```

## Best Practices

1. **Default to `buffer`** - Use `buffer` unless you specifically need raw `ptr`
2. **Match alloc/free** - Every `alloc()` should have exactly one `free()`
3. **Use try/finally** - Ensure cleanup with exception handling
4. **Null after free** - Set pointers to `null` after freeing to catch use-after-free
5. **Bounds check** - Use buffer indexing for automatic bounds checking
6. **Document ownership** - Make clear which code owns and frees each allocation

## Examples

### Example: Dynamic String Builder

```hemlock
fn build_message(count: i32): ptr {
    let size = count * 10;
    let buf = alloc(size);

    let i = 0;
    while (i < count) {
        memset(buf + (i * 10), 65 + i, 10);
        i = i + 1;
    }

    return buf;  // Caller must free
}

let msg = build_message(5);
// ... use msg
free(msg);
```

### Example: Safe Array Operations

```hemlock
fn process_array(size: i32) {
    let arr = buffer(size);

    try {
        // Fill array
        let i = 0;
        while (i < arr.length) {
            arr[i] = i * 2;
            i = i + 1;
        }

        // Process
        i = 0;
        while (i < arr.length) {
            print(arr[i]);
            i = i + 1;
        }
    } finally {
        free(arr);  // Always cleanup
    }
}
```

### Example: Memory Pool Pattern

```hemlock
// Simple memory pool (simplified)
let pool = alloc(10000);
let pool_offset = 0;

fn pool_alloc(size: i32): ptr {
    if (pool_offset + size > 10000) {
        throw "Pool exhausted";
    }

    let ptr = pool + pool_offset;
    pool_offset = pool_offset + size;
    return ptr;
}

// Use pool
let p1 = pool_alloc(100);
let p2 = pool_alloc(200);

// Free entire pool at once
free(pool);
```

## Limitations

Current limitations to be aware of:

- **Raw pointers require manual free** - `alloc()` returns `ptr` with no refcounting
- **No custom allocators** - Only system malloc/free

**Note:** Refcounted types (string, array, object, buffer) ARE automatically freed when scope exits. Only raw `ptr` from `alloc()` requires explicit `free()`.

## Related Topics

- [Strings](strings.md) - String memory management and UTF-8 encoding
- [Arrays](arrays.md) - Dynamic arrays and their memory characteristics
- [Objects](objects.md) - Object allocation and lifetime
- [Error Handling](error-handling.md) - Using try/finally for cleanup

## See Also

- **Design Philosophy**: See CLAUDE.md section "Memory Management"
- **Type System**: See [Types](types.md) for `ptr` and `buffer` type details
- **FFI**: Raw pointers are essential for Foreign Function Interface
