# Quick Start

Get up and running with Hemlock in minutes!

## Your First Program

Create a file called `hello.hml`:

```hemlock
print("Hello, Hemlock!");
```

Run with the interpreter:

```bash
./hemlock hello.hml
```

Or compile to a native executable:

```bash
./hemlockc hello.hml -o hello
./hello
```

Output:
```
Hello, Hemlock!
```

### Interpreter vs Compiler

Hemlock provides two ways to run programs:

| Tool | Use Case | Type Checking |
|------|----------|---------------|
| `hemlock` | Quick scripts, REPL, development | Runtime only |
| `hemlockc` | Production binaries, better performance | Compile-time (default) |

The compiler (`hemlockc`) type-checks your code before generating an executable, catching errors early.

## Basic Syntax

### Variables

```hemlock
// Variables are declared with 'let'
let x = 42;
let name = "Alice";
let pi = 3.14159;

// Type annotations are optional
let count: i32 = 100;
let ratio: f64 = 0.618;
```

**Important**: Semicolons are **mandatory** in Hemlock!

### Types

Hemlock has a rich type system:

```hemlock
// Integers
let small: i8 = 127;          // 8-bit signed
let byte: u8 = 255;           // 8-bit unsigned
let num: i32 = 2147483647;    // 32-bit signed (default)
let big: i64 = 9223372036854775807;  // 64-bit signed

// Floats
let f: f32 = 3.14;            // 32-bit float
let d: f64 = 2.71828;         // 64-bit float (default)

// Strings and characters
let text: string = "Hello";   // UTF-8 string
let emoji: rune = '🚀';       // Unicode codepoint

// Boolean and null
let flag: bool = true;
let empty = null;
```

### Control Flow

```hemlock
// If statements
if (x > 0) {
    print("positive");
} else if (x < 0) {
    print("negative");
} else {
    print("zero");
}

// While loops
let i = 0;
while (i < 5) {
    print(i);
    i = i + 1;
}

// For loops
for (let j = 0; j < 10; j = j + 1) {
    print(j);
}
```

### Functions

```hemlock
// Named function
fn add(a: i32, b: i32): i32 {
    return a + b;
}

let result = add(5, 3);  // 8

// Anonymous function
let multiply = fn(x, y) {
    return x * y;
};

print(multiply(4, 7));  // 28
```

## Working with Strings

Strings in Hemlock are **mutable** and **UTF-8**:

```hemlock
let s = "hello";
s[0] = 'H';              // Now "Hello"
print(s);

// String methods
let upper = s.to_upper();     // "HELLO"
let words = "a,b,c".split(","); // ["a", "b", "c"]
let sub = s.substr(1, 3);     // "ell"

// Concatenation
let greeting = "Hello" + ", " + "World!";
print(greeting);  // "Hello, World!"
```

## Arrays

Dynamic arrays with mixed types:

```hemlock
let numbers = [1, 2, 3, 4, 5];
print(numbers[0]);      // 1
print(numbers.length);  // 5

// Array methods
numbers.push(6);        // [1, 2, 3, 4, 5, 6]
let last = numbers.pop();  // 6
let slice = numbers.slice(1, 4);  // [2, 3, 4]

// Mixed types allowed
let mixed = [1, "two", true, null];
```

## Objects

JavaScript-style objects:

```hemlock
// Object literal
let person = {
    name: "Alice",
    age: 30,
    city: "NYC"
};

print(person.name);  // "Alice"
person.age = 31;     // Modify field

// Methods with 'self'
let counter = {
    count: 0,
    increment: fn() {
        self.count = self.count + 1;
    }
};

counter.increment();
print(counter.count);  // 1
```

## Memory Management

Hemlock uses **manual memory management**:

```hemlock
// Safe buffer (recommended)
let buf = buffer(64);   // Allocate 64 bytes
buf[0] = 65;            // Set first byte to 'A'
print(buf[0]);          // 65
free(buf);              // Free memory

// Raw pointer (advanced)
let ptr = alloc(100);
memset(ptr, 0, 100);    // Fill with zeros
free(ptr);
```

**Important**: You must `free()` what you `alloc()`!

## Error Handling

```hemlock
fn divide(a, b) {
    if (b == 0) {
        throw "division by zero";
    }
    return a / b;
}

try {
    let result = divide(10, 0);
    print(result);
} catch (e) {
    print("Error: " + e);
} finally {
    print("Done");
}
```

## Command-Line Arguments

Access program arguments via the `args` array:

```hemlock
// script.hml
print("Script:", args[0]);
print(`Arguments: ${args.length - 1}`);

let i = 1;
while (i < args.length) {
    print(`  arg ${i}: ${args[i]}`);
    i = i + 1;
}
```

Run with:
```bash
./hemlock script.hml hello world
```

Output:
```
Script: script.hml
Arguments: 2
  arg 1: hello
  arg 2: world
```

## File I/O

```hemlock
// Write to file
let f = open("data.txt", "w");
f.write("Hello, File!");
f.close();

// Read from file
let f2 = open("data.txt", "r");
let content = f2.read();
print(content);  // "Hello, File!"
f2.close();
```

## What's Next?

Now that you've seen the basics, explore more:

- [Tutorial](tutorial.md) - Comprehensive step-by-step guide
- [Language Guide](../language-guide/syntax.md) - Deep dive into all features
- [Examples](../../examples/) - Real-world example programs
- [API Reference](../reference/builtins.md) - Complete API documentation

## Common Pitfalls

### Forgetting Semicolons

```hemlock
// ❌ ERROR: Missing semicolon
let x = 42
let y = 10

// ✅ CORRECT
let x = 42;
let y = 10;
```

### Forgetting to Free Memory

```hemlock
// ❌ MEMORY LEAK
let buf = buffer(100);
// ... use buf ...
// Forgot to call free(buf)!

// ✅ CORRECT
let buf = buffer(100);
// ... use buf ...
free(buf);
```

### Braces Are Required

```hemlock
// ❌ ERROR: Missing braces
if (x > 0)
    print("positive");

// ✅ CORRECT
if (x > 0) {
    print("positive");
}
```

## Getting Help

- Read the [full documentation](../README.md)
- Check [examples directory](../../examples/)
- Look at [test files](../../tests/) for usage patterns
- Report issues on GitHub
