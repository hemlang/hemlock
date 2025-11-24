# @stdlib/testing - Test Framework

Comprehensive testing framework with describe/test/expect syntax, assertions, and test runner with colored output.

## Overview

The testing module provides a modern test framework for Hemlock programs with:
- **describe/test structure** - Organize tests into suites
- **Fluent expect API** - Chainable assertions with clear error messages
- **Simple assertions** - Traditional assert_eq, assert_ne, etc.
- **Hooks** - before_each and after_each setup/teardown
- **Test runner** - Automatic test discovery and execution with colored output
- **Pass/fail reporting** - Detailed failure messages and summary statistics

## Import

```hemlock
import {
    describe, test, expect,
    assert_eq, assert_ne, assert_true, assert_false, assert_throws,
    before_each, after_each, run
} from "@stdlib/testing";
```

## Quick Start

```hemlock
import { describe, test, expect, run } from "@stdlib/testing";

describe("Math operations", fn() {
    test("addition works", fn() {
        expect(2 + 2).to_equal(4);
    });

    test("subtraction works", fn() {
        expect(10 - 5).to_equal(5);
    });
});

describe("String operations", fn() {
    test("concatenation works", fn() {
        expect("hello" + " world").to_equal("hello world");
    });

    test("length is correct", fn() {
        expect("test".length).to_equal(4);
    });
});

// Run all tests
let results = run();
```

**Output:**
```
Running tests...

Math operations
  ✓ addition works
  ✓ subtraction works

String operations
  ✓ concatenation works
  ✓ length is correct

Test Summary:
  Total:  4
  Passed: 4
  Failed: 0
```

## Core API

### describe(name, fn)

Create a test suite (group of related tests).

```hemlock
describe("Array methods", fn() {
    test("push adds element", fn() {
        let arr = [1, 2, 3];
        arr.push(4);
        expect(arr.length).to_equal(4);
    });

    test("pop removes element", fn() {
        let arr = [1, 2, 3];
        let last = arr.pop();
        expect(last).to_equal(3);
        expect(arr.length).to_equal(2);
    });
});
```

**Notes:**
- All `test()` calls must be inside a `describe()` block
- Suites can contain multiple tests
- Suites are executed in registration order

### test(name, fn)

Define a test case within a suite.

```hemlock
describe("Calculator", fn() {
    test("adds positive numbers", fn() {
        expect(add(2, 3)).to_equal(5);
    });

    test("handles negative numbers", fn() {
        expect(add(-5, 3)).to_equal(-2);
    });

    test("handles zero", fn() {
        expect(add(0, 0)).to_equal(0);
    });
});
```

**Notes:**
- Must be called inside `describe()`
- Tests run independently (no shared state between tests)
- If a test throws an exception, it fails

### expect(value)

Create an expectation object for assertions. Returns an object with assertion methods.

```hemlock
test("expect API", fn() {
    expect(42).to_equal(42);
    expect("hello").to_contain("ell");
    expect(true).to_be_true();
});
```

## Expectation API

The `expect()` function returns an object with chainable assertion methods:

### Equality Assertions

#### .to_equal(expected)

Deep equality check (compares values, not references).

```hemlock
test("to_equal", fn() {
    expect(42).to_equal(42);
    expect("hello").to_equal("hello");
    expect([1, 2, 3]).to_equal([1, 2, 3]);  // Deep comparison
    expect(true).to_equal(true);
});
```

**Supported types:**
- Primitives: i8-i64, u8-u64, f32, f64, bool, string, rune, null
- Arrays: Recursively compares elements

#### .to_be(expected)

Reference/primitive equality (uses `==` operator).

```hemlock
test("to_be", fn() {
    let x = 10;
    expect(x).to_be(10);

    let arr = [1, 2, 3];
    expect(arr).to_be(arr);  // Same reference
});
```

#### .not_to_equal(expected) / .not_to_be(expected)

