# Operators Reference

Complete reference for all operators in Hemlock, including precedence, associativity, and behavior.

---

## Overview

Hemlock provides C-style operators with explicit precedence rules. All operators follow strict typing rules with automatic type promotion where applicable.

---

## Arithmetic Operators

### Binary Arithmetic

| Operator | Name           | Example    | Description                  |
|----------|----------------|------------|------------------------------|
| `+`      | Addition       | `a + b`    | Add two values               |
| `-`      | Subtraction    | `a - b`    | Subtract b from a            |
| `*`      | Multiplication | `a * b`    | Multiply two values          |
| `/`      | Division       | `a / b`    | Divide a by b                |

**Type Promotion:**
Results follow type promotion rules (see [Type System](type-system.md#type-promotion-rules)).

**Examples:**
```hemlock
let a = 10 + 5;        // 15 (i32)
let b = 10 - 3;        // 7 (i32)
let c = 4 * 5;         // 20 (i32)
let d = 20 / 4;        // 5 (i32)

// Float division
let e = 10.0 / 3.0;    // 3.333... (f64)

// Mixed types
let f: u8 = 10;
let g: i32 = 20;
let h = f + g;         // 30 (i32, promoted)
```

**Division by Zero:**
- Integer division by zero: Runtime error
- Float division by zero: Returns `inf` or `-inf`

---

### Unary Arithmetic

| Operator | Name     | Example | Description          |
|----------|----------|---------|----------------------|
| `-`      | Negation | `-a`    | Negate value         |
| `+`      | Plus     | `+a`    | Identity (no-op)     |

**Examples:**
```hemlock
let a = 5;
let b = -a;            // -5
let c = +a;            // 5 (no change)

let x = -3.14;         // -3.14
```

---

## Comparison Operators

| Operator | Name                  | Example    | Returns |
|----------|-----------------------|------------|---------|
| `==`     | Equal                 | `a == b`   | `bool`  |
| `!=`     | Not equal             | `a != b`   | `bool`  |
| `<`      | Less than             | `a < b`    | `bool`  |
| `>`      | Greater than          | `a > b`    | `bool`  |
| `<=`     | Less than or equal    | `a <= b`   | `bool`  |
| `>=`     | Greater than or equal | `a >= b`   | `bool`  |

**Type Promotion:**
Operands are promoted before comparison.

**Examples:**
```hemlock
print(5 == 5);         // true
print(10 != 5);        // true
print(3 < 7);          // true
print(10 > 5);         // true
print(5 <= 5);         // true
print(10 >= 5);        // true

// String comparison
print("hello" == "hello");  // true
print("abc" < "def");       // true (lexicographic)

// Mixed types
let a: u8 = 10;
let b: i32 = 10;
print(a == b);         // true (promoted to i32)
```

---

## Logical Operators

| Operator | Name        | Example      | Description              |
|----------|-------------|--------------|--------------------------|
| `&&`     | Logical AND | `a && b`     | True if both are true    |
| `||`     | Logical OR  | `a || b`     | True if either is true   |
| `!`      | Logical NOT | `!a`         | Negate boolean           |

**Short-Circuit Evaluation:**
- `&&` - Stops at first false value
- `||` - Stops at first true value

**Examples:**
```hemlock
let a = true;
let b = false;

print(a && b);         // false
print(a || b);         // true
print(!a);             // false
print(!b);             // true

// Short-circuit
if (x != 0 && (10 / x) > 2) {
    print("safe");
}

if (x == 0 || (10 / x) > 2) {
    print("safe");
}
```

---

## Bitwise Operators

**Restriction:** Integer types only (i8-i64, u8-u64)

### Binary Bitwise

| Operator | Name         | Example    | Description              |
|----------|--------------|------------|--------------------------|
| `&`      | Bitwise AND  | `a & b`    | AND each bit             |
| `|`      | Bitwise OR   | `a | b`    | OR each bit              |
| `^`      | Bitwise XOR  | `a ^ b`    | XOR each bit             |
| `<<`     | Left shift   | `a << b`   | Shift left by b bits     |
| `>>`     | Right shift  | `a >> b`   | Shift right by b bits    |

**Type Preservation:**
Result type matches operand types (with type promotion).

**Examples:**
```hemlock
let a = 12;  // 1100 in binary
let b = 10;  // 1010 in binary

print(a & b);          // 8  (1000)
print(a | b);          // 14 (1110)
print(a ^ b);          // 6  (0110)
print(a << 2);         // 48 (110000)
print(a >> 1);         // 6  (110)
```

**Unsigned Example:**
```hemlock
let c: u8 = 15;        // 00001111
let d: u8 = 7;         // 00000111

print(c & d);          // 7  (00000111)
print(c | d);          // 15 (00001111)
print(c ^ d);          // 8  (00001000)
```

**Right Shift Behavior:**
- Signed types: Arithmetic shift (sign-extends)
- Unsigned types: Logical shift (zero-fills)

---

### Unary Bitwise

| Operator | Name        | Example | Description              |
|----------|-------------|---------|--------------------------|
| `~`      | Bitwise NOT | `~a`    | Flip all bits            |

**Examples:**
```hemlock
let a = 12;            // 00001100 (i32)
print(~a);             // -13 (two's complement)

let b: u8 = 15;        // 00001111
print(~b);             // 240 (11110000)
```

---

## String Operators

### Concatenation

| Operator | Name           | Example    | Description        |
|----------|----------------|------------|--------------------|
| `+`      | Concatenation  | `a + b`    | Join strings       |

**Examples:**
```hemlock
let s = "hello" + " " + "world";  // "hello world"
let msg = "Count: " + typeof(42); // "Count: 42"

// String + rune
let greeting = "Hello" + '!';      // "Hello!"

// Rune + string
let prefix = '>' + " Message";     // "> Message"
```

---

## Assignment Operators

### Basic Assignment

| Operator | Name       | Example    | Description              |
|----------|------------|------------|--------------------------|
| `=`      | Assignment | `a = b`    | Assign value to variable |

**Examples:**
```hemlock
let x = 10;
x = 20;

let arr = [1, 2, 3];
arr[0] = 99;

let obj = { x: 10 };
obj.x = 20;
```

**Note:** Hemlock does NOT support compound assignment operators (`+=`, `-=`, etc.)

---

## Member Access Operators

### Dot Operator

| Operator | Name             | Example      | Description           |
|----------|------------------|--------------|-----------------------|
| `.`      | Member access    | `obj.field`  | Access object field   |
| `.`      | Property access  | `arr.length` | Access property       |

**Examples:**
```hemlock
// Object field access
let person = { name: "Alice", age: 30 };
print(person.name);        // "Alice"

// Array property
let arr = [1, 2, 3];
print(arr.length);         // 3

// String property
let s = "hello";
print(s.length);           // 5

// Method call
let result = s.to_upper(); // "HELLO"
```

---

### Index Operator

| Operator | Name    | Example   | Description          |
|----------|---------|-----------|----------------------|
| `[]`     | Index   | `arr[i]`  | Access element       |

**Examples:**
```hemlock
// Array indexing
let arr = [10, 20, 30];
print(arr[0]);             // 10
arr[1] = 99;

// String indexing (returns rune)
let s = "hello";
print(s[0]);               // 'h'
s[0] = 'H';                // "Hello"

// Buffer indexing
let buf = buffer(10);
buf[0] = 65;
print(buf[0]);             // 65
```

---

## Function Call Operator

| Operator | Name          | Example      | Description        |
|----------|---------------|--------------|--------------------|
| `()`     | Function call | `f(a, b)`    | Call function      |

**Examples:**
```hemlock
fn add(a, b) {
    return a + b;
}

let result = add(5, 3);    // 8

// Method call
let s = "hello";
let upper = s.to_upper();  // "HELLO"

// Builtin call
print("message");
```

---

## Operator Precedence

Operators are listed from highest to lowest precedence:

| Precedence | Operators                  | Description                    | Associativity |
|------------|----------------------------|--------------------------------|---------------|
| 1          | `()` `[]` `.`              | Call, index, member access     | Left-to-right |
| 2          | `!` `~` `-` (unary) `+` (unary) | Logical NOT, bitwise NOT, negation | Right-to-left |
| 3          | `*` `/`                    | Multiplication, division       | Left-to-right |
| 4          | `+` `-`                    | Addition, subtraction          | Left-to-right |
| 5          | `<<` `>>`                  | Bit shifts                     | Left-to-right |
| 6          | `<` `<=` `>` `>=`          | Relational                     | Left-to-right |
| 7          | `==` `!=`                  | Equality                       | Left-to-right |
| 8          | `&`                        | Bitwise AND                    | Left-to-right |
| 9          | `^`                        | Bitwise XOR                    | Left-to-right |
| 10         | `|`                        | Bitwise OR                     | Left-to-right |
| 11         | `&&`                       | Logical AND                    | Left-to-right |
| 12         | `||`                       | Logical OR                     | Left-to-right |
| 13         | `=`                        | Assignment                     | Right-to-left |

---

## Precedence Examples

### Example 1: Arithmetic and Comparison
```hemlock
let result = 5 + 3 * 2;
// Evaluated as: 5 + (3 * 2) = 11
// Multiplication has higher precedence than addition

let cmp = 10 > 5 + 3;
// Evaluated as: 10 > (5 + 3) = true
// Addition has higher precedence than comparison
```

### Example 2: Bitwise Operators
```hemlock
let result1 = 12 | 10 & 8;
// Evaluated as: 12 | (10 & 8) = 12 | 8 = 12
// & has higher precedence than |

let result2 = 8 | 1 << 2;
// Evaluated as: 8 | (1 << 2) = 8 | 4 = 12
// Shift has higher precedence than bitwise OR

// Use parentheses for clarity
let result3 = (5 & 3) | (2 << 1);
// Evaluated as: 1 | 4 = 5
```

### Example 3: Logical Operators
```hemlock
let result = true || false && false;
// Evaluated as: true || (false && false) = true
// && has higher precedence than ||

let cmp = 5 < 10 && 10 < 20;
// Evaluated as: (5 < 10) && (10 < 20) = true
// Comparison has higher precedence than &&
```

### Example 4: Using Parentheses
```hemlock
// Without parentheses
let a = 2 + 3 * 4;        // 14

// With parentheses
let b = (2 + 3) * 4;      // 20

// Complex expression
let c = (a + b) * (a - b);
```

---

## Type-Specific Operator Behavior

### Integer Division

> **Warning: Silent Truncation**
>
> When both operands of `/` are integers, Hemlock performs **floor division** (truncation toward negative infinity for negative results), not true division. This can cause subtle bugs if you expect decimal results.

Integer division truncates toward negative infinity:

```hemlock
print(10 / 3);             // 3 (i32)
print(-10 / 3);            // -3 (i32)
print(10 / -3);            // -3 (i32)
print(5 / 2);              // 2, NOT 2.5!
```

**To get float division**, ensure at least one operand is a float:

```hemlock
print(5.0 / 2);            // 2.5 (f64)
print(5 / 2.0);            // 2.5 (f64)
print((5 * 1.0) / 2);      // 2.5 (f64)

// Using explicit type annotation
let a: f64 = 5;
print(a / 2);              // 2.5 (f64)
```

**Integer-returning math functions:**
If you want floor/ceil/round results as integers (without the need for type annotation), use the integer-returning variants:

```hemlock
print(floori(3.7));        // 3 (i64)
print(ceili(3.2));         // 4 (i64)
print(roundi(3.5));        // 4 (i64)
print(trunci(3.9));        // 3 (i64)

// These can be used directly as array indices
let arr = [10, 20, 30, 40];
print(arr[floori(1.9)]);   // 20 (index 1)
```

### Float Division

Float division preserves precision:

```hemlock
print(10.0 / 3.0);         // 3.333... (f64)
print(10.0 / 4.0);         // 2.5 (f64)
```

### String Comparison

Strings are compared lexicographically:

```hemlock
print("abc" < "def");      // true
print("apple" > "banana"); // false
print("hello" == "hello"); // true
```

### Null Comparison

```hemlock
let x = null;

print(x == null);          // true
print(x != null);          // false
```

### Type Errors

Some operations are not allowed between incompatible types:

```hemlock
// ERROR: Cannot use bitwise operators on floats
let x = 3.14 & 2.71;

// ERROR: Cannot use bitwise operators on strings
let y = "hello" & "world";

// OK: Type promotion for arithmetic
let a: u8 = 10;
let b: i32 = 20;
let c = a + b;             // i32 (promoted)
```

---

## See Also

- [Type System](type-system.md) - Type promotion and conversion rules
- [Built-in Functions](builtins.md) - Built-in operations
- [String API](string-api.md) - String concatenation and methods
