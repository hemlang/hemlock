# Hemlock Test Suite

This directory contains the test suite for the Hemlock interpreter, compiler/parity checks, standard library modules, tooling, and documentation consistency.

## Running Tests

To run all tests from the project root:

```bash
bash tests/run_tests.sh
# or use the Makefile target
make test
```

The test runner will:
- Build the project when invoked through `make test`
- Run tests organized by category
- Report results with colored output
- Show a summary at the end

Documentation-only changes can also be checked without building Hemlock:

```bash
python3 tests/check_docs.py
```

The documentation audit verifies relative Markdown links and confirms every `stdlib/*.hml` module has a matching `stdlib/docs/*.md` file.

## Test Organization

Tests are organized by feature area and subsystem. Common top-level groups include:

- **Language semantics** - arithmetic, arrays, async, bitwise operators, control flow, conversions, enums, error handling, exceptions, functions, interpolation, loops, objects, optional chaining, pointers, primitives, and strings.
- **Runtime and systems features** - buffers, circular references, defer, exec, FFI, file I/O, memory, networking, signals, and command-line args.
- **Compiler and tooling** - compiler checks, interpreter/compiler parity tests, formatter tests, LSP tests, bundler tests, and AST serialization.
- **Standard library modules** - `tests/stdlib_<module>/` directories mirror `stdlib/<module>.hml` where practical.
- **Regression and manual tests** - focused reproductions for bugs and opt-in tests that require special environment setup.

Use `find tests -name '*.hml' | wc -l` or the test runner summary when you need current counts.

## Test Naming Conventions

Tests with certain keywords in their names are expected to fail (error tests):
- **overflow** - Tests that values exceed type bounds
- **negative** - Tests with negative values where they're not allowed
- **invalid** - Tests with invalid syntax or semantics
- **error** - General error cases

These tests verify that the interpreter correctly rejects invalid code.

## Writing New Tests

To add a new test:

1. Create a `.hml` file in the appropriate category directory
2. For tests that should pass: use any descriptive name
3. For tests that should fail: include `overflow`, `negative`, `invalid`, or `error` in the filename
4. Run `make test` (or `bash tests/run_tests.sh`) from the project root to verify your test

## Current Test Coverage

The repository now contains a broad Hemlock test corpus (language, runtime, compiler parity, stdlib, LSP, formatter, bundler, and regression tests). Avoid hard-coding exact totals in this file; use the test runners for current counts.

### Coverage Areas

The test suite covers:
- ✅ All arithmetic operators (+, -, *, /)
- ✅ Operator precedence
- ✅ Unary negation
- ✅ All comparison operators (==, !=, <, >, <=, >=)
- ✅ Boolean operators (&&, ||, !)
- ✅ Control flow (if, if-else, nested if, while loops)
- ✅ Type conversions and promotions
- ✅ All primitive types (i8, i16, i32, u8, u16, u32, f32, f64)
- ✅ Range checking for typed variables
- ✅ String operations (concatenation, indexing, mutation, length)
- ✅ Memory management (alloc, free, memset, memcpy)
- ✅ Pointer arithmetic
- ✅ Variable reassignment
- ✅ Error handling (division by zero, overflow, underflow)
