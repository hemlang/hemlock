# Syntax Overview

This document covers the fundamental syntax rules and structure of Hemlock programs.

## Core Syntax Rules

### Semicolons Are Mandatory

Unlike JavaScript or Python, semicolons are **always required** at the end of statements:

```hemlock
let x = 42;
let y = 10;
print(x + y);
```

**This will cause an error:**
```hemlock
let x = 42  // ERROR: Missing semicolon
let y = 10  // ERROR: Missing semicolon
```

### Braces Are Always Required

All control flow blocks must use braces, even for single statements:

```hemlock
// ✅ CORRECT
if (x > 0) {
    print("positive");
}

// ❌ ERROR: Missing braces
if (x > 0)
    print("positive");
```

### Comments

```hemlock
// This is a single-line comment

/*
   This is a
   multi-line comment
*/

let x = 42;  // Inline comment
```

## Variables

### Declaration

Variables are declared with `let`:

```hemlock
let count = 0;
let name = "Alice";
let pi = 3.14159;
```

### Type Annotations (Optional)

```hemlock
let age: i32 = 30;
let ratio: f64 = 1.618;
let flag: bool = true;
let text: string = "hello";
```

### Constants

Use `const` for immutable values:

```hemlock
const MAX_SIZE: i32 = 1000;
const PI: f64 = 3.14159;
```

Attempting to reassign a const will result in a runtime error: "Cannot assign to const variable".

## Expressions

### Arithmetic Operators

```hemlock
let a = 10;
let b = 3;

print(a + b);   // 13 - Addition
print(a - b);   // 7  - Subtraction
print(a * b);   // 30 - Multiplication
print(a / b);   // 3  - Division (integer)
```

### Comparison Operators

```hemlock
print(a == b);  // false - Equal
print(a != b);  // true  - Not equal
print(a > b);   // true  - Greater than
print(a < b);   // false - Less than
print(a >= b);  // true  - Greater or equal
print(a <= b);  // false - Less or equal
```

### Logical Operators

```hemlock
let x = true;
let y = false;

print(x && y);  // false - AND
print(x || y);  // true  - OR
print(!x);      // false - NOT
```

### Bitwise Operators

```hemlock
let a = 12;  // 1100
let b = 10;  // 1010

print(a & b);   // 8  - Bitwise AND
print(a | b);   // 14 - Bitwise OR
print(a ^ b);   // 6  - Bitwise XOR
print(a << 2);  // 48 - Left shift
print(a >> 1);  // 6  - Right shift
print(~a);      // -13 - Bitwise NOT
```

### Operator Precedence

From highest to lowest:

1. `()` - Grouping
2. `!`, `~`, `-` (unary) - Unary operators
3. `*`, `/` - Multiplication, Division
4. `+`, `-` - Addition, Subtraction
5. `<<`, `>>` - Bit shifts
6. `<`, `<=`, `>`, `>=` - Comparisons
7. `==`, `!=` - Equality
8. `&` - Bitwise AND
9. `^` - Bitwise XOR
10. `|` - Bitwise OR
11. `&&` - Logical AND
12. `||` - Logical OR

**Examples:**
```hemlock
let x = 2 + 3 * 4;      // 14 (not 20)
let y = (2 + 3) * 4;    // 20
let z = 5 << 2 + 1;     // 40 (5 << 3)
```

## Control Flow

### If Statements

```hemlock
if (condition) {
    // body
}

if (condition) {
    // then branch
} else {
    // else branch
}

if (condition1) {
    // branch 1
} else if (condition2) {
    // branch 2
} else {
    // default branch
}
```

### While Loops

```hemlock
while (condition) {
    // body
}
```

**Example:**
```hemlock
let i = 0;
while (i < 10) {
    print(i);
    i = i + 1;
}
```

### For Loops

**C-style for:**
```hemlock
for (initializer; condition; increment) {
    // body
}
```

**Example:**
```hemlock
for (let i = 0; i < 10; i = i + 1) {
    print(i);
}
```

**For-in (arrays):**
```hemlock
for (let item in array) {
    // body
}
```

