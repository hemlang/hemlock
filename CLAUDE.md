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
- `/` always returns float (use `divi()` for integer division)
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
let oct = 0o777;         // octal literal
let sep = 1_000_000;     // numeric separators allowed
let pi = 3.14;           // f64
let half = .5;           // f64 (no leading zero)
let s = "hello";         // string
let esc = "\x41\u{1F600}"; // hex and unicode escapes
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

### Introspection
```hemlock
typeof(42);              // "i32"
typeof("hello");         // "string"
typeof([1, 2, 3]);       // "array"
typeof(null);            // "null"
len("hello");            // 5 (string length in bytes)
len([1, 2, 3]);          // 3 (array length)
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
for (let i = 0; i < 10; i++) { }
for (item in array) { }
loop { if (done) { break; } }   // infinite loop (cleaner than while(true))
switch (x) { case 1: break; default: break; }  // C-style fall-through
defer cleanup();         // runs when function returns

// Loop labels for targeted break/continue in nested loops
outer: while (cond) {
    inner: for (let i = 0; i < 10; i++) {
        if (i == 5) { break outer; }     // break outer loop
        if (i == 3) { continue outer; }  // continue outer loop
    }
}
```

### Pattern Matching
```hemlock
// Match expression - returns value
let result = match (value) {
    0 => "zero",                    // Literal pattern
    1 | 2 | 3 => "small",           // OR pattern
    n if n < 10 => "medium",        // Guard expression
    n => "large: " + n              // Variable binding
};

// Type patterns
match (val) {
    n: i32 => "integer",
    s: string => "string",
    _ => "other"                    // Wildcard
}

// Object destructuring
match (point) {
    { x: 0, y: 0 } => "origin",
    { x, y } => "at " + x + "," + y
}

// Array destructuring with rest
match (arr) {
    [] => "empty",
    [first, ...rest] => "head: " + first,
    _ => "other"
}

// Nested patterns
match (user) {
    { name, address: { city } } => name + " in " + city
}
```

See `docs/language-guide/pattern-matching.md` for full documentation.

### Null Coalescing Operators
```hemlock
// Null coalescing (??) - returns left if non-null, else right
let name = user.name ?? "Anonymous";
let first = a ?? b ?? c ?? "fallback";

// Null coalescing assignment (??=) - assigns only if null
let config = null;
config ??= { timeout: 30 };    // config is now { timeout: 30 }
config ??= { timeout: 60 };    // config unchanged (not null)

// Works with properties and indices
obj.field ??= "default";
arr[0] ??= "first";

// Safe navigation (?.) - returns null if object is null
let city = user?.address?.city;  // null if any part is null
let upper = name?.to_upper();    // safe method call
let item = arr?.[0];             // safe indexing
```

### Functions
```hemlock
fn add(a: i32, b: i32): i32 { return a + b; }
fn greet(name: string, msg?: "Hello") { print(msg + " " + name); }
let f = fn(x) { return x * 2; };  // anonymous/closure

// Expression-bodied functions (arrow syntax)
fn double(x: i32): i32 => x * 2;
fn max(a: i32, b: i32): i32 => a > b ? a : b;
let square = fn(x: i32): i32 => x * x;  // anonymous expression-bodied

// Parameter modifiers
fn swap(ref a: i32, ref b: i32) { let t = a; a = b; b = t; }  // pass-by-reference
fn print_all(const items: array) { for (i in items) { print(i); } }  // immutable
```

### Named Arguments
```hemlock
// Functions can be called with named arguments
fn create_user(name: string, age?: 18, active?: true) {
    print(name + " is " + age + " years old");
}

// Positional arguments (traditional)
create_user("Alice", 25, false);

// Named arguments - can be in any order
create_user(name: "Bob", age: 30);
create_user(age: 25, name: "Charlie", active: false);

// Skip optional parameters by naming what you need
create_user("David", active: false);  // Uses default age=18

// Named arguments must come after positional arguments
create_user("Eve", age: 21);          // OK: positional then named
// create_user(name: "Bad", 25);      // ERROR: positional after named
```

**Rules:**
- Named arguments use `name: value` syntax
- Can appear in any order after positional arguments
- Positional arguments cannot follow named arguments
- Works with default/optional parameters
- Unknown parameter names cause runtime errors

### Objects & Enums
```hemlock
define Person { name: string, age: i32, active?: true }
let p: Person = { name: "Alice", age: 30 };
let json = p.serialize();
let restored = json.deserialize();

// Object shorthand syntax (ES6-style)
let name = "Alice";
let age = 30;
let person = { name, age };         // equivalent to { name: name, age: age }

// Object spread operator
let defaults = { theme: "dark", size: "medium" };
let config = { ...defaults, size: "large" };  // copies defaults, overrides size

enum Color { RED, GREEN, BLUE }
enum Status { OK = 0, ERROR = 1 }
```

