# Built-in Functions Reference

Complete reference for all built-in functions and constants in Hemlock.

---

## Overview

Hemlock provides a set of built-in functions for I/O, type introspection, memory management, concurrency, and system interaction. All built-ins are available globally without imports.

---

## I/O Functions

### print

Print values to stdout with newline.

**Signature:**
```hemlock
print(...values): null
```

**Parameters:**
- `...values` - Any number of values to print

**Returns:** `null`

**Examples:**
```hemlock
print("Hello, World!");
print(42);
print(3.14);
print(true);
print([1, 2, 3]);
print({ x: 10, y: 20 });

// Multiple values
print("x =", 10, "y =", 20);
```

**Behavior:**
- Converts all values to strings
- Separates multiple values with spaces
- Adds newline at end
- Flushes stdout

---

### read_line

Read a line of text from stdin (user input).

**Signature:**
```hemlock
read_line(): string | null
```

**Parameters:** None

**Returns:**
- `string` - The line read from stdin (newline stripped)
- `null` - On EOF (end of file/input)

**Examples:**
```hemlock
// Simple prompt
print("What is your name?");
let name = read_line();
print("Hello, " + name + "!");

// Reading numbers (requires manual parsing)
print("Enter a number:");
let input = read_line();
let num = parse_int(input);  // See below for parse_int
print("Double:", num * 2);

// Handle EOF
let line = read_line();
if (line == null) {
    print("End of input");
}

// Read multiple lines
print("Enter lines (Ctrl+D to stop):");
while (true) {
    let line = read_line();
    if (line == null) {
        break;
    }
    print("You said:", line);
}
```

**Behavior:**
- Blocks until user presses Enter
- Strips trailing newline (`\n`) and carriage return (`\r`)
- Returns `null` on EOF (Ctrl+D on Unix, Ctrl+Z on Windows)
- Reads from stdin only (not from files)

**Parsing User Input:**

Since `read_line()` always returns a string, you need to parse numeric input manually:

```hemlock
// Simple integer parser
fn parse_int(s: string): i32 {
    let result: i32 = 0;
    let negative = false;
    let i = 0;

    if (s.length > 0 && s.char_at(0) == '-') {
        negative = true;
        i = 1;
    }

    while (i < s.length) {
        let c = s.char_at(i);
        let code: i32 = c;
        if (code >= 48 && code <= 57) {
            result = result * 10 + (code - 48);
        } else {
            break;
        }
        i = i + 1;
    }

    if (negative) {
        return -result;
    }
    return result;
}

// Usage
print("Enter your age:");
let age = parse_int(read_line());
print("In 10 years you'll be", age + 10);
```

**See Also:** [File API](file-api.md) for reading from files

---

### eprint

Print a value to stderr with newline.

**Signature:**
```hemlock
eprint(value: any): null
```

**Parameters:**
- `value` - Single value to print to stderr

**Returns:** `null`

**Examples:**
```hemlock
eprint("Error: file not found");
eprint(404);
eprint("Warning: " + message);

// Typical error handling pattern
fn load_config(path: string) {
    if (!exists(path)) {
        eprint("Error: config file not found: " + path);
        return null;
    }
    // ...
}
```

**Behavior:**
- Prints to stderr (standard error stream)
- Adds newline at end
- Only accepts one argument (unlike `print`)
- Useful for error messages that shouldn't mix with normal output

**Difference from print:**
- `print()` → stdout (normal output, can be redirected with `>`)
- `eprint()` → stderr (error output, can be redirected with `2>`)

```bash
# Shell example: separate stdout and stderr
./hemlock script.hml > output.txt 2> errors.txt
```

---

## Type Introspection

### typeof

Get the type name of a value.

**Signature:**
```hemlock
typeof(value: any): string
```

**Parameters:**
- `value` - Any value

**Returns:** Type name as string

