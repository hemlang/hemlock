# Testing Guide for Hemlock

This guide explains Hemlock's testing philosophy, how to write tests, and how to run the test suite.

---

## Table of Contents

- [Testing Philosophy](#testing-philosophy)
- [Test Suite Structure](#test-suite-structure)
- [Running Tests](#running-tests)
- [Documentation Checks](#documentation-checks)
- [Writing Tests](#writing-tests)
- [Test Categories](#test-categories)
- [Memory Leak Testing](#memory-leak-testing)
- [Continuous Integration](#continuous-integration)
- [Best Practices](#best-practices)

---

## Testing Philosophy

### Core Principles

**1. Test-Driven Development (TDD)**

Write tests **before** implementing features:

```
1. Write a failing test
2. Implement the feature
3. Run the test (should pass)
4. Refactor if needed
5. Repeat
```

**Benefits:**
- Ensures features actually work
- Prevents regressions
- Documents expected behavior
- Makes refactoring safer

**2. Comprehensive Coverage**

Test both success and failure cases:

```hemlock
// Success case
let x: u8 = 255;  // Should work

// Failure case
let y: u8 = 256;  // Should error
```

**3. Test Early and Often**

Run tests:
- Before committing code
- After making changes
- Before submitting pull requests
- During code review

**Rule:** All tests must pass before merging.

### What to Test

**Always test:**
- ✅ Basic functionality (happy path)
- ✅ Error conditions (sad path)
- ✅ Edge cases (boundary conditions)
- ✅ Type checking and conversions
- ✅ Memory management (no leaks)
- ✅ Concurrency and race conditions

**Example test coverage:**
```hemlock
// Feature: String.substr(start, length)

// Happy path
print("hello".substr(0, 5));  // "hello"

// Edge cases
print("hello".substr(0, 0));  // "" (empty)
print("hello".substr(5, 0));  // "" (at end)
print("hello".substr(2, 100)); // "llo" (past end)

// Error cases
// "hello".substr(-1, 5);  // Error: negative index
// "hello".substr(0, -1);  // Error: negative length
```

---

## Test Suite Structure

### Directory Organization

The suite is organized by feature area and subsystem rather than by one fixed, exhaustive tree. Common groups include:

- `tests/<language-feature>/` for language semantics such as arithmetic, arrays, async, bitwise operators, control flow, conversions, enums, functions, loops, objects, optional chaining, pointers, primitives, strings, and error handling.
- `tests/stdlib_<module>/` for standard library modules, mirroring `stdlib/<module>.hml` where practical.
- `tests/parity/` for interpreter/compiler parity coverage.
- `tests/compiler/`, `tests/formatter/`, `tests/lsp/`, `tests/bundler/`, and `tests/ast_serialize/` for tooling and compiler-specific behavior.
- `tests/memory/`, `tests/ffi/`, `tests/networking/`, `tests/signals/`, and similar directories for systems/runtime features.
- `tests/manual/` for opt-in tests that require special environment setup.

Use the test runner summary or `find tests -name '*.hml' | wc -l` for current test counts instead of hard-coding totals in documentation.

### Test File Naming

**Conventions:**
- Use descriptive names: `method_chaining.hml` not `test1.hml`
- Group related tests: `string_substr.hml`, `string_slice.hml`
- One feature area per file
- Keep files focused and small

---

## Running Tests

### Run All Tests

```bash
# From hemlock root directory
make test

# Or directly
./tests/run_tests.sh
```

**Output:**
```
Running tests in tests/primitives/...
  ✓ integers.hml
  ✓ floats.hml
  ✓ booleans.hml

Running tests in tests/strings/...
  ✓ concat.hml
  ✓ methods.hml

...

Total: 251 tests
Passed: 251
Failed: 0
```

### Run Specific Category

```bash
# Run only string tests
./tests/run_tests.sh tests/strings/

# Run only one test file
./tests/run_tests.sh tests/strings/concat.hml

# Run multiple categories
./tests/run_tests.sh tests/strings/ tests/arrays/
```

### Run with Valgrind (Memory Leak Check)

```bash
# Check single test for leaks
valgrind --leak-check=full ./hemlock tests/memory/alloc.hml

# Check all tests (slow!)
for test in tests/**/*.hml; do
    echo "Testing $test"
    valgrind --leak-check=full --error-exitcode=1 ./hemlock "$test"
done
```

### Debug Failed Tests

```bash
# Run with verbose output
./hemlock tests/failing_test.hml

# Run with gdb
gdb --args ./hemlock tests/failing_test.hml
(gdb) run
(gdb) backtrace  # if it crashes
```


## Documentation Checks

Run the lightweight documentation audit after editing Markdown files, stdlib modules, or documentation indexes:

```bash
python3 tests/check_docs.py
```

This check is dependency-free and verifies that:

- Relative Markdown links point to existing files or directories.
- Every `stdlib/*.hml` implementation has a matching `stdlib/docs/*.md` API document.
- Every stdlib API document has a matching implementation.
- `docs/reference/stdlib-overview.md` links every stdlib module document.

---

## Writing Tests

### Test File Format

Test files are just Hemlock programs with expected output:

**Example: tests/primitives/integers.hml**
```hemlock
// Test basic integer literals
let x = 42;
print(x);  // Expect: 42

let y: i32 = 100;
print(y);  // Expect: 100

// Test arithmetic
let sum = x + y;
print(sum);  // Expect: 142

// Test type inference
let small = 10;
print(typeof(small));  // Expect: i32

let large = 5000000000;
print(typeof(large));  // Expect: i64
```

**How tests work:**
1. Test runner executes the .hml file
2. Captures stdout output
3. Compares with expected output (from comments or separate .out file)
4. Reports pass/fail

### Expected Output Methods

**Method 1: Inline comments (recommended for simple tests)**

```hemlock
print("hello");  // Expect: hello
print(42);       // Expect: 42
```

The test runner parses `// Expect: ...` comments.

**Method 2: Separate .out file**

Create `test_name.hml.out` with expected output:

**test_name.hml:**
```hemlock
print("line 1");
print("line 2");
print("line 3");
```

**test_name.hml.out:**
```
line 1
line 2
line 3
```

### Testing Error Cases

Error tests should cause the program to exit with non-zero status:

**Example: tests/primitives/range_error.hml**
```hemlock
// This should fail with a type error
let x: u8 = 256;  // Out of range for u8
```

**Expected behavior:**
- Program exits with non-zero status
- Prints error message to stderr

**Test runner handling:**
- Tests expecting errors should be in separate files
- Use naming convention: `*_error.hml` or `*_fail.hml`
- Document expected error in comments

### Testing Success Cases

**Example: tests/strings/methods.hml**
```hemlock
// Test substr
let s = "hello world";
let sub = s.substr(6, 5);
print(sub);  // Expect: world

// Test find
let pos = s.find("world");
print(pos);  // Expect: 6

// Test contains
let has = s.contains("lo");
print(has);  // Expect: true

// Test trim
let padded = "  hello  ";
let trimmed = padded.trim();
print(trimmed);  // Expect: hello
```

### Testing Edge Cases

**Example: tests/arrays/edge_cases.hml**
```hemlock
// Empty array
let empty = [];
print(empty.length);  // Expect: 0

// Single element
let single = [42];
print(single[0]);  // Expect: 42

// Negative index (should error in separate test file)
// print(single[-1]);  // Error

// Past-end index (should error)
// print(single[100]);  // Error

// Boundary conditions
let arr = [1, 2, 3];
print(arr.slice(0, 0));  // Expect: [] (empty)
print(arr.slice(3, 3));  // Expect: [] (empty)
print(arr.slice(1, 2));  // Expect: [2]
```

### Testing Type System

**Example: tests/conversions/promotion.hml**
```hemlock
// Test type promotion in binary operations

// i32 + i64 -> i64
let a: i32 = 10;
let b: i64 = 20;
let c = a + b;
print(typeof(c));  // Expect: i64

// i32 + f32 -> f32
let d: i32 = 10;
let e: f32 = 3.14;
let f = d + e;
print(typeof(f));  // Expect: f32

// u8 + i32 -> i32
let g: u8 = 5;
let h: i32 = 10;
let i = g + h;
print(typeof(i));  // Expect: i32
```

### Testing Concurrency

**Example: tests/async/basic.hml**
```hemlock
async fn compute(n: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}

// Spawn tasks
let t1 = spawn(compute, 10);
let t2 = spawn(compute, 20);

// Join and print results
let r1 = join(t1);
let r2 = join(t2);
print(r1);  // Expect: 45
print(r2);  // Expect: 190
```

### Testing Exceptions

**Example: tests/exceptions/try_catch.hml**
```hemlock
// Test basic try/catch
try {
    throw "error message";
} catch (e) {
    print("Caught: " + e);  // Expect: Caught: error message
}

// Test finally
let executed = false;
try {
    print("try");  // Expect: try
} finally {
    executed = true;
    print("finally");  // Expect: finally
}

// Test exception propagation
fn risky(): i32 {
    throw "failure";
}

try {
    risky();
} catch (e) {
    print(e);  // Expect: failure
}
```

---

## Test Categories

### Primitives Tests

**What to test:**
- Integer types (i8, i16, i32, i64, u8, u16, u32, u64)
- Float types (f32, f64)
- Boolean type
- String type
- Rune type
- Null type

**Example areas:**
- Literal syntax
- Type inference
- Range checking
- Overflow behavior
- Type annotations

### Conversion Tests

**What to test:**
- Implicit type promotion
- Explicit type conversion
- Lossy conversions (should error)
- Type promotion in operations
- Cross-type comparisons

### Memory Tests

**What to test:**
- alloc/free correctness
- Buffer creation and access
- Bounds checking on buffers
- memset, memcpy, realloc
- Memory leak detection (valgrind)

### String Tests

**What to test:**
- Concatenation
- All 18 string methods
- UTF-8 handling
- Rune indexing
- String + rune concatenation
- Edge cases (empty strings, single char, etc.)

### Control Flow Tests

**What to test:**
- if/else/else if
- while loops
- for loops
- switch statements
- break/continue
- return statements

### Function Tests

**What to test:**
- Function definition and calling
- Parameter passing
- Return values
- Recursion
- Closures and capture
- First-class functions
- Anonymous functions

### Object Tests

**What to test:**
- Object literals
- Field access and assignment
- Methods and self binding
- Duck typing
- Optional fields
- JSON serialization/deserialization
- Circular reference detection

### Array Tests

**What to test:**
- Array creation
- Indexing and assignment
- All 15 array methods
- Mixed types
- Dynamic resizing
- Edge cases (empty, single element)

### Exception Tests

**What to test:**
- try/catch/finally
- throw statement
- Exception propagation
- Nested try/catch
- Return in try/catch/finally
- Uncaught exceptions

### I/O Tests

**What to test:**
- File opening modes
- Read/write operations
- Seek/tell
- File properties
- Error handling (missing files, etc.)
- Resource cleanup

### Async Tests

**What to test:**
- spawn/join/detach
- Channel send/recv
- Exception propagation in tasks
- Multiple concurrent tasks
- Channel blocking behavior

### FFI Tests

**What to test:**
- dlopen/dlclose
- dlsym
- dlcall with various types
- Type conversion
- Error handling

---

## Memory Leak Testing

### Using Valgrind

**Basic usage:**
```bash
valgrind --leak-check=full ./hemlock test.hml
```

**Example output (no leaks):**
```
==12345== HEAP SUMMARY:
==12345==     in use at exit: 0 bytes in 0 blocks
==12345==   total heap usage: 10 allocs, 10 frees, 1,024 bytes allocated
==12345==
==12345== All heap blocks were freed -- no leaks are possible
```

**Example output (with leak):**
```
==12345== LEAK SUMMARY:
==12345==    definitely lost: 64 bytes in 1 blocks
==12345==    indirectly lost: 0 bytes in 0 blocks
==12345==      possibly lost: 0 bytes in 0 blocks
==12345==    still reachable: 0 bytes in 0 blocks
==12345==         suppressed: 0 bytes in 0 blocks
```

### Common Leak Sources

**1. Missing free() calls:**
```c
// BAD
char *str = malloc(100);
// ... use str
// Forgot to free!

// GOOD
char *str = malloc(100);
// ... use str
free(str);
```

**2. Lost pointers:**
```c
// BAD
char *ptr = malloc(100);
ptr = malloc(200);  // Lost reference to first allocation!

// GOOD
char *ptr = malloc(100);
free(ptr);
ptr = malloc(200);
```

**3. Exception paths:**
```c
// BAD
void func() {
    char *data = malloc(100);
    if (error_condition) {
        return;  // Leak!
    }
    free(data);
}

// GOOD
void func() {
    char *data = malloc(100);
    if (error_condition) {
        free(data);
        return;
    }
    free(data);
}
```

### Known Acceptable Leaks

Some small "leaks" are intentional startup allocations:

**Global built-ins:**
```hemlock
// Built-in functions, FFI types, and constants are allocated at startup
// and not freed at exit (typically ~200 bytes)
```

These are not true leaks - they're one-time allocations that persist for the program lifetime and are cleaned up by the OS on exit.

---

## Continuous Integration

### GitHub Actions (Future)

Once CI is set up, all tests will run automatically on:
- Push to main branch
- Pull request creation/update
- Scheduled daily runs

**CI workflow:**
1. Build Hemlock
2. Run test suite
3. Check for memory leaks (valgrind)
4. Report results on PR

### Pre-Commit Checks

Before committing, run:

```bash
# Build fresh
make clean && make

# Run all tests
make test

# Check a few tests for leaks
valgrind --leak-check=full ./hemlock tests/memory/alloc.hml
valgrind --leak-check=full ./hemlock tests/strings/concat.hml
```

---

## Best Practices

### Do's

✅ **Write tests first (TDD)**
```bash
1. Create tests/feature/new_feature.hml
2. Implement feature in src/
3. Run tests until they pass
```

✅ **Test both success and failure**
```hemlock
// Success: tests/feature/success.hml
let result = do_thing();
print(result);  // Expect: expected value

// Failure: tests/feature/failure.hml
do_invalid_thing();  // Should error
```

✅ **Use descriptive test names**
```
Good: tests/strings/substr_utf8_boundary.hml
Bad:  tests/test1.hml
```

✅ **Keep tests focused**
- One feature area per file
- Clear setup and assertions
- Minimal code

✅ **Add comments explaining tricky tests**
```hemlock
// Test that closure captures outer variable by reference
fn outer() {
    let x = 10;
    let f = fn() { return x; };
    x = 20;  // Modify after closure creation
    return f();  // Should return 20, not 10
}
```

✅ **Test edge cases**
- Empty inputs
- Null values
- Boundary values (min/max)
- Large inputs
- Negative values

### Don'ts

❌ **Don't skip tests**
- All tests must pass before merging
- Don't comment out failing tests
- Fix the bug or remove the feature

❌ **Don't write tests that depend on each other**
```hemlock
// BAD: test2.hml depends on test1.hml output
// Tests should be independent
```

❌ **Don't use random values in tests**
```hemlock
// BAD: Non-deterministic
let x = random();
print(x);  // Can't predict output

// GOOD: Deterministic
let x = 42;
print(x);  // Expect: 42
```

❌ **Don't test implementation details**
```hemlock
// BAD: Testing internal structure
let obj = { x: 10 };
// Don't check internal field order, capacity, etc.

// GOOD: Testing behavior
print(obj.x);  // Expect: 10
```

❌ **Don't ignore memory leaks**
- All tests should be valgrind-clean
- Document known/acceptable leaks
- Fix leaks before merging

### Test Maintenance

**When to update tests:**
- Feature behavior changes
- Bug fixes require new test cases
- Edge cases discovered
- Performance improvements

**When to remove tests:**
- Feature removed from language
- Test duplicates existing coverage
- Test was incorrect

**Refactoring tests:**
- Group related tests together
- Extract common setup code
- Use consistent naming
- Keep tests simple and readable

---

## Example Test Session

Here's a complete example of adding a feature with tests:

### Feature: Add `array.first()` method

**1. Write the test first:**

```bash
# Create test file
cat > tests/arrays/first_method.hml << 'EOF'
// Test array.first() method

// Basic case
let arr = [1, 2, 3];
print(arr.first());  // Expect: 1

// Single element
let single = [42];
print(single.first());  // Expect: 42

// Empty array (should error - separate test file)
// let empty = [];
// print(empty.first());  // Error
EOF
```

**2. Run the test (should fail):**

```bash
./hemlock tests/arrays/first_method.hml
# Error: Method 'first' not found on array
```

**3. Implement the feature:**

Edit `src/interpreter/builtins.c`:

```c
// Add array_first method
Value *array_first(Value *self, Value **args, int arg_count)
{
    if (self->array_value->length == 0) {
        fprintf(stderr, "Error: Cannot get first element of empty array\n");
        exit(1);
    }

    return value_copy(&self->array_value->elements[0]);
}

// Register in array method table
// ... add to array method registration
```

**4. Run the test (should pass):**

```bash
./hemlock tests/arrays/first_method.hml
1
42
# Success!
```

**5. Check for memory leaks:**

```bash
valgrind --leak-check=full ./hemlock tests/arrays/first_method.hml
# All heap blocks were freed -- no leaks are possible
```

**6. Run full test suite:**

```bash
make test
# Total: <current count> tests (<previous count> + new one)
# Passed: <current count>
# Failed: 0
```

**7. Commit:**

```bash
git add tests/arrays/first_method.hml src/interpreter/builtins.c
git commit -m "Add array.first() method with tests"
```

---

## Summary

**Remember:**
- Write tests first (TDD)
- Test success and failure cases
- Run all tests before committing
- Check for memory leaks
- Document known issues
- Keep tests simple and focused

**Test quality is just as important as code quality!**
