# Type System Reference

Complete reference for Hemlock's type system, including all primitive and composite types.

---

## Overview

Hemlock uses a **dynamic type system** with runtime type tags and optional type annotations. Every value has a runtime type, and type conversions follow explicit promotion rules.

**Key Features:**
- Runtime type checking (interpreter)
- Compile-time type checking (hemlockc - enabled by default)
- Optional type annotations
- Automatic type inference for literals
- Explicit type promotion rules
- No implicit conversions that lose precision

---

## Compile-Time Type Checking (hemlockc)

The Hemlock compiler (`hemlockc`) includes a compile-time type checker that validates your code before generating executables. This catches type errors early without needing to run the program.

### Default Behavior

Type checking is **enabled by default** in hemlockc:

```bash
# Type checking happens automatically
hemlockc program.hml -o program

# Errors are reported before compilation
hemlockc bad_types.hml
# Output: 1 type error found
```

### Compiler Flags

| Flag | Description |
|------|-------------|
| `--check` | Check types only, don't compile (exit after validation) |
| `--no-type-check` | Disable type checking (not recommended) |
| `--strict-types` | Enable stricter type warnings |

**Examples:**

```bash
# Just validate types without compiling
hemlockc --check program.hml
# Output: program.hml: no type errors

# Disable type checking (use with caution)
hemlockc --no-type-check dynamic_code.hml -o program

# Enable strict warnings for implicit any types
hemlockc --strict-types program.hml -o program
```

### What the Type Checker Validates

1. **Type annotations** - Ensures assigned values match declared types
2. **Function calls** - Validates argument types against parameter types
3. **Return types** - Checks return statements match declared return type
4. **Operator usage** - Verifies operands are compatible
5. **Property access** - Validates object field types for typed objects

### Permissive Numeric Conversions

The type checker allows numeric type conversions at compile time, with range validation happening at runtime:

```hemlock
let x: i8 = 100;      // OK - 100 fits in i8 (validated at runtime)
let y: u8 = 255;      // OK - within u8 range
let z: f64 = 42;      // OK - i32 to f64 is safe
```

### Dynamic Code Support

Code without type annotations is treated as dynamic (`any` type) and always passes the type checker:

```hemlock
let x = get_value();  // Dynamic - no annotation
process(x);           // OK - dynamic values accepted anywhere
```

---

## Primitive Types

### Numeric Types

#### Signed Integers

| Type   | Size    | Range                                      | Alias     |
|--------|---------|-------------------------------------------|-----------|
| `i8`   | 1 byte  | -128 to 127                               | -         |
| `i16`  | 2 bytes | -32,768 to 32,767                         | -         |
| `i32`  | 4 bytes | -2,147,483,648 to 2,147,483,647           | `integer` |
| `i64`  | 8 bytes | -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807 | - |

**Examples:**
```hemlock
let a: i8 = 127;
let b: i16 = 32000;
let c: i32 = 1000000;
let d: i64 = 9223372036854775807;

// Type alias
let x: integer = 42;  // Same as i32
```

#### Unsigned Integers

| Type   | Size    | Range                     | Alias  |
|--------|---------|---------------------------|--------|
| `u8`   | 1 byte  | 0 to 255                  | `byte` |
| `u16`  | 2 bytes | 0 to 65,535               | -      |
| `u32`  | 4 bytes | 0 to 4,294,967,295        | -      |
| `u64`  | 8 bytes | 0 to 18,446,744,073,709,551,615 | - |

**Examples:**
```hemlock
let a: u8 = 255;
let b: u16 = 65535;
let c: u32 = 4294967295;
let d: u64 = 18446744073709551615;

// Type alias
let byte_val: byte = 65;  // Same as u8
```

#### Floating Point

| Type   | Size    | Precision      | Alias    |
|--------|---------|----------------|----------|
| `f32`  | 4 bytes | ~7 digits      | -        |
| `f64`  | 8 bytes | ~15 digits     | `number` |

**Examples:**
```hemlock
let pi: f32 = 3.14159;
let precise: f64 = 3.14159265359;

// Type alias
let x: number = 2.718;  // Same as f64
```

---

### Integer Literal Inference

Integer literals are automatically typed based on their value:

**Rules:**
- Values in i32 range (-2,147,483,648 to 2,147,483,647): infer as `i32`
- Values outside i32 range but within i64 range: infer as `i64`
- Use explicit type annotations for other types (i8, i16, u8, u16, u32, u64)