**Examples:**
```hemlock
print(typeof(42));              // "i32"
print(typeof(3.14));            // "f64"
print(typeof("hello"));         // "string"
print(typeof('A'));             // "rune"
print(typeof(true));            // "bool"
print(typeof(null));            // "null"
print(typeof([1, 2, 3]));       // "array"
print(typeof({ x: 10 }));       // "object"

// Typed objects
define Person { name: string }
let p: Person = { name: "Alice" };
print(typeof(p));               // "Person"

// Other types
print(typeof(alloc(10)));       // "ptr"
print(typeof(buffer(10)));      // "buffer"
print(typeof(open("file.txt"))); // "file"
```

**Type Names:**
- Primitives: `"i8"`, `"i16"`, `"i32"`, `"i64"`, `"u8"`, `"u16"`, `"u32"`, `"u64"`, `"f32"`, `"f64"`, `"bool"`, `"string"`, `"rune"`, `"null"`
- Composites: `"array"`, `"object"`, `"ptr"`, `"buffer"`, `"function"`
- Special: `"file"`, `"task"`, `"channel"`
- Custom: User-defined type names from `define`

**See Also:** [Type System](type-system.md)

---

## Command Execution

### exec

Execute shell command and capture output.

**Signature:**
```hemlock
exec(command: string): object
```

**Parameters:**
- `command` - Shell command to execute

**Returns:** Object with fields:
- `output` (string) - Command's stdout
- `exit_code` (i32) - Exit status code (0 = success)

**Examples:**
```hemlock
let result = exec("echo hello");
print(result.output);      // "hello\n"
print(result.exit_code);   // 0

// Check exit status
let r = exec("grep pattern file.txt");
if (r.exit_code == 0) {
    print("Found:", r.output);
} else {
    print("Pattern not found");
}

// Process multi-line output
let r2 = exec("ls -la");
let lines = r2.output.split("\n");
```

**Behavior:**
- Executes command via `/bin/sh`
- Captures stdout only (stderr goes to terminal)
- Blocks until command completes
- Returns empty string if no output

**Error Handling:**
```hemlock
try {
    let r = exec("nonexistent_command");
} catch (e) {
    print("Failed to execute:", e);
}
```

**Security Warning:** ⚠️ Vulnerable to shell injection. Always validate/sanitize user input.

**Limitations:**
- No stderr capture
- No streaming
- No timeout
- No signal handling

---

### exec_argv

Execute a command with explicit argument array (no shell interpretation).

**Signature:**
```hemlock
exec_argv(argv: array): object
```

**Parameters:**
- `argv` - Array of strings: `[command, arg1, arg2, ...]`

**Returns:** Object with fields:
- `output` (string) - Command's stdout
- `exit_code` (i32) - Exit status code (0 = success)

**Examples:**
```hemlock
// Simple command
let result = exec_argv(["ls", "-la"]);
print(result.output);

// Command with arguments containing spaces (safe!)
let r = exec_argv(["grep", "hello world", "file.txt"]);

// Run a script with arguments
let r2 = exec_argv(["python", "script.py", "--input", "data.json"]);
print(r2.exit_code);
```

**Difference from exec:**
```hemlock
// exec() uses shell - UNSAFE with user input
exec("ls " + user_input);  // Shell injection risk!

// exec_argv() bypasses shell - SAFE
exec_argv(["ls", user_input]);  // No injection possible
```

**When to use:**
- When arguments contain spaces, quotes, or special characters
- When processing user input (security)
- When you need predictable argument parsing

**See Also:** `exec()` for simple shell commands

---

## Error Handling

### throw

Throw an exception.

**Signature:**
```hemlock
throw expression
```

**Parameters:**
- `expression` - Value to throw (any type)

**Returns:** Never returns (transfers control)

**Examples:**
```hemlock
throw "error message";
throw 404;
throw { code: 500, message: "Internal error" };
throw null;
```

**See Also:** try/catch/finally statements

---

### panic

Immediately terminate program with error message (unrecoverable).

**Signature:**
```hemlock
panic(message?: any): never
```

