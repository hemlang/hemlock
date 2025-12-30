# Hemlock Tutorial

A comprehensive step-by-step guide to learning Hemlock.

## Table of Contents

1. [Hello World](#hello-world)
2. [Variables and Types](#variables-and-types)
3. [Arithmetic and Operations](#arithmetic-and-operations)
4. [Control Flow](#control-flow)
5. [Functions](#functions)
6. [Strings and Runes](#strings-and-runes)
7. [Arrays](#arrays)
8. [Objects](#objects)
9. [Memory Management](#memory-management)
10. [Error Handling](#error-handling)
11. [File I/O](#file-io)
12. [Putting It All Together](#putting-it-all-together)

## Hello World

Let's start with the traditional first program:

```hemlock
print("Hello, World!");
```

Save this as `hello.hml` and run:

```bash
./hemlock hello.hml
```

**Key Points:**
- `print()` is a built-in function that outputs to stdout
- Strings are enclosed in double quotes
- Semicolons are **mandatory**

## Variables and Types

### Declaring Variables

```hemlock
// Basic variable declaration
let x = 42;
let name = "Alice";
let pi = 3.14159;

print(x);      // 42
print(name);   // Alice
print(pi);     // 3.14159
```

### Type Annotations

While types are inferred by default, you can be explicit:

```hemlock
let age: i32 = 30;
let height: f64 = 5.9;
let initial: rune = 'A';
let active: bool = true;
```

### Type Inference

Hemlock infers types based on values:

```hemlock
let small = 42;              // i32 (fits in 32-bit)
let large = 5000000000;      // i64 (too big for i32)
let decimal = 3.14;          // f64 (default for floats)
let text = "hello";          // string
let flag = true;             // bool
```

### Type Checking

```hemlock
// Check types with typeof()
print(typeof(42));        // "i32"
print(typeof(3.14));      // "f64"
print(typeof("hello"));   // "string"
print(typeof(true));      // "bool"
print(typeof(null));      // "null"
```

## Arithmetic and Operations

### Basic Arithmetic

```hemlock
let a = 10;
let b = 3;

print(a + b);   // 13
print(a - b);   // 7
print(a * b);   // 30
print(a / b);   // 3 (integer division)
print(a == b);  // false
print(a > b);   // true
```

### Type Promotion

When mixing types, Hemlock promotes to the larger/more precise type:

```hemlock
let x: i32 = 10;
let y: f64 = 3.5;
let result = x + y;  // result is f64 (10.0 + 3.5 = 13.5)

print(result);       // 13.5
print(typeof(result)); // "f64"
```

### Bitwise Operations

```hemlock
let a = 12;  // 1100 in binary
let b = 10;  // 1010 in binary

print(a & b);   // 8  (AND)
print(a | b);   // 14 (OR)
print(a ^ b);   // 6  (XOR)
print(a << 1);  // 24 (left shift)
print(a >> 1);  // 6  (right shift)
print(~a);      // -13 (NOT)
```

## Control Flow

### If Statements

```hemlock
let x = 10;

if (x > 0) {
    print("positive");
} else if (x < 0) {
    print("negative");
} else {
    print("zero");
}
```

**Note:** Braces are **always required**, even for single statements.

### While Loops

```hemlock
let count = 0;
while (count < 5) {
    print(`Count: ${count}`);
    count = count + 1;
}
```

### For Loops

```hemlock
// C-style for loop
for (let i = 0; i < 10; i = i + 1) {
    print(i);
}

// For-in loop (arrays)
let items = [10, 20, 30, 40];
for (let item in items) {
    print(`Item: ${item}`);
}
```

### Switch Statements

```hemlock
let day = 3;

switch (day) {
    case 1:
        print("Monday");
        break;
    case 2:
        print("Tuesday");
        break;
    case 3:
        print("Wednesday");
        break;
    default:
        print("Other day");
        break;
}
```

### Break and Continue

```hemlock
// Break: exit loop early
let i = 0;
while (i < 10) {
    if (i == 5) {
        break;
    }
    print(i);
    i = i + 1;
}
// Prints: 0, 1, 2, 3, 4

// Continue: skip to next iteration
for (let j = 0; j < 5; j = j + 1) {
    if (j == 2) {
        continue;
    }
    print(j);
}
// Prints: 0, 1, 3, 4
```

## Functions

### Named Functions

```hemlock
fn greet(name: string): string {
    return "Hello, " + name + "!";
}

let message = greet("Alice");
print(message);  // "Hello, Alice!"
```

### Anonymous Functions

```hemlock
let add = fn(a, b) {
    return a + b;
};

print(add(5, 3));  // 8
```

### Recursion

```hemlock
fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

print(factorial(5));  // 120
```

### Closures

Functions capture their environment:

```hemlock
fn makeCounter() {
    let count = 0;
    return fn() {
        count = count + 1;
        return count;
    };
}

let counter = makeCounter();
print(counter());  // 1
print(counter());  // 2
print(counter());  // 3
```

### Higher-Order Functions

```hemlock
fn apply(f, x) {
    return f(x);
}

fn double(n) {
    return n * 2;
}

let result = apply(double, 21);
print(result);  // 42
```

## Strings and Runes

### String Basics

Strings are **mutable** and **UTF-8**:

```hemlock
let s = "hello";
print(s.length);      // 5 (character count)
print(s.byte_length); // 5 (byte count)

// Mutation
s[0] = 'H';
print(s);  // "Hello"
```

### String Methods

```hemlock
let text = "  Hello, World!  ";

// Case conversion
print(text.to_upper());  // "  HELLO, WORLD!  "
print(text.to_lower());  // "  hello, world!  "

// Trimming
print(text.trim());      // "Hello, World!"

// Substring extraction
let hello = text.substr(2, 5);  // "Hello"
let world = text.slice(9, 14);  // "World"

// Searching
let pos = text.find("World");   // 9
let has = text.contains("o");   // true

// Splitting
let parts = "a,b,c".split(","); // ["a", "b", "c"]

// Replacement
let s = "hello world".replace("world", "there");
print(s);  // "hello there"
```

### Runes (Unicode Codepoints)

```hemlock
let ch: rune = 'A';
let emoji: rune = '🚀';

print(ch);      // 'A'
print(emoji);   // U+1F680

// Rune + String concatenation
let msg = '>' + " Important";
print(msg);  // "> Important"

// Convert between rune and integer
let code: i32 = ch;     // 65 (ASCII code)
let r: rune = 128640;   // U+1F680 (🚀)
```

## Arrays

### Array Basics

```hemlock
let numbers = [1, 2, 3, 4, 5];
print(numbers[0]);      // 1
print(numbers.length);  // 5

// Modify elements
numbers[2] = 99;
print(numbers[2]);  // 99
```

### Array Methods

```hemlock
let arr = [10, 20, 30];

// Add/remove at end
arr.push(40);           // [10, 20, 30, 40]
let last = arr.pop();   // 40, arr is now [10, 20, 30]

// Add/remove at beginning
arr.unshift(5);         // [5, 10, 20, 30]
let first = arr.shift(); // 5, arr is now [10, 20, 30]

// Insert/remove at index
arr.insert(1, 15);      // [10, 15, 20, 30]
let removed = arr.remove(2);  // 20

// Search
let index = arr.find(15);     // 1
let has = arr.contains(10);   // true

// Slice
let slice = arr.slice(0, 2);  // [10, 15]

// Join to string
let text = arr.join(", ");    // "10, 15, 30"
```

### Iteration

```hemlock
let items = ["apple", "banana", "cherry"];

// For-in loop
for (let item in items) {
    print(item);
}

// Manual iteration
let i = 0;
while (i < items.length) {
    print(items[i]);
    i = i + 1;
}
```

## Objects

### Object Literals

```hemlock
let person = {
    name: "Alice",
    age: 30,
    city: "NYC"
};

print(person.name);  // "Alice"
print(person.age);   // 30

// Add/modify fields
person.email = "alice@example.com";
person.age = 31;
```

### Methods and `self`

```hemlock
let calculator = {
    value: 0,
    add: fn(x) {
        self.value = self.value + x;
    },
    get: fn() {
        return self.value;
    }
};

calculator.add(10);
calculator.add(5);
print(calculator.get());  // 15
```

### Type Definitions (Duck Typing)

```hemlock
define Person {
    name: string,
    age: i32,
    active?: true,  // Optional with default
}

let p = { name: "Bob", age: 25 };
let typed: Person = p;  // Duck typing validates structure

print(typeof(typed));   // "Person"
print(typed.active);    // true (default applied)
```

### JSON Serialization

```hemlock
let obj = { x: 10, y: 20, name: "test" };

// Object to JSON
let json = obj.serialize();
print(json);  // {"x":10,"y":20,"name":"test"}

// JSON to Object
let restored = json.deserialize();
print(restored.name);  // "test"
```

## Memory Management

### Safe Buffers (Recommended)

```hemlock
// Allocate buffer
let buf = buffer(10);
print(buf.length);    // 10
print(buf.capacity);  // 10

// Set values (bounds-checked)
buf[0] = 65;  // 'A'
buf[1] = 66;  // 'B'
buf[2] = 67;  // 'C'

// Access values
print(buf[0]);  // 65

// Must free when done
free(buf);
```

### Raw Pointers (Advanced)

```hemlock
// Allocate raw memory
let ptr = alloc(100);

// Fill with zeros
memset(ptr, 0, 100);

// Copy data
let src = alloc(50);
memcpy(ptr, src, 50);

// Free both
free(src);
free(ptr);
```

### Memory Functions

```hemlock
// Reallocate
let p = alloc(64);
p = realloc(p, 128);  // Resize to 128 bytes
free(p);

// Typed allocation (future)
// let arr = talloc(i32, 100);  // Array of 100 i32s
```

## Error Handling

### Try/Catch

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
}
// Output: Error: division by zero
```

### Finally Block

```hemlock
let file = null;

try {
    file = open("data.txt", "r");
    let content = file.read();
    print(content);
} catch (e) {
    print("Error: " + e);
} finally {
    // Always runs
    if (file != null) {
        file.close();
    }
}
```

### Throwing Objects

```hemlock
try {
    throw { code: 404, message: "Not found" };
} catch (e) {
    print(`Error ${e.code}: ${e.message}`);
}
// Output: Error 404: Not found
```

### Panic (Unrecoverable Errors)

```hemlock
fn validate(x) {
    if (x < 0) {
        panic("x must be non-negative");
    }
    return x * 2;
}

validate(-5);  // Program exits with: panic: x must be non-negative
```

## File I/O

### Reading Files

```hemlock
// Read entire file
let f = open("data.txt", "r");
let content = f.read();
print(content);
f.close();

// Read specific number of bytes
let f2 = open("data.txt", "r");
let chunk = f2.read(100);  // Read 100 bytes
f2.close();
```

### Writing Files

```hemlock
// Write text
let f = open("output.txt", "w");
f.write("Hello, File!\n");
f.write("Second line\n");
f.close();

// Append to file
let f2 = open("output.txt", "a");
f2.write("Appended line\n");
f2.close();
```

### Binary I/O

```hemlock
// Write binary data
let buf = buffer(256);
buf[0] = 255;
buf[1] = 128;

let f = open("data.bin", "w");
f.write_bytes(buf);
f.close();

// Read binary data
let f2 = open("data.bin", "r");
let data = f2.read_bytes(256);
print(data[0]);  // 255
f2.close();

free(buf);
free(data);
```

### File Properties

```hemlock
let f = open("/path/to/file.txt", "r");

print(f.path);    // "/path/to/file.txt"
print(f.mode);    // "r"
print(f.closed);  // false

f.close();
print(f.closed);  // true
```

## Putting It All Together

Let's build a simple word counter program:

```hemlock
// wordcount.hml - Count words in a file

fn count_words(filename: string): i32 {
    let file = null;
    let count = 0;

    try {
        file = open(filename, "r");
        let content = file.read();

        // Split by whitespace and count
        let words = content.split(" ");
        count = words.length;

    } catch (e) {
        print("Error reading file: " + e);
        return -1;
    } finally {
        if (file != null) {
            file.close();
        }
    }

    return count;
}

// Main program
if (args.length < 2) {
    print("Usage: " + args[0] + " <filename>");
} else {
    let filename = args[1];
    let words = count_words(filename);

    if (words >= 0) {
        print(`Word count: ${words}`);
    }
}
```

Run with:
```bash
./hemlock wordcount.hml data.txt
```

## Next Steps

Congratulations! You've learned the basics of Hemlock. Here's what to explore next:

- [Async & Concurrency](../advanced/async-concurrency.md) - True multi-threading
- [FFI](../advanced/ffi.md) - Call C functions
- [Signal Handling](../advanced/signals.md) - Process signals
- [API Reference](../reference/builtins.md) - Complete API docs
- [Examples](../../examples/) - More real-world programs

## Practice Exercises

Try building these programs to practice:

1. **Calculator**: Implement a simple calculator with +, -, *, /
2. **File Copy**: Copy one file to another
3. **Fibonacci**: Generate Fibonacci numbers
4. **JSON Parser**: Read and parse JSON files
5. **Text Processor**: Find and replace text in files

Happy coding with Hemlock! 🚀
