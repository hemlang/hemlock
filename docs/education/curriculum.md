# Hemlock Learning Curriculum

A structured learning path for mastering systems programming concepts through Hemlock.

---

## Who Is This For?

This curriculum is designed for programmers who:
- Know at least one high-level language (Python, JavaScript, Ruby, etc.)
- Want to understand how computers actually manage memory
- Are curious about systems programming but intimidated by C
- Want to build a foundation for learning Rust, C, or low-level programming

---

## How to Use This Curriculum

1. **Follow the modules in order** - Each builds on the previous
2. **Do the exercises** - Reading isn't enough; you need hands-on practice
3. **Run the code** - Type it yourself, don't just copy-paste
4. **Make mistakes** - Crashes are learning opportunities
5. **Use both backends** - Interpret for quick iteration, compile to see generated C

---

## Module 0: Getting Started

**Duration:** 1-2 hours

### Learning Objectives
- Install Hemlock
- Write and run your first program
- Understand the interpreter vs compiler

### Content
1. **Installation**
   ```bash
   git clone https://github.com/hemlang/hemlock.git
   cd hemlock
   make
   ```

2. **Hello World**
   ```hemlock
   // hello.hml
   print("Hello, Systems Programming!");
   ```

3. **Running Programs**
   ```bash
   ./hemlock hello.hml              # Interpret
   ./hemlockc hello.hml -o hello    # Compile
   ./hello                          # Run compiled
   ```

### Exercises
- [ ] Install Hemlock successfully
- [ ] Write a program that prints your name
- [ ] Compile the program and examine the binary

### Resources
- [Installation Guide](../getting-started/installation.md)
- [Quick Start](../getting-started/quick-start.md)

---

## Module 1: Hemlock Syntax Basics

**Duration:** 2-3 hours

### Learning Objectives
- Variables, types, and operators
- Control flow (if, while, for)
- Functions and closures
- Arrays and objects

### Content

**Variables and Types:**
```hemlock
let x = 42;           // i32 (inferred)
let y: i64 = 100;     // i64 (explicit)
let name = "Alice";   // string
let active = true;    // bool
let pi = 3.14159;     // f64
```

**Control Flow:**
```hemlock
if (x > 0) {
    print("positive");
} else {
    print("non-positive");
}

for (let i = 0; i < 10; i++) {
    print(i);
}

for (item in [1, 2, 3]) {
    print(item);
}
```

**Functions:**
```hemlock
fn add(a: i32, b: i32): i32 {
    return a + b;
}

let double = fn(x) => x * 2;
```

### Exercises
- [ ] Write a function that calculates factorial recursively
- [ ] Create an array and find its maximum value
- [ ] Build an object representing a person with name and age

### Resources
- [Tutorial](../getting-started/tutorial.md)
- [Syntax Guide](../language-guide/syntax.md)

---

## Module 2: Understanding Memory

**Duration:** 4-6 hours (core module)

### Learning Objectives
- What memory is and how it's organized
- Stack vs heap allocation
- Pointers as addresses
- Why memory management matters

### Content

**Read First:**
- [Why Manual Memory Matters](./why-memory-matters.md)

**Key Concepts:**
1. Memory is a giant array of bytes
2. Every byte has an address (number)
3. A pointer is just an address
4. Stack: automatic, fast, limited
5. Heap: manual, flexible, unlimited

**Core Functions:**
```hemlock
alloc(n)              // Request n bytes from heap
free(p)               // Return memory to heap
ptr_read_i32(p)       // Read 4 bytes as i32
ptr_write_i32(p, v)   // Write i32 to address
sizeof(type)          // Size of type in bytes
memset(p, val, n)     // Set n bytes to val
memcpy(dst, src, n)   // Copy n bytes
```

### Exercises (Complete All)
- [ ] [Exercise 1: Your First Allocation](./exercises/01-first-allocation.md)
- [ ] [Exercise 2: Stack vs Heap](./exercises/02-stack-vs-heap.md)
- [ ] [Exercise 3: Pointers Explained](./exercises/03-pointers-explained.md)

### Milestone Project
**Build a Ring Buffer:** A fixed-size circular queue that overwrites old data when full.
- Allocate buffer on heap
- Track head and tail pointers
- Implement push and pop operations
- Free buffer when done

---

## Module 3: Memory Safety Patterns

