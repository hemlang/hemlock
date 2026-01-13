# Type System

Hemlock features a **dynamic type system** with optional type annotations and runtime type checking.

---

## Type Selection Guide: What Type Should I Use?

**New to types?** Start here. If you're familiar with type systems, skip to [Philosophy](#philosophy).

### The Short Answer

**Just let Hemlock figure it out:**

```hemlock
let count = 42;        // Hemlock knows this is an integer
let price = 19.99;     // Hemlock knows this is a decimal
let name = "Alice";    // Hemlock knows this is text
let active = true;     // Hemlock knows this is yes/no
```

Hemlock automatically picks the right type for your values. You don't *need* to specify types.

### When to Add Type Annotations

Add types when you want to:

1. **Be specific about size** - `i8` vs `i64` matters for memory or FFI
2. **Document your code** - Types show what a function expects
3. **Catch mistakes early** - Hemlock checks types at runtime

```hemlock
// Without types (works fine):
fn add(a, b) {
    return a + b;
}

// With types (more explicit):
fn add(a: i32, b: i32): i32 {
    return a + b;
}
```

### Quick Reference: Choosing Number Types

| What you're storing | Suggested type | Example |
|---------------------|----------------|---------|
| Regular whole numbers | `i32` (default) | `let count = 42;` |
| Very large numbers | `i64` | `let population = 8000000000;` |
| Never-negative counts | `u32` | `let items: u32 = 100;` |
| Bytes (0-255) | `u8` | `let pixel: u8 = 255;` |
| Decimals/fractions | `f64` (default) | `let price = 19.99;` |
| Performance-critical decimals | `f32` | `let x: f32 = 1.5;` |

### Quick Reference: All Types

| Category | Types | When to use |
|----------|-------|-------------|
| **Whole numbers** | `i8`, `i16`, `i32`, `i64` | Counting, IDs, ages, etc. |
| **Positive-only numbers** | `u8`, `u16`, `u32`, `u64` | Bytes, sizes, array lengths |
| **Decimals** | `f32`, `f64` | Money, measurements, math |
| **Yes/No** | `bool` | Flags, conditions |
| **Text** | `string` | Names, messages, any text |
| **Single character** | `rune` | Individual letters, emoji |
| **Lists** | `array` | Collections of values |
| **Named fields** | `object` | Grouping related data |
| **Raw memory** | `ptr`, `buffer` | Low-level programming |
| **Nothing** | `null` | Absence of a value |

### Common Scenarios

**"I just need a number"**
```hemlock
let x = 42;  // Done! Hemlock picks i32
```

**"I need decimals"**
```hemlock
let price = 19.99;  // Done! Hemlock picks f64
```

**"I'm working with bytes (files, network)"**
```hemlock
let byte: u8 = 255;  // 0-255 range
```

**"I need really big numbers"**
```hemlock
let big = 9000000000000;  // Hemlock auto-picks i64 (> i32 max)
// Or be explicit:
let big: i64 = 9000000000000;
```

**"I'm storing money"**
```hemlock
// Option 1: Float (simple, but has precision limits)
let price: f64 = 19.99;

// Option 2: Store as cents (more precise)
let price_cents: i32 = 1999;  // $19.99 as integer cents
```

**"I'm passing data to C code (FFI)"**
```hemlock
// Match C types exactly
let c_int: i32 = 100;      // C 'int'
let c_long: i64 = 100;     // C 'long' (on 64-bit)
let c_char: u8 = 65;       // C 'char'
let c_double: f64 = 3.14;  // C 'double'
```

### What Happens When Types Mix?

When you combine different types, Hemlock promotes to the "bigger" type:

```hemlock
let a: i32 = 10;
let b: f64 = 2.5;
let result = a + b;  // result is f64 (12.5)
// The integer became a decimal automatically
```

**Rule of thumb:** Floats always "win" - mixing any integer with a float gives you a float.

### Type Errors

If you try to use the wrong type, Hemlock tells you at runtime:

```hemlock
let age: i32 = "thirty";  // ERROR: type mismatch - expected i32, got string
```

To convert types, use type constructor functions:

```hemlock
let text = "42";
let number = i32(text);   // Parse string to integer: 42
let back = text + "";     // Already a string
```

---

## Philosophy

