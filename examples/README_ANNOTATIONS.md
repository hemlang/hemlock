# Compiler Annotation Examples

This directory contains practical examples demonstrating Hemlock's compiler helper annotations. These annotations provide explicit optimization hints to GCC/Clang via generated C `__attribute__(())` directives.

## Examples

### 1. `annotations_simple_demo.hml` - Quick Reference
**What it demonstrates:** All major annotations in one place
**Key annotations:** `@inline`, `@hot`, `@cold`, `@const`, `@pure`, `@flatten`, `@optimize`, `@section`, `@warn_unused`
**Best for:** Quick reference and learning annotation syntax

### 2. `annotations_vector_performance.hml` - Performance Optimization
**What it demonstrates:** High-performance numerical computing
**Key annotations:**
- `@const` on pure math functions
- `@inline @hot @optimize("3")` for inner loops
- `@hot @optimize("3")` for main computation
**Best for:** Understanding hot path optimization
**Use case:** Tight computational loops, scientific computing

### 3. `annotations_error_handling.hml` - Safety Annotations
**What it demonstrates:** Using `@warn_unused` to catch bugs
**Key annotations:** `@warn_unused`
**Best for:** Learning when return values should not be ignored
**Use case:** Allocation functions, error-returning functions, validation

### 4. `annotations_hot_cold_paths.hml` - Code Organization
**What it demonstrates:** Separating hot and cold execution paths
**Key annotations:**
- `@section(".text.hot")` `@hot` for frequently executed code
- `@section(".text.cold")` `@cold` `@optimize("s")` for error handlers
- `@section(".text.startup")` for initialization
**Best for:** Understanding cache locality optimization
**Use case:** Servers, daemons, long-running programs

### 5. `annotations_mixed_optimization.hml` - Optimization Levels
**What it demonstrates:** Using different optimization levels in one program
**Key annotations:**
- `@optimize("0")` - Debug functions (no optimization)
- `@optimize("s")` - Error handlers (minimize size)
- `@optimize("2")` - Business logic (balanced)
- `@optimize("3")` - Hot loops (maximum speed)
- `@optimize("fast")` - Math code (aggressive, less safe)
**Best for:** Understanding when to use each optimization level
**Use case:** Mixed workloads with different performance requirements

### 6. `annotations_pure_functional.hml` - Functional Programming
**What it demonstrates:** Pure function annotations and aggressive inlining
**Key annotations:**
- `@const` - No side effects, no global reads
- `@pure` - No side effects, can read globals
- `@flatten` - Inline all function calls inside
**Best for:** Understanding purity annotations and when to use `@flatten`
**Use case:** Functional pipelines, mathematical libraries

### 7. `annotations_realistic_server.hml` - Comprehensive Example
**What it demonstrates:** Real-world HTTP server request handler
**Combines:** All annotation types in a realistic scenario
**Key patterns:**
- Cold path: startup code with `@cold` `@section(".text.startup")`
- Hot path: request routing with `@hot` `@optimize("3")` `@flatten`
- Error handling: `@cold` `@optimize("s")` `@section(".text.cold")`
- Safety: `@warn_unused` on error checking functions
**Best for:** Seeing how annotations work together
**Use case:** Web servers, network services, request handlers

## Running Examples

### With Interpreter (warnings expected):
```bash
./hemlock examples/annotations_simple_demo.hml
```

The interpreter will show warnings about unknown annotations - this is expected. Annotations are compiler hints and don't affect interpreter execution.

### With Compiler (recommended):
```bash
# Generate C code to see attributes
./hemlockc -c examples/annotations_simple_demo.hml -o /tmp/demo.c
grep "__attribute__" /tmp/demo.c

# Compile and run
./hemlockc examples/annotations_simple_demo.hml -o /tmp/demo
/tmp/demo
```

## Quick Annotation Reference

| Annotation | Purpose | Example Use Case |
|------------|---------|------------------|
| `@inline` | Force inlining | Small helper functions |
| `@noinline` | Prevent inlining | Large functions, debug |
| `@hot` | Frequently executed | Main loops, request handlers |
| `@cold` | Rarely executed | Error handlers, logging |
| `@pure` | No side effects, reads globals | Math functions with config |
| `@const` | No side effects, no globals | Pure computation |
| `@flatten` | Inline ALL calls inside | Pipelines, wrappers |
| `@optimize("0")` | No optimization | Debug functions |
| `@optimize("1")` | Basic optimization | Light optimization |
| `@optimize("2")` | Standard optimization | Most code (default) |
| `@optimize("3")` | Aggressive optimization | Hot paths |
| `@optimize("s")` | Size optimization | Error handlers |
| `@optimize("fast")` | Fast math | Physics, graphics |
| `@section(name)` | Custom ELF section | Code organization |
| `@warn_unused` | Warn if ignored | Allocations, errors |

## Best Practices

1. **Start without annotations** - Measure first, optimize second
2. **Profile before annotating** - Use annotations where they matter
3. **@hot for hot paths** - Annotate 20% of code that runs 80% of time
4. **@cold for error handlers** - Save space on rarely-executed code
5. **@const for pure math** - Enable aggressive compiler optimizations
6. **@warn_unused for safety** - Catch bugs where errors are ignored
7. **@section for locality** - Group hot code together for better caching

## Performance Impact

Annotations provide hints but don't guarantee specific behavior:
- **@inline**: Compiler may still not inline if too complex
- **@hot/@cold**: Affects code layout and branch prediction
- **@optimize**: Overrides global `-O` flag per function
- **@section**: Custom placement can improve cache locality
- **@const/@pure**: Enables CSE, loop hoisting, reordering

## See Also

- `docs/annotations-implementation-summary.md` - Implementation details
- `CLAUDE.md` - Full annotation reference
- `tests/compiler/annotations/` - Test suite
