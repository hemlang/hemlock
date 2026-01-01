# Hemlock Language Design Philosophy

> "A small, unsafe language for writing unsafe things safely."

This document captures the core design principles for AI assistants working with Hemlock.
For detailed documentation, see `docs/README.md` and the `stdlib/docs/` directory.

---

## Core Identity

Hemlock is a **systems scripting language** with manual memory management and explicit control:
- The power of C with modern scripting ergonomics
- Structured async concurrency built-in
- No hidden behavior or magic

**Hemlock is NOT:** Memory-safe, a GC language, or hiding complexity.
**Hemlock IS:** Explicit over implicit, educational, a "C scripting layer" for systems work.

---

## Design Principles

### 1. Explicit Over Implicit
- Semicolons mandatory (no ASI)
- Manual memory management (alloc/free)
- Type annotations optional but checked at runtime

### 2. Dynamic by Default, Typed by Choice
- Every value has a runtime type tag
- Literals infer types: `42` → i32, `5000000000` → i64, `3.14` → f64
- Optional type annotations enforce runtime checks

### 3. Unsafe is a Feature
- Pointer arithmetic allowed (user's responsibility)
- No bounds checking on raw `ptr` (use `buffer` for safety)
- Double-free crashes allowed

### 4. Structured Concurrency First-Class
- `async`/`await` built-in with pthread-based parallelism
- Channels for communication
- `spawn`/`join`/`detach` for task management

### 5. C-like Syntax
- `{}` blocks always required
- Operators match C: `+`, `-`, `*`, `%`, `&&`, `||`, `!`, `&`, `|`, `^`, `<<`, `>>`
- `/` always returns float (use `div()` or `divi()` for floor division)
- Type syntax: `let x: type = value;`

---

## Quick Reference

### Types
```
Signed:   i8, i16, i32, i64
Unsigned: u8, u16, u32, u64
Floats:   f32, f64
Other:    bool, string, rune, array, ptr, buffer, null, object, file, task, channel
Aliases:  integer (i32), number (f64), byte (u8)
```

**Type promotion:** i8 → i16 → i32 → i64 → f32 → f64 (floats always win, but i64/u64 + f32 → f64 to preserve precision)

### Literals
```hemlock
let x = 42;              // i32
let big = 5000000000;    // i64 (> i32 max)
let hex = 0xDEADBEEF;    // hex literal
let bin = 0b1010;        // binary literal
let pi = 3.14;           // f64
let s = "hello";         // string
let ch = 'A';            // rune
let emoji = '🚀';        // rune (Unicode)
let arr = [1, 2, 3];     // array
let obj = { x: 10 };     // object
```

### Type Conversion
```hemlock
// Type constructor functions - parse strings to types
let n = i32("42");       // Parse string to i32
let f = f64("3.14");     // Parse string to f64
let b = bool("true");    // Parse string to bool ("true" or "false")

// All numeric types supported
let a = i8("-128");      // i8, i16, i32, i64
let c = u8("255");       // u8, u16, u32, u64
let d = f32("1.5");      // f32, f64

// Hex and negative numbers
let hex = i32("0xFF");   // 255
let neg = i32("-42");    // -42

// Type aliases work too
let x = integer("100");  // Same as i32("100")
let y = number("1.5");   // Same as f64("1.5")
let z = byte("200");     // Same as u8("200")

// Convert between numeric types
let big = i64(42);       // i32 to i64
let truncated = i32(3.99); // f64 to i32 (truncates to 3)

// Type annotations validate types (but don't parse strings)
let f: f64 = 100;        // i32 to f64 via annotation (numeric coercion OK)
// let n: i32 = "42";    // ERROR - use i32("42") for string parsing
```

### Memory
```hemlock
let p = alloc(64);       // raw pointer
let b = buffer(64);      // safe buffer (bounds checked)
memset(p, 0, 64);
memcpy(dest, src, 64);
free(p);                 // manual cleanup required
```

### Control Flow
```hemlock
if (x > 0) { } else if (x < 0) { } else { }
while (cond) { break; continue; }
for (let i = 0; i < 10; i = i + 1) { }
for (item in array) { }
switch (x) { case 1: break; default: break; }
defer cleanup();         // runs when function returns
```

### Functions
```hemlock
fn add(a: i32, b: i32): i32 { return a + b; }
fn greet(name: string, msg?: "Hello") { print(msg + " " + name); }
let f = fn(x) { return x * 2; };  // anonymous/closure
```

### Objects & Enums
```hemlock
define Person { name: string, age: i32, active?: true }
let p: Person = { name: "Alice", age: 30 };
let json = p.serialize();
let restored = json.deserialize();

enum Color { RED, GREEN, BLUE }
enum Status { OK = 0, ERROR = 1 }
```

### Error Handling
```hemlock
try { throw "error"; } catch (e) { print(e); } finally { cleanup(); }
panic("unrecoverable");  // exits immediately, not catchable
```

### Async/Concurrency
```hemlock
async fn compute(n: i32): i32 { return n * n; }
let task = spawn(compute, 42);
let result = await task;     // or join(task)
detach(spawn(background_work));

let ch = channel(10);
ch.send(value);
let val = ch.recv();
ch.close();
```

### User Input
```hemlock
let name = read_line();          // Read line from stdin (blocks)
print("Hello, " + name);
eprint("Error message");         // Print to stderr

// read_line() returns null on EOF
while (true) {
    let line = read_line();
    if (line == null) { break; }
    print("Got:", line);
}
```

### File I/O
```hemlock
let f = open("file.txt", "r");  // modes: r, w, a, r+, w+, a+
let content = f.read();
f.write("data");
f.seek(0);
f.close();
```

### Signals
```hemlock
signal(SIGINT, fn(sig) { print("Interrupted"); });
raise(SIGUSR1);
```

---

## String Methods (19)

`substr`, `slice`, `find`, `contains`, `split`, `trim`, `to_upper`, `to_lower`,
`starts_with`, `ends_with`, `replace`, `replace_all`, `repeat`, `char_at`,
`byte_at`, `chars`, `bytes`, `to_bytes`, `deserialize`

Template strings: `` `Hello ${name}!` ``

## Array Methods (18)

`push`, `pop`, `shift`, `unshift`, `insert`, `remove`, `find`, `contains`,
`slice`, `join`, `concat`, `reverse`, `first`, `last`, `clear`, `map`, `filter`, `reduce`

Typed arrays: `let nums: array<i32> = [1, 2, 3];`

---

## Standard Library (39 modules)

Import with `@stdlib/` prefix:
```hemlock
import { sin, cos, PI } from "@stdlib/math";
import { HashMap, Queue, Set } from "@stdlib/collections";
import { read_file, write_file } from "@stdlib/fs";
import { TcpStream, UdpSocket } from "@stdlib/net";
```

| Module | Description |
|--------|-------------|
| `args` | Command-line argument parsing |
| `assert` | Assertion utilities |
| `async` | ThreadPool, parallel_map |
| `async_fs` | Async file I/O operations |
| `collections` | HashMap, Queue, Stack, Set, LinkedList, LRUCache |
| `compression` | gzip, gunzip, deflate |
| `crypto` | aes_encrypt, rsa_sign, random_bytes |
| `csv` | CSV parsing and generation |
| `datetime` | DateTime class, formatting, parsing |
| `encoding` | base64_encode, hex_encode, url_encode |
| `env` | getenv, setenv, exit, get_pid |
| `fmt` | String formatting utilities |
| `fs` | read_file, write_file, list_dir, exists |
| `glob` | File pattern matching |
| `hash` | sha256, sha512, md5, djb2 |
| `http` | http_get, http_post, http_request |
| `ipc` | Inter-process communication |
| `iter` | Iterator utilities |
| `json` | parse, stringify, pretty, get, set |
| `logging` | Logger with levels |
| `math` | sin, cos, sqrt, pow, rand, PI, E |
| `net` | TcpListener, TcpStream, UdpSocket |
| `os` | platform, arch, cpu_count, hostname |
| `path` | File path manipulation |
| `process` | fork, exec, wait, kill |
| `random` | Random number generation |
| `regex` | compile, test (POSIX ERE) |
| `retry` | Retry logic with backoff |
| `semver` | Semantic versioning |
| `shell` | Shell command utilities |
| `sqlite` | SQLite database, query, exec, transactions |
| `strings` | pad_left, is_alpha, reverse, lines |
| `terminal` | ANSI colors and styles |
| `testing` | describe, test, expect |
| `time` | now, time_ms, sleep, clock |
| `toml` | TOML parsing and generation |
| `url` | URL parsing and manipulation |
| `uuid` | UUID generation |
| `websocket` | WebSocket client |

See `stdlib/docs/` for detailed module documentation.

---

## FFI (Foreign Function Interface)

Declare and call C functions from shared libraries:
```hemlock
import "libc.so.6";

extern fn strlen(s: string): i32;
extern fn getpid(): i32;

let len = strlen("Hello!");  // 6
let pid = getpid();
```

Export FFI functions from modules:
```hemlock
// string_utils.hml
import "libc.so.6";

export extern fn strlen(s: string): i32;
export fn string_length(s: string): i32 {
    return strlen(s);
}
```

Dynamic FFI (runtime binding):
```hemlock
let lib = ffi_open("libc.so.6");
let puts = ffi_bind(lib, "puts", [FFI_POINTER], FFI_INT);
puts("Hello from C!");
ffi_close(lib);
```

Types: `FFI_INT`, `FFI_DOUBLE`, `FFI_POINTER`, `FFI_STRING`, `FFI_VOID`, etc.

---

## Atomic Operations

Lock-free concurrent programming with atomic operations:

```hemlock
// Allocate memory for atomic i32
let p = alloc(4);
ptr_write_i32(p, 0);

// Atomic load/store
let val = atomic_load_i32(p);        // Read atomically
atomic_store_i32(p, 42);             // Write atomically

// Fetch-and-modify operations (return OLD value)
let old = atomic_add_i32(p, 10);     // Add, return old
old = atomic_sub_i32(p, 5);          // Subtract, return old
old = atomic_and_i32(p, 0xFF);       // Bitwise AND
old = atomic_or_i32(p, 0x10);        // Bitwise OR
old = atomic_xor_i32(p, 0x0F);       // Bitwise XOR

// Compare-and-swap (CAS)
let success = atomic_cas_i32(p, 42, 100);  // If *p == 42, set to 100
// Returns true if swap succeeded, false otherwise

// Atomic exchange
old = atomic_exchange_i32(p, 999);   // Swap, return old

free(p);

// i64 variants available (atomic_load_i64, atomic_add_i64, etc.)

// Memory fence (full barrier)
atomic_fence();
```

All operations use sequential consistency (`memory_order_seq_cst`).

---

## Project Structure

```
hemlock/
├── src/
│   ├── frontend/         # Shared: lexer, parser, AST, modules
│   ├── backends/
│   │   ├── interpreter/  # hemlock: tree-walking interpreter
│   │   └── compiler/     # hemlockc: C code generator
│   ├── lsp/              # Language Server Protocol
│   └── bundler/          # Bundle/package tools
├── runtime/              # Compiled program runtime (libhemlock_runtime.a)
├── stdlib/               # Standard library (39 modules)
│   └── docs/             # Module documentation
├── docs/                 # Full documentation
│   ├── language-guide/   # Types, strings, arrays, etc.
│   ├── reference/        # API references
│   └── advanced/         # Async, FFI, signals, etc.
├── tests/                # 625+ tests
└── examples/             # Example programs
```

---

## Code Style Guidelines

### Constants and Magic Numbers

When adding numeric constants to the C codebase, follow these guidelines:

1. **Define constants in `include/hemlock_limits.h`** - This file is the central location for all compile-time and runtime limits, capacities, and named constants.

2. **Use descriptive names with `HML_` prefix** - All constants should be prefixed with `HML_` for namespace clarity.

3. **Avoid magic numbers** - Replace hard-coded numeric values with named constants. Examples:
   - Type range limits: `HML_I8_MIN`, `HML_I8_MAX`, `HML_U32_MAX`
   - Buffer capacities: `HML_INITIAL_ARRAY_CAPACITY`, `HML_INITIAL_LEXER_BUFFER_CAPACITY`
   - Time conversions: `HML_NANOSECONDS_PER_SECOND`, `HML_MILLISECONDS_PER_SECOND`
   - Hash seeds: `HML_DJB2_HASH_SEED`
   - ASCII values: `HML_ASCII_CASE_OFFSET`, `HML_ASCII_PRINTABLE_START`

4. **Include `hemlock_limits.h`** - Source files should include this header (often via `internal.h`) to access constants.

5. **Document the purpose** - Add a comment explaining what each constant represents.

---

## What NOT to Do

❌ Add implicit behavior (ASI, GC, auto-cleanup)
❌ Hide complexity (magic optimizations, hidden refcounts)
❌ Break existing semantics (semicolons, manual memory, mutable strings)
❌ Lose precision in implicit conversions
❌ Use magic numbers - define named constants in `hemlock_limits.h` instead

---

## Testing

```bash
make test              # Run interpreter tests
make test-compiler     # Run compiler tests
make parity            # Run parity tests (both must match)
make test-all          # Run all test suites
```

**Important:** Tests may hang due to async/concurrency issues. Always use a timeout when running tests:
```bash
timeout 60 make test   # 60 second timeout
timeout 120 make parity
```

Test categories: primitives, memory, strings, arrays, functions, objects, async, ffi, defer, signals, switch, bitwise, typed_arrays, modules, stdlib_*

---

## Compiler/Interpreter Architecture

Hemlock has two execution backends that share a common frontend:

```
Source (.hml)
    ↓
┌─────────────────────────────┐
│  SHARED FRONTEND            │
│  - Lexer (src/frontend/)    │
│  - Parser (src/frontend/)   │
│  - AST (src/frontend/)      │
└─────────────────────────────┘
    ↓                    ↓
┌────────────┐    ┌────────────┐
│ INTERPRETER│    │  COMPILER  │
│ (hemlock)  │    │ (hemlockc) │
│            │    │            │
│ Tree-walk  │    │ Type check │
│ evaluation │    │ AST → C    │
│            │    │ gcc link   │
└────────────┘    └────────────┘
```

### Compiler Type Checking

The compiler (`hemlockc`) includes compile-time type checking, **enabled by default**:

```bash
hemlockc program.hml -o program    # Type checks, then compiles
hemlockc --check program.hml       # Type check only, don't compile
hemlockc --no-type-check prog.hml  # Disable type checking
hemlockc --strict-types prog.hml   # Warn on implicit 'any' types
```

The type checker:
- Validates type annotations at compile time
- Treats untyped code as dynamic (`any` type) - always valid
- Provides optimization hints for unboxing
- Uses permissive numeric conversions (range validated at runtime)

### Directory Structure

```
hemlock/
├── src/
│   ├── frontend/           # Shared: lexer, parser, AST, modules
│   │   ├── lexer.c
│   │   ├── parser/
│   │   ├── ast.c
│   │   └── module.c
│   ├── backends/
│   │   ├── interpreter/    # hemlock: tree-walking interpreter
│   │   │   ├── main.c
│   │   │   ├── runtime/
│   │   │   └── builtins/
│   │   └── compiler/       # hemlockc: C code generator
│   │       ├── main.c
│   │       └── codegen/
│   ├── lsp/                # Language server
│   └── bundler/            # Bundle/package tools
├── runtime/                # libhemlock_runtime.a for compiled programs
├── stdlib/                 # Shared standard library
└── tests/
    ├── parity/             # Tests that MUST pass both backends
    ├── interpreter/        # Interpreter-specific tests
    └── compiler/           # Compiler-specific tests
```

---

## Parity-First Development

**Both the interpreter and compiler must produce identical output for the same input.**

### Development Policy

When adding or modifying language features:

1. **Design** - Define the AST/semantic change in the shared frontend
2. **Implement interpreter** - Add tree-walking evaluation
3. **Implement compiler** - Add C code generation
4. **Add parity test** - Write test in `tests/parity/` with `.expected` file
5. **Verify** - Run `make parity` before merging

### Parity Test Structure

```
tests/parity/
├── language/       # Core language features (control flow, closures, etc.)
├── builtins/       # Built-in functions (print, typeof, memory, etc.)
├── methods/        # String and array methods
└── modules/        # Import/export, stdlib imports
```

Each test has two files:
- `feature.hml` - The test program
- `feature.expected` - Expected output (must match for both backends)

### Parity Test Results

| Status | Meaning |
|--------|---------|
| `✓ PASSED` | Both interpreter and compiler match expected output |
| `◐ INTERP_ONLY` | Interpreter works, compiler fails (needs compiler fix) |
| `◑ COMPILER_ONLY` | Compiler works, interpreter fails (rare) |
| `✗ FAILED` | Both fail (test or implementation bug) |

### What Requires Parity

- All language constructs (if, while, for, switch, defer, try/catch)
- All operators (arithmetic, bitwise, logical, comparison)
- All built-in functions (print, typeof, alloc, etc.)
- All string and array methods
- Type coercion and promotion rules
- Error messages for runtime errors

### What May Differ

- Performance characteristics
- Memory layout details
- Debug/stack trace format
- Compilation errors (compiler may catch more at compile time)

### Adding a Parity Test

```bash
# 1. Create test file
cat > tests/parity/language/my_feature.hml << 'EOF'
// Test description
let x = some_feature();
print(x);
EOF

# 2. Generate expected output from interpreter
./hemlock tests/parity/language/my_feature.hml > tests/parity/language/my_feature.expected

# 3. Verify parity
make parity
```

---

## Version

**v1.6.1** - Current release with:
- **Compile-time type checking** in hemlockc (enabled by default)
- **LSP integration** with type checking for real-time diagnostics
- **Compound bitwise operators** (`&=`, `|=`, `^=`, `<<=`, `>>=`, `%=`)
- **Type precision fix**: i64/u64 + f32 → f64 to preserve precision
- Unified type system with unboxing optimization hints
- Full type system (i8-i64, u8-u64, f32/f64, bool, string, rune, ptr, buffer, array, object, enum, file, task, channel)
- UTF-8 strings with 19 methods
- Arrays with 18 methods including map/filter/reduce
- Manual memory management with `talloc()` and `sizeof()`
- Async/await with true pthread parallelism
- Atomic operations for lock-free concurrent programming
- 39 stdlib modules (+ assert, semver, toml, retry, iter, random, shell)
- FFI for C interop with `export extern fn` for reusable library wrappers
- FFI struct support in compiler (pass C structs by value)
- FFI pointer helpers (`ptr_null`, `ptr_read_*`, `ptr_write_*`)
- defer, try/catch/finally/throw, panic
- File I/O, signal handling, command execution
- [hpm](https://github.com/hemlang/hpm) package manager with GitHub-based registry
- Compiler backend (C code generation) with 100% interpreter parity
- LSP server with go-to-definition and find-references
- AST optimization pass and variable resolution for O(1) lookup
- apply() builtin for dynamic function calls
- Unbuffered channels and many-params support
- 114 parity tests (100% pass rate)

---

## Philosophy

> We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).

**If you're not sure whether a feature fits Hemlock, ask: "Does this give the programmer more explicit control, or does it hide something?"**

If it hides, it probably doesn't belong in Hemlock.
