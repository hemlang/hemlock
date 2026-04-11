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
- Comments: `// line` and `/* block */`
- Operators match C: `+`, `-`, `*`, `%`, `&&`, `||`, `!`, `&`, `|`, `^`, `<<`, `>>`
- Increment/decrement: `++x`, `x++`, `--x`, `x--` (prefix and postfix)
- Compound assignment: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
- `/` always returns float (use `divi()` from `@stdlib/math` for integer division)
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

**Type promotion:** i8 → u8 → i16 → u16 → i32 → u32 → i64 → u64 → f32 → f64 (floats always win, but i64/u64 + f32 → f64 to preserve precision)

### Literals
```hemlock
let x = 42;              // i32
let big = 5000000000;    // i64 (> i32 max)
let hex = 0xDEADBEEF;    // hex literal
let bin = 0b1010;        // binary literal
let oct = 0o777;         // octal literal
let sep = 1_000_000;     // numeric separators allowed
let pi = 3.14;           // f64
let half = .5;           // f64 (no leading zero)
let s = "hello";         // string
let esc = "\x41\u{1F600}"; // hex and unicode escapes
let ch = 'A';            // rune
let arr = [1, 2, 3];     // array
let obj = { x: 10 };     // object
```

### Type Conversion
```hemlock
let n = i32("42");       // Parse string to i32
let f = f64("3.14");     // Parse string to f64
let big = i64(42);       // i32 to i64
let truncated = i32(3.99); // f64 to i32 (truncates to 3)
let f: f64 = 100;        // i32 to f64 via annotation (numeric coercion OK)
// let n: i32 = "42";    // ERROR - use i32("42") for string parsing
```

### Introspection
```hemlock
typeof(42);              // "i32"
typeof("hello");         // "string"
"hello".length;          // 5 (rune count)
"hello".byte_length;     // 5 (byte count)

// typeid() - fast integer-based type detection (no string allocation)
typeid(42);              // 2 (TYPEID_I32)
if (typeid(val) == TYPEID_I32 || typeid(val) == TYPEID_I64) { ... }
```

**TYPEID constants:** `TYPEID_I8` (0), `TYPEID_I16` (1), `TYPEID_I32` (2), `TYPEID_I64` (3), `TYPEID_U8` (4), `TYPEID_U16` (5), `TYPEID_U32` (6), `TYPEID_U64` (7), `TYPEID_F32` (8), `TYPEID_F64` (9), `TYPEID_BOOL` (10), `TYPEID_STRING` (11), `TYPEID_RUNE` (12), `TYPEID_PTR` (13), `TYPEID_BUFFER` (14), `TYPEID_ARRAY` (15), `TYPEID_OBJECT` (16), `TYPEID_FILE` (17), `TYPEID_FUNCTION` (18), `TYPEID_TASK` (19), `TYPEID_CHANNEL` (20), `TYPEID_NULL` (21)

### Memory
```hemlock
let p = alloc(64);       // raw pointer
let b = buffer(64);      // safe buffer (bounds checked)
memset(p, 0, 64); memcpy(dest, src, 64);
free(p);                 // manual cleanup required
let view = b.slice(0, 16);  // zero-copy buffer view
ptr_write_f32(b, 3.14);     // ptr_read/write accept buffers directly
```

### Control Flow
```hemlock
if (x > 0) { } else if (x < 0) { } else { }
while (cond) { break; continue; }
for (let i = 0; i < 10; i++) { }
for (item in array) { }
loop { if (done) { break; } }   // infinite loop
switch (x) { case 1: break; default: break; }  // C-style fall-through
defer cleanup();         // runs when function returns

// Loop labels for nested break/continue
outer: while (cond) {
    for (let i = 0; i < 10; i++) {
        if (i == 5) { break outer; }
    }
}
```

### Pattern Matching
```hemlock
let result = match (value) {
    0 => "zero",
    1 | 2 | 3 => "small",           // OR pattern
    n if n < 10 => "medium",        // Guard
    n => "large: " + n              // Variable binding
};

// Also supports: type patterns (n: i32), object/array destructuring,
// nested patterns, wildcard (_). See docs/language-guide/pattern-matching.md
```

### Null Coalescing
```hemlock
let name = user.name ?? "Anonymous";     // null coalescing
config ??= { timeout: 30 };             // null coalescing assignment
let city = user?.address?.city;          // safe navigation
```