**Examples:**
```hemlock
let small = 42;                    // i32 (fits in i32)
let large = 5000000000;            // i64 (> i32 max)
let max_i64 = 9223372036854775807; // i64 (INT64_MAX)
let explicit: u32 = 100;           // u32 (type annotation overrides)
```

---

### Boolean Type

**Type:** `bool`

**Values:** `true`, `false`

**Size:** 1 byte (internally)

**Examples:**
```hemlock
let is_active: bool = true;
let done = false;

if (is_active && !done) {
    print("working");
}
```

---

### Character Types

#### Rune

**Type:** `rune`

**Description:** Unicode codepoint (U+0000 to U+10FFFF)

**Size:** 4 bytes (32-bit value)

**Range:** 0 to 0x10FFFF (1,114,111)

**Literal Syntax:** Single quotes `'x'`

**Examples:**
```hemlock
// ASCII
let a = 'A';
let digit = '0';

// Multi-byte UTF-8
let rocket = '🚀';      // U+1F680
let heart = '❤';        // U+2764
let chinese = '中';     // U+4E2D

// Escape sequences
let newline = '\n';
let tab = '\t';
let backslash = '\\';
let quote = '\'';
let null = '\0';

// Unicode escapes
let emoji = '\u{1F680}';   // Up to 6 hex digits
let max = '\u{10FFFF}';    // Maximum codepoint
```

**Type Conversions:**
```hemlock
// Integer to rune
let code: rune = 65;        // 'A'
let r: rune = 128640;       // 🚀

// Rune to integer
let value: i32 = 'Z';       // 90

// Rune to string
let s: string = 'H';        // "H"

// u8 to rune
let byte: u8 = 65;
let rune_val: rune = byte;  // 'A'
```

**See Also:** [String API](string-api.md) for string + rune concatenation

---

### String Type

**Type:** `string`

**Description:** UTF-8 encoded, mutable, heap-allocated text

**Encoding:** UTF-8 (U+0000 to U+10FFFF)

**Mutability:** Mutable (unlike most languages)

**Properties:**
- `.length` - Codepoint count (number of characters)
- `.byte_length` - Byte count (UTF-8 encoding size)

**Literal Syntax:** Double quotes `"text"`

**Examples:**
```hemlock
let s = "hello";
s[0] = 'H';             // Mutate (now "Hello")
print(s.length);        // 5 (codepoint count)
print(s.byte_length);   // 5 (UTF-8 bytes)

let emoji = "🚀";
print(emoji.length);        // 1 (one codepoint)
print(emoji.byte_length);   // 4 (four UTF-8 bytes)
```

**Indexing:**
```hemlock
let s = "hello";
let ch = s[0];          // Returns rune 'h'
s[0] = 'H';             // Set with rune
```

**See Also:** [String API](string-api.md) for complete method reference

---

### Null Type

**Type:** `null`

**Description:** The null value (absence of value)

**Size:** 8 bytes (internally)

**Value:** `null`

**Examples:**
```hemlock
let x = null;
let y: i32 = null;  // ERROR: type mismatch

if (x == null) {
    print("x is null");
}
```

---

## Composite Types

### Array Type

**Type:** `array`

**Description:** Dynamic, heap-allocated, mixed-type array

**Properties:**
- `.length` - Number of elements

**Zero-indexed:** Yes

**Literal Syntax:** `[elem1, elem2, ...]`

**Examples:**
```hemlock
let arr = [1, 2, 3, 4, 5];
print(arr[0]);         // 1
print(arr.length);     // 5

// Mixed types
let mixed = [1, "hello", true, null];
```

**See Also:** [Array API](array-api.md) for complete method reference

---

### Object Type

**Type:** `object`

**Description:** JavaScript-style object with dynamic fields

**Literal Syntax:** `{ field: value, ... }`

**Examples:**
```hemlock
let person = { name: "Alice", age: 30 };
print(person.name);  // "Alice"

// Add field dynamically
person.email = "alice@example.com";
```

**Type Definitions:**
```hemlock
define Person {
    name: string,
    age: i32,
    active?: bool,  // Optional field
}

let p: Person = { name: "Bob", age: 25 };
print(typeof(p));  // "Person"
```

---

### Pointer Types

#### Raw Pointer (ptr)

**Type:** `ptr`

**Description:** Raw memory address (unsafe)

**Size:** 8 bytes

**Bounds Checking:** None

**Examples:**
```hemlock
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);
```

#### Buffer (buffer)

**Type:** `buffer`

**Description:** Safe pointer wrapper with bounds checking

**Structure:** Pointer + length + capacity

