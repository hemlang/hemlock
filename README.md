![Hemlock](logo.png)

# Hemlock

> A small, unsafe language for writing unsafe things safely.

Hemlock is a systems scripting language that combines the power of C with the ergonomics of modern scripting languages. It embraces manual memory management and explicit control while providing structured async concurrency built-in.

## Design Philosophy

- **Explicit over implicit** - No hidden behavior, no magic
- **Manual memory management** - You allocate, you free
- **Dynamic by default, typed by choice** - Optional type annotations with runtime checking
- **Unsafe is a feature** - Full control when you need it, safety tools when you want them

## Features

- **Familiar syntax** - C-like with modern improvements
- **Rich type system** - i8-i64, u8-u64, f32/f64, bool, string, rune, ptr, buffer, array, object, null
- **First-class functions** - Closures, recursion, higher-order functions
- **Objects** - JavaScript-style objects with methods, duck typing, optional fields, and JSON serialization
- **Two pointer types** - Raw `ptr` for experts, safe `buffer` with bounds checking
- **Memory API** - alloc, free, memset, memcpy, realloc, talloc, sizeof
- **UTF-8 strings** - First-class mutable strings with 18 methods (substr, slice, find, split, trim, to_upper, etc.)
- **Dynamic arrays** - 15 array methods (push, pop, shift, unshift, insert, remove, slice, join, etc.)
- **Rune type** - Unicode codepoints as distinct 32-bit type with full escape sequence support
- **Error handling** - try/catch/finally/throw + panic() for unrecoverable errors
- **File I/O** - File object API with methods and properties
- **Signal handling** - POSIX signals with signal() and raise() functions
- **Command execution** - exec() function to run shell commands and capture output
- **Command-line arguments** - Access program arguments via built-in `args` array
- **Structured concurrency** - async/await syntax, real OS threads via pthreads, thread-safe channels
- **Foreign Function Interface** - Call C functions from shared libraries directly

## Quick Examples

### Hello World
```hemlock
print("Hello, World!");
```

### Memory Management
```hemlock
// Raw pointer (dangerous but flexible)
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);

// Safe buffer (bounds checked)
let buf: buffer = buffer(64);
buf[0] = 65;
print(buf.length);  // 64
free(buf);
```

### Type System
```hemlock
let x = 42;                   // i32 inferred (small value)
let big = 5000000000;         // i64 inferred (large value)
let y: u8 = 255;              // explicit u8
let z = x + y;                // promotes to i32
let pi: f64 = 3.14159;        // explicit precision

// Rune type - Unicode codepoints
let ch = 'A';                 // ASCII character
let emoji = '🚀';             // Multi-byte emoji
let unicode = '\u{1F680}';    // Unicode escape
```

### String Operations
```hemlock
// Mutable UTF-8 strings
let s = "hello";
s[0] = 'H';                      // Mutate with rune
print(s.length);                 // 5 (codepoint count)

// Rich string methods (18 total)
let upper = s.to_upper();        // "HELLO"
let slice = s.slice(1, 4);       // "ell"
let parts = "a,b,c".split(",");  // ["a", "b", "c"]
let trimmed = "  text  ".trim(); // "text"
let replaced = s.replace("lo", "p"); // "help"
let idx = s.find("ll");          // 2

// Unicode support
let emoji = "🚀";
print(emoji.length);             // 1 (one codepoint)
print(emoji.byte_length);        // 4 (UTF-8 bytes)
```

### Functions and Closures
```hemlock
// Named function with recursion
fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

// Closures capture their environment
fn makeAdder(x) {
    return fn(y) {
        return x + y;
    };
}

let add5 = makeAdder(5);
print(add5(3));  // 8
```

### Objects
```hemlock
// Anonymous objects
let person = { name: "Alice", age: 30 };
print(person.name);

// Methods with self keyword
let counter = {
    count: 0,
    increment: fn() {
        self.count = self.count + 1;
    }
};
counter.increment();
print(counter.count);  // 1

// Type definitions with duck typing
define Person {
    name: string,
    age: i32,
    active?: true,  // Optional field with default
}

let p = { name: "Bob", age: 25 };
let typed_p: Person = p;  // Duck typing validates structure
print(typeof(typed_p));   // "Person"

// JSON serialization
let json = person.serialize();
print(json);  // {"name":"Alice","age":30}
let restored = json.deserialize();
```