### Compound Types (Intersection/Duck Types)
```hemlock
// Define structural types
define HasName { name: string }
define HasAge { age: i32 }
define HasEmail { email: string }

// Compound type: object must satisfy ALL types
let person: HasName & HasAge = { name: "Alice", age: 30 };

// Function parameters with compound types
fn greet(p: HasName & HasAge) {
    print(p.name + " is " + p.age);
}

// Three or more types
fn describe(p: HasName & HasAge & HasEmail) {
    print(p.name + " <" + p.email + ">");
}

// Extra fields allowed (duck typing)
let employee: HasName & HasAge = {
    name: "Bob",
    age: 25,
    department: "Engineering"  // OK - extra fields ignored
};
```

Compound types provide interface-like behavior without a separate `interface` keyword,
building on the existing `define` and duck typing paradigms.

### Type Aliases
```hemlock
// Simple type alias
type Integer = i32;
type Text = string;

// Function type alias
type Callback = fn(i32): void;
type Predicate = fn(i32): bool;
type AsyncHandler = async fn(string): i32;

// Compound type alias (great for reusable interfaces)
define HasName { name: string }
define HasAge { age: i32 }
type Person = HasName & HasAge;

// Generic type alias
type Pair<T> = { first: T, second: T };

// Using type aliases
let x: Integer = 42;
let cb: Callback = fn(n) { print(n); };
let p: Person = { name: "Alice", age: 30 };
```

Type aliases create named shortcuts for complex types, improving readability and maintainability.

### Function Types
```hemlock
// Function type annotations for parameters
fn apply_fn(f: fn(i32): i32, x: i32): i32 {
    return f(x);
}

// Higher-order function returning a function
fn make_adder(n: i32): fn(i32): i32 {
    return fn(x) { return x + n; };
}

// Async function types
fn run_async(handler: async fn(): void) {
    spawn(handler);
}

// Function types with multiple parameters
type BinaryOp = fn(i32, i32): i32;
let add: BinaryOp = fn(a, b) { return a + b; };
```

### Const Parameters
```hemlock
// Const parameter - deep immutability
fn print_all(const items: array) {
    // items.push(4);  // ERROR: cannot mutate const parameter
    for (item in items) {
        print(item);
    }
}

// Const with objects - no mutation through any path
fn describe(const person: object) {
    print(person.name);       // OK: reading is allowed
    // person.name = "Bob";   // ERROR: cannot mutate
}

// Nested access is allowed for reading
fn get_city(const user: object) {
    return user.address.city;  // OK: reading nested properties
}
```

The `const` modifier prevents any mutation of the parameter, including nested properties.
This provides compile-time safety for functions that should not modify their inputs.

### Ref Parameters (Pass-by-Reference)
```hemlock
// Ref parameter - caller's variable is modified directly
fn increment(ref x: i32) {
    x = x + 1;  // Modifies the original variable
}

let count = 10;
increment(count);
print(count);  // 11 - original was modified

// Classic swap function
fn swap(ref a: i32, ref b: i32) {
    let temp = a;
    a = b;
    b = temp;
}

let x = 1;
let y = 2;
swap(x, y);
print(x, y);  // 2 1

// Mix ref and regular parameters
fn add_to(ref target: i32, amount: i32) {
    target = target + amount;
}

let total = 100;
add_to(total, 50);
print(total);  // 150
```

The `ref` modifier passes a reference to the caller's variable, allowing the function to
modify it directly. Without `ref`, primitives are passed by value (copied). Use `ref` when
you need to mutate the caller's state without returning a value.