**Properties:**
- `.length` - Buffer size
- `.capacity` - Allocated capacity

**Examples:**
```hemlock
let b: buffer = buffer(64);
b[0] = 65;              // Bounds checked
print(b.length);        // 64
free(b);
```

**See Also:** [Memory API](memory-api.md) for allocation functions

---

## Special Types

### File Type

**Type:** `file`

**Description:** File handle for I/O operations

**Properties:**
- `.path` - File path (string)
- `.mode` - Open mode (string)
- `.closed` - Whether file is closed (bool)

**See Also:** [File API](file-api.md)

---

### Task Type

**Type:** `task`

**Description:** Handle for concurrent task

**See Also:** [Concurrency API](concurrency-api.md)

---

### Channel Type

**Type:** `channel`

**Description:** Thread-safe communication channel

**See Also:** [Concurrency API](concurrency-api.md)

---

### Function Type

**Type:** `function`

**Description:** First-class function value

**Examples:**
```hemlock
fn add(a, b) {
    return a + b;
}

let multiply = fn(x, y) {
    return x * y;
};

print(typeof(add));      // "function"
print(typeof(multiply)); // "function"
```

---

### Void Type

**Type:** `void`

**Description:** Absence of return value (internal use)

---

## Type Promotion Rules

When mixing types in operations, Hemlock promotes to the "higher" type:

**Promotion Hierarchy:**
```
f64 (highest precision)
 ↑
f32
 ↑
u64
 ↑
i64
 ↑
u32
 ↑
i32
 ↑
u16
 ↑
i16
 ↑
u8
 ↑
i8 (lowest)
```

**Rules:**
1. Float always wins over integer
2. Larger size wins within same category (int/uint/float)
3. Both operands are promoted to result type
4. **Precision preservation:** i64/u64 + f32 promotes to f64 (not f32)

**Examples:**
```hemlock
// Size promotion
u8 + i32    → i32    // Larger size wins
i32 + i64   → i64    // Larger size wins
u32 + u64   → u64    // Larger size wins

// Float promotion
i32 + f32   → f32    // Float wins, f32 sufficient for i32
i64 + f32   → f64    // Promotes to f64 to preserve i64 precision
i64 + f64   → f64    // Float always wins
i8 + f64    → f64    // Float + largest wins
```

**Why i64 + f32 → f64?**

f32 has only a 24-bit mantissa, which cannot precisely represent integers larger
than 2^24 (16,777,216). Since i64 can hold values up to 2^63, mixing i64 with f32
would cause severe precision loss. Hemlock promotes to f64 (53-bit mantissa) instead.

---

## Range Checking

Type annotations enforce range checks at assignment:

**Valid Assignments:**
```hemlock
let x: u8 = 255;             // OK
let y: i8 = 127;             // OK
let a: i64 = 2147483647;     // OK
let b: u64 = 4294967295;     // OK
```

**Invalid Assignments (Runtime Error):**
```hemlock
let x: u8 = 256;             // ERROR: out of range
let y: i8 = 128;             // ERROR: max is 127
let z: u64 = -1;             // ERROR: u64 cannot be negative
```

---

## Type Introspection

### typeof(value)

Returns the type name as a string.

**Signature:**
```hemlock
typeof(value: any): string
```

**Returns:**
- Primitive types: `"i8"`, `"i16"`, `"i32"`, `"i64"`, `"u8"`, `"u16"`, `"u32"`, `"u64"`, `"f32"`, `"f64"`, `"bool"`, `"string"`, `"rune"`, `"null"`
- Composite types: `"array"`, `"object"`, `"ptr"`, `"buffer"`, `"function"`
- Special types: `"file"`, `"task"`, `"channel"`
- Typed objects: Custom type name (e.g., `"Person"`)

**Examples:**
```hemlock
print(typeof(42));              // "i32"
print(typeof(3.14));            // "f64"
print(typeof("hello"));         // "string"
print(typeof('A'));             // "rune"
print(typeof(true));            // "bool"
print(typeof([1, 2, 3]));       // "array"
print(typeof({ x: 10 }));       // "object"

define Person { name: string }
let p: Person = { name: "Alice" };
print(typeof(p));               // "Person"
```