Inverse equality checks.

```hemlock
test("inequality", fn() {
    expect(5).not_to_equal(10);
    expect([1, 2]).not_to_equal([3, 4]);
});
```

### Null Checks

#### .to_be_null() / .not_to_be_null()

Check if value is null.

```hemlock
test("null checks", fn() {
    let x = null;
    expect(x).to_be_null();

    let y = 42;
    expect(y).not_to_be_null();
});
```

### Boolean Assertions

#### .to_be_true() / .to_be_false()

Check boolean values.

```hemlock
test("boolean checks", fn() {
    expect(true).to_be_true();
    expect(false).to_be_false();
    expect(5 > 3).to_be_true();
    expect(10 < 5).to_be_false();
});
```

### Numeric Comparisons

#### .to_be_greater_than(value)

Check if actual > value.

```hemlock
test("greater than", fn() {
    expect(10).to_be_greater_than(5);
    expect(3.14).to_be_greater_than(3.0);
});
```

#### .to_be_less_than(value)

Check if actual < value.

```hemlock
test("less than", fn() {
    expect(5).to_be_less_than(10);
    expect(-5).to_be_less_than(0);
});
```

#### .to_be_greater_than_or_equal(value) / .to_be_less_than_or_equal(value)

Check if actual >= or <= value.

```hemlock
test("comparisons", fn() {
    expect(10).to_be_greater_than_or_equal(10);
    expect(10).to_be_greater_than_or_equal(5);

    expect(5).to_be_less_than_or_equal(5);
    expect(5).to_be_less_than_or_equal(10);
});
```

### Type Checks

#### .to_be_type(expected_type)

Check value type using `typeof()`.

```hemlock
test("type checks", fn() {
    expect(42).to_be_type("i32");
    expect("hello").to_be_type("string");
    expect([1, 2, 3]).to_be_type("array");
    expect(true).to_be_type("bool");
    expect(null).to_be_type("null");
});
```

### Container Assertions

#### .to_contain(value) / .not_to_contain(value)

Check if array or string contains value.

```hemlock
test("contain checks", fn() {
    // Arrays
    expect([1, 2, 3]).to_contain(2);
    expect([1, 2, 3]).not_to_contain(5);

    // Strings
    expect("hello world").to_contain("world");
    expect("hello").not_to_contain("goodbye");
});
```

### Exception Assertions

#### .to_throw(expected_msg?)

Check if function throws an exception. Optionally verify error message.

```hemlock
test("throw checks", fn() {
    // Check that function throws
    expect(fn() {
        throw "error!";
    }).to_throw();

    // Check specific error message
    expect(fn() {
        throw "division by zero";
    }).to_throw("division by zero");

    // Function that doesn't throw
    fn safe() {
        return 42;
    }

    expect(safe).not_to_throw();
});
```

**Note:** The value passed to `expect()` must be a function, not a function call.

```hemlock
// ✓ Correct
expect(fn() { risky_operation(); }).to_throw();

// ✗ Wrong - calls the function immediately
expect(risky_operation()).to_throw();
```

#### .not_to_throw()

Check that function does not throw.

```hemlock
test("safe function", fn() {
    expect(fn() {
        let x = 2 + 2;
        return x;
    }).not_to_throw();
});
```

## Simple Assertions

Traditional assertion functions for quick checks:

### assert_eq(actual, expected)

Assert deep equality.

```hemlock
test("simple equality", fn() {
    assert_eq(2 + 2, 4);
    assert_eq("hello", "hello");
    assert_eq([1, 2, 3], [1, 2, 3]);
});
```

### assert_ne(actual, expected)

Assert inequality.

```hemlock
test("simple inequality", fn() {
    assert_ne(5, 10);
    assert_ne("foo", "bar");
});
```

### assert_true(value) / assert_false(value)

Assert boolean values.

