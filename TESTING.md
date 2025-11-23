# Hemlock Testing Guide

Comprehensive guide to Hemlock's test suite and testing infrastructure.

## Quick Start

```bash
# Run all tests
make test

# Or run the test script directly
./tests/run_tests.sh
```

## Test Organization

```
tests/
├── run_tests.sh           # Main test runner
├── primitives/            # Type system tests
├── conversions/           # Type conversion tests
├── memory/                # Pointer and buffer tests
├── strings/               # String operation tests
├── arrays/                # Array method tests
├── control/               # Control flow tests
├── loops/                 # Loop tests (for, while, break, continue)
├── functions/             # Function and closure tests
├── objects/               # Object, method, duck typing tests
├── exceptions/            # Try/catch/finally/throw tests
├── io/                    # File I/O tests
├── args/                  # Command-line argument tests
├── stdlib_collections/    # Collections module tests
├── stdlib_math/           # Math module tests
├── stdlib_time/           # Time module tests
├── stdlib_env/            # Environment module tests
├── stdlib_net/            # Networking module tests (TCP/UDP)
├── stdlib_regex/          # Regular expression tests
├── stdlib_http/           # HTTP module tests
│   ├── README.md          # HTTP test documentation
│   ├── test_http_basic.hml
│   └── test_http_requests.hml
└── stdlib_websocket/      # WebSocket module tests
    ├── README.md          # WebSocket test documentation
    ├── test_websocket_basic.hml
    ├── test_websocket_echo.hml
    └── test_websocket_server.hml
```

## Test Categories

### Core Language Tests

**Primitives** - Type system fundamentals
- Integer types (i8-i64, u8-u64)
- Floating point (f32, f64)
- Booleans, null
- String basics
- Rune (Unicode codepoint) type

**Conversions** - Type promotion and casting
- Implicit type promotion
- Explicit type conversions
- Range checking

**Memory** - Manual memory management
- alloc/free operations
- Pointer arithmetic
- Buffer operations
- Memory safety edge cases

**Strings** - String operations
- UTF-8 support
- String methods (18 total)
- Concatenation
- Substring operations

**Arrays** - Dynamic arrays
- Array methods (15 total)
- Mixed type arrays
- Typed arrays

**Objects** - Object system
- Object literals
- Methods and `self` keyword
- Duck typing
- JSON serialization

**Control Flow** - Conditionals and loops
- if/else/else if
- while loops
- for loops
- switch statements
- break/continue
- defer

**Functions** - Functions and closures
- Named functions
- Anonymous functions
- Closures
- Recursion

**Exceptions** - Error handling
- try/catch/finally
- throw
- panic
- Exception propagation

**I/O** - File operations
- File object API
- read/write operations
- Binary I/O

### Standard Library Tests

**Collections** - Data structures
- HashMap
- Queue
- Stack
- Set
- LinkedList

**Math** - Mathematical functions
- Trigonometry
- Exponential/logarithmic
- Random numbers

**Time** - Time operations
- Timestamps
- Sleep/delays
- Benchmarking

**Environment** - Environment variables
- getenv/setenv
- Process control
- exit codes

**Networking** - TCP/UDP sockets
- TcpListener
- TcpStream
- UdpSocket
- DNS resolution

**Regular Expressions** - Pattern matching
- POSIX ERE syntax
- Compile and test
- Case-insensitive matching

**HTTP** - HTTP client
- GET/POST requests
- HTTPS support
- JSON handling
- See [tests/stdlib_http/README.md](tests/stdlib_http/README.md)

**WebSocket** - WebSocket client/server
- WebSocket connections
- Bidirectional messaging
- Server functionality
- See [tests/stdlib_websocket/README.md](tests/stdlib_websocket/README.md)

## Running Tests

### Run All Tests

```bash
make test
# or
./tests/run_tests.sh
```

### Run Specific Category

```bash
# Run all tests in a category
./hemlock tests/strings/*.hml
./hemlock tests/arrays/*.hml
./hemlock tests/stdlib_http/*.hml
```

### Run Single Test

```bash
./hemlock tests/strings/test_string_methods.hml
./hemlock tests/stdlib_http/test_http_basic.hml
```

## Test Output