### Functions
```hemlock
fn add(a: i32, b: i32): i32 { return a + b; }
fn greet(name: string, msg?: "Hello") { print(msg + " " + name); }
let f = fn(x) { return x * 2; };  // anonymous/closure
fn double(x: i32): i32 => x * 2;  // expression-bodied

// Parameter modifiers
fn swap(ref a: i32, ref b: i32) { let t = a; a = b; b = t; }  // pass-by-reference
fn print_all(const items: array) { for (i in items) { print(i); } }  // immutable

// Named arguments
create_user(name: "Bob", age: 30);
create_user("David", active: false);  // positional then named
```

### Objects, Enums & Types
```hemlock
define Person { name: string, age: i32, active?: true }
let p: Person = { name: "Alice", age: 30 };
let person = { name, age };             // shorthand syntax
let config = { ...defaults, size: "large" }; // spread operator

// Bracket notation with key coercion (non-string keys auto-coerce to string)
let map = {};
map[42] = "value";              // integer key → "42"
map[true] = "yes";              // bool key → "true"
map['A'] = "alpha";             // rune key → "A"
print(map[42]);                 // "value"
print(map.has(42));             // true
map.delete(42);                 // removes field "42"
let keys = map.keys();          // returns array of string keys

enum Color { RED, GREEN, BLUE }

// Compound types (intersection/duck typing)
let p: HasName & HasAge = { name: "Alice", age: 30 };

// Type aliases
type Callback = fn(i32): void;
type Person = HasName & HasAge;

// Method signatures in define
define Comparable { value: i32, fn compare(other: Self): i32 }
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
let t = spawn_with({ stack_size: 4194304, name: "worker" }, compute, 42);

let ch = channel(10);
ch.send(value); let val = ch.recv(); ch.close();
```

**Memory ownership:** Tasks share pointers but copy primitives. Use `join()` before `free()` when sharing `ptr`.

### I/O
```hemlock
let name = read_line();          // stdin (returns null on EOF)
print("hello"); write("no newline"); eprint("stderr");
let f = open("file.txt", "r");  // modes: r, w, a, r+, w+, a+
f.read(); f.write("data"); f.close();
```

---

## String Methods (22)

`substr`, `slice`, `find`, `contains`, `split`, `trim`, `trim_start`, `trim_end`,
`to_upper`, `to_lower`, `starts_with`, `ends_with`, `replace`, `replace_all`,
`repeat`, `char_at`, `byte_at`, `chars`, `bytes`, `to_bytes`, `byte_ptr`, `deserialize`

Template strings: `` `Hello ${name}!` ``

**String mutability:** Strings are mutable via index assignment (`s[0] = 'H'`), but all string methods return new strings.

## Array Methods (28)

`push`, `pop`, `shift`, `unshift`, `insert`, `remove`, `find`, `findIndex`, `contains`,
`slice`, `join`, `concat`, `reverse`, `first`, `last`, `clear`, `map`, `filter`, `reduce`,
`every`, `some`, `indexOf`, `lastIndexOf`, `sort`, `fill`, `reserve`, `flat`, `serialize`

Typed arrays: `let nums: array<i32> = [1, 2, 3];`

---

## Standard Library (53 modules)

Import with `@stdlib/` prefix: `import { sin, cos, PI } from "@stdlib/math";`

| Module | Description |
|--------|-------------|
| `arena` | Arena memory allocator (bump allocation) |
| `args` | Command-line argument parsing |
| `assert` | Assertion utilities |
| `async` | ThreadPool, parallel_map |
| `async_fs` | Async file I/O operations |
| `atomic` | Atomic operations (load, store, add, CAS, fence) |
| `bytes` | Byte order utils (bswap, hton/ntoh, endian-aware I/O) |
| `collections` | HashMap, Queue, Stack, Set, LinkedList, LRUCache |
| `compression` | gzip, gunzip, deflate |
| `crypto` | aes_encrypt, rsa_sign, random_bytes |
| `csv` | CSV parsing and generation |
| `debug` | Task inspection and stack management |
| `datetime` | DateTime class, formatting, parsing |
| `decimal` | to_fixed, to_hex, parse_int, parse_float, StringBuilder |
| `encoding` | base64_encode, hex_encode, url_encode |
| `env` | getenv, setenv, exit, get_pid |
| `ffi` | FFI callback management |
| `fmt` | String formatting utilities |
| `fs` | open, read_file, write_file, list_dir, exists |
| `glob` | File pattern matching |
| `hash` | sha1, sha256, sha512, md5, djb2, crc32, adler32 |
| `http` | http_get, http_post, http_request |
| `ipc` | Inter-process communication |
| `iter` | Iterator utilities |
| `jinja` | Jinja2-compatible template rendering |
| `json` | parse, stringify, pretty, get, set |
| `logging` | Logger with levels |
| `math` | sin, cos, sqrt, pow, rand, PI, E |
| `matrix` | Dense matrix operations (add, multiply, transpose, determinant, inverse) |
| `mmap` | Memory-mapped file I/O (mmap, munmap, msync) |
| `net` | TcpListener, TcpStream, UdpSocket |
| `os` | platform, arch, cpu_count, hostname |
| `path` | File path manipulation |
| `process` | fork, exec, wait, kill |
| `random` | Random number generation |
| `regex` | compile, test (POSIX ERE) |
| `retry` | Retry logic with backoff |
| `semver` | Semantic versioning |
| `shell` | Shell command utilities |
| `signal` | Signal constants (SIGINT, SIGTERM, etc.) |
| `sqlite` | SQLite database, query, exec, transactions |
| `strings` | pad_left, is_alpha, reverse, lines |
| `terminal` | ANSI colors and styles |
| `termios` | Raw terminal input, key detection |
| `testing` | describe, test, expect |
| `time` | now, time_ms, sleep, clock |
| `toml` | TOML parsing and generation |
| `url` | URL parsing and manipulation |
| `unix_socket` | Unix domain sockets (AF_UNIX stream/datagram) |
| `uuid` | UUID generation |
| `vector` | Vector similarity search (USearch ANN) |
| `websocket` | WebSocket client |
| `yaml` | YAML parsing and generation |

