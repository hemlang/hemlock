# Hemlock Implementation Details

This document describes the technical implementation of the Hemlock language, including project structure, compilation pipeline, runtime architecture, and design decisions.

---

## Table of Contents

- [Project Structure](#project-structure)
- [Compilation Pipeline](#compilation-pipeline)
- [Modular Interpreter Design](#modular-interpreter-design)
- [Runtime Architecture](#runtime-architecture)
- [Value Representation](#value-representation)
- [Type System Implementation](#type-system-implementation)
- [Memory Management](#memory-management)
- [Concurrency Model](#concurrency-model)
- [Future Plans](#future-plans)

---

## Project Structure

```
hemlock/
├── src/
│   ├── frontend/              # Shared: lexer, parser, AST
│   │   ├── lexer.c            # Tokenization
│   │   ├── parser/            # Recursive descent parser
│   │   ├── ast.c              # AST node management
│   │   └── module.c           # Module resolution
│   ├── backends/
│   │   ├── interpreter/       # hemlock: tree-walking interpreter
│   │   │   ├── main.c         # CLI entry point
│   │   │   ├── runtime.c      # Expression/statement evaluation
│   │   │   ├── builtins.c     # Built-in functions
│   │   │   └── ...
│   │   └── compiler/          # hemlockc: C code generator
│   │       ├── main.c         # CLI, orchestration
│   │       ├── type_check.c   # Compile-time type checking
│   │       ├── codegen.c      # Code generation context
│   │       ├── codegen_expr.c # Expression codegen
│   │       ├── codegen_stmt.c # Statement codegen
│   │       └── ...
│   ├── tools/
│   │   ├── lsp/               # Language Server Protocol
│   │   └── bundler/           # Bundle/package tools
├── runtime/                   # libhemlock_runtime.a (for compiled programs)
├── stdlib/                    # Standard library (39 modules)
│   └── docs/                  # Module documentation
├── tests/
│   ├── parity/                # Tests that must pass both backends
│   ├── interpreter/           # Interpreter-specific tests
│   └── compiler/              # Compiler-specific tests
├── examples/                  # Example programs
└── docs/                      # Documentation
```

### Directory Organization

**`include/`** - Public API headers that define the interface between components:
- Clean separation between lexer, parser, AST, and interpreter
- Forward declarations to minimize dependencies
- Public API for embedding Hemlock in other programs

**`src/`** - Implementation files:
- Top-level files handle lexing, parsing, AST management
- `main.c` provides CLI and REPL
- Interpreter is modularized into separate subsystems

**`src/interpreter/`** - Modular interpreter implementation:
- Each module has a single, clear responsibility
- Internal API defined in `internal.h` for inter-module communication
- Modules can be compiled independently for faster builds

**`tests/`** - Comprehensive test suite:
- Organized by feature area
- Each directory contains focused test cases
- `run_tests.sh` orchestrates test execution

---

## Compilation Pipeline

Hemlock uses a traditional compilation pipeline with distinct phases:

### Phase 1: Lexical Analysis (Lexer)

**Input:** Source code text
**Output:** Token stream
**Implementation:** `src/lexer.c`

```
Source: "let x = 42;"
   ↓
Tokens: [LET, IDENTIFIER("x"), EQUALS, INTEGER(42), SEMICOLON]
```

**Key features:**
- Recognizes keywords, identifiers, literals, operators, punctuation
- Handles UTF-8 string literals and rune literals
- Reports line numbers for error messages
- Single-pass, no backtracking

### Phase 2: Syntax Analysis (Parser)

**Input:** Token stream
**Output:** Abstract Syntax Tree (AST)
**Implementation:** `src/parser.c`

```
Tokens: [LET, IDENTIFIER("x"), EQUALS, INTEGER(42), SEMICOLON]
   ↓
AST: LetStmt {
    name: "x",
    type: null,
    value: IntLiteral(42)
}
```

**Key features:**
- Recursive descent parser
- Builds tree representation of program structure
- Handles operator precedence
- Validates syntax (braces, semicolons, etc.)
- No semantic analysis yet (done at runtime)

**Operator Precedence (lowest to highest):**
1. Assignment: `=`
2. Logical OR: `||`
3. Logical AND: `&&`
4. Bitwise OR: `|`
5. Bitwise XOR: `^`
6. Bitwise AND: `&`
7. Equality: `==`, `!=`
8. Comparison: `<`, `>`, `<=`, `>=`
9. Bitwise shifts: `<<`, `>>`
10. Addition/Subtraction: `+`, `-`
11. Multiplication/Division/Modulo: `*`, `/`, `%`
12. Unary: `!`, `-`, `~`
13. Call/Index/Member: `()`, `[]`, `.`

### Phase 3a: Interpretation (Tree-Walking)

**Input:** AST
**Output:** Program execution
**Implementation:** `src/backends/interpreter/runtime.c`

```
AST: LetStmt { ... }
   ↓
Execution: Evaluates AST nodes recursively
   ↓
Result: Variable x created with value 42
```

**Key features:**
- Direct AST traversal (tree-walking interpreter)
- Dynamic type checking at runtime
- Environment-based variable storage

### Phase 3b: Compilation (hemlockc)

**Input:** AST
**Output:** Native executable via C code generation
**Implementation:** `src/backends/compiler/`

```
AST: LetStmt { ... }
   ↓
Type Check: Validate types at compile time
   ↓
C Codegen: Generate equivalent C code
   ↓
GCC: Compile C to native binary
   ↓
Result: Standalone executable
```

**Key features:**
- Compile-time type checking (enabled by default)
- C code generation for portability
- Links against `libhemlock_runtime.a`
- Significantly faster execution than interpreter

---

## Compiler Backend (hemlockc)

The Hemlock compiler generates C code from the AST, which is then compiled to a native executable using GCC.

### Compiler Architecture

```
src/backends/compiler/
├── main.c              # CLI, argument parsing, orchestration
├── codegen.c           # Core code generation context
├── codegen_expr.c      # Expression code generation
├── codegen_stmt.c      # Statement code generation
├── codegen_call.c      # Function call generation
├── codegen_closure.c   # Closure implementation
├── codegen_program.c   # Top-level program generation
├── codegen_module.c    # Module/import handling
├── type_check.c        # Compile-time type checking
└── type_check.h        # Type checker API
```

### Type Checking

The compiler includes a unified type checking system that:

1. **Validates types at compile time** - Catches type errors before execution
2. **Supports dynamic code** - Untyped code treated as `any` (always valid)
3. **Provides optimization hints** - Identifies variables that can be unboxed

**Type Checking Flags:**

| Flag | Description |
|------|-------------|
| (default) | Type checking enabled |
| `--check` | Type check only, don't compile |
| `--no-type-check` | Disable type checking |
| `--strict-types` | Warn on implicit `any` types |

**Type Checker Implementation:**

```c
// type_check.h - Key structures
typedef struct TypeCheckContext {
    const char *filename;
    int error_count;
    int warning_count;
    UnboxableVar *unboxable_vars;  // Optimization hints
    // ... type environment, definitions, etc.
} TypeCheckContext;

// Main entry point
int type_check_program(TypeCheckContext *ctx, Stmt **stmts, int count);
```

### Code Generation

The codegen phase translates AST nodes to C code:

**Expression Mapping:**
```
Hemlock                 →  Generated C
----------------------------------------
let x = 42;            →  HmlValue x = hml_val_i32(42);
x + y                  →  hml_add(x, y)
arr[i]                 →  hml_array_get(arr, i)
obj.field              →  hml_object_get_field(obj, "field")
fn(a, b) { ... }       →  Closure with environment capture
```

**Runtime Integration:**

Generated C code links against `libhemlock_runtime.a` which provides:
- `HmlValue` tagged union type
- Memory management (reference counting)
- Built-in functions (print, typeof, etc.)
- Concurrency primitives (tasks, channels)
- FFI support

### Unboxing Optimization

The type checker identifies variables that can use native C types instead of boxed `HmlValue`:

**Unboxable Patterns:**
- Loop counters with known integer type
- Accumulator variables in loops
- Variables with explicit type annotations (i32, i64, f64, bool)

```hemlock
// Loop counter 'i' can be unboxed to native int32_t
for (let i: i32 = 0; i < 1000000; i = i + 1) {
    sum = sum + i;
}
```

---

## Modular Interpreter Design

The interpreter is split into focused modules for maintainability and scalability.

### Module Responsibilities

#### 1. Environment (`environment.c`) - 121 lines

**Purpose:** Variable scoping and name resolution

**Key functions:**
- `env_create()` - Create new environment with optional parent
- `env_define()` - Define new variable in current scope
- `env_get()` - Lookup variable in current or parent scopes
- `env_set()` - Update existing variable value
- `env_free()` - Free environment and all variables

**Design:**
- Linked scopes (each environment has pointer to parent)
- HashMap for fast variable lookup
- Supports lexical scoping for closures

#### 2. Values (`values.c`) - 394 lines

**Purpose:** Value constructors and data structure management

**Key functions:**
- `value_create_*()` - Constructors for each value type
- `value_copy()` - Deep/shallow copying logic
- `value_free()` - Cleanup and memory deallocation
- `value_to_string()` - String representation for printing

**Data structures:**
- Objects (dynamic field arrays)
- Arrays (dynamic resizing)
- Buffers (ptr + length + capacity)
- Closures (function + captured environment)
- Tasks and Channels (concurrency primitives)

#### 3. Types (`types.c`) - 440 lines

**Purpose:** Type system, conversions, and duck typing

**Key functions:**
- `type_check()` - Runtime type validation
- `type_convert()` - Implicit type conversions/promotions
- `duck_type_check()` - Structural type checking for objects
- `type_name()` - Get printable type name

**Features:**
- Type promotion hierarchy (i8 → i16 → i32 → i64 → f32 → f64, with i64/u64 + f32 → f64)
- Range checking for numeric types
- Duck typing for object type definitions
- Optional field defaults

#### 4. Builtins (`builtins.c`) - 955 lines

**Purpose:** Built-in functions and global registration

**Key functions:**
- `register_builtins()` - Register all built-in functions and constants
- Built-in function implementations (print, typeof, alloc, free, etc.)
- Signal handling functions
- Command execution (exec)

**Categories of builtins:**
- I/O: print, open, read_file, write_file
- Memory: alloc, free, memset, memcpy, realloc
- Types: typeof, assert
- Concurrency: spawn, join, detach, channel
- System: exec, signal, raise, panic
- FFI: dlopen, dlsym, dlcall, dlclose

#### 5. I/O (`io.c`) - 449 lines

**Purpose:** File I/O and JSON serialization

**Key functions:**
- File object methods (read, write, seek, tell, close)
- JSON serialization/deserialization
- Circular reference detection

**Features:**
- File object with properties (path, mode, closed)
- UTF-8 aware text I/O
- Binary I/O support
- JSON round-tripping for objects and arrays

#### 6. FFI (`ffi.c`) - Foreign Function Interface

**Purpose:** Calling C functions from shared libraries

**Key functions:**
- `dlopen()` - Load shared library
- `dlsym()` - Get function pointer by name
- `dlcall()` - Call C function with type conversion
- `dlclose()` - Unload library

**Features:**
- Integration with libffi for dynamic function calls
- Automatic type conversion (Hemlock ↔ C types)
- Support for all primitive types
- Pointer and buffer support

#### 7. Runtime (`runtime.c`) - 865 lines

**Purpose:** Expression evaluation and statement execution

**Key functions:**
- `eval_expr()` - Evaluate expressions (recursive)
- `eval_stmt()` - Execute statements
- Control flow handling (if, while, for, switch, etc.)
- Exception handling (try/catch/finally/throw)

**Features:**
- Recursive expression evaluation
- Short-circuit boolean evaluation
- Method call detection and `self` binding
- Exception propagation
- Break/continue/return handling

### Benefits of Modular Design

**1. Separation of Concerns**
- Each module has one clear responsibility
- Easy to find where features are implemented
- Reduces cognitive load when making changes

**2. Faster Incremental Builds**
- Only modified modules need recompilation
- Parallel compilation possible
- Shorter iteration times during development

**3. Easier Testing and Debugging**
- Modules can be tested in isolation
- Bugs are localized to specific subsystems
- Mock implementations possible for testing

**4. Scalability**
- New features can be added to appropriate modules
- Modules can be refactored independently
- Code size per file stays manageable

**5. Code Organization**
- Logical grouping of related functionality
- Clear dependency graph
- Easier onboarding for new contributors

---

## Runtime Architecture

### Value Representation

All values in Hemlock are represented by the `Value` struct using a tagged union:

```c
typedef struct Value {
    ValueType type;  // Runtime type tag
    union {
        int32_t i32_value;
        int64_t i64_value;
        uint8_t u8_value;
        uint32_t u32_value;
        uint64_t u64_value;
        float f32_value;
        double f64_value;
        bool bool_value;
        char *string_value;
        uint32_t rune_value;
        void *ptr_value;
        Buffer *buffer_value;
        Array *array_value;
        Object *object_value;
        Function *function_value;
        File *file_value;
        Task *task_value;
        Channel *channel_value;
    };
} Value;
```

**Design decisions:**
- **Tagged union** for type safety while maintaining flexibility
- **Runtime type tags** enable dynamic typing with type checking
- **Direct value storage** for primitives (no boxing)
- **Pointer storage** for heap-allocated types (strings, objects, arrays)

### Memory Layout Examples

**Integer (i32):**
```
Value {
    type: TYPE_I32,
    i32_value: 42
}
```
- Total size: ~16 bytes (8-byte tag + 8-byte union)
- Stack allocated
- No heap allocation needed

**String:**
```
Value {
    type: TYPE_STRING,
    string_value: 0x7f8a4c000000  // Pointer to heap
}

Heap: "hello\0" (6 bytes, null-terminated UTF-8)
```
- Value is 16 bytes on stack
- String data is heap-allocated
- Must be freed manually

**Object:**
```
Value {
    type: TYPE_OBJECT,
    object_value: 0x7f8a4c001000  // Pointer to heap
}

Heap: Object {
    type_name: "Person",
    fields: [
        { name: "name", value: Value{TYPE_STRING, "Alice"} },
        { name: "age", value: Value{TYPE_I32, 30} }
    ],
    field_count: 2,
    capacity: 4
}
```
- Object structure on heap
- Fields stored in dynamic array
- Field values are embedded Value structs

### Environment Implementation

Variables are stored in environment chains:

```c
typedef struct Environment {
    HashMap *bindings;           // name → Value
    struct Environment *parent;  // Lexical parent scope
} Environment;
```

**Scope chain example:**
```
Global Scope: { print: <builtin>, args: <array> }
    ↑
Function Scope: { x: 10, y: 20 }
    ↑
Block Scope: { i: 0 }
```

**Lookup algorithm:**
1. Check current environment's hashmap
2. If not found, check parent environment
3. Repeat until found or reach global scope
4. Error if not found in any scope

---

## Type System Implementation

### Type Checking Strategy

Hemlock uses **runtime type checking** with **optional type annotations**:

```hemlock
let x = 42;           // No type check, infers i32
let y: u8 = 255;      // Runtime check: value must fit in u8
let z: i32 = x + y;   // Runtime check + type promotion
```

**Implementation flow:**
1. **Literal inference** - Lexer/parser determine initial type from literal
2. **Type annotation check** - If annotation present, validate at assignment
3. **Promotion** - Binary operations promote to common type
4. **Conversion** - Explicit conversions happen on demand

### Type Promotion Implementation

Type promotion follows a fixed hierarchy with precision preservation:

```c
// Simplified promotion logic
ValueType promote_types(ValueType a, ValueType b) {
    // f64 always wins
    if (a == TYPE_F64 || b == TYPE_F64) return TYPE_F64;

    // f32 with i64/u64 promotes to f64 (precision preservation)
    if (a == TYPE_F32 || b == TYPE_F32) {
        ValueType other = (a == TYPE_F32) ? b : a;
        if (other == TYPE_I64 || other == TYPE_U64) return TYPE_F64;
        return TYPE_F32;
    }

    // Larger integer types win
    int rank_a = get_type_rank(a);
    int rank_b = get_type_rank(b);
    return (rank_a > rank_b) ? a : b;
}
```

**Type ranks:**
- i8: 0
- u8: 1
- i16: 2
- u16: 3
- i32: 4
- u32: 5
- i64: 6
- u64: 7
- f32: 8
- f64: 9

### Duck Typing Implementation

Object type checking uses structural comparison:

```c
bool duck_type_check(Object *obj, TypeDef *type_def) {
    // Check all required fields
    for (each field in type_def) {
        if (!object_has_field(obj, field.name)) {
            return false;  // Missing field
        }

        Value *field_value = object_get_field(obj, field.name);
        if (!type_matches(field_value, field.type)) {
            return false;  // Wrong type
        }
    }

    return true;  // All required fields present and correct type
}
```

**Duck typing allows:**
- Extra fields in objects (ignored)
- Substructural typing (object can have more than required)
- Type name assignment after validation

---

## Memory Management

### Allocation Strategy

Hemlock uses **manual memory management** with two allocation primitives:

**1. Raw pointers (`ptr`):**
```c
void *alloc(size_t bytes) {
    void *ptr = malloc(bytes);
    if (!ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return ptr;
}
```
- Direct malloc/free
- No tracking
- User responsibility to free

**2. Buffers (`buffer`):**
```c
typedef struct Buffer {
    void *data;
    size_t length;
    size_t capacity;
} Buffer;

Buffer *create_buffer(size_t size) {
    Buffer *buf = malloc(sizeof(Buffer));
    buf->data = malloc(size);
    buf->length = size;
    buf->capacity = size;
    return buf;
}
```
- Tracks size and capacity
- Bounds checking on access
- Still requires manual free

### Heap-Allocated Types

**Strings:**
- UTF-8 byte array on heap
- Null-terminated for C interop
- Mutable (can modify in place)
- Refcounted (auto-freed when scope exits)

**Objects:**
- Dynamic field array
- Field names and values on heap
- Refcounted (auto-freed when scope exits)
- Circular references possible (handled with visited-set tracking)

**Arrays:**
- Dynamic capacity doubling growth
- Elements are embedded Value structs
- Automatic reallocation on growth
- Refcounted (auto-freed when scope exits)

**Closures:**
- Captures environment by reference
- Environment is heap-allocated
- Closure environments are properly freed when no longer referenced

---

## Concurrency Model

### Threading Architecture

Hemlock uses **1:1 threading** with POSIX threads (pthreads):

```
User Task          OS Thread          CPU Core
---------          ---------          --------
spawn(f1) ------>  pthread_create --> Core 0
spawn(f2) ------>  pthread_create --> Core 1
spawn(f3) ------>  pthread_create --> Core 2
```

**Key characteristics:**
- Each `spawn()` creates a new pthread
- Kernel schedules threads across cores
- True parallel execution (no GIL)
- Pre-emptive multitasking

### Task Implementation

```c
typedef struct Task {
    pthread_t thread;        // OS thread handle
    Value result;            // Return value
    char *error;             // Exception message (if thrown)
    pthread_mutex_t lock;    // Protects state
    TaskState state;         // RUNNING, FINISHED, ERROR
} Task;
```

**Task lifecycle:**
1. `spawn(func, args)` → Create Task, start pthread
2. Thread runs function with arguments
3. On return: Store result, set state to FINISHED
4. On exception: Store error message, set state to ERROR
5. `join(task)` → Wait for thread, return result or throw exception

### Channel Implementation

```c
typedef struct Channel {
    void **buffer;           // Circular buffer of Value*
    size_t capacity;         // Maximum buffered items
    size_t count;            // Current items in buffer
    size_t read_index;       // Next read position
    size_t write_index;      // Next write position
    bool closed;             // Channel closed flag
    pthread_mutex_t lock;    // Protects buffer
    pthread_cond_t not_full; // Signal when space available
    pthread_cond_t not_empty;// Signal when data available
} Channel;
```

**Send operation:**
1. Lock mutex
2. Wait if buffer full (cond_wait on not_full)
3. Write value to buffer[write_index]
4. Increment write_index (circular)
5. Signal not_empty
6. Unlock mutex

**Receive operation:**
1. Lock mutex
2. Wait if buffer empty (cond_wait on not_empty)
3. Read value from buffer[read_index]
4. Increment read_index (circular)
5. Signal not_full
6. Unlock mutex

**Synchronization guarantees:**
- Thread-safe send/recv (protected by mutex)
- Blocking semantics (producer waits if full, consumer waits if empty)
- Ordered delivery (FIFO within a channel)

---

## Future Plans

### Completed: Compiler Backend ✓

The compiler backend (`hemlockc`) has been implemented with:
- C code generation from AST
- Compile-time type checking (enabled by default)
- Runtime library (`libhemlock_runtime.a`)
- Full parity with interpreter (98% test pass rate)
- Unboxing optimization framework

### Current Focus: Type System Enhancements

**Recent improvements:**
- Unified type checking and type inference systems
- Compile-time type checking enabled by default
- `--check` flag for type-only validation
- Type context passed to codegen for optimization hints

### Future Enhancements

**Potential additions:**
- Generics/templates
- Pattern matching
- LSP integration for type-aware IDE support
- More aggressive unboxing optimizations
- Escape analysis for stack allocation

### Long-term Optimizations

**Possible improvements:**
- Inline caching for method calls
- JIT compilation for hot code paths
- Work-stealing scheduler for better concurrency
- Profile-guided optimization

---

## Implementation Guidelines

### Adding New Features

When implementing new features, follow these guidelines:

**1. Choose the right module:**
- New value types → `values.c`
- Type conversions → `types.c`
- Built-in functions → `builtins.c`
- I/O operations → `io.c`
- Control flow → `runtime.c`

**2. Update all layers:**
- Add AST node types if needed (`ast.h`, `ast.c`)
- Add lexer tokens if needed (`lexer.c`)
- Add parser rules (`parser.c`)
- Implement runtime behavior (`runtime.c` or appropriate module)
- Add tests (`tests/`)

**3. Maintain consistency:**
- Follow existing code style
- Use consistent naming conventions
- Document public API in headers
- Keep error messages clear and consistent

**4. Test thoroughly:**
- Add test cases before implementing
- Test success and error paths
- Test edge cases
- Verify no memory leaks (valgrind)

### Performance Considerations

**Current bottlenecks:**
- HashMap lookups for variable access
- Recursive function calls (no TCO)
- String concatenation (allocates new string each time)
- Type checking overhead on every operation

**Optimization opportunities:**
- Cache variable locations (inline caching)
- Tail call optimization
- String builder for concatenation
- Type inference to skip runtime checks

### Debugging Tips

**Useful tools:**
- `valgrind` - Memory leak detection
- `gdb` - Debugging crashes
- `-g` flag - Debug symbols
- `printf` debugging - Simple but effective

**Common issues:**
- Segfault → Null pointer dereference (check return values)
- Memory leak → Missing free() call (check value_free paths)
- Type error → Check type_convert() and type_check() logic
- Crash in threads → Race condition (check mutex usage)

---

## Conclusion

Hemlock's implementation prioritizes:
- **Modularity** - Clean separation of concerns
- **Simplicity** - Straightforward implementation
- **Explicitness** - No hidden magic
- **Maintainability** - Easy to understand and modify

The current tree-walking interpreter is intentionally simple to facilitate rapid feature development and experimentation. Future compiler backend will improve performance while maintaining the same semantics.
