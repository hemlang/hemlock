# Error Handling

Hemlock supports exception-based error handling with `try`, `catch`, `finally`, `throw`, and `panic`. This guide covers recoverable errors with exceptions and unrecoverable errors with panic.

## Overview

```hemlock
// Basic error handling
try {
    risky_operation();
} catch (e) {
    print("Error: " + e);
}

// With cleanup
try {
    process_file();
} catch (e) {
    print("Failed: " + e);
} finally {
    cleanup();
}

// Throwing errors
fn divide(a, b) {
    if (b == 0) {
        throw "division by zero";
    }
    return a / b;
}
```

## Try-Catch-Finally

### Syntax

**Basic try/catch:**
```hemlock
try {
    // risky code
} catch (e) {
    // handle error, e contains the thrown value
}
```

**Try/finally:**
```hemlock
try {
    // risky code
} finally {
    // always executes, even if exception thrown
}
```

**Try/catch/finally:**
```hemlock
try {
    // risky code
} catch (e) {
    // handle error
} finally {
    // cleanup code
}
```

### Try Block

The try block executes statements sequentially:

```hemlock
try {
    print("Starting...");
    risky_operation();
    print("Success!");  // Only if no exception
}
```

**Behavior:**
- Executes statements in order
- If exception thrown: jumps to `catch` or `finally`
- If no exception: executes `finally` (if present) then continues

### Catch Block

The catch block receives the thrown value:

```hemlock
try {
    throw "oops";
} catch (error) {
    print("Caught: " + error);  // error = "oops"
    // error only accessible here
}
// error not accessible here
```

**Catch parameter:**
- Receives the thrown value (any type)
- Scoped to the catch block
- Can be named anything (conventionally `e`, `err`, or `error`)

**What you can do in catch:**
```hemlock
try {
    risky_operation();
} catch (e) {
    // Log the error
    print("Error: " + e);

    // Re-throw same error
    throw e;

    // Throw different error
    throw "different error";

    // Return a default value
    return null;

    // Handle and continue
    // (no re-throw)
}
```

### Finally Block

The finally block **always executes**:

```hemlock
try {
    print("1: try");
    throw "error";
} catch (e) {
    print("2: catch");
} finally {
    print("3: finally");  // Always runs
}
print("4: after");

// Output: 1: try, 2: catch, 3: finally, 4: after
```

**When finally runs:**
- After try block (if no exception)
- After catch block (if exception caught)
- Even if try/catch contains `return`, `break`, or `continue`
- Before control flow exits the try/catch

**Finally with return:**
```hemlock
fn example() {
    try {
        return 1;  // ✅ Returns 1 after finally runs
    } finally {
        print("cleanup");  // Runs before returning
    }
}

fn example2() {
    try {
        return 1;
    } finally {
        return 2;  // ⚠️ Finally return overrides - returns 2
    }
}
```

**Finally with control flow:**
```hemlock
for (let i = 0; i < 10; i = i + 1) {
    try {
        if (i == 5) {
            break;  // ✅ Breaks after finally runs
        }
    } finally {
        print("cleanup " + typeof(i));
    }
}
```

## Throw Statement

### Basic Throw

Throw any value as an exception:

```hemlock
throw "error message";
throw 404;
throw { code: 500, message: "Internal error" };
throw null;
throw ["error", "details"];
```

**Execution:**
1. Evaluates the expression
2. Immediately jumps to nearest enclosing `catch`
3. If no `catch`, propagates up the call stack

### Throwing Errors

```hemlock
fn validate_age(age: i32) {
    if (age < 0) {
        throw "Age cannot be negative";
    }
    if (age > 150) {
        throw "Age is unrealistic";
    }
}

try {
    validate_age(-5);
} catch (e) {
    print("Validation error: " + e);
}
```

### Throwing Error Objects

Create structured error information:

```hemlock
fn read_file(path: string) {
    if (!file_exists(path)) {
        throw {
            type: "FileNotFound",
            path: path,
            message: "File does not exist"
        };
    }
    // ... read file
}

try {
    read_file("missing.txt");
} catch (e) {
    if (e.type == "FileNotFound") {
        print("File not found: " + e.path);
    }
}
```