**Duration:** 3-4 hours

### Learning Objectives
- Recognize common memory bugs
- Use `defer` for automatic cleanup
- Develop defensive coding habits
- Debug memory issues

### Content

**Read First:**
- [Common Mistakes and Debugging](./common-mistakes.md)

**Key Patterns:**

1. **Defer for Cleanup:**
   ```hemlock
   fn safe_processing() {
       let p = alloc(100);
       defer free(p);  // Guaranteed cleanup
       // ... work ...
   }  // defer runs here
   ```

2. **Null After Free:**
   ```hemlock
   free(p);
   p = ptr_null();  // Prevent use-after-free
   ```

3. **Check Before Dereference:**
   ```hemlock
   if (p != ptr_null()) {
       let value = ptr_read_i32(p);
   }
   ```

### Exercises
- [ ] Find and fix the bugs in a provided program
- [ ] Add defensive checks to your ring buffer
- [ ] Implement a simple memory leak detector

### Resources
- [Memory Guide](../language-guide/memory.md)

---

## Module 4: Types in Depth

**Duration:** 2-3 hours

### Learning Objectives
- Numeric type ranges and sizes
- Type promotion and coercion
- String internals (UTF-8, bytes vs characters)
- Custom types with `define`

### Content

**Numeric Types:**
| Type | Size | Range |
|------|------|-------|
| i8   | 1B   | -128 to 127 |
| i16  | 2B   | -32,768 to 32,767 |
| i32  | 4B   | ±2 billion |
| i64  | 8B   | ±9 quintillion |
| u8   | 1B   | 0 to 255 |
| f32  | 4B   | ~7 decimal digits |
| f64  | 8B   | ~15 decimal digits |

**String Internals:**
```hemlock
let s = "hello 🚀";
print(len(s));       // 10 (bytes)
print(s.length);     // 7 (characters)
print(s.byte_at(6)); // First byte of emoji
```

**Custom Types:**
```hemlock
define Point {
    x: f64,
    y: f64
}

define HasName {
    name: string,
    fn greet(): string
}
```

### Exercises
- [ ] Explore integer overflow behavior
- [ ] Write a function that counts emoji in a string
- [ ] Create a compound type for a game character

---

## Module 5: Concurrency Fundamentals

**Duration:** 4-5 hours

### Learning Objectives
- Tasks and real parallelism
- Race conditions and why they happen
- Atomic operations
- Channels for communication

### Content

**Run First:**
- [examples/educational/concurrency_basics.hml](../../examples/educational/concurrency_basics.hml)

**Key Concepts:**
1. `spawn` creates real OS threads
2. Shared mutable state causes race conditions
3. Atomics provide lock-free synchronization
4. Channels allow safe message passing

**Core Functions:**
```hemlock
spawn(fn, args...)    // Start parallel task
join(task)            // Wait for result
detach(task)          // Fire and forget
channel(size)         // Create channel
ch.send(value)        // Send message
ch.recv()             // Receive message
atomic_add_i32(p, n)  // Atomic addition
```

### Exercises
- [ ] Write a parallel map function
- [ ] Implement a thread-safe counter with atomics
- [ ] Build a producer-consumer pipeline with channels

### Resources
- [Async/Concurrency Guide](../advanced/async-concurrency.md)
- [Atomics Reference](../advanced/atomics.md)

---

## Module 6: Error Handling

**Duration:** 2-3 hours

### Learning Objectives
- try/catch/finally for recoverable errors
- panic for unrecoverable errors
- Error propagation patterns
- Using defer with errors

### Content

```hemlock
try {
    let result = risky_operation();
} catch (e) {
    print("Caught error: " + e);
} finally {
    cleanup();  // Always runs
}

// For unrecoverable errors
if (critical_failure) {
    panic("Cannot continue: " + reason);
}
```

**Pattern - Defer with Errors:**
```hemlock
fn process_file(path: string) {
    let f = open(path, "r");
    defer f.close();  // Runs even if exception thrown

    try {
        let data = f.read();
        process(data);
    } catch (e) {
        print("Error processing: " + e);
    }
}  // f.close() called here
```

### Exercises
- [ ] Write a function that validates input and throws on error
- [ ] Implement retry logic for flaky operations
- [ ] Create a RAII-style resource wrapper

---

## Module 7: File I/O and System Interaction

**Duration:** 2-3 hours

