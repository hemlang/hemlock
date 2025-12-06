# Hemlock Documentation

Welcome to the Hemlock programming language documentation!

> A small, unsafe language for writing unsafe things safely.

## Table of Contents

### Getting Started
- [Installation](getting-started/installation.md) - Build and install Hemlock
- [Quick Start](getting-started/quick-start.md) - Your first Hemlock program
- [Tutorial](getting-started/tutorial.md) - Step-by-step guide to Hemlock basics

### Language Guide
- [Syntax Overview](language-guide/syntax.md) - Basic syntax and structure
- [Type System](language-guide/types.md) - Primitive types, type inference, and conversions
- [Memory Management](language-guide/memory.md) - Pointers, buffers, and manual memory
- [Strings](language-guide/strings.md) - UTF-8 strings and operations
- [Runes](language-guide/runes.md) - Unicode codepoints and character handling
- [Control Flow](language-guide/control-flow.md) - if/else, loops, switch, and operators
- [Functions](language-guide/functions.md) - Functions, closures, and recursion
- [Objects](language-guide/objects.md) - Object literals, methods, and duck typing
- [Arrays](language-guide/arrays.md) - Dynamic arrays and operations
- [Error Handling](language-guide/error-handling.md) - try/catch/finally/throw/panic
- [Modules](language-guide/modules.md) - Import/export system

### Advanced Topics
- [Async & Concurrency](advanced/async-concurrency.md) - True multi-threading with async/await
- [Bundling & Packaging](advanced/bundling-packaging.md) - Create bundles and standalone executables
- [Foreign Function Interface](advanced/ffi.md) - Call C functions from shared libraries
- [File I/O](advanced/file-io.md) - File operations and resource management
- [Signal Handling](advanced/signals.md) - POSIX signal handling
- [Command-Line Arguments](advanced/command-line-args.md) - Access program arguments
- [Command Execution](advanced/command-execution.md) - Execute shell commands

### API Reference
- [Type System Reference](reference/type-system.md) - Complete type reference
- [Operators Reference](reference/operators.md) - All operators and precedence
- [Built-in Functions](reference/builtins.md) - Global functions and constants
- [String API](reference/string-api.md) - String methods and properties
- [Array API](reference/array-api.md) - Array methods and properties
- [Memory API](reference/memory-api.md) - Memory allocation and manipulation
- [File API](reference/file-api.md) - File I/O methods
- [Concurrency API](reference/concurrency-api.md) - Tasks and channels

### Design & Philosophy
- [Design Philosophy](design/philosophy.md) - Core principles and goals
- [Implementation Details](design/implementation.md) - How Hemlock works internally

### Contributing
- [Contributing Guidelines](contributing/guidelines.md) - How to contribute
- [Testing Guide](contributing/testing.md) - Writing and running tests

## Quick Reference

### Hello World
```hemlock
print("Hello, World!");
```

### Basic Types
```hemlock
let x: i32 = 42;           // 32-bit signed integer
let y: u8 = 255;           // 8-bit unsigned integer
let pi: f64 = 3.14159;     // 64-bit float
let name: string = "Alice"; // UTF-8 string
let flag: bool = true;     // Boolean
let ch: rune = '🚀';       // Unicode codepoint
```

### Memory Management
```hemlock
// Safe buffer (recommended)
let buf = buffer(64);
buf[0] = 65;
free(buf);

// Raw pointer (for experts)
let ptr = alloc(64);
memset(ptr, 0, 64);
free(ptr);
```

### Async/Concurrency
```hemlock
async fn compute(n: i32): i32 {
    return n * n;
}

let task = spawn(compute, 42);
let result = join(task);  // 1764
```

## Philosophy

Hemlock is **explicit over implicit**, always:
- Semicolons are mandatory
- Manual memory management (no GC)
- Optional type annotations with runtime checks
- Unsafe operations are allowed (your responsibility)

We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).

## Getting Help

- **Source Code**: [GitHub Repository](https://github.com/nbeerbower/hemlock)
- **Issues**: Report bugs and request features
- **Examples**: See the `examples/` directory
- **Tests**: See the `tests/` directory for usage examples

## License

Hemlock is released under the MIT License.