**Example:**
```hemlock
let items = [10, 20, 30];
for (let x in items) {
    print(x);
}
```

### Switch Statements

```hemlock
switch (expression) {
    case value1:
        // body
        break;
    case value2:
        // body
        break;
    default:
        // default body
        break;
}
```

**Example:**
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
        print("Other");
        break;
}
```

### Break and Continue

```hemlock
// Break: exit loop
for (let i = 0; i < 10; i = i + 1) {
    if (i == 5) {
        break;
    }
    print(i);
}

// Continue: skip to next iteration
for (let i = 0; i < 10; i = i + 1) {
    if (i == 5) {
        continue;
    }
    print(i);
}
```

## Functions

### Named Functions

```hemlock
fn function_name(param1: type1, param2: type2): return_type {
    // body
    return value;
}
```

**Example:**
```hemlock
fn add(a: i32, b: i32): i32 {
    return a + b;
}
```

### Anonymous Functions

```hemlock
let func = fn(params) {
    // body
};
```

**Example:**
```hemlock
let multiply = fn(x, y) {
    return x * y;
};
```

### Type Annotations (Optional)

```hemlock
// No annotations (types inferred)
fn greet(name) {
    return "Hello, " + name;
}

// With annotations (checked at runtime)
fn divide(a: i32, b: i32): f64 {
    return a / b;
}
```

## Objects

### Object Literals

```hemlock
let obj = {
    field1: value1,
    field2: value2,
};
```

**Example:**
```hemlock
let person = {
    name: "Alice",
    age: 30,
    active: true,
};
```

### Methods

```hemlock
let obj = {
    method: fn() {
        self.field = value;
    },
};
```

**Example:**
```hemlock
let counter = {
    count: 0,
    increment: fn() {
        self.count = self.count + 1;
    },
};
```

### Type Definitions

```hemlock
define TypeName {
    field1: type1,
    field2: type2,
    optional_field?: default_value,
}
```

**Example:**
```hemlock
define Person {
    name: string,
    age: i32,
    active?: true,
}
```

## Arrays

### Array Literals

```hemlock
let arr = [element1, element2, element3];
```

**Example:**
```hemlock
let numbers = [1, 2, 3, 4, 5];
let mixed = [1, "two", true, null];
let empty = [];
```

### Array Indexing

```hemlock
let arr = [10, 20, 30];
print(arr[0]);   // 10
arr[1] = 99;     // Modify element
```

## Error Handling

### Try/Catch

```hemlock
try {
    // risky code
} catch (e) {
    // handle error
}
```

### Try/Finally

```hemlock
try {
    // risky code
} finally {
    // always runs
}
```

### Try/Catch/Finally

```hemlock
try {
    // risky code
} catch (e) {
    // handle error
} finally {
    // cleanup
}
```

### Throw

```hemlock
throw expression;
```

**Example:**
```hemlock
if (x < 0) {
    throw "x must be positive";
}
```

### Panic

```hemlock
panic(message);
```

**Example:**
```hemlock
panic("unrecoverable error");
```

## Modules (Experimental)

### Export Statements

```hemlock
export fn function_name() { }
export const CONSTANT = value;
export let variable = value;
export { name1, name2 };
```

### Import Statements

```hemlock
import { name1, name2 } from "./module.hml";
import * as namespace from "./module.hml";
import { name as alias } from "./module.hml";
```

## Async (Experimental)

### Async Functions

```hemlock
async fn function_name(params): return_type {
    // body
}
```

### Spawn/Join

```hemlock
let task = spawn(async_function, arg1, arg2);
let result = join(task);
```

### Channels

```hemlock
let ch = channel(capacity);
ch.send(value);
let value = ch.recv();
ch.close();
```

## FFI (Foreign Function Interface)

### Import Shared Library

```hemlock
import "library_name.so";
```

### Declare External Function

```hemlock
extern fn function_name(param: type): return_type;
```

**Example:**
```hemlock
import "libc.so.6";
extern fn strlen(s: string): i32;
```

## Literals

### Integer Literals

```hemlock
let decimal = 42;
let negative = -100;
let large = 5000000000;  // Auto i64