**Successful test:**
```
✓ primitives/test_i32.hml
```

**Failed test:**
```
✗ primitives/test_overflow.hml
  Error: Runtime error: Integer overflow
```

**Expected error test:**
```
✓ primitives/test_overflow.hml (expected error)
```

Tests with `overflow`, `negative`, `invalid`, or `error` in their name are expected to fail (error tests).

## Test Summary Format

```
======================================
              Summary
======================================
Passed:           347
Error tests:      25 (expected failures)
Failed:           0
======================================

All 372 tests behaved as expected! 🎉
```

## Writing Tests

### Basic Test Structure

```hemlock
// tests/my_category/test_feature.hml

print("Testing feature X...");

let tests_passed = 0;
let tests_failed = 0;

// Test 1
print("Test 1: Basic functionality");
try {
    assert(condition, "Description of what should be true");
    print("✓ Test 1 passed");
    tests_passed = tests_passed + 1;
} catch (e) {
    print("✗ Test 1 failed: " + e);
    tests_failed = tests_failed + 1;
}

// Test 2
print("");
print("Test 2: Edge cases");
try {
    // Test code...
    tests_passed = tests_passed + 1;
} catch (e) {
    print("✗ Test 2 failed: " + e);
    tests_failed = tests_failed + 1;
}

// Summary
print("");
print("========================================");
print("Test Summary:");
print("  Passed: " + typeof(tests_passed));
print("  Failed: " + typeof(tests_failed));
print("========================================");

// Assert all tests passed
assert(tests_failed == 0, "All tests should pass");
```

### Error Tests

For tests that should fail (testing error handling):

```hemlock
// tests/category/test_feature_error.hml
// The "error" keyword in filename marks this as expected to fail

print("Testing error condition...");

// This should throw an error
let invalid_operation = 10 / 0;  // Division by zero

// Should not reach here
assert(false, "Should have thrown error");
```

### Network Tests

Tests requiring network connectivity should:
- Check for connectivity issues gracefully
- Use reliable test endpoints (httpbin.org, echo.websocket.org)
- Include timeout handling
- Document network requirements in comments

Example:
```hemlock
// Requires: libwebsockets-dev, network connectivity
import * as http from "@stdlib/http";

try {
    let response = http.get("http://httpbin.org/get", []);
    if (response.status_code != 200) {
        throw "HTTP request failed";
    }
} catch (e) {
    print("Network test failed (check connectivity): " + e);
}
```

## Special Test Requirements

### HTTP/WebSocket Tests

**Requirements:**
- libwebsockets-dev package installed
- Compiled lws_wrapper.so (`make stdlib`)
- Network connectivity (for request tests)

**Setup:**
```bash
# Install dependencies
sudo apt-get install libwebsockets-dev

# Build FFI wrapper
make stdlib

# Run tests
./hemlock tests/stdlib_http/test_http_requests.hml
./hemlock tests/stdlib_websocket/test_websocket_echo.hml
```

See detailed documentation:
- [HTTP Tests README](tests/stdlib_http/README.md)
- [WebSocket Tests README](tests/stdlib_websocket/README.md)

### Async/Concurrency Tests

Tests using async/await, spawn, channels:
- May take longer to run (use reasonable timeouts in test runner)
- Test both success and timeout cases
- Clean up spawned tasks properly

## Continuous Integration

The test runner is designed for CI/CD integration:

```bash
# Exit code 0 if all tests pass
./tests/run_tests.sh && echo "Success" || echo "Failed"

# Can be used in GitHub Actions, GitLab CI, etc.
```

## Performance Testing

For benchmarking and performance tests:
- Use `@stdlib/time` for timing
- Run multiple iterations
- Report min/max/average times
- Compare against baseline

Example:
```hemlock
import { time_ms } from "@stdlib/time";

let iterations = 1000;
let start = time_ms();

let i = 0;
while (i < iterations) {
    // Code to benchmark
    i = i + 1;
}

let elapsed = time_ms() - start;
let per_iteration = elapsed / iterations;
print("Average: " + typeof(per_iteration) + "ms per iteration");
```

## Debugging Failed Tests

1. **Run test individually:**
   ```bash
   ./hemlock tests/category/test_file.hml
   ```