See `stdlib/docs/` for detailed module documentation.

---

## FFI (Foreign Function Interface)

```hemlock
import "libc.so.6";
extern fn strlen(s: string): i32;
let len = strlen("Hello!");  // 6

// Dynamic FFI
let lib = ffi_open("libc.so.6");
let puts = ffi_bind(lib, "puts", [FFI_POINTER], FFI_INT);
puts("Hello from C!");
ffi_close(lib);
```

See `docs/advanced/ffi.md` for full documentation.

---

## Project Structure

```
hemlock/
├── src/
│   ├── frontend/         # Shared: lexer, parser, AST, modules
│   ├── backends/
│   │   ├── interpreter/  # hemlock: tree-walking interpreter
│   │   └── compiler/     # hemlockc: C code generator
│   ├── modules/          # Native module implementations
│   ├── runtime/          # Runtime-related C code
│   ├── shared/           # Shared utilities (type promotion, etc.)
│   ├── tools/
│   │   ├── lsp/          # Language Server Protocol
│   │   ├── bundler/      # Bundle/package tools
│   │   └── formatter/    # Code formatter
├── runtime/              # Compiled program runtime (libhemlock_runtime.a)
├── stdlib/               # Standard library
│   └── docs/             # Module documentation
├── include/              # C header files (hemlock_limits.h, etc.)
├── docs/                 # Full documentation
├── tests/                # 978+ tests
├── examples/             # Example programs
├── benchmark/            # Benchmarks
├── editors/              # Editor integrations
└── wasm/                 # WebAssembly support
```

### Compiler/Interpreter Architecture

Both backends share a common frontend (lexer, parser, AST). The interpreter does tree-walk evaluation; the compiler generates C code and links with GCC. See `docs/` for details.

---

## Code Style Guidelines

1. **Define constants in `include/hemlock_limits.h`** with `HML_` prefix
2. **Avoid magic numbers** - use named constants
3. **Include `hemlock_limits.h`** via `internal.h` to access constants

---

## What NOT to Do

- Add implicit behavior (ASI, GC, auto-cleanup)
- Hide complexity (magic optimizations, hidden refcounts)
- Break existing semantics (semicolons, manual memory, mutable strings)
- Lose precision in implicit conversions
- Use magic numbers - define named constants in `hemlock_limits.h` instead

---

## Testing

```bash
make test              # Run interpreter tests
make test-compiler     # Run compiler tests
make parity            # Run parity tests (both must match)
make test-all          # Run all test suites
```

**Important:** Always use a timeout when running tests (async tests may hang):
```bash
timeout 60 make test
timeout 120 make parity
```

---

## Parity-First Development

**Both the interpreter and compiler must produce identical output for the same input.**

When adding/modifying language features:
1. Design the AST/semantic change in the shared frontend
2. Implement in interpreter (tree-walking evaluation)
3. Implement in compiler (C code generation)
4. Add parity test in `tests/parity/` with `.expected` file
5. Run `make parity` before merging

Each test has `feature.hml` + `feature.expected`. Both backends must match the expected output.

---

## Philosophy

> We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).

**If you're not sure whether a feature fits Hemlock, ask: "Does this give the programmer more explicit control, or does it hide something?"**

If it hides, it probably doesn't belong in Hemlock.