- **Dynamic by default** - Every value has a runtime type tag
- **Typed by choice** - Optional type annotations enforce runtime checks
- **Explicit conversions** - Implicit conversions follow clear promotion rules
- **Honest about types** - `typeof()` always tells the truth

## Primitive Types

### Integer Types

**Signed integers:**
```hemlock
let tiny: i8 = 127;              // 8-bit  (-128 to 127)
let small: i16 = 32767;          // 16-bit (-32768 to 32767)
let normal: i32 = 2147483647;    // 32-bit (default)
let large: i64 = 9223372036854775807;  // 64-bit
```

**Unsigned integers:**
```hemlock
let byte: u8 = 255;              // 8-bit  (0 to 255)
let word: u16 = 65535;           // 16-bit (0 to 65535)
let dword: u32 = 4294967295;     // 32-bit (0 to 4294967295)
let qword: u64 = 18446744073709551615;  // 64-bit
```

**Type aliases:**
```hemlock
let i: integer = 42;   // Alias for i32
let b: byte = 255;     // Alias for u8
```

### Floating-Point Types

```hemlock
let f: f32 = 3.14159;        // 32-bit float
let d: f64 = 2.718281828;    // 64-bit float (default)
let n: number = 1.618;       // Alias for f64
```

### Boolean Type

```hemlock
let flag: bool = true;
let active: bool = false;
```

### String Type

```hemlock
let text: string = "Hello, World!";
let empty: string = "";
```

Strings are **mutable**, **UTF-8 encoded**, and **heap-allocated**.

See [Strings](strings.md) for full details.

### Rune Type

```hemlock
let ch: rune = 'A';
let emoji: rune = '🚀';
let newline: rune = '\n';
let unicode: rune = '\u{1F680}';
```

Runes represent **Unicode codepoints** (U+0000 to U+10FFFF).

See [Runes](runes.md) for full details.

### Null Type

```hemlock
let nothing = null;
let uninitialized: string = null;
```

`null` is its own type with a single value.

## Composite Types

### Array Type

```hemlock
let numbers: array = [1, 2, 3, 4, 5];
let mixed = [1, "two", true, null];  // Mixed types allowed
let empty: array = [];
```

See [Arrays](arrays.md) for full details.

### Object Type

```hemlock
let obj: object = { x: 10, y: 20 };
let person = { name: "Alice", age: 30 };
```

See [Objects](objects.md) for full details.

### Pointer Types

**Raw pointer:**
```hemlock
let p: ptr = alloc(64);
// No bounds checking, manual lifetime management
free(p);
```

**Safe buffer:**
```hemlock
let buf: buffer = buffer(64);
// Bounds-checked, tracks length and capacity
free(buf);
```

See [Memory Management](memory.md) for full details.

## Enum Types

Enums define a set of named constants:

### Basic Enums

```hemlock
enum Color {
    RED,
    GREEN,
    BLUE
}

let c = Color.RED;
print(c);              // 0
print(typeof(c));      // "Color"

// Comparison
if (c == Color.RED) {
    print("It's red!");
}

// Switch on enum
switch (c) {
    case Color.RED:
        print("Stop");
        break;
    case Color.GREEN:
        print("Go");
        break;
    case Color.BLUE:
        print("Blue?");
        break;
}
```

### Enums with Values

Enums can have explicit integer values:

```hemlock
enum Status {
    OK = 0,
    ERROR = 1,
    PENDING = 2
}

print(Status.OK);      // 0
print(Status.ERROR);   // 1

enum HttpCode {
    OK = 200,
    NOT_FOUND = 404,
    SERVER_ERROR = 500
}

let code = HttpCode.NOT_FOUND;
print(code);           // 404
```

### Auto-incrementing Values

Without explicit values, enums auto-increment from 0:

```hemlock
enum Priority {
    LOW,       // 0
    MEDIUM,    // 1
    HIGH,      // 2
    CRITICAL   // 3
}

// Can mix explicit and auto values
enum Level {
    DEBUG = 10,
    INFO,      // 11
    WARN,      // 12
    ERROR = 50,
    FATAL      // 51
}
```

### Enum Usage Patterns

```hemlock
// As function parameters
fn set_priority(p: Priority) {
    if (p == Priority.CRITICAL) {
        print("Urgent!");
    }
}

set_priority(Priority.HIGH);

// In objects
define Task {
    name: string,
    priority: Priority
}

let task: Task = {
    name: "Fix bug",
    priority: Priority.HIGH
};
```