**See Also:** [Built-in Functions](builtins.md#typeof)

---

## Type Conversions

### Implicit Conversions

Hemlock performs implicit type conversions in arithmetic operations following the type promotion rules.

**Examples:**
```hemlock
let a: u8 = 10;
let b: i32 = 20;
let result = a + b;     // result is i32 (promoted)
```

### Explicit Conversions

Use type annotations for explicit conversions:

**Examples:**
```hemlock
// Integer to float
let i: i32 = 42;
let f: f64 = i;         // 42.0

// Float to integer (truncates)
let x: f64 = 3.14;
let y: i32 = x;         // 3

// Integer to rune
let code: rune = 65;    // 'A'

// Rune to integer
let value: i32 = 'Z';   // 90

// Rune to string
let s: string = 'H';    // "H"
```

---

## Type Aliases

### Built-in Aliases

Hemlock provides built-in type aliases for common types:

| Alias     | Actual Type | Usage                    |
|-----------|-------------|--------------------------|
| `integer` | `i32`       | General-purpose integers |
| `number`  | `f64`       | General-purpose floats   |
| `byte`    | `u8`        | Byte values              |

**Examples:**
```hemlock
let count: integer = 100;       // Same as i32
let price: number = 19.99;      // Same as f64
let b: byte = 255;              // Same as u8
```

### Custom Type Aliases

Define custom type aliases using the `type` keyword:

```hemlock
// Simple aliases
type Integer = i32;
type Text = string;

// Function type aliases
type Callback = fn(i32): void;
type Predicate = fn(any): bool;
type BinaryOp = fn(i32, i32): i32;

// Compound type aliases
define HasName { name: string }
define HasAge { age: i32 }
type Person = HasName & HasAge;

// Generic type aliases
type Pair<T> = { first: T, second: T };
type Result<T, E> = { value: T?, error: E? };
```

**Using custom aliases:**
```hemlock
let cb: Callback = fn(n) { print(n); };
let p: Person = { name: "Alice", age: 30 };
let coords: Pair<f64> = { first: 3.14, second: 2.71 };
```

**Note:** Type aliases are transparent - `typeof()` returns the underlying type name.

---

## Function Types

Function types specify the signature of function values:

### Syntax

```hemlock
fn(param_types): return_type
```

### Examples

```hemlock
// Basic function type
let add: fn(i32, i32): i32 = fn(a, b) { return a + b; };

// Function parameter
fn apply(f: fn(i32): i32, x: i32): i32 {
    return f(x);
}

// Higher-order function returning function
fn make_adder(n: i32): fn(i32): i32 {
    return fn(x) { return x + n; };
}

// Async function type
fn run_async(handler: async fn(): void) {
    spawn(handler);
}
```

---

## Compound Types (Intersection)

Compound types use `&` to require multiple type constraints:

```hemlock
define HasName { name: string }
define HasAge { age: i32 }
define HasEmail { email: string }

// Object must satisfy all types
let person: HasName & HasAge = { name: "Alice", age: 30 };

// Three or more types
fn describe(p: HasName & HasAge & HasEmail) {
    print(p.name + " <" + p.email + ">");
}
```

---

## Summary Table

| Type       | Size     | Mutable | Heap-allocated | Description                    |
|------------|----------|---------|----------------|--------------------------------|
| `i8`-`i64` | 1-8 bytes| No      | No             | Signed integers                |
| `u8`-`u64` | 1-8 bytes| No      | No             | Unsigned integers              |
| `f32`      | 4 bytes  | No      | No             | Single-precision float         |
| `f64`      | 8 bytes  | No      | No             | Double-precision float         |
| `bool`     | 1 byte   | No      | No             | Boolean                        |
| `rune`     | 4 bytes  | No      | No             | Unicode codepoint              |
| `string`   | Variable | Yes     | Yes            | UTF-8 text                     |
| `array`    | Variable | Yes     | Yes            | Dynamic array                  |
| `object`   | Variable | Yes     | Yes            | Dynamic object                 |
| `ptr`      | 8 bytes  | No      | No             | Raw pointer                    |
| `buffer`   | Variable | Yes     | Yes            | Safe pointer wrapper           |
| `file`     | Opaque   | Yes     | Yes            | File handle                    |
| `task`     | Opaque   | No      | Yes            | Concurrent task handle         |
| `channel`  | Opaque   | Yes     | Yes            | Thread-safe channel            |
| `function` | Opaque   | No      | Yes            | Function value                 |
| `null`     | 8 bytes  | No      | No             | Null value                     |

---

## See Also

- [Operators Reference](operators.md) - Type behavior in operations
- [Built-in Functions](builtins.md) - Type introspection and conversion
- [String API](string-api.md) - String type methods
- [Array API](array-api.md) - Array type methods
- [Memory API](memory-api.md) - Pointer and buffer operations