### Dynamic Arrays
```hemlock
// Dynamic arrays with 15 methods
let arr = [1, 2, 3];
arr.push(4);                    // [1, 2, 3, 4]
let last = arr.pop();           // 4
arr.unshift(0);                 // [0, 1, 2, 3]

// Array manipulation
arr.insert(2, 99);              // Insert at index
let removed = arr.remove(0);    // Remove at index
arr.reverse();                  // Reverse in-place

// Search and slicing
let idx = arr.find(99);         // Find index
let sub = arr.slice(1, 3);      // Extract subarray
let joined = arr.join(", ");    // "3, 99, 1"
let combined = arr.concat([5, 6]); // Combine arrays
```

### Command-Line Arguments
```hemlock
// Access arguments via the built-in 'args' array
// args[0] is the script name
// args[1+] are the actual arguments

print("Script: " + args[0]);
print("Total arguments: " + typeof(args.length));

if (args.length > 1) {
    let i = 1;
    while (i < args.length) {
        print(args[i]);
        i = i + 1;
    }
}
```

Run with:
```bash
./hemlock script.hml arg1 arg2 "argument with spaces"
```

### Error Handling
```hemlock
// Basic try/catch
try {
    throw "something went wrong";
} catch (e) {
    print("Error: " + e);
}

// Try/finally (cleanup always runs)
try {
    print("Attempting operation...");
} finally {
    print("Cleanup");
}

// Try/catch/finally
fn divide(a, b) {
    if (b == 0) {
        throw "division by zero";
    }
    return a / b;
}

try {
    let result = divide(10, 0);
    print(result);
} catch (e) {
    print("Caught: " + e);
} finally {
    print("Done");
}

// Any value can be thrown
try {
    throw { code: 404, message: "Not found" };
} catch (e) {
    print(serialize(e));  // {"code":404,"message":"Not found"}
}

// Re-throwing
try {
    try {
        throw "inner error";
    } catch (e) {
        print("Logging: " + e);
        throw e;  // Re-throw to outer handler
    }
} catch (e) {
    print("Outer: " + e);
}

// Unrecoverable errors
panic("critical error");  // Exits program immediately
```

### Async/Concurrency (TRUE Multi-Threading!)
```hemlock
// Async functions for concurrent execution
async fn compute(n: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}

// Spawn tasks on separate OS threads (real pthreads!)
let t1 = spawn(compute, 1000);
let t2 = spawn(compute, 2000);
let t3 = spawn(compute, 3000);

// Wait for results (all three computing in parallel!)
let r1 = join(t1);
let r2 = join(t2);
let r3 = join(t3);

print("Results: " + typeof(r1) + ", " + typeof(r2) + ", " + typeof(r3));
```

**Producer-Consumer with Channels:**
```hemlock
async fn producer(ch, count: i32) {
    let i = 0;
    while (i < count) {
        ch.send(i * 10);
        i = i + 1;
    }
    ch.close();
    return null;
}

async fn consumer(ch): i32 {
    let sum = 0;
    let val = ch.recv();
    while (val != null) {
        sum = sum + val;
        val = ch.recv();
    }
    return sum;
}

let ch = channel(10);
let p = spawn(producer, ch, 5);
let c = spawn(consumer, ch);

join(p);
let total = join(c);  // 100
print("Total: " + typeof(total));
```

**What makes this special:**
- ✅ Real OS threads (POSIX pthreads) - not green threads or coroutines
- ✅ True parallelism across multiple CPU cores
- ✅ Thread-safe channels with blocking send/recv
- ✅ Exception propagation across thread boundaries
- ✅ Same threading model as C/C++/Rust

### Foreign Function Interface (FFI)
```hemlock
// Call C functions from shared libraries
import "libc.so.6";

extern fn strlen(s: string): i32;
extern fn getpid(): i32;

let msg = "Hello from Hemlock!";
let len = strlen(msg);
let pid = getpid();

print("String length: " + typeof(len));  // 19
print("Process ID: " + typeof(pid));     // e.g., 5086
```

**Supported types:**
- Primitive types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64
- Special types: bool, string, ptr, void
- Automatic type conversion between Hemlock and C
- Thread-safe library loading