**Rules:**
- `ref` parameters must be passed variables, not literals or expressions
- Works with all types (primitives, arrays, objects)
- Combine with type annotations: `ref x: i32`
- Cannot combine with `const` (they're opposites)

### Method Signatures in Define
```hemlock
// Define with method signatures (interface pattern)
define Comparable {
    value: i32,
    fn compare(other: Self): i32   // Required method signature
}

// Objects must provide the required method
let a: Comparable = {
    value: 10,
    compare: fn(other) { return self.value - other.value; }
};

// Optional methods with ?
define Serializable {
    fn serialize(): string,        // Required
    fn pretty?(): string           // Optional method
}

// Self type refers to the defining type
define Cloneable {
    fn clone(): Self   // Returns same type as the object
}
```

Method signatures in `define` blocks use comma delimiters (like TypeScript interfaces),
establishing contracts that objects must fulfill and enabling interface-like programming
patterns with Hemlock's duck typing system.

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

**Memory ownership:** Tasks receive copies of primitive values but share pointers. If you pass a `ptr` to a spawned task, you must ensure the memory remains valid until the task completes. Use `join()` before `free()`, or use channels to signal completion.

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

**String mutability:** Strings are mutable via index assignment (`s[0] = 'H'`), but all string methods return new strings without modifying the original. This allows in-place mutation when needed while keeping method chaining functional.

**String length properties:**
```hemlock
let s = "hello 🚀";
print(s.length);       // 7 (character/rune count)
print(s.byte_length);  // 10 (byte count - emoji is 4 bytes UTF-8)
```

## Array Methods (18)

`push`, `pop`, `shift`, `unshift`, `insert`, `remove`, `find`, `contains`,
`slice`, `join`, `concat`, `reverse`, `first`, `last`, `clear`, `map`, `filter`, `reduce`

Typed arrays: `let nums: array<i32> = [1, 2, 3];`

---

## Standard Library (40 modules)

Import with `@stdlib/` prefix:
```hemlock
import { sin, cos, PI } from "@stdlib/math";
import { HashMap, Queue, Set } from "@stdlib/collections";
import { read_file, write_file } from "@stdlib/fs";
import { TcpStream, UdpSocket } from "@stdlib/net";
```

| Module | Description |
|--------|-------------|
| `arena` | Arena memory allocator (bump allocation) |
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
│   ├── tools/
│   │   ├── lsp/          # Language Server Protocol
│   │   └── bundler/      # Bundle/package tools
├── runtime/              # Compiled program runtime (libhemlock_runtime.a)
├── stdlib/               # Standard library (40 modules)
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
│   ├── tools/
│   │   ├── lsp/            # Language server
│   │   └── bundler/        # Bundle/package tools
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

**v1.8.0** - Current release with:
- **Pattern matching** (`match` expressions) - Powerful destructuring and control flow:
  - Literal, wildcard, and variable binding patterns
  - OR patterns (`1 | 2 | 3`)
  - Guard expressions (`n if n > 0`)
  - Object destructuring (`{ x, y }`)
  - Array destructuring with rest (`[first, ...rest]`)
  - Type patterns (`n: i32`)
  - Full parity between interpreter and compiler
- **Compiler helper annotations** - 11 optimization annotations for GCC/Clang control:
  - `@inline`, `@noinline` - function inlining control
  - `@hot`, `@cold` - branch prediction hints
  - `@pure`, `@const` - side-effect annotations
  - `@flatten` - inline all calls within function
  - `@optimize(level)` - per-function optimization level ("0", "1", "2", "3", "s", "fast")
  - `@warn_unused` - warn on ignored return values
  - `@section(name)` - custom ELF section placement (e.g., `@section(".text.hot")`)
- **Expression-bodied functions** (`fn double(x): i32 => x * 2;`) - concise single-expression function syntax
- **Single-line statements** - braceless `if`, `while`, `for` syntax (e.g., `if (x > 0) print(x);`)
- **Type aliases** (`type Name = Type;`) - named shortcuts for complex types
- **Function type annotations** (`fn(i32): i32`) - first-class function types
- **Const parameters** (`fn(const x: array)`) - deep immutability for parameters
- **Ref parameters** (`fn(ref x: i32)`) - pass-by-reference for direct caller mutation
- **Method signatures in define** (`fn method(): Type`) - interface-like contracts (comma-delimited)
- **Self type** in method signatures - refers to the defining type
- **Loop keyword** (`loop { }`) - cleaner infinite loops, replaces `while (true)`
- **Loop labels** (`outer: while`) - targeted break/continue for nested loops
- **Object shorthand** (`{ name }`) - ES6-style shorthand property syntax
- **Object spread** (`{ ...obj }`) - copy and merge object fields
- **Compound duck types** (`A & B & C`) - intersection types for structural typing
- **Named arguments** for function calls (`foo(name: "value", age: 30)`)
- **Null coalescing operators** (`??`, `??=`, `?.`) for safe null handling
- **Octal literals** (`0o777`, `0O123`)
- **Numeric separators** (`1_000_000`, `0xFF_FF`, `0b1111_0000`)
- **Block comments** (`/* ... */`)
- **Hex escape sequences** in strings/runes (`\x41` = 'A')
- **Unicode escape sequences** in strings (`\u{1F600}` = 😀)
- **Float literals without leading zero** (`.5`, `.123`, `.5e2`)
- **Compile-time type checking** in hemlockc (enabled by default)
- **LSP integration** with type checking for real-time diagnostics
- **Compound assignment operators** (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`)
- **Increment/decrement operators** (`++x`, `x++`, `--x`, `x--`)
- **Type precision fix**: i64/u64 + f32 → f64 to preserve precision
- Unified type system with unboxing optimization hints
- Full type system (i8-i64, u8-u64, f32/f64, bool, string, rune, ptr, buffer, array, object, enum, file, task, channel)
- UTF-8 strings with 19 methods
- Arrays with 18 methods including map/filter/reduce
- Manual memory management with `talloc()` and `sizeof()`
- Async/await with true pthread parallelism
- Atomic operations for lock-free concurrent programming
- 40 stdlib modules (+ arena, assert, semver, toml, retry, iter, random, shell)
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
- 159 parity tests (100% pass rate)

---

## Philosophy

> We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).

**If you're not sure whether a feature fits Hemlock, ask: "Does this give the programmer more explicit control, or does it hide something?"**

If it hides, it probably doesn't belong in Hemlock.