**Parameters:**
- `message` (optional) - Error message to print

**Returns:** Never returns (program exits)

**Examples:**
```hemlock
panic();                          // Default: "panic!"
panic("unreachable code reached");
panic(42);

// Common use case
fn process_state(state: i32): string {
    if (state == 1) { return "ready"; }
    if (state == 2) { return "running"; }
    panic("invalid state: " + typeof(state));
}
```

**Behavior:**
- Prints error to stderr: `panic: <message>`
- Exits with code 1
- **NOT catchable** with try/catch
- Use for bugs and unrecoverable errors

**Panic vs Throw:**
- `panic()` - Unrecoverable error, exits immediately
- `throw` - Recoverable error, can be caught

---

### assert

Assert that a condition is true, or terminate with an error message.

**Signature:**
```hemlock
assert(condition: any, message?: string): null
```

**Parameters:**
- `condition` - Value to check for truthiness
- `message` (optional) - Custom error message if assertion fails

**Returns:** `null` (if assertion passes)

**Examples:**
```hemlock
// Basic assertions
assert(x > 0);
assert(name != null);
assert(arr.length > 0, "Array must not be empty");

// With custom messages
fn divide(a: i32, b: i32): f64 {
    assert(b != 0, "Division by zero");
    return a / b;
}

// Validate function arguments
fn process_data(data: array) {
    assert(data != null, "data cannot be null");
    assert(data.length > 0, "data cannot be empty");
    // ...
}
```

**Behavior:**
- If condition is truthy: returns `null`, execution continues
- If condition is falsy: prints error and exits with code 1
- Falsy values: `false`, `0`, `0.0`, `null`, `""` (empty string)
- Truthy values: everything else

**Output on failure:**
```
Assertion failed: Array must not be empty
```

**When to use:**
- Validating function preconditions
- Checking invariants during development
- Catching programmer errors early

**assert vs panic:**
- `assert(cond, msg)` - Checks a condition, fails if false
- `panic(msg)` - Always fails unconditionally

---

## Signal Handling

### signal

Register or reset signal handler.

**Signature:**
```hemlock
signal(signum: i32, handler: function | null): function | null
```

**Parameters:**
- `signum` - Signal number (use constants like `SIGINT`)
- `handler` - Function to call when signal received, or `null` to reset to default

**Returns:** Previous handler function, or `null`

**Examples:**
```hemlock
fn handle_interrupt(sig) {
    print("Caught SIGINT!");
}

signal(SIGINT, handle_interrupt);

// Reset to default
signal(SIGINT, null);
```

**Handler Signature:**
```hemlock
fn handler(signum: i32) {
    // signum contains the signal number
}
```