```hemlock
test("boolean assertions", fn() {
    assert_true(10 > 5);
    assert_false(10 < 5);
});
```

### assert_throws(fn, expected_msg?)

Assert function throws exception.

```hemlock
test("throw assertion", fn() {
    assert_throws(fn() {
        throw "error!";
    });

    assert_throws(fn() {
        throw "specific error";
    }, "specific error");
});
```

## Test Hooks

### before_each(fn)

Run setup code before each test in the suite.

```hemlock
describe("Database tests", fn() {
    let db = null;

    before_each(fn() {
        db = create_db();
        db.connect();
    });

    after_each(fn() {
        db.close();
    });

    test("can insert record", fn() {
        db.insert("users", { name: "Alice" });
        expect(db.count("users")).to_equal(1);
    });

    test("can query records", fn() {
        db.insert("users", { name: "Bob" });
        let users = db.query("users");
        expect(users.length).to_equal(1);
    });
});
```

### after_each(fn)

Run cleanup code after each test in the suite.

**Notes:**
- Hooks run for every test in the suite
- `before_each` runs before the test function
- `after_each` runs after the test function (even if test fails)
- Hooks can access and modify suite-level variables

## Test Runner

### run(options?)

Execute all registered tests and print results.

```hemlock
// Basic usage
let results = run();

// With options
let results = run({
    verbose: true,    // Print errors inline
    no_color: false   // Disable colored output
});

// Check if all tests passed
if (!results.success) {
    import { exit } from "@stdlib/env";
    exit(1);
}
```

**Options:**
- `verbose: bool` - Print error messages inline with each failed test (default: false)
- `no_color: bool` - Disable ANSI color codes in output (default: false)

**Returns:** Object with test statistics:
```hemlock
{
    total: i32,      // Total tests run
    passed: i32,     // Tests that passed
    failed: i32,     // Tests that failed
    success: bool    // true if all tests passed
}
```

**Output format:**
```
Running tests...

Suite Name
  ✓ passing test
  ✗ failing test

Test Summary:
  Total:  2
  Passed: 1
  Failed: 1

Failures:

Suite Name > failing test
Expected 5 to equal 10
```

**Colors:**
- Green ✓ - Passing tests
- Red ✗ - Failing tests
- Blue - Suite names
- Bold - Headers

## Complete Example

```hemlock
import {
    describe, test, expect,
    before_each, after_each, run
} from "@stdlib/testing";
import { exit } from "@stdlib/env";

// Helper function to test
fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

describe("Factorial function", fn() {
    test("base case n=0", fn() {
        expect(factorial(0)).to_equal(1);
    });

    test("base case n=1", fn() {
        expect(factorial(1)).to_equal(1);
    });

    test("small values", fn() {
        expect(factorial(5)).to_equal(120);
        expect(factorial(6)).to_equal(720);
    });

    test("returns positive numbers", fn() {
        expect(factorial(3)).to_be_greater_than(0);
    });
});

describe("Array operations", fn() {
    let arr = null;

    before_each(fn() {
        arr = [1, 2, 3];
    });

    test("push adds element", fn() {
        arr.push(4);
        expect(arr).to_equal([1, 2, 3, 4]);
    });

    test("pop removes element", fn() {
        let last = arr.pop();
        expect(last).to_equal(3);
        expect(arr.length).to_equal(2);
    });

    test("contains checks membership", fn() {
        expect(arr).to_contain(2);
        expect(arr).not_to_contain(10);
    });
});

describe("Error handling", fn() {
    test("division by zero throws", fn() {
        fn divide_by_zero() {
            if (true) {
                throw "division by zero";
            }
            return null;
        }

        expect(divide_by_zero).to_throw("division by zero");
    });

    test("safe operations don't throw", fn() {
        expect(fn() {
            let x = 10 + 5;
            return x;
        }).not_to_throw();
    });
});

// Run all tests
let results = run();

// Exit with appropriate code
if (!results.success) {
    exit(1);
}
```