## Special Types

### File Type

```hemlock
let f: file = open("data.txt", "r");
f.close();
```

Represents an open file handle.

### Task Type

```hemlock
async fn compute(): i32 { return 42; }
let task = spawn(compute);
let result: i32 = join(task);
```

Represents an async task handle.

### Channel Type

```hemlock
let ch: channel = channel(10);
ch.send(42);
let value = ch.recv();
```

Represents a communication channel between tasks.

### Void Type

```hemlock
extern fn exit(code: i32): void;
```

Used for functions that don't return a value (FFI only).

## Type Inference

### Integer Literal Inference

Hemlock infers integer types based on value range:

```hemlock
let a = 42;              // i32 (fits in 32-bit)
let b = 5000000000;      // i64 (> i32 max)
let c = 128;             // i32
let d: u8 = 128;         // u8 (explicit annotation)
```

**Rules:**
- Values in i32 range (-2147483648 to 2147483647): infer as `i32`
- Values outside i32 range but within i64: infer as `i64`
- Use explicit annotations for other types (i8, i16, u8, u16, u32, u64)

### Float Literal Inference

```hemlock
let x = 3.14;        // f64 (default)
let y: f32 = 3.14;   // f32 (explicit)
```

### Scientific Notation

Hemlock supports scientific notation for numeric literals:

```hemlock
let a = 1e10;        // 10000000000.0 (f64)
let b = 1e-12;       // 0.000000000001 (f64)
let c = 3.14e2;      // 314.0 (f64)
let d = 2.5e-3;      // 0.0025 (f64)
let e = 1E10;        // Case insensitive
let f = 1e+5;        // Explicit positive exponent
```

**Note:** Any literal using scientific notation is always inferred as `f64`.

### Other Type Inference

```hemlock
let s = "hello";     // string
let ch = 'A';        // rune
let flag = true;     // bool
let arr = [1, 2, 3]; // array
let obj = { x: 10 }; // object
let nothing = null;  // null
```

## Type Annotations

### Variable Annotations

```hemlock
let age: i32 = 30;
let ratio: f64 = 1.618;
let name: string = "Alice";
```

### Function Parameter Annotations

```hemlock
fn greet(name: string, age: i32) {
    print("Hello, " + name + "!");
}
```

### Function Return Type Annotations

```hemlock
fn add(a: i32, b: i32): i32 {
    return a + b;
}
```

### Object Type Annotations (Duck Typing)

```hemlock
define Person {
    name: string,
    age: i32,
}

let p: Person = { name: "Bob", age: 25 };
```

## Type Checking

### Runtime Type Checking

Type annotations are checked at **runtime**, not compile-time:

```hemlock
let x: i32 = 42;     // OK
let y: i32 = 3.14;   // Runtime error: type mismatch

fn add(a: i32, b: i32): i32 {
    return a + b;
}

add(5, 3);           // OK
add(5, "hello");     // Runtime error: type mismatch
```

### Type Queries

Use `typeof()` to check value types:

```hemlock
print(typeof(42));         // "i32"
print(typeof(3.14));       // "f64"
print(typeof("hello"));    // "string"
print(typeof(true));       // "bool"
print(typeof(null));       // "null"
print(typeof([1, 2, 3]));  // "array"
print(typeof({ x: 10 }));  // "object"
```

## Type Conversions

### Implicit Type Promotion

When mixing types in operations, Hemlock promotes to the "higher" type:

**Promotion Hierarchy (lowest to highest):**
```
i8 → i16 → i32 → u32 → i64 → u64 → f32 → f64
      ↑     ↑     ↑
     u8    u16
```

**Float always wins:**
```hemlock
let x: i32 = 10;
let y: f64 = 3.5;
let result = x + y;  // result is f64 (13.5)
```

**Larger size wins:**
```hemlock
let a: i32 = 100;
let b: i64 = 200;
let sum = a + b;     // sum is i64 (300)
```

**Precision preservation:** When mixing 64-bit integers with f32, Hemlock promotes
to f64 to avoid precision loss (f32 has only 24-bit mantissa, insufficient for i64/u64):
```hemlock
let big: i64 = 9007199254740993;
let small: f32 = 1.0;
let result = big + small;  // result is f64, not f32!
```