### Learning Objectives
- Reading and writing files
- Command-line arguments
- Environment variables
- Running shell commands

### Content

**File I/O:**
```hemlock
let f = open("data.txt", "r");
let content = f.read();
f.close();

// With defer
let f = open("output.txt", "w");
defer f.close();
f.write("Hello, file!");
```

**Command-Line Args:**
```hemlock
import { args } from "@stdlib/args";

let argv = args();
print("Program: " + argv[0]);
print("Arg 1: " + argv[1]);
```

### Exercises
- [ ] Write a file copy utility
- [ ] Create a simple grep-like search tool
- [ ] Build a command-line calculator

### Resources
- [File I/O Guide](../advanced/file-io.md)
- [Command Line Args](../advanced/command-line-args.md)

---

## Module 8: FFI (Foreign Function Interface)

**Duration:** 3-4 hours

### Learning Objectives
- Declare and call C functions
- Work with C strings and pointers
- Load shared libraries dynamically
- Safety considerations with FFI

### Content

```hemlock
// Static binding
import "libc.so.6";

extern fn strlen(s: string): i32;
extern fn getpid(): i32;

print("Length: " + strlen("Hello"));
print("PID: " + getpid());

// Dynamic binding
let lib = ffi_open("libm.so.6");
let sqrt = ffi_bind(lib, "sqrt", [FFI_DOUBLE], FFI_DOUBLE);
print(sqrt(2.0));
ffi_close(lib);
```

### Exercises
- [ ] Call math functions from libm
- [ ] Create a wrapper for a simple C library
- [ ] Implement a function that calls getenv/setenv

### Resources
- [FFI Guide](../advanced/ffi.md)

---

## Module 9: Building Real Programs

**Duration:** Ongoing

### Capstone Projects

Choose one or more to solidify your learning:

**1. Memory Allocator (Intermediate)**
Build a simple allocator on top of a fixed buffer.
- Implement `my_alloc` and `my_free`
- Track free blocks in a list
- Handle fragmentation

**2. Concurrent Web Scraper (Intermediate)**
Fetch multiple URLs in parallel.
- Use `spawn` for parallel requests
- Collect results with channels
- Handle errors gracefully

**3. Key-Value Store (Advanced)**
Build an in-memory database.
- Hash table implementation
- Thread-safe access with atomics
- Persistence to disk

**4. Simple Shell (Advanced)**
Build a basic command-line shell.
- Parse commands
- Fork and execute processes
- Handle signals (Ctrl+C)

---

## Assessment Checkpoints

### After Module 2 (Memory Basics)
You should be able to:
- [ ] Explain the difference between stack and heap
- [ ] Allocate memory, write to it, read from it, free it
- [ ] Identify what a dangling pointer is
- [ ] Draw a memory diagram showing pointer relationships

### After Module 5 (Concurrency)
You should be able to:
- [ ] Explain why race conditions happen
- [ ] Use atomics to prevent data races
- [ ] Implement a producer-consumer with channels
- [ ] Identify memory ownership issues with spawned tasks

### After Module 8 (FFI)
You should be able to:
- [ ] Call C standard library functions from Hemlock
- [ ] Understand memory layout for FFI compatibility
- [ ] Wrap a simple C library for use in Hemlock

---

## Quick Reference

### Memory Functions
```hemlock
alloc(n)           // Allocate n bytes
free(p)            // Free allocation
buffer(n)          // Bounds-checked buffer
talloc(type, n)    // Typed allocation
memset(p, v, n)    // Fill memory
memcpy(d, s, n)    // Copy memory
```

### Pointer Operations
```hemlock
ptr_null()         // Null pointer
&variable          // Address of variable
ptr_read_*type*(p) // Read typed value
ptr_write_*type*(p, v) // Write typed value
```

### Concurrency
```hemlock
spawn(fn, args)    // Start task
join(task)         // Wait for result
channel(size)      // Create channel
ch.send(v)         // Send message
ch.recv()          // Receive message
atomic_*           // Atomic operations
```

---

## Getting Help

- Read error messages carefully
- Check [Common Mistakes](./common-mistakes.md)
- Print intermediate values
- Use `hemlockc --keep-c` to see generated code
- Ask questions at https://github.com/hemlang/hemlock/issues

---

*"The journey of a thousand miles begins with a single allocation."*