## Test Organization

**Best practices:**

1. **One file per module:**
```
tests/
  my_module/
    test_basic.hml
    test_advanced.hml
    test_edge_cases.hml
```

2. **Descriptive suite names:**
```hemlock
describe("HashMap.set() method", fn() { ... });
describe("TcpListener connection handling", fn() { ... });
```

3. **Clear test names:**
```hemlock
test("returns null when key not found", fn() { ... });
test("throws error on invalid input", fn() { ... });
test("handles empty array correctly", fn() { ... });
```

4. **Group related tests:**
```hemlock
describe("String methods", fn() {
    describe("substr()", fn() {
        test("extracts substring", fn() { ... });
        test("handles out of bounds", fn() { ... });
    });

    describe("split()", fn() {
        test("splits on delimiter", fn() { ... });
        test("returns single element for no matches", fn() { ... });
    });
});
```

5. **Use hooks for setup:**
```hemlock
describe("Database", fn() {
    let db = null;

    before_each(fn() {
        db = create_test_db();
    });

    after_each(fn() {
        db.destroy();
    });

    // Tests use db...
});
```

## Comparison with Shell-Based Tests

**Old style (shell scripts):**
```bash
#!/bin/bash
echo "Test: addition"
result=$(./hemlock -e "print(2 + 2);")
if [ "$result" == "4" ]; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
fi
```

**New style (@stdlib/testing):**
```hemlock
import { describe, test, expect, run } from "@stdlib/testing";

describe("Arithmetic", fn() {
    test("addition", fn() {
        expect(2 + 2).to_equal(4);
    });
});

run();
```

**Advantages:**
- ✓ Better error messages
- ✓ Test organization (suites)
- ✓ Setup/teardown hooks
- ✓ Fluent assertion API
- ✓ Colored output
- ✓ Statistics and reporting
- ✓ Pure Hemlock (no shell dependencies)

## Tips & Tricks

### Test-Driven Development (TDD)

1. Write the test first:
```hemlock
test("parses JSON string", fn() {
    let obj = parse_json('{"x": 10}');
    expect(obj.x).to_equal(10);
});
```

2. Run and see it fail
3. Implement the function
4. Run and see it pass

### Testing Exceptions

Always wrap risky calls in functions:
```hemlock
// ✓ Correct
expect(fn() {
    let result = divide(10, 0);
}).to_throw();

// ✗ Wrong - throws immediately, test fails
expect(divide(10, 0)).to_throw();
```

### Debugging Failed Tests

Use verbose mode to see errors inline:
```hemlock
run({ verbose: true });
```

Or add temporary print statements:
```hemlock
test("complex calculation", fn() {
    let x = calculate_something();
    print("x = " + typeof(x));  // Debug output
    expect(x).to_equal(expected);
});
```

### Performance Testing

Measure execution time:
```hemlock
import { time_ms } from "@stdlib/time";

test("operation is fast", fn() {
    let start = time_ms();
    expensive_operation();
    let elapsed = time_ms() - start;

    expect(elapsed).to_be_less_than(1000);  // < 1 second
});
```

## Limitations

- **No nested describe blocks** - Only one level of nesting supported
- **No test filtering** - All tests run (no --filter flag yet)
- **No parallel execution** - Tests run sequentially
- **Object comparison** - Currently uses reference equality (deep object comparison planned)
- **No test discovery** - Must manually run test files
- **No coverage reporting** - Code coverage not tracked

## Future Enhancements

Planned features:
- Test filtering by name/pattern
- Parallel test execution
- Deep object comparison
- Test coverage reporting
- Watch mode (re-run on file changes)
- Snapshot testing
- Mocking/stubbing utilities

## See Also

- stdlib/docs/env.md - exit() for test exit codes
- stdlib/docs/time.md - Timing test execution
- stdlib/docs/fs.md - File I/O for test fixtures