**Examples:**
```hemlock
u8 + i32  → i32
i32 + i64 → i64
u32 + u64 → u64
i32 + f32 → f32    // f32 sufficient for i32
i64 + f32 → f64    // f64 needed to preserve i64 precision
i64 + f64 → f64
i8 + f64  → f64
```

### Explicit Type Conversion

**Integer ↔ Float:**
```hemlock
let i: i32 = 42;
let f: f64 = i;      // i32 → f64 (42.0)

let x: f64 = 3.14;
let n: i32 = x;      // f64 → i32 (3, truncated)
```

**Integer ↔ Rune:**
```hemlock
let code: i32 = 65;
let ch: rune = code;  // i32 → rune ('A')

let r: rune = 'Z';
let value: i32 = r;   // rune → i32 (90)
```

**Rune → String:**
```hemlock
let ch: rune = '🚀';
let s: string = ch;   // rune → string ("🚀")
```

**u8 → Rune:**
```hemlock
let b: u8 = 65;
let r: rune = b;      // u8 → rune ('A')
```

### Type Constructor Functions

Type names can be used as functions to convert or parse values:

**Parsing strings to numbers:**
```hemlock
let n = i32("42");       // Parse string to i32: 42
let f = f64("3.14159");  // Parse string to f64: 3.14159
let b = bool("true");    // Parse string to bool: true

// All numeric types supported
let a = i8("-128");      // Parse to i8
let c = u8("255");       // Parse to u8
let d = i16("1000");     // Parse to i16
let e = u16("50000");    // Parse to u16
let g = i64("9000000000000"); // Parse to i64
let h = u64("18000000000000"); // Parse to u64
let j = f32("1.5");      // Parse to f32
```

**Hex and negative numbers:**
```hemlock
let hex = i32("0xFF");   // 255
let neg = i32("-42");    // -42
let bin = i32("0b1010"); // 10 (binary)
```

**Type aliases work too:**
```hemlock
let x = integer("100");  // Same as i32("100")
let y = number("1.5");   // Same as f64("1.5")
let z = byte("200");     // Same as u8("200")
```

**Converting between numeric types:**
```hemlock
let big = i64(42);           // i32 to i64
let truncated = i32(3.99);   // f64 to i32 (truncates to 3)
let promoted = f64(100);     // i32 to f64 (100.0)
let narrowed = i8(127);      // i32 to i8
```

**Type annotations perform numeric coercion (but NOT string parsing):**
```hemlock
let f: f64 = 100;        // i32 to f64 via annotation (OK)
let s: string = 'A';     // Rune to string via annotation (OK)
let code: i32 = 'A';     // Rune to i32 via annotation (gets codepoint, OK)

// String parsing requires explicit type constructors:
let n = i32("42");       // Use type constructor for string parsing
// let x: i32 = "42";    // ERROR - type annotations don't parse strings
```

**Error handling:**
```hemlock
// Invalid strings throw errors when using type constructors
let bad = i32("hello");  // Runtime error: cannot parse "hello" as i32
let overflow = u8("256"); // Runtime error: 256 out of range for u8
```

**Boolean parsing:**
```hemlock
let t = bool("true");    // true
let f = bool("false");   // false
let bad = bool("yes");   // Runtime error: must be "true" or "false"
```

## Range Checking

Type annotations enforce range checks at assignment:

```hemlock
let x: u8 = 255;    // OK
let y: u8 = 256;    // ERROR: out of range for u8

let a: i8 = 127;    // OK
let b: i8 = 128;    // ERROR: out of range for i8

let c: i64 = 2147483647;   // OK
let d: u64 = 4294967295;   // OK
let e: u64 = -1;           // ERROR: u64 cannot be negative
```

## Type Promotion Examples

### Mixed Integer Types

```hemlock
let a: i8 = 10;
let b: i32 = 20;
let sum = a + b;     // i32 (30)

let c: u8 = 100;
let d: u32 = 200;
let total = c + d;   // u32 (300)
```

### Integer + Float

```hemlock
let i: i32 = 5;
let f: f32 = 2.5;
let result = i * f;  // f32 (12.5)
```

### Complex Expressions

