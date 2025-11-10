# Hemlock Language Design Philosophy

> "A small, unsafe language for writing unsafe things safely."

This document captures the core design principles and philosophy of Hemlock. Read this first before making any changes or additions to the language.

---

## Core Identity

Hemlock is a **systems scripting language** that embraces manual memory management and explicit control. It's designed for programmers who want:
- The power of C
- The ergonomics of modern scripting languages
- Structured async concurrency built-in
- No hidden behavior or magic

**Hemlock is NOT:**
- Memory-safe (dangling pointers are your responsibility)
- A replacement for Rust, Go, or Lua
- A language that hides complexity from you

**Hemlock IS:**
- Explicit over implicit, always
- Educational and experimental
- A "C scripting layer" for systems work
- Honest about tradeoffs

---

## Design Principles

### 1. **Explicit Over Implicit**
- Semicolons are mandatory (no ASI)
- No garbage collection
- Manual memory management (alloc/free)
- Type annotations are optional but checked at runtime
- No automatic resource cleanup (no RAII, no defer yet)

**Bad (implicit):**
```hemlock
let x = 5  // Missing semicolon - should error
```

**Good (explicit):**
```hemlock
let x = 5;
free(ptr);  // You allocated it, you free it
```

### 2. **Dynamic by Default, Typed by Choice**
- Every value has a runtime type tag
- Literals infer types: `42` → `i32`, `3.14` → `f64`
- Optional type annotations enforce runtime checks
- Implicit type conversions follow clear promotion rules

```hemlock
let x = 42;              // i32 inferred
let y: u8 = 255;         // explicit u8
let z = x + y;           // promotes to i32
```

