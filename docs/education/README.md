# Learning Systems Programming with Hemlock

> "Hemlock exists to teach you what your computer is actually doing."

This educational guide positions Hemlock as a **teaching language for systems programming concepts**. Whether you're coming from Python, JavaScript, or another high-level language, Hemlock provides a gentle but honest introduction to how computers actually manage memory and execute code.

---

## Why Hemlock for Learning?

### The Problem with Learning C

C is the traditional path to systems programming, but it presents several pedagogical challenges:

1. **Compilation complexity** - Header files, makefiles, linker errors distract from concepts
2. **Cryptic errors** - Segfaults give no context; debugging requires external tools
3. **No guardrails** - Undefined behavior can silently corrupt memory
4. **Boilerplate overhead** - `#include`, `main()` signatures, type declarations everywhere

### The Problem with Staying High-Level

Python and JavaScript hide fundamental concepts:

1. **Memory is invisible** - Garbage collection prevents understanding allocation
2. **Types are hidden** - Dynamic typing masks how data is represented
3. **Pointers don't exist** - References are abstracted away
4. **Performance is magic** - No model for why code is fast or slow

### Hemlock: The Middle Path

Hemlock gives you **manual control with modern ergonomics**:

```hemlock
// Simple syntax, explicit memory
let p = alloc(64);     // You allocate
memset(p, 0, 64);      // You initialize
// ... use memory ...
free(p);               // You clean up
```

**What Hemlock offers:**
- **Scripting syntax** - No headers, no makefiles, just `.hml` files
- **Helpful errors** - Runtime errors include context and line numbers
- **Optional safety** - Use `buffer` for bounds checking, `ptr` for raw access
- **Dual backends** - Interpret for rapid iteration, compile for understanding generated code
- **Real parallelism** - pthreads under the hood, not green threads

---

## The Hemlock Learning Philosophy

### 1. Make the Invisible Visible

High-level languages hide memory. Hemlock makes it explicit:

```hemlock
// In Python: x = [1, 2, 3]  -- Where does this live? How big is it?

// In Hemlock: You decide
let arr = [1, 2, 3];           // Dynamic array (managed)
let p = alloc(3 * 4);          // Raw memory: 3 integers × 4 bytes
ptr_write_i32(p, 1);           // You write each value
ptr_write_i32(p + 4, 2);       // Pointer arithmetic is explicit
ptr_write_i32(p + 8, 3);
free(p);                       // You clean up
```

### 2. Unsafe is Educational

Hemlock lets you make mistakes safely:

```hemlock
// This WILL crash - and that's the lesson
let p = alloc(8);
free(p);
free(p);  // Double-free: crashes with error message
```

In C, this might silently corrupt memory. In Hemlock, you get an error you can learn from.

### 3. Graduate from Training Wheels

Hemlock provides a safety spectrum:

```hemlock
// Level 1: Managed arrays (safest)
let arr = [1, 2, 3];
arr[10];  // Runtime error: Index out of bounds

// Level 2: Bounds-checked buffers
let b = buffer(64);
b[100];   // Runtime error: Buffer bounds exceeded

// Level 3: Raw pointers (full control)
let p = alloc(64);
// p[100] - No checking. Your responsibility. Real systems programming.
```

### 4. Types Tell Stories

Every value in Hemlock carries its type at runtime:

```hemlock
let x = 42;
print(typeof(x));  // "i32" - 32-bit signed integer

let y = 5000000000;
print(typeof(y));  // "i64" - Too big for i32, auto-promoted

let z = 3.14;
print(typeof(z));  // "f64" - Floating point
```

This teaches you to think about data representation.

---

## Learning Tracks

### Track 1: Memory Fundamentals (Start Here)
*For programmers from GC languages*

1. [Why Manual Memory Matters](./why-memory-matters.md)
2. [Your First Allocation](./exercises/01-first-allocation.md)
3. [The Stack vs The Heap](./exercises/02-stack-vs-heap.md)
4. [Pointers Are Just Numbers](./exercises/03-pointers-explained.md)
5. [Common Memory Mistakes](./common-mistakes.md)

### Track 2: Types and Representation
*Understanding how data lives in memory*

1. [Numeric Types and Their Ranges](./exercises/04-numeric-types.md)
2. [Type Promotion and Coercion](./exercises/05-type-promotion.md)
3. [Strings Are Byte Arrays](./exercises/06-strings-as-bytes.md)
4. [Structs and Memory Layout](./exercises/07-memory-layout.md)

### Track 3: Concurrent Thinking
*Real parallelism, real problems*