### Re-throwing

Catch and re-throw errors:

```hemlock
fn wrapper() {
    try {
        risky_operation();
    } catch (e) {
        print("Logging error: " + e);
        throw e;  // Re-throw to caller
    }
}

try {
    wrapper();
} catch (e) {
    print("Caught in main: " + e);
}
```

## Uncaught Exceptions

If an exception propagates to the top of the call stack without being caught:

```hemlock
fn foo() {
    throw "uncaught!";
}

foo();  // Crashes with: Runtime error: uncaught!
```

**Behavior:**
- Program crashes
- Prints error message to stderr
- Exits with non-zero status code
- Stack trace to be added in future versions

## Panic - Unrecoverable Errors

### What is Panic?

`panic()` is for **unrecoverable errors** that should immediately terminate the program:

```hemlock
panic();                    // Default message: "panic!"
panic("custom message");    // Custom message
panic(42);                  // Non-string values are printed
```

**Semantics:**
- **Immediately exits** the program with exit code 1
- Prints error message to stderr: `panic: <message>`
- **NOT catchable** with try/catch
- Use for bugs and unrecoverable errors

### Panic vs Throw

```hemlock
// throw - Recoverable error (can be caught)
try {
    throw "recoverable error";
} catch (e) {
    print("Caught: " + e);  // ✅ Caught successfully
}

// panic - Unrecoverable error (cannot be caught)
try {
    panic("unrecoverable error");  // ❌ Program exits immediately
} catch (e) {
    print("This never runs");       // ❌ Never executes
}
```

### When to Use Panic

**Use panic for:**
- **Bugs**: Unreachable code was reached
- **Invalid state**: Data structure corruption detected
- **Unrecoverable errors**: Critical resource unavailable
- **Assertion failures**: When `assert()` isn't sufficient

**Examples:**
```hemlock
// Unreachable code
fn process_state(state: i32) {
    if (state == 1) {
        return "ready";
    } else if (state == 2) {
        return "running";
    } else if (state == 3) {
        return "stopped";
    } else {
        panic("invalid state: " + typeof(state));  // Should never happen
    }
}

// Critical resource check
fn init_system() {
    let config = read_file("config.json");
    if (config == null) {
        panic("config.json not found - cannot start");
    }
    // ...
}

// Data structure invariant
fn pop_stack(stack) {
    if (stack.length == 0) {
        panic("pop() called on empty stack");
    }
    return stack.pop();
}
```

### When NOT to Use Panic

**Use throw instead for:**
- User input validation
- File not found
- Network errors
- Expected error conditions

```hemlock
// BAD: Panic for expected errors
fn divide(a, b) {
    if (b == 0) {
        panic("division by zero");  // ❌ Too harsh
    }
    return a / b;
}

// GOOD: Throw for expected errors
fn divide(a, b) {
    if (b == 0) {
        throw "division by zero";  // ✅ Recoverable
    }
    return a / b;
}
```

## Control Flow Interactions

### Return Inside Try/Catch/Finally

```hemlock
fn example() {
    try {
        return 1;  // ✅ Returns 1 after finally runs
    } finally {
        print("cleanup");
    }
}

fn example2() {
    try {
        return 1;
    } finally {
        return 2;  // ⚠️ Finally return overrides try return - returns 2
    }
}
```

**Rule:** Finally block return values override try/catch return values.

### Break/Continue Inside Try/Catch/Finally

```hemlock
for (let i = 0; i < 10; i = i + 1) {
    try {
        if (i == 5) { break; }  // ✅ Breaks after finally runs
    } finally {
        print("cleanup " + typeof(i));
    }
}
```

**Rule:** Break/continue execute after finally block.

### Nested Try/Catch

```hemlock
try {
    try {
        throw "inner";
    } catch (e) {
        print("Caught: " + e);  // Prints: Caught: inner
        throw "outer";  // Re-throw different error
    }
} catch (e) {
    print("Caught: " + e);  // Prints: Caught: outer
}
```