### 3. **Unsafe is a Feature, Not a Bug**
- Pointer arithmetic can overflow (user's responsibility)
- No bounds checking on raw `ptr` (use `buffer` if you want safety)
- Double-free crashes are allowed (manual memory management)
- Type system prevents accidents but allows footguns when needed

```hemlock
let p = alloc(10);
let q = p + 100;  // Way past allocation - allowed but dangerous
```

### 4. **Structured Concurrency First-Class**
- `async`/`await` built into the language
- Channels for communication
- `spawn`/`join`/`detach` for task management
- No raw threads, no locks - structured only

### 5. **C-like Syntax, Low Ceremony**
- Familiar to systems programmers
- `{}` blocks always, no optional braces
- Operators match C: `+`, `-`, `*`, `/`, `&&`, `||`, `!`
- Type syntax matches Rust/TypeScript: `let x: type = value;`

---

## Type System

### Numeric Types
- **Signed integers:** `i8`, `i16`, `i32`
- **Unsigned integers:** `u8`, `u16`, `u32`
- **Floats:** `f32`, `f64`
- **Aliases:** `integer` (i32), `number` (f64), `char` (u8)

### Other Primitives
- **bool:** `true`, `false`
- **string:** UTF-8, mutable, heap-allocated with `.length` property
- **ptr:** Raw pointer (8 bytes, no bounds checking)
- **buffer:** Safe wrapper (ptr + length + capacity, bounds checked)
- **null:** The null value

### Type Promotion Rules
When mixing types in operations, promote to the "higher" type:
```
f64 (highest precision)
 ↑
f32
 ↑
u32
 ↑
i32
 ↑
u16
 ↑
i16
 ↑
u8
 ↑
i8 (lowest)
```

**Examples:**
- `u8 + i32` → `i32` (larger size wins)
- `i32 + f32` → `f32` (float always wins)
- `i8 + f64` → `f64` (float + largest wins)

### Range Checking
Type annotations enforce range checks at assignment:
```hemlock
let x: u8 = 255;   // OK
let y: u8 = 256;   // ERROR: out of range
let z: i8 = 128;   // ERROR: max is 127
```

---

## Memory Management

### The Two Pointer Types

**`ptr` - Raw Pointer (Dangerous)**
- Just an address, no tracking
- No bounds checking
- User manages lifetime
- For experts and FFI

```hemlock
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);  // You must remember to free
```

**`buffer` - Safe Wrapper (Recommended)**
- Pointer + length + capacity
- Bounds checked on access
- Still manual free required
- Better default for most code

```hemlock
let b: buffer = buffer(64);
b[0] = 65;              // bounds checked
print(b.length);        // 64
free(b);                // still manual
```

### Memory API

**Core Allocation:**
- `alloc(bytes)` - allocate raw memory, returns `ptr`
- `free(ptr)` - free memory (works on both `ptr` and `buffer`)
- `buffer(size)` - allocate safe buffer

**Operations:**
- `memset(ptr, byte, size)` - fill memory
- `memcpy(dest, src, size)` - copy memory
- `realloc(ptr, size)` - resize allocation

**Typed Allocation (TODO):**
- `talloc(type, count)` - allocate array of typed values
- `sizeof(type)` - get size of type

---

## Strings

Strings are **first-class mutable sequences**:
```hemlock
let s = "hello";
s[0] = 72;              // mutate to "Hello"
print(s.length);        // 5
let c = s[0];           // returns u8 (byte value)
let msg = s + " world"; // concatenation
```

**Properties:**
- Mutable (unlike Python/JS/Java)
- Indexing returns `u8` (byte value, not substring)
- UTF-8 encoded
- `.length` is a property, not a method
- Heap-allocated with internal capacity tracking

---

## Control Flow

### Current Features
- `if`/`else` with mandatory braces
- `while` loops
- Boolean operators: `&&`, `||`, `!`
- Comparisons: `==`, `!=`, `<`, `>`, `<=`, `>=`

### Planned Features
- `for` loops
- `break`/`continue`
- `return` (already works in statements)

---

## Functions (TODO)

Target design:
```hemlock
fn add(a: i32, b: i32): i32 {
    return a + b;
}

fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}
```

**Design decisions:**
- Optional parameter types (runtime checked if provided)
- Optional return type (runtime checked if provided)
- `return` is mandatory (no last-expr-as-return)
- Lexical scoping with closure support
- Recursion fully supported

---

## Structs/Objects (TODO)

Target design:
```hemlock
struct Person {
    name: string,
    age: i32,
    active: bool,
}

let p = Person { name: "Alice", age: 30, active: true };
print(p.name);
p.age = 31;
```

**Design decisions:**
- Structs are value types (no reference semantics yet)
- Field types are optional but recommended
- JSON serialization/deserialization built-in
- Methods come after functions are implemented

---

## Async/Concurrency (TODO)

Target design:
```hemlock
async fn fetch_data(url: string): string {
    // ... fetch implementation
    return data;
}

fn main() {
    let h1 = spawn fetch_data("http://example.com");
    let h2 = spawn fetch_data("http://example.org");
    
    let r1 = join(h1);
    let r2 = join(h2);
    
    print(r1);
    print(r2);
}
```

**Design decisions:**
- Structured concurrency only (no raw threads)
- `spawn` creates tasks, `join` waits for completion
- `detach` for fire-and-forget tasks
- Channels for communication between tasks
- `select` for multiplexing channels

---

## Implementation Details

### Project Structure
```
hemlock/
├── include/          # Public headers
│   ├── ast.h
│   ├── lexer.h
│   ├── parser.h
│   └── interpreter.h
├── src/              # Implementation
│   ├── ast.c
│   ├── lexer.c
│   ├── parser.c
│   ├── interpreter.c
│   └── main.c
├── tests/            # Test suite
│   └── run_tests.sh
└── examples/         # Example programs
```

### Compilation Pipeline
1. **Lexer** → tokens
2. **Parser** → AST
3. **Interpreter** → tree-walking execution (current)
4. **Compiler** → C code generation (future)

### Current Runtime
- Tree-walking interpreter
- Tagged union for values (`Value` struct)
- Environment-based variable storage
- No optimization yet

### Future Runtime
- Compile to C code
- Keep runtime library for dynamic features
- Optional `--no-tags` flag for fully static builds

---

## Testing Philosophy

- **Test-driven development** for new features
- Comprehensive test suite in `tests/`
- Test both success and error cases
- Run `make test` before committing

Example test structure:
```bash
tests/
├── primitives/       # Type system tests
├── conversions/      # Implicit conversion tests
├── memory/           # Pointer/buffer tests
├── strings/          # String operation tests
├── control/          # Control flow tests
└── run_tests.sh      # Test runner
```

---

## Common Patterns

### Error Handling (Current)
```hemlock
// Runtime errors just exit with message
let x: u8 = 256;  // Exits: "Value 256 out of range for u8"
```

### Memory Patterns
```hemlock
// Pattern: Allocate, use, free
let data = alloc(1024);
// ... use data
free(data);

// Pattern: Safe buffer usage
let buf = buffer(256);
let i = 0;
while (i < buf.length) {
    buf[i] = i;
    i = i + 1;
}
free(buf);
```

### Type Patterns
```hemlock
// Pattern: Let types infer when obvious
let count = 0;
let sum = 0.0;

// Pattern: Annotate when precision matters
let byte: u8 = 255;
let precise: f64 = 3.14159265359;
```

---

## What NOT to Do

### ❌ Don't Add Implicit Behavior
```hemlock
// BAD: Automatic semicolon insertion
let x = 5
let y = 10

// BAD: Automatic memory management
let s = "hello"  // String auto-freed at end of scope? NO!

// BAD: Implicit type conversions that lose precision
let x: i32 = 3.14  // Should truncate or error?
```

### ❌ Don't Hide Complexity
```hemlock
// BAD: Magic behind-the-scenes optimization
let arr = [1, 2, 3]  // Is this stack or heap? User should know!

// BAD: Automatic reference counting
let p = create_thing()  // Does this increment a refcount? NO!
```

### ❌ Don't Break Existing Semantics
- Semicolons are mandatory - don't make them optional
- Manual memory management - don't add GC
- Mutable strings - don't make them immutable
- Runtime type checking - don't remove it

---

## Future Considerations

### Maybe Add (Under Discussion)
- `defer` for cleanup (explicit, not automatic)
- `break`/`continue` for loops
- Array/list types (not just buffers)
- Pattern matching
- Error types (`Result<T, E>`)

### Probably Never Add
- Garbage collection
- Automatic memory management
- Implicit type conversions that lose data
- Operator overloading (maybe for user types)
- Macros (too complex)

---

## Philosophy on Safety

**Hemlock's take on safety:**

"We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).

The default should guide toward safety, but the escape hatch should always be available."

**Examples:**
- Default to `buffer` in docs, but `ptr` exists for FFI
- Encourage type annotations, but don't require them
- Provide bounds checking, but allow raw pointer math
- Include `sizeof()` and typed allocation to reduce errors

---

## Contributing Guidelines (Future)

When adding features to Hemlock:

1. **Read this document first**
2. **Write tests before implementation**
3. **Keep it explicit** - no magic
4. **Document the unsafe parts** - be honest
5. **Follow existing patterns** - consistency matters
6. **Update CLAUDE.md** - keep this doc current

---

## Version History

- **v0.1** - Primitives, basic memory, strings, control flow (current)
- **v0.2** - Functions, closures, recursion (planned)
- **v0.3** - Structs, methods, JSON (planned)
- **v0.4** - Async/await, channels, structured concurrency (planned)
- **v0.5** - FFI, C interop, compiler backend (planned)

---

## Final Thoughts

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

**If you're not sure whether a feature fits Hemlock, ask: "Does this give the programmer more explicit control, or does it hide something?"**

If it hides, it probably doesn't belong in Hemlock.