1. [Tasks and Threads](./exercises/08-tasks-intro.md)
2. [Channels for Communication](./exercises/09-channels.md)
3. [Race Conditions (On Purpose)](./exercises/10-race-conditions.md)
4. [Atomics and Lock-Free Code](./exercises/11-atomics.md)

### Track 4: Systems Integration
*Connecting to the real world*

1. [Calling C Functions (FFI)](./exercises/12-ffi-basics.md)
2. [Working with Files](./exercises/13-file-io.md)
3. [Handling Signals](./exercises/14-signals.md)
4. [Building Real Tools](./exercises/15-real-tools.md)

---

## Quick Comparison: Python vs Hemlock

| Concept | Python | Hemlock |
|---------|--------|---------|
| Create a list | `x = [1, 2, 3]` | `let x = [1, 2, 3];` |
| Allocate memory | *(hidden)* | `let p = alloc(64);` |
| Free memory | *(GC does it)* | `free(p);` |
| Get a pointer | *(not possible)* | `let p = &x;` |
| Type of value | `type(x)` | `typeof(x)` |
| String length | `len(s)` | `len(s)` or `s.length` |
| Check bounds | *(automatic)* | Use `buffer` or check manually |
| Run in parallel | `threading`/`multiprocessing` | `spawn(fn)` (real threads) |
| Call C code | `ctypes`/`cffi` | `extern fn` or `ffi_bind` |

---

## Pedagogical Features

### 1. Introspection Everywhere

```hemlock
// Always know what you're working with
let x = 42;
print(typeof(x));     // "i32"
print(sizeof(i32));   // 4 (bytes)

let arr = [1, 2, 3];
print(len(arr));      // 3
print(typeof(arr));   // "array"
```

### 2. Explicit Type Annotations (Optional)

```hemlock
// Types are checked but not required
let x = 42;              // Type inferred as i32
let y: i64 = 42;         // Explicitly i64
let z: i32 = "hello";    // Runtime error: Type mismatch
```

### 3. Memory Tracking

```hemlock
import { memory_stats } from "@stdlib/env";

let p1 = alloc(100);
let p2 = alloc(200);

let stats = memory_stats();
print("Allocated: " + stats.allocated + " bytes");
print("Allocations: " + stats.allocations);

free(p1);
free(p2);
```

### 4. Helpful Runtime Errors

```hemlock
// Instead of "Segmentation fault (core dumped)"
// You get:

// Error at line 15: Double-free detected
// Error at line 23: Index 10 out of bounds for array of length 3
// Error at line 31: Null pointer dereference
// Error at line 42: Type mismatch: expected i32, got string
```

---

## Setting Up Your Learning Environment

### Installation

```bash
git clone https://github.com/hemlang/hemlock.git
cd hemlock
make
```

### Running Programs

```bash
# Interpret (fast iteration)
./hemlock program.hml

# Compile (see generated C)
./hemlockc program.hml -o program --keep-c
cat program.c  # Examine generated code
./program
```

### Recommended Workflow

1. **Write** your `.hml` file
2. **Interpret** first to verify correctness
3. **Compile** to see the C translation
4. **Read** the generated C to understand what happens

---

## The Journey

```
┌─────────────────────────────────────────────────────────────────┐
│                    Your Learning Journey                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Python/JS ──► Hemlock (managed) ──► Hemlock (raw) ──► C        │
│                                                                  │
│  "I don't      "I see where      "I control      "I understand  │
│   think about   memory lives"     every byte"     everything"   │
│   memory"                                                        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

Hemlock is a **bridge language**. It's not meant to replace C for production systems work. It's meant to teach you the concepts that make C comprehensible.

---

## Next Steps

- **New to systems programming?** Start with [Why Manual Memory Matters](./why-memory-matters.md)
- **Coming from Python/JS?** Read [From High-Level to Systems](./from-high-level.md)
- **Want exercises?** Jump to [Memory Exercises](./exercises/)
- **Just want to code?** See the [examples/](../../examples/) directory

---

## Educational Resources

| Resource | Description |
|----------|-------------|
| [exercises/](./exercises/) | Hands-on exercises with solutions |
| [common-mistakes.md](./common-mistakes.md) | Mistakes and how to debug them |
| [why-memory-matters.md](./why-memory-matters.md) | Conceptual foundation |
| [from-high-level.md](./from-high-level.md) | Bridge guide for Python/JS devs |
| [curriculum.md](./curriculum.md) | Structured learning path |

---

*"The goal isn't to make systems programming easy. The goal is to make it understandable."*