**Rule:** Nested try/catch blocks work as expected, inner catches happen first.

## Common Patterns

### Pattern: Resource Cleanup

Always use `finally` for cleanup:

```hemlock
fn process_file(filename) {
    let file = null;
    try {
        file = open(filename);
        let content = file.read();
        process(content);
    } catch (e) {
        print("Error processing file: " + e);
    } finally {
        if (file != null) {
            file.close();  // Always closes, even on error
        }
    }
}
```

### Pattern: Error Wrapping

Wrap lower-level errors with context:

```hemlock
fn load_config(path) {
    try {
        let content = read_file(path);
        return parse_json(content);
    } catch (e) {
        throw "Failed to load config from " + path + ": " + e;
    }
}
```

### Pattern: Error Recovery

Provide fallback on error:

```hemlock
fn safe_divide(a, b) {
    try {
        if (b == 0) {
            throw "division by zero";
        }
        return a / b;
    } catch (e) {
        print("Error: " + e);
        return null;  // Fallback value
    }
}
```

### Pattern: Validation

Use exceptions for validation:

```hemlock
fn validate_user(user) {
    if (user.name == null || user.name == "") {
        throw "Name is required";
    }
    if (user.age < 0 || user.age > 150) {
        throw "Invalid age";
    }
    if (user.email == null || !user.email.contains("@")) {
        throw "Invalid email";
    }
}

try {
    validate_user({ name: "Alice", age: -5, email: "invalid" });
} catch (e) {
    print("Validation failed: " + e);
}
```

### Pattern: Multiple Error Types

Use error objects to distinguish error types:

```hemlock
fn process_data(data) {
    if (data == null) {
        throw { type: "NullData", message: "Data is null" };
    }

    if (typeof(data) != "array") {
        throw { type: "TypeError", message: "Expected array" };
    }

    if (data.length == 0) {
        throw { type: "EmptyData", message: "Array is empty" };
    }

    // ... process
}

try {
    process_data(null);
} catch (e) {
    if (e.type == "NullData") {
        print("No data provided");
    } else if (e.type == "TypeError") {
        print("Wrong data type: " + e.message);
    } else {
        print("Error: " + e.message);
    }
}
```

## Best Practices

1. **Use exceptions for exceptional cases** - Not for normal control flow
2. **Throw meaningful errors** - Use strings or objects with context
3. **Always use finally for cleanup** - Ensures resources are freed
4. **Don't catch and ignore** - At least log the error
5. **Re-throw when appropriate** - Let caller handle if you can't
6. **Panic for bugs** - Use panic for unrecoverable errors
7. **Document exceptions** - Make clear what functions can throw

## Common Pitfalls

### Pitfall: Swallowing Errors

```hemlock
// BAD: Silent failure
try {
    risky_operation();
} catch (e) {
    // Error ignored - silent failure
}

// GOOD: Log or handle
try {
    risky_operation();
} catch (e) {
    print("Operation failed: " + e);
    // Handle appropriately
}
```

### Pitfall: Finally Override

```hemlock
// BAD: Finally overrides return
fn get_value() {
    try {
        return 42;
    } finally {
        return 0;  // ⚠️ Returns 0, not 42!
    }
}

// GOOD: Don't return in finally
fn get_value() {
    try {
        return 42;
    } finally {
        cleanup();  // Just cleanup, no return
    }
}
```

### Pitfall: Forgetting Cleanup

```hemlock
// BAD: File may not be closed on error
fn process() {
    let file = open("data.txt");
    let content = file.read();  // May throw
    file.close();  // Never reached if error
}

// GOOD: Use finally
fn process() {
    let file = null;
    try {
        file = open("data.txt");
        let content = file.read();
    } finally {
        if (file != null) {
            file.close();
        }
    }
}
```

### Pitfall: Using Panic for Expected Errors

```hemlock
// BAD: Panic for expected error
fn read_config(path) {
    if (!file_exists(path)) {
        panic("Config file not found");  // ❌ Too harsh
    }
    return read_file(path);
}

// GOOD: Throw for expected error
fn read_config(path) {
    if (!file_exists(path)) {
        throw "Config file not found: " + path;  // ✅ Recoverable
    }
    return read_file(path);
}
```