2. **Check test output:**
   - Look for assertion failures
   - Check error messages
   - Verify expected vs actual values

3. **Add debug output:**
   ```hemlock
   print("Debug: variable = " + typeof(variable));
   ```

4. **Check dependencies:**
   - For stdlib tests: ensure `make stdlib` was run
   - For network tests: verify connectivity
   - For FFI tests: check library installation

## Test Coverage

Current test statistics:
- **372 tests total**
- **347 passing tests**
- **25 expected error tests**
- **100% success rate** (all tests behave as expected)

Coverage areas:
- ✅ Core language features (100%)
- ✅ Type system (100%)
- ✅ Memory management
- ✅ String operations
- ✅ Array operations
- ✅ Object system
- ✅ Error handling
- ✅ File I/O
- ✅ Async/concurrency
- ✅ Standard library modules
- ✅ HTTP/WebSocket (new)

## Contributing Tests

When adding new features to Hemlock:

1. **Write tests first** (TDD approach)
2. **Create test file** in appropriate category
3. **Follow naming conventions:**
   - `test_feature.hml` for regular tests
   - `test_feature_error.hml` for error tests
4. **Document requirements** in comments
5. **Run full test suite** before committing
6. **Update this guide** if adding new test category

## Memory Leak Detection

Hemlock provides comprehensive memory leak detection tools for ensuring code quality.

### Valgrind Integration

Run memory leak checks using Valgrind:

```bash
# Quick check on a simple test
make valgrind

# Run valgrind on specific file
make valgrind-file FILE=tests/arrays/test_array_methods.hml

# Run valgrind on all tests (slow - generates many logs)
make valgrind-test

# View summary of all leaks
make valgrind-summary

# Clean up log files
make valgrind-clean
```

**Requirements:**
```bash
sudo apt-get install valgrind
```

### AddressSanitizer (ASAN)

Build and test with AddressSanitizer for runtime memory error detection:

```bash
# Build with ASAN
make clean
CFLAGS="-Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc -fsanitize=address -fno-omit-frame-pointer" \
LDFLAGS="-lm -lpthread -lffi -ldl -fsanitize=address" \
make

# Run tests with ASAN
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 make test
```

ASAN detects:
- Memory leaks
- Buffer overflows
- Use-after-free
- Double-free
- Heap corruption

### Continuous Integration

Memory leak detection runs automatically on every push:

**GitHub Actions Jobs:**
- `asan-test` - Runs all tests with AddressSanitizer
  - Fails build if ASAN detects errors
  - Uploads ASAN logs as artifacts

- `valgrind-test` - Runs basic valgrind checks
  - Currently informational (doesn't fail build)
  - Uploads valgrind logs as artifacts
  - View results in Actions > Artifacts

### Interpreting Valgrind Results

**Leak categories:**
- **Definitely lost:** Direct memory leaks (must fix)
- **Indirectly lost:** Memory referenced by leaked structures
- **Possibly lost:** May or may not be leaks (investigate)
- **Still reachable:** Allocated but freed at exit (usually acceptable)

**Example output:**
```
LEAK SUMMARY:
   definitely lost: 96 bytes in 4 blocks
   indirectly lost: 195 bytes in 6 blocks
     possibly lost: 0 bytes in 0 blocks
   still reachable: 1,024 bytes in 12 blocks
```

**Acceptable leaks:**
- Global type registries (freed at exit)
- Builtin function names (static lifetime)
- pthread internal structures (system-managed)

### Memory Management Best Practices

When writing Hemlock code:

1. **Reference Counting** - Values created with `ref_count = 1`
2. **Retention** - `value_retain()` increments when storing/passing
3. **Release** - `value_release()` decrements, frees at 0
4. **Cleanup** - Always call `env_break_cycles()` before final `env_release()`

See [docs/development/MODULE_IMPORT_LEAK_FIXES.md](docs/development/MODULE_IMPORT_LEAK_FIXES.md) for detailed architecture.

## See Also

- [CLAUDE.md](CLAUDE.md) - Language design philosophy
- [stdlib/README.md](stdlib/README.md) - Standard library overview
- [examples/](examples/) - Example programs
- [docs/development/](docs/development/) - Memory leak analysis and fixes
