# Hemlock Missing Features Analysis

This document identifies features that are missing, partially implemented, or could be added to the Hemlock programming language.

---

## Documented But Not Fully Implemented

### 1. Pass-by-Reference Parameters (`ref` keyword)
- **Status**: Keyword parsed but NOT implemented
- **Location**: `docs/language-guide/functions.md`
- **Impact**: Cannot pass primitives by reference to modify them in functions
- **Priority**: Medium

### 2. Documentation Mismatch (stdlib/README.md)
The following modules are listed as "Future Modules" but are **actually implemented**:
- `path` - Path manipulation (stdlib/path.hml exists)
- `crypto` - Cryptographic functions (stdlib/crypto.hml exists)
- `compression` - zlib/gzip compression (stdlib/compression.hml exists)

**Action needed**: Update stdlib/README.md to reflect reality.

---

## Language Features Under Consideration

These are documented in `docs/design/philosophy.md` as potential additions:

### 1. Pattern Matching
```hemlock
match (value) {
    case i32: print("integer");
    case string: print("text");
    case _: print("other");
}
```

### 2. Result/Error Types
`Result<T, E>` type for explicit error handling as alternative to exceptions.

### 3. Generics/Templates
Currently no way to write generic data structures or functions.

### 4. Fixed-Size Arrays (Stack Allocation)
Explicit stack vs. heap allocation for arrays.

---

## Missing Control Flow Features

| Feature | Description | Priority |
|---------|-------------|----------|
| Do-While Loops | `do { } while (condition)` construct | Low |
| Labeled Breaks | `break outer;` to exit nested loops | Medium |
| Labeled Continues | `continue outer;` for nested loops | Medium |

---

## Missing Type System Features

| Feature | Description | Priority |
|---------|-------------|----------|
| Union Types | `type Result = Success \| Error` | Medium |
| Tuple Types | First-class tuples | Low |
| Type Aliases | `type UserId = i32` for custom types | Medium |
| Intersection Types | Combining multiple types | Low |

---

## Missing Operators & Syntax

| Feature | Status | Priority |
|---------|--------|----------|
| `/=` compound assignment | Missing (others exist: `+=`, `-=`, `*=`, etc.) | High |
| `**` exponentiation | Must use `pow()` | Low |
| `\|>` pipeline operator | Not implemented | Low |
| `<=>` spaceship operator | Three-way comparison | Low |
| Spread operator | `[...arr]`, `{...obj}` | Medium |
| Destructuring | `let [a, b] = arr;` | Medium |

---

## Missing Function Features

| Feature | Description | Priority |
|---------|-------------|----------|
| Function Overloading | Multiple functions with same name | Low (intentional) |
| Tail Call Optimization | Deep recursion limited by stack | Medium |
| Generators/Yield | `function*` and `yield` | Medium |
| Async Generators | Async iteration protocol | Low |

---

## Missing OOP Features (Intentional)

These are intentionally not included, per Hemlock's design philosophy:
- Classes (uses `define` for structs instead)
- Inheritance (composition over inheritance)
- Interfaces/Traits

---

## Missing Concurrency Features

| Feature | Description | Priority |
|---------|-------------|----------|
| Work-Stealing Scheduler | Uses basic pthread model | Low |
| Coroutines | Current async uses real threads | Medium |
| Select with Send | `select()` only receives | Medium |

---

## Missing Built-in Functions

| Function | Description | Priority |
|----------|-------------|----------|
| `range()` | Generate number sequences | High |
| `enumerate()` | Index + value iteration | Medium |
| `zip()` | Parallel iteration | Medium |
| `string()` | Direct string conversion constructor | Medium |

---

## Standard Library Gaps

### Missing Modules
| Module | Description | Priority |
|--------|-------------|----------|
| XML Parser | JSON/TOML exist, XML missing | Low |
| YAML Parser | No YAML support | Low |
| Debugger/Profiler | Built-in debugging tools | Medium |
| Additional DBs | Only SQLite, no MySQL/PostgreSQL | Low |

### Partial/Basic Modules
| Module | Status | Issue |
|--------|--------|-------|
| `time` | Basic | Only ~13 lines, minimal functionality |
| `regex` | Basic | Uses POSIX ERE, limited features |
| `fs` | Partial | Some tests incomplete |

---

## FFI Limitations

| Limitation | Description | Priority |
|------------|-------------|----------|
| C++ Interop | Only C FFI, no C++ name mangling | Low |
| Variadic C Functions | Cannot call `printf`, etc. | Medium |
| Struct Padding Control | No control over layout | Low |

---

## Performance Features (Future)

These are listed as potential future optimizations:
- Escape Analysis
- Inline Caching
- JIT Compilation
- Profile-Guided Optimization
- Link-Time Optimization
- SIMD intrinsics

---

## Development/Testing Gaps

| Feature | Description | Priority |
|---------|-------------|----------|
| Built-in Benchmarking | No benchmarking in testing module | Medium |
| Code Coverage | No coverage tools | Low |
| Memory Sanitizer | Not integrated | Low |

---

## What IS Implemented (Recent Additions)

For reference, these features are confirmed working:
- ✅ Optional chaining (`?.`)
- ✅ Null coalescing (`??`)
- ✅ Rest parameters (`...args`)
- ✅ Compile-time type checking (hemlockc)
- ✅ LSP server with full IDE features
- ✅ All 39 stdlib modules
- ✅ Atomic operations for lock-free concurrency
- ✅ Compound bitwise operators (`&=`, `|=`, `^=`, `<<=`, `>>=`, `%=`)
- ✅ FFI with `export extern fn`
- ✅ 121 parity tests at 100% pass rate

---

## Summary

| Category | Count |
|----------|-------|
| Major Language Features Missing | 15-20 |
| Operators Missing | 5-6 |
| Stdlib Gaps | 5-10 |
| Documentation Outdated | 3 items |
| Partially Implemented | 3 features |

---

## Recommended Priorities

### High Priority
1. Implement `/=` compound assignment operator
2. Add `range()` builtin function
3. Update stdlib/README.md documentation

### Medium Priority
1. Implement `ref` keyword or remove from parser
2. Add destructuring for arrays and objects
3. Improve `time` module functionality
4. Add labeled break/continue for nested loops

### Low Priority
1. Pattern matching (under consideration)
2. Generics (significant undertaking)
3. Additional stdlib modules
4. Performance optimizations

---

*Generated: 2026-01-03*