## Examples

### Example: Basic Error Handling

```hemlock
fn divide(a, b) {
    if (b == 0) {
        throw "division by zero";
    }
    return a / b;
}

try {
    print(divide(10, 0));
} catch (e) {
    print("Error: " + e);  // Prints: Error: division by zero
}
```

### Example: Resource Management

```hemlock
fn copy_file(src, dst) {
    let src_file = null;
    let dst_file = null;

    try {
        src_file = open(src, "r");
        dst_file = open(dst, "w");

        let content = src_file.read();
        dst_file.write(content);

        print("File copied successfully");
    } catch (e) {
        print("Failed to copy file: " + e);
        throw e;  // Re-throw
    } finally {
        if (src_file != null) { src_file.close(); }
        if (dst_file != null) { dst_file.close(); }
    }
}
```

### Example: Nested Error Handling

```hemlock
fn process_users(users) {
    let success_count = 0;
    let error_count = 0;

    let i = 0;
    while (i < users.length) {
        try {
            validate_user(users[i]);
            save_user(users[i]);
            success_count = success_count + 1;
        } catch (e) {
            print("Failed to process user: " + e);
            error_count = error_count + 1;
        }
        i = i + 1;
    }

    print("Processed: " + typeof(success_count) + " success, " + typeof(error_count) + " errors");
}
```

### Example: Custom Error Types

```hemlock
fn create_error(type, message, details) {
    return {
        type: type,
        message: message,
        details: details,
        toString: fn() {
            return self.type + ": " + self.message;
        }
    };
}

fn divide(a, b) {
    if (typeof(a) != "i32" && typeof(a) != "f64") {
        throw create_error("TypeError", "a must be a number", { value: a });
    }
    if (typeof(b) != "i32" && typeof(b) != "f64") {
        throw create_error("TypeError", "b must be a number", { value: b });
    }
    if (b == 0) {
        throw create_error("DivisionByZero", "Cannot divide by zero", { a: a, b: b });
    }
    return a / b;
}

try {
    divide(10, 0);
} catch (e) {
    print(e.toString());
    if (e.type == "DivisionByZero") {
        print("Details: a=" + typeof(e.details.a) + ", b=" + typeof(e.details.b));
    }
}
```

### Example: Retry Logic

```hemlock
fn retry(operation, max_attempts) {
    let attempt = 0;

    while (attempt < max_attempts) {
        try {
            return operation();  // Success!
        } catch (e) {
            attempt = attempt + 1;
            if (attempt >= max_attempts) {
                throw "Operation failed after " + typeof(max_attempts) + " attempts: " + e;
            }
            print("Attempt " + typeof(attempt) + " failed, retrying...");
        }
    }
}

fn unreliable_operation() {
    // Simulated unreliable operation
    if (random() < 0.7) {
        throw "Operation failed";
    }
    return "Success";
}

try {
    let result = retry(unreliable_operation, 3);
    print(result);
} catch (e) {
    print("All retries failed: " + e);
}
```

## Execution Order

Understanding the execution order:

```hemlock
try {
    print("1: try block start");
    throw "error";
    print("2: never reached");
} catch (e) {
    print("3: catch block");
} finally {
    print("4: finally block");
}
print("5: after try/catch/finally");

// Output:
// 1: try block start
// 3: catch block
// 4: finally block
// 5: after try/catch/finally
```

## Current Limitations

- **Some built-ins exit** - Some built-in functions still `exit()` instead of throwing (to be reviewed)
- **No custom exception types** - Any value can be thrown, but no formal exception hierarchy

## Related Topics

- [Functions](functions.md) - Exceptions and function returns
- [Control Flow](control-flow.md) - How exceptions affect control flow
- [Memory](memory.md) - Using finally for memory cleanup

## See Also

- **Exception Semantics**: See CLAUDE.md section "Error Handling"
- **Panic vs Throw**: Different use cases for different error types
- **Finally Guarantee**: Always executes, even with return/break/continue