### File I/O
```hemlock
// File object API
let f = open("data.txt", "r");
let content = f.read();
print(content);
f.close();

// Write to file
let out = open("output.txt", "w");
out.write("Hello, World!\n");
out.close();

// File properties
print(f.path);    // "/path/to/file"
print(f.mode);    // "r"
print(f.closed);  // false
```

### Signal Handling
```hemlock
// Handle Ctrl+C gracefully
let running = true;

fn handle_interrupt(sig) {
    print("Caught SIGINT!");
    running = false;
}

signal(SIGINT, handle_interrupt);

// User-defined signals
signal(SIGUSR1, fn(sig) {
    print("Received SIGUSR1");
});

raise(SIGUSR1);  // Trigger handler
```

### Command Execution
```hemlock
// Execute shell commands and capture output
let result = exec("ls -la");
print(result.output);
print("Exit code: " + typeof(result.exit_code));

// Handle command failures
let r = exec("grep pattern file.txt");
if (r.exit_code == 0) {
    print("Found: " + r.output);
} else {
    print("Not found");
}
```

## Building

### Dependencies

On Ubuntu/Debian:
```bash
sudo apt-get install libffi-dev libssl-dev libwebsockets-dev
```

**Required libraries:**
- `libffi-dev` - Foreign Function Interface for FFI support
- `libssl-dev` - OpenSSL for cryptographic hash functions (md5, sha1, sha256)
- `libwebsockets-dev` - WebSocket and HTTP client/server support

On other Linux distributions, install the equivalent development packages.

### Compile

```bash
make
```

## Running Tests

```bash
make test
```

## Running Programs

```bash
# Run a program
./hemlock program.hml

# Run with command-line arguments
./hemlock program.hml arg1 arg2 arg3

# Start REPL
./hemlock
```

## Project Status

Hemlock is currently in early development (v0.1). All 251 tests passing! The following features are implemented:

**Type System:**
- ✅ Primitives: i8-i64, u8-u64, f32/f64, bool, string, rune, null
- ✅ Complex types: ptr, buffer, array, object, file, task, channel
- ✅ 64-bit integer support (i64/u64) with full type promotion
- ✅ Rune type for Unicode codepoints (U+0000 to U+10FFFF)

**Memory Management:**
- ✅ Manual allocation (alloc, free, talloc, realloc, memset, memcpy, sizeof)
- ✅ Two pointer types: raw `ptr` and safe `buffer`

**Data Structures:**
- ✅ UTF-8 strings with 18 methods (mutable, codepoint-based indexing)
- ✅ Dynamic arrays with 15 methods (push, pop, slice, join, etc.)
- ✅ Objects with methods, duck typing, optional fields, JSON serialization

**Control Flow:**
- ✅ if/else, while, for, for-in, break, continue, switch
- ✅ Bitwise operators (&, |, ^, <<, >>, ~)

**Functions:**
- ✅ First-class functions, closures, recursion, lexical scoping

**Error Handling:**
- ✅ try/catch/finally/throw exception handling
- ✅ panic() for unrecoverable errors

**I/O & System:**
- ✅ File object API (read, write, seek, tell, close)
- ✅ Signal handling (POSIX signals with 15 constants)
- ✅ Command execution (exec() with output capture)
- ✅ Command-line arguments (built-in `args` array)

**Concurrency:**
- ✅ async/await syntax, structured concurrency
- ✅ Real OS threads via pthreads (true parallelism)
- ✅ Thread-safe channels (send/recv/close)
- ✅ Task management (spawn/join/detach)
- ✅ Exception propagation across threads

**Foreign Function Interface:**
- ✅ Call C functions from shared libraries (libffi)
- ✅ Full primitive type support (including i64/u64)
- ✅ Automatic type conversion
- ✅ Thread-safe library loading

## Why Hemlock?

Hemlock is for programmers who want:
- The power of C without the boilerplate
- Manual memory management without the verbosity
- Dynamic types with optional static guarantees
- Structured concurrency without the complexity

Hemlock is **NOT** memory-safe. Dangling pointers, use-after-free, and buffer overflows are your responsibility. We provide tools to help (`buffer`, type annotations, bounds checking) but we don't force you to use them.

## Philosophy on Safety

> "We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations). The default should guide toward safety, but the escape hatch should always be available."

## License

MIT License

## Contributing

Hemlock is experimental and evolving. If you're interested in contributing, please read `CLAUDE.md` first to understand the design philosophy.