```hemlock
let a: i8 = 10;
let b: i32 = 20;
let c: f64 = 3.0;

let result = a + b * c;  // f64 (70.0)
// Evaluation: b * c → f64(60.0)
//             a + f64(60.0) → f64(70.0)
```

## Duck Typing (Objects)

Objects use **structural typing** (duck typing):

```hemlock
define Person {
    name: string,
    age: i32,
}

// OK: Has all required fields
let p1: Person = { name: "Alice", age: 30 };

// OK: Extra fields allowed
let p2: Person = { name: "Bob", age: 25, city: "NYC" };

// ERROR: Missing 'age' field
let p3: Person = { name: "Carol" };

// ERROR: Wrong type for 'age'
let p4: Person = { name: "Dave", age: "thirty" };
```

**Type checking happens at assignment:**
- Validates all required fields present
- Validates field types match
- Extra fields are allowed and preserved
- Sets object's type name for `typeof()`

## Optional Fields

```hemlock
define Config {
    host: string,
    port: i32,
    debug?: false,     // Optional with default
    timeout?: i32,     // Optional, defaults to null
}

let cfg1: Config = { host: "localhost", port: 8080 };
print(cfg1.debug);    // false (default)
print(cfg1.timeout);  // null

let cfg2: Config = { host: "0.0.0.0", port: 80, debug: true };
print(cfg2.debug);    // true (overridden)
```

## Type Aliases

Hemlock supports custom type aliases using the `type` keyword:

### Basic Type Aliases

```hemlock
// Simple type alias
type Integer = i32;
type Text = string;

// Using the alias
let x: Integer = 42;
let msg: Text = "hello";
```

### Function Type Aliases

```hemlock
// Function type alias
type Callback = fn(i32): void;
type Predicate = fn(i32): bool;
type AsyncHandler = async fn(string): i32;

// Using function type aliases
let cb: Callback = fn(n) { print(n); };
let isEven: Predicate = fn(n) { return n % 2 == 0; };
```

### Compound Type Aliases

```hemlock
// Combine multiple defines into one type
define HasName { name: string }
define HasAge { age: i32 }

type Person = HasName & HasAge;

let p: Person = { name: "Alice", age: 30 };
```

### Generic Type Aliases

```hemlock
// Generic type alias
type Pair<T> = { first: T, second: T };
type Result<T, E> = { value: T?, error: E? };

// Using generic aliases
let coords: Pair<f64> = { first: 3.14, second: 2.71 };
```

**Note:** Type aliases are transparent - `typeof()` returns the underlying type name, not the alias.

## Type System Limitations

Current limitations:

- **No generics on functions** - Function type parameters not yet supported
- **No union types** - Cannot express "A or B"
- **No nullable types** - All types can be null (use `?` suffix for explicit nullability)

**Note:** The compiler (`hemlockc`) provides compile-time type checking. The interpreter performs runtime type checking only. See the [compiler documentation](../design/implementation.md) for details.

## Best Practices

### When to Use Type Annotations

**DO use annotations when:**
- Precise type matters (e.g., `u8` for byte values)
- Documenting function interfaces
- Enforcing constraints (e.g., range checks)

```hemlock
fn hash(data: buffer, length: u32): u64 {
    // Implementation
}
```

**DON'T use annotations when:**
- Type is obvious from literal
- Internal implementation details
- Unnecessary ceremony

```hemlock
// Unnecessary
let x: i32 = 42;

// Better
let x = 42;
```

### Type Safety Patterns

**Check before use:**
```hemlock
if (typeof(value) == "i32") {
    // Safe to use as i32
}
```

**Validate function arguments:**
```hemlock
fn divide(a, b) {
    if (typeof(a) != "i32" || typeof(b) != "i32") {
        throw "arguments must be integers";
    }
    if (b == 0) {
        throw "division by zero";
    }
    return a / b;
}
```

**Use duck typing for flexibility:**
```hemlock
define Printable {
    toString: fn,
}

fn print_item(item: Printable) {
    print(item.toString());
}
```

## Next Steps

- [Strings](strings.md) - UTF-8 string type and operations
- [Runes](runes.md) - Unicode codepoint type
- [Arrays](arrays.md) - Dynamic array type
- [Objects](objects.md) - Object literals and duck typing
- [Memory](memory.md) - Pointer and buffer types