// Hexadecimal (0x prefix)
let hex = 0xDEADBEEF;
let hex2 = 0xFF;

// Binary (0b prefix)
let bin = 0b1010;
let bin2 = 0b11110000;

// Octal (0o prefix)
let oct = 0o777;
let oct2 = 0O123;

// Numeric separators for readability
let million = 1_000_000;
let hex_sep = 0xFF_FF_FF;
let bin_sep = 0b1111_0000_1010_0101;
let oct_sep = 0o77_77;
```

### Float Literals

```hemlock
let f = 3.14;
let e = 2.71828;
let sci = 1.5e-10;       // Scientific notation
let sci2 = 2.5E+3;       // Uppercase E also works
let no_lead = .5;        // No leading zero (0.5)
let sep = 3.14_159_265;  // Numeric separators
```

### String Literals

```hemlock
let s = "hello";
let escaped = "line1\nline2\ttabbed";
let quote = "She said \"hello\"";

// Hex escape sequences
let hex_esc = "\x48\x65\x6c\x6c\x6f";  // "Hello"

// Unicode escape sequences
let emoji = "\u{1F600}";               // 😀
let heart = "\u{2764}";                // ❤
let mixed = "Hello \u{1F30D}!";        // Hello 🌍!
```

**Escape sequences:**
- `\n` - newline
- `\t` - tab
- `\r` - carriage return
- `\\` - backslash
- `\"` - double quote
- `\'` - single quote
- `\0` - null character
- `\xNN` - hex escape (2 digits)
- `\u{XXXX}` - unicode escape (1-6 digits)

### Rune Literals

```hemlock
let ch = 'A';
let emoji = '🚀';
let escaped = '\n';
let unicode = '\u{1F680}';
let hex_rune = '\x41';      // 'A'
```

### Boolean Literals

```hemlock
let t = true;
let f = false;
```

### Null Literal

```hemlock
let nothing = null;
```

## Scoping Rules

### Block Scope

Variables are scoped to the nearest enclosing block:

```hemlock
let x = 1;  // Outer scope

if (true) {
    let x = 2;  // Inner scope (shadows outer)
    print(x);   // 2
}

print(x);  // 1
```

### Function Scope

Functions create their own scope:

```hemlock
let global = "global";

fn foo() {
    let local = "local";
    print(global);  // Can read outer scope
}

foo();
// print(local);  // ERROR: 'local' not defined here
```

### Closure Scope

Closures capture variables from enclosing scope:

```hemlock
fn makeCounter() {
    let count = 0;
    return fn() {
        count = count + 1;  // Captures 'count'
        return count;
    };
}

let counter = makeCounter();
print(counter());  // 1
print(counter());  // 2
```

## Whitespace and Formatting

### Indentation

Hemlock doesn't enforce specific indentation, but 4 spaces is recommended:

```hemlock
fn example() {
    if (true) {
        print("indented");
    }
}
```

### Line Breaks

Statements can span multiple lines:

```hemlock
let result =
    very_long_function_name(
        arg1,
        arg2,
        arg3
    );
```

## Loop Statement

The `loop` keyword provides a cleaner syntax for infinite loops:

```hemlock
loop {
    // ... do work
    if (done) {
        break;
    }
}
```

This is equivalent to `while (true)` but makes the intent clearer.

## Reserved Keywords

The following keywords are reserved in Hemlock:

```
let, const, fn, if, else, while, for, in, loop, break, continue,
return, true, false, null, typeof, import, export, from,
try, catch, finally, throw, panic, async, await, spawn, join,
detach, channel, define, switch, case, default, extern, self,
type, defer, enum, ref, buffer, Self
```

## Next Steps

- [Type System](types.md) - Learn about Hemlock's type system
- [Control Flow](control-flow.md) - Deep dive into control structures
- [Functions](functions.md) - Master functions and closures
- [Memory Management](memory.md) - Understand pointers and buffers