**See Also:**
- [Signal constants](#signal-constants)
- `raise()`

---

### raise

Send signal to current process.

**Signature:**
```hemlock
raise(signum: i32): null
```

**Parameters:**
- `signum` - Signal number to raise

**Returns:** `null`

**Examples:**
```hemlock
let count = 0;

fn increment(sig) {
    count = count + 1;
}

signal(SIGUSR1, increment);

raise(SIGUSR1);
raise(SIGUSR1);
print(count);  // 2
```

---

## Global Variables

### args

Command-line arguments array.

**Type:** `array` of strings

**Structure:**
- `args[0]` - Script filename
- `args[1..n]` - Command-line arguments

**Examples:**
```bash
# Command: ./hemlock script.hml hello world
```

```hemlock
print(args[0]);        // "script.hml"
print(args.length);    // 3
print(args[1]);        // "hello"
print(args[2]);        // "world"

// Iterate arguments
let i = 1;
while (i < args.length) {
    print("Argument", i, ":", args[i]);
    i = i + 1;
}
```

**REPL Behavior:** In the REPL, `args.length` is 0 (empty array)

---

## Signal Constants

Standard POSIX signal constants (i32 values):

### Interrupt & Termination

| Constant   | Value | Description                            |
|------------|-------|----------------------------------------|
| `SIGINT`   | 2     | Interrupt from keyboard (Ctrl+C)       |
| `SIGTERM`  | 15    | Termination request                    |
| `SIGQUIT`  | 3     | Quit from keyboard (Ctrl+\)            |
| `SIGHUP`   | 1     | Hangup detected on controlling terminal|
| `SIGABRT`  | 6     | Abort signal                           |

### User-Defined

| Constant   | Value | Description                |
|------------|-------|----------------------------|
| `SIGUSR1`  | 10    | User-defined signal 1      |
| `SIGUSR2`  | 12    | User-defined signal 2      |

### Process Control

| Constant   | Value | Description                     |
|------------|-------|---------------------------------|
| `SIGALRM`  | 14    | Alarm clock timer               |
| `SIGCHLD`  | 17    | Child process status change     |
| `SIGCONT`  | 18    | Continue if stopped             |
| `SIGSTOP`  | 19    | Stop process (cannot be caught) |
| `SIGTSTP`  | 20    | Terminal stop (Ctrl+Z)          |

### I/O

| Constant   | Value | Description                        |
|------------|-------|------------------------------------|
| `SIGPIPE`  | 13    | Broken pipe                        |
| `SIGTTIN`  | 21    | Background read from terminal      |
| `SIGTTOU`  | 22    | Background write to terminal       |

**Examples:**
```hemlock
fn handle_signal(sig) {
    if (sig == SIGINT) {
        print("Interrupt detected");
    }
    if (sig == SIGTERM) {
        print("Termination requested");
    }
}

signal(SIGINT, handle_signal);
signal(SIGTERM, handle_signal);
```

**Note:** `SIGKILL` (9) and `SIGSTOP` (19) cannot be caught or ignored.

---

## Memory Management Functions

See [Memory API](memory-api.md) for complete reference:
- `alloc(size)` - Allocate raw memory
- `free(ptr)` - Free memory
- `buffer(size)` - Allocate safe buffer
- `memset(ptr, byte, size)` - Fill memory
- `memcpy(dest, src, size)` - Copy memory
- `realloc(ptr, new_size)` - Resize allocation

### sizeof

Get the size of a type in bytes.

**Signature:**
```hemlock
sizeof(type): i32
```

**Parameters:**
- `type` - A type constant (`i32`, `f64`, `ptr`, etc.) or type name string

**Returns:** Size in bytes as `i32`

**Examples:**
```hemlock
print(sizeof(i8));       // 1
print(sizeof(i16));      // 2
print(sizeof(i32));      // 4
print(sizeof(i64));      // 8
print(sizeof(f32));      // 4
print(sizeof(f64));      // 8
print(sizeof(ptr));      // 8
print(sizeof(rune));     // 4

// Using type aliases
print(sizeof(byte));     // 1 (same as u8)
print(sizeof(integer));  // 4 (same as i32)
print(sizeof(number));   // 8 (same as f64)

// String form also works
print(sizeof("i32"));    // 4
```

**Supported Types:**
| Type | Size | Aliases |
|------|------|---------|
| `i8` | 1 | - |
| `i16` | 2 | - |
| `i32` | 4 | `integer` |
| `i64` | 8 | - |
| `u8` | 1 | `byte` |
| `u16` | 2 | - |
| `u32` | 4 | - |
| `u64` | 8 | - |
| `f32` | 4 | - |
| `f64` | 8 | `number` |
| `ptr` | 8 | - |
| `rune` | 4 | - |
| `bool` | 1 | - |

**See Also:** `talloc()` for typed allocation

---

### talloc

Allocate memory for a typed array (type-aware allocation).

**Signature:**
```hemlock
talloc(type, count: i32): ptr
```

**Parameters:**
- `type` - A type constant (`i32`, `f64`, `ptr`, etc.)
- `count` - Number of elements to allocate

**Returns:** `ptr` to allocated memory, or `null` on failure

**Examples:**
```hemlock
// Allocate array of 10 i32s (40 bytes)
let int_arr = talloc(i32, 10);
ptr_write_i32(int_arr, 42);
ptr_write_i32(ptr_offset(int_arr, 1, 4), 100);

// Allocate array of 5 f64s (40 bytes)
let float_arr = talloc(f64, 5);

// Allocate array of 100 bytes
let byte_arr = talloc(u8, 100);

// Don't forget to free!
free(int_arr);
free(float_arr);
free(byte_arr);
```

**Comparison with alloc:**
```hemlock
// These are equivalent:
let p1 = talloc(i32, 10);      // Type-aware: 10 i32s
let p2 = alloc(sizeof(i32) * 10);  // Manual calculation

// talloc is clearer and less error-prone
```

**Error Handling:**
- Returns `null` if allocation fails
- Exits with error if count is not positive
- Checks for size overflow (count * element_size)

**See Also:** `alloc()`, `sizeof()`, `free()`

---

## File I/O Functions

See [File API](file-api.md) for complete reference:
- `open(path, mode?)` - Open file

---

## Concurrency Functions

See [Concurrency API](concurrency-api.md) for complete reference:
- `spawn(fn, args...)` - Spawn task
- `join(task)` - Wait for task
- `detach(task)` - Detach task
- `channel(capacity)` - Create channel

### apply

Call a function dynamically with an array of arguments.

**Signature:**
```hemlock
apply(fn: function, args: array): any
```

**Parameters:**
- `fn` - The function to call
- `args` - Array of arguments to pass to the function

**Returns:** The return value of the called function

**Examples:**
```hemlock
fn add(a, b) {
    return a + b;
}

// Call with array of arguments
let result = apply(add, [2, 3]);
print(result);  // 5

// Dynamic dispatch
let operations = {
    add: fn(a, b) { return a + b; },
    mul: fn(a, b) { return a * b; },
    sub: fn(a, b) { return a - b; }
};

fn calculate(op: string, args: array) {
    return apply(operations[op], args);
}

print(calculate("add", [10, 5]));  // 15
print(calculate("mul", [10, 5]));  // 50
print(calculate("sub", [10, 5]));  // 5

// Variable arguments
fn sum(...nums) {
    let total = 0;
    for (n in nums) {
        total = total + n;
    }
    return total;
}

let numbers = [1, 2, 3, 4, 5];
print(apply(sum, numbers));  // 15
```

**Use Cases:**
- Dynamic function dispatch based on runtime values
- Calling functions with variable argument lists
- Implementing higher-order utilities (map, filter, etc.)
- Plugin/extension systems

---

### select

Wait for data from multiple channels, returning when any has data.

**Signature:**
```hemlock
select(channels: array, timeout_ms?: i32): object | null
```

**Parameters:**
- `channels` - Array of channel values
- `timeout_ms` (optional) - Timeout in milliseconds (-1 or omit for infinite)

**Returns:**
- `{ channel, value }` - Object with the channel that had data and the received value
- `null` - On timeout

**Examples:**
```hemlock
let ch1 = channel(1);
let ch2 = channel(1);

// Producer tasks
spawn(fn() {
    sleep(100);
    ch1.send("from channel 1");
});

spawn(fn() {
    sleep(50);
    ch2.send("from channel 2");
});

// Wait for first message
let result = select([ch1, ch2]);
print(result.value);  // "from channel 2" (arrived first)

// With timeout
let result2 = select([ch1, ch2], 1000);  // Wait up to 1 second
if (result2 == null) {
    print("Timeout - no data received");
} else {
    print("Received:", result2.value);
}

// Continuous select loop
while (true) {
    let msg = select([ch1, ch2], 5000);
    if (msg == null) {
        print("No activity for 5 seconds");
        break;
    }
    print("Got message:", msg.value);
}
```

**Behavior:**
- Blocks until one channel has data or timeout expires
- Returns immediately if a channel already has data
- If channel is closed and empty, returns `{ channel, value: null }`
- Polls channels in order (first ready channel wins)

**Use Cases:**
- Multiplexing multiple producers
- Implementing timeouts on channel operations
- Building event loops with multiple sources

---

## Summary Table

### Functions

| Function   | Category        | Returns      | Description                     |
|------------|-----------------|--------------|----------------------------------|
| `print`    | I/O             | `null`       | Print to stdout                  |
| `read_line`| I/O             | `string?`    | Read line from stdin             |
| `eprint`   | I/O             | `null`       | Print to stderr                  |
| `typeof`   | Type            | `string`     | Get type name                    |
| `exec`     | Command         | `object`     | Execute shell command            |
| `exec_argv`| Command         | `object`     | Execute with argument array      |
| `assert`   | Error           | `null`       | Assert condition or exit         |
| `panic`    | Error           | `never`      | Unrecoverable error (exits)      |
| `signal`   | Signal          | `function?`  | Register signal handler          |
| `raise`    | Signal          | `null`       | Send signal to process           |
| `alloc`    | Memory          | `ptr`        | Allocate raw memory              |
| `talloc`   | Memory          | `ptr`        | Typed allocation                 |
| `sizeof`   | Memory          | `i32`        | Get type size in bytes           |
| `free`     | Memory          | `null`       | Free memory                      |
| `buffer`   | Memory          | `buffer`     | Allocate safe buffer             |
| `memset`   | Memory          | `null`       | Fill memory                      |
| `memcpy`   | Memory          | `null`       | Copy memory                      |
| `realloc`  | Memory          | `ptr`        | Resize allocation                |
| `open`     | File I/O        | `file`       | Open file                        |
| `spawn`    | Concurrency     | `task`       | Spawn concurrent task            |
| `join`     | Concurrency     | `any`        | Wait for task result             |
| `detach`   | Concurrency     | `null`       | Detach task                      |
| `channel`  | Concurrency     | `channel`    | Create communication channel     |
| `select`   | Concurrency     | `object?`    | Wait on multiple channels        |
| `apply`    | Functions       | `any`        | Call function with args array    |

### Global Variables

| Variable   | Type     | Description                       |
|------------|----------|-----------------------------------|
| `args`     | `array`  | Command-line arguments            |

### Constants

| Constant   | Type  | Category | Value | Description               |
|------------|-------|----------|-------|---------------------------|
| `SIGINT`   | `i32` | Signal   | 2     | Keyboard interrupt        |
| `SIGTERM`  | `i32` | Signal   | 15    | Termination request       |
| `SIGQUIT`  | `i32` | Signal   | 3     | Keyboard quit             |
| `SIGHUP`   | `i32` | Signal   | 1     | Hangup                    |
| `SIGABRT`  | `i32` | Signal   | 6     | Abort                     |
| `SIGUSR1`  | `i32` | Signal   | 10    | User-defined 1            |
| `SIGUSR2`  | `i32` | Signal   | 12    | User-defined 2            |
| `SIGALRM`  | `i32` | Signal   | 14    | Alarm timer               |
| `SIGCHLD`  | `i32` | Signal   | 17    | Child status change       |
| `SIGCONT`  | `i32` | Signal   | 18    | Continue                  |
| `SIGSTOP`  | `i32` | Signal   | 19    | Stop (uncatchable)        |
| `SIGTSTP`  | `i32` | Signal   | 20    | Terminal stop             |
| `SIGPIPE`  | `i32` | Signal   | 13    | Broken pipe               |
| `SIGTTIN`  | `i32` | Signal   | 21    | Background terminal read  |
| `SIGTTOU`  | `i32` | Signal   | 22    | Background terminal write |

---

## See Also

- [Type System](type-system.md) - Types and conversions
- [Memory API](memory-api.md) - Memory allocation functions
- [File API](file-api.md) - File I/O functions
- [Concurrency API](concurrency-api.md) - Async/concurrency functions
- [String API](string-api.md) - String methods
- [Array API](array-api.md) - Array methods
