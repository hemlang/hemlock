# Hemlock Language Design Philosophy

> "A small, unsafe language for writing unsafe things safely."

This document captures the core design principles and philosophy of Hemlock. Read this first before making any changes or additions to the language.

---

## Core Identity

Hemlock is a **systems scripting language** that embraces manual memory management and explicit control. It's designed for programmers who want:
- The power of C
- The ergonomics of modern scripting languages
- Structured async concurrency built-in
- No hidden behavior or magic

**Hemlock is NOT:**
- Memory-safe (dangling pointers are your responsibility)
- A replacement for Rust, Go, or Lua
- A language that hides complexity from you

**Hemlock IS:**
- Explicit over implicit, always
- Educational and experimental
- A "C scripting layer" for systems work
- Honest about tradeoffs

---

## Design Principles

### 1. **Explicit Over Implicit**
- Semicolons are mandatory (no ASI)
- No garbage collection
- Manual memory management (alloc/free)
- Type annotations are optional but checked at runtime
- No automatic resource cleanup (no RAII, no defer yet)

**Bad (implicit):**
```hemlock
let x = 5  // Missing semicolon - should error
```

**Good (explicit):**
```hemlock
let x = 5;
free(ptr);  // You allocated it, you free it
```

### 2. **Dynamic by Default, Typed by Choice**
- Every value has a runtime type tag
- Literals infer types intelligently:
  - Small integers (fits in i32): `42` → `i32`
  - Large integers (> i32 range): `9223372036854775807` → `i64`
  - Floats: `3.14` → `f64`
- Optional type annotations enforce runtime checks
- Implicit type conversions follow clear promotion rules

```hemlock
let x = 42;              // i32 inferred (small value)
let y: u8 = 255;         // explicit u8
let z = x + y;           // promotes to i32
let big = 5000000000;    // i64 inferred (> i32 max)
```

### 3. **Unsafe is a Feature, Not a Bug**
- Pointer arithmetic can overflow (user's responsibility)
- No bounds checking on raw `ptr` (use `buffer` if you want safety)
- Double-free crashes are allowed (manual memory management)
- Type system prevents accidents but allows footguns when needed

```hemlock
let p = alloc(10);
let q = p + 100;  // Way past allocation - allowed but dangerous
```

### 4. **Structured Concurrency First-Class**
- `async`/`await` built into the language
- Channels for communication
- `spawn`/`join`/`detach` for task management
- No raw threads, no locks - structured only

### 5. **C-like Syntax, Low Ceremony**
- Familiar to systems programmers
- `{}` blocks always, no optional braces
- Operators match C: `+`, `-`, `*`, `/`, `%`, `&&`, `||`, `!`
- Type syntax matches Rust/TypeScript: `let x: type = value;`

---

## Type System

### Numeric Types
- **Signed integers:** `i8`, `i16`, `i32`, `i64`
- **Unsigned integers:** `u8`, `u16`, `u32`, `u64`
- **Floats:** `f32`, `f64`
- **Aliases:** `integer` (i32), `number` (f64), `byte` (u8)

### Other Primitives
- **bool:** `true`, `false`
- **string:** UTF-8, mutable, heap-allocated with `.length` property (see [Strings](#strings) section for 18 methods)
- **rune:** Unicode codepoint (U+0000 to U+10FFFF), 32-bit value (see [Runes](#runes) section)
- **array:** Dynamic arrays with mixed types, `.length` property (see [Arrays](#arrays) section for 15 methods)
- **ptr:** Raw pointer (8 bytes, no bounds checking)
- **buffer:** Safe wrapper (ptr + length + capacity, bounds checked)
- **null:** The null value

### Type Promotion Rules
When mixing types in operations, promote to the "higher" type:
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

**Examples:**
- `u8 + i32` → `i32` (larger size wins)
- `i32 + i64` → `i64` (larger size wins)
- `u32 + u64` → `u64` (larger size wins)
- `i32 + f32` → `f32` (float always wins)
- `i64 + f64` → `f64` (float always wins)
- `i8 + f64` → `f64` (float + largest wins)

### Range Checking
Type annotations enforce range checks at assignment:
```hemlock
let x: u8 = 255;   // OK
let y: u8 = 256;   // ERROR: out of range
let z: i8 = 128;   // ERROR: max is 127

// 64-bit types
let a: i64 = 2147483647;   // OK
let b: u64 = 4294967295;   // OK
let c: u64 = -1;           // ERROR: u64 cannot be negative
```

### Integer Literal Inference
Integer literals are automatically typed based on their value range:
- Values in i32 range (-2147483648 to 2147483647): infer as `i32`
- Values outside i32 range but within i64 range: infer as `i64`
- Use explicit type annotations for other types (i8, i16, u8, u16, u32, u64)

```hemlock
let small = 42;                    // i32 (fits in i32)
let large = 5000000000;            // i64 (> i32 max)
let max_i64 = 9223372036854775807; // i64 (INT64_MAX)
let explicit: u32 = 100;           // u32 (type annotation overrides)
```

---

## Memory Management

### The Two Pointer Types

**`ptr` - Raw Pointer (Dangerous)**
- Just an address, no tracking
- No bounds checking
- User manages lifetime
- For experts and FFI

```hemlock
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);  // You must remember to free
```

**`buffer` - Safe Wrapper (Recommended)**
- Pointer + length + capacity
- Bounds checked on access
- Still manual free required
- Better default for most code

```hemlock
let b: buffer = buffer(64);
b[0] = 65;              // bounds checked
print(b.length);        // 64
free(b);                // still manual
```

### Memory API

**Core Allocation:**
- `alloc(bytes)` - allocate raw memory, returns `ptr`
- `free(ptr)` - free memory (works on both `ptr` and `buffer`)
- `buffer(size)` - allocate safe buffer

**Operations:**
- `memset(ptr, byte, size)` - fill memory
- `memcpy(dest, src, size)` - copy memory
- `realloc(ptr, size)` - resize allocation

**Typed Allocation (TODO):**
- `talloc(type, count)` - allocate array of typed values
- `sizeof(type)` - get size of type

---

## Strings

Strings are **UTF-8 first-class mutable sequences** with full Unicode support and a rich set of methods for text processing:

```hemlock
let s = "hello";
s[0] = 'H';             // mutate with rune (now "Hello")
print(s.length);        // 5 (codepoint count)
let c = s[0];           // returns rune (Unicode codepoint)
let msg = s + " world"; // concatenation
let emoji = "🚀";
print(emoji.length);    // 1 (one codepoint)
print(emoji.byte_length); // 4 (four UTF-8 bytes)
```

**Properties:**
- **UTF-8 encoded** - Full Unicode support (U+0000 to U+10FFFF)
- **Mutable** (unlike Python/JS/Java)
- **Indexing returns `rune`** (Unicode codepoint, not byte)
- **`.length`** - Codepoint count (number of characters)
- **`.byte_length`** - Byte count (UTF-8 encoding size)
- **Heap-allocated** with internal capacity tracking

**UTF-8 Behavior:**
- All string operations work with **codepoints** (characters), not bytes
- Multi-byte characters (emojis, CJK, etc.) count as 1 character
- `"Hello".length` → 5, `"🚀".length` → 1
- Indexing, slicing, and substring operations use codepoint positions

### String Methods

**Substring & Slicing (codepoint-based):**
```hemlock
let s = "hello world";
let sub = s.substr(6, 5);       // "world" (start, length in codepoints)
let slice = s.slice(0, 5);      // "hello" (start, end exclusive, codepoints)

// UTF-8 example
let text = "Hi🚀!";
let emoji = text.substr(2, 1);  // "🚀" (position 2, length 1)
```

**Search & Find:**
```hemlock
let pos = s.find("world");      // 6 (index of first occurrence)
let pos2 = s.find("foo");       // -1 (not found)
let has = s.contains("world");  // true
```

**Split & Trim:**
```hemlock
let parts = "a,b,c".split(",");  // ["a", "b", "c"]
let clean = "  hello  ".trim();  // "hello"
```

**Case Conversion:**
```hemlock
let upper = s.to_upper();       // "HELLO WORLD"
let lower = s.to_lower();       // "hello world"
```

**Prefix/Suffix:**
```hemlock
let starts = s.starts_with("hello");  // true
let ends = s.ends_with("world");      // true
```

**Replacement:**
```hemlock
let s2 = s.replace("world", "there");      // "hello there"
let s3 = "foo foo".replace_all("foo", "bar"); // "bar bar"
```

**Repetition:**
```hemlock
let repeated = "ha".repeat(3);  // "hahaha"
```

**Character & Byte Access:**
```hemlock
// Character-level access (codepoints)
let char = s.char_at(0);        // Returns rune (Unicode codepoint)
let chars = s.chars();          // Array of runes ['h', 'e', 'l', 'l', 'o']

// Byte-level access (UTF-8 bytes)
let byte = s.byte_at(0);        // Returns u8 (byte value)
let bytes = s.bytes();          // Array of u8 bytes [104, 101, 108, 108, 111]
let buf = s.to_bytes();         // Convert to buffer (legacy)

// UTF-8 example
let emoji = "🚀";
print(emoji.char_at(0));        // U+1F680 (rocket rune)
print(emoji.byte_at(0));        // 240 (first UTF-8 byte)
```

**Method Chaining:**
```hemlock
let result = "  Hello World  "
    .trim()
    .to_lower()
    .replace("world", "hemlock");  // "hello hemlock"
```

**Available String Methods:**
- `substr(start, length)` - Extract substring by codepoint position and length
- `slice(start, end)` - Extract substring by codepoint range (end exclusive)
- `find(needle)` - Find first occurrence, returns codepoint index or -1
- `contains(needle)` - Check if string contains substring
- `split(delimiter)` - Split into array of strings
- `trim()` - Remove leading/trailing whitespace
- `to_upper()` - Convert to uppercase
- `to_lower()` - Convert to lowercase
- `starts_with(prefix)` - Check if starts with prefix
- `ends_with(suffix)` - Check if ends with suffix
- `replace(old, new)` - Replace first occurrence
- `replace_all(old, new)` - Replace all occurrences
- `repeat(count)` - Repeat string n times
- `char_at(index)` - Get Unicode codepoint at index (returns rune)
- `byte_at(index)` - Get byte value at index (returns u8)
- `chars()` - Convert to array of runes (codepoints)
- `bytes()` - Convert to array of bytes (u8 values)
- `to_bytes()` - Convert to buffer for low-level access

### String Interpolation

**Syntax:** Use backticks (`` ` ``) with `${}` for template strings (like JavaScript)

```hemlock
// Basic variable interpolation
let name = "Alice";
let greeting = `Hello, ${name}!`;
print(greeting);  // "Hello, Alice!"

// Multiple interpolations
let age = 30;
let msg = `Hello, ${name}! You are ${age} years old.`;
print(msg);  // "Hello, Alice! You are 30 years old."

// Arbitrary expressions
let x = 10;
let y = 20;
let result = `Sum: ${x + y}, Product: ${x * y}`;
print(result);  // "Sum: 30, Product: 200"

// Works with any expression
let items = [1, 2, 3];
print(`Array length: ${items.length}`);  // "Array length: 3"
```

**Three string types in Hemlock:**
- **Regular strings:** `"hello"` - No interpolation
- **Template strings:** `` `hello ${name}` `` - With interpolation
- **Rune literals:** `'A'` - Single Unicode codepoint

**Escape sequences in template strings:**
- `\$` - Literal dollar sign: `` `Price: \$100` `` → `"Price: $100"`
- `` \` `` - Literal backtick: `` `Use \` for code` `` → ``"Use ` for code"``
- `\n`, `\t`, `\\`, etc. - Standard escape sequences work

**How it works:**
- Backtick strings are parsed for `${...}` expressions at compile time
- Expressions are evaluated at runtime
- Results are automatically converted to strings via `value_to_string()`
- All parts are concatenated into the final string
- UTF-8 safe (works with runes, emojis, etc.)

---

## Runes

Runes represent **Unicode codepoints** (U+0000 to U+10FFFF) as a distinct type for character manipulation:

```hemlock
let ch = 'A';           // Rune literal
let emoji = '🚀';       // Multi-byte character as single rune
print(ch);              // 'A'
print(emoji);           // U+1F680

let s = "Hello " + '!'; // String + rune concatenation
let r = '>' + " msg";   // Rune + string concatenation
```

**Properties:**
- **32-bit value** representing a Unicode codepoint
- **Range:** 0 to 0x10FFFF (1,114,111)
- **Not a numeric type** - used for character representation
- **Distinct from u8/char** - runes are Unicode, u8 is bytes

### Rune Literals

**Basic Syntax:**
```hemlock
let a = 'A';            // ASCII character
let b = '0';            // Digit character
let c = '!';            // Punctuation
```

**Multi-byte UTF-8 Characters:**
```hemlock
let rocket = '🚀';      // Emoji (U+1F680)
let heart = '❤';        // Heart (U+2764)
let chinese = '中';     // CJK character (U+4E2D)
```

**Escape Sequences:**
```hemlock
let newline = '\n';     // Newline (U+000A)
let tab = '\t';         // Tab (U+0009)
let backslash = '\\';   // Backslash (U+005C)
let quote = '\'';       // Single quote (U+0027)
let dquote = '"';       // Double quote (U+0022)
let null = '\0';        // Null character (U+0000)
let cr = '\r';          // Carriage return (U+000D)
```

**Unicode Escapes:**
```hemlock
let rocket = '\u{1F680}';   // Emoji via Unicode escape
let heart = '\u{2764}';     // Up to 6 hex digits
let ascii = '\u{41}';       // 'A' via escape
let max = '\u{10FFFF}';     // Maximum Unicode codepoint
```

### String + Rune Concatenation

Runes can be concatenated with strings:

```hemlock
// String + rune
let greeting = "Hello" + '!';       // "Hello!"
let decorated = "Text" + '✓';       // "Text✓"

// Rune + string
let prefix = '>' + " Message";      // "> Message"
let bullet = '•' + " Item";         // "• Item"

// Multiple concatenations
let msg = "Hi " + '👋' + " World " + '🌍';  // "Hi 👋 World 🌍"

// Method chaining
let result = ('>' + " Important").to_upper();  // "> IMPORTANT"
```

**Implementation:** Runes are automatically encoded to UTF-8 and converted to strings during concatenation.

### Rune Type Conversions

**Integer ↔ Rune:**
```hemlock
// Integer to rune (codepoint value)
let code: rune = 65;            // 'A'
let emoji_code: rune = 128640;  // U+1F680 (🚀)

// Rune to integer (get codepoint)
let r = 'Z';
let value: i32 = r;             // 90
```

**Rune → String:**
```hemlock
// Explicit conversion
let ch: string = 'H';           // "H"
let emoji: string = '🚀';       // "🚀"

// Automatic during concatenation
let s = "" + 'A';               // "A"
```

**u8 (char) → Rune:**
```hemlock
// Any u8 value 0-255 can convert to rune
let byte: u8 = 65;
let rune_val: rune = byte;      // 'A'

// Note: Values 0-127 are ASCII, 128-255 are Latin-1
let extended: u8 = 200;
let r: rune = extended;         // U+00C8 (È)
```

**Chained Conversions:**
```hemlock
// i32 → rune → string
let code: i32 = 128512;         // Grinning face codepoint
let r: rune = code;             // 😀
let s: string = r;              // "😀"
```

**Range Checking:**
- Integer to rune: Must be in range [0, 0x10FFFF]
- Out of range values cause runtime error
- Rune to integer: Always succeeds (returns codepoint)

### Rune Operations

**Printing:**
```hemlock
let ascii = 'A';
print(ascii);                   // 'A' (quoted, printable ASCII)

let emoji = '🚀';
print(emoji);                   // U+1F680 (Unicode notation)

let tab = '\t';
print(tab);                     // U+0009 (non-printable as hex)
```

**Type Checking:**
```hemlock
let r = '🚀';
print(typeof(r));               // "rune"

let s = "text";
let ch = s[0];
print(typeof(ch));              // "rune" (indexing returns runes)
```

**Comparison:**
```hemlock
let a = 'A';
let b = 'B';
print(a == a);                  // true
print(a == b);                  // false

// Runes can be compared with integers (codepoint values)
print(a == 65);                 // true (implicit conversion)
```

---

## Control Flow

### Current Features
- `if`/`else`/`else if` with mandatory braces
- `while` loops
- `for` loops (C-style and for-in iteration)
- `switch` statements with case/default
- `break`/`continue`
- Arithmetic operators: `+`, `-`, `*`, `/`, `%` (modulo)
- Boolean operators: `&&`, `||`, `!`
- Comparisons: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Bitwise operators: `&`, `|`, `^`, `<<`, `>>`, `~` (integer types only)

### If Statements

**Basic if/else:**
```hemlock
if (x > 10) {
    print("large");
} else {
    print("small");
}
```

**Else-if chains:**
```hemlock
if (x > 100) {
    print("very large");
} else if (x > 50) {
    print("large");
} else if (x > 10) {
    print("medium");
} else {
    print("small");
}
```

**Notes:**
- Braces are always required for all branches
- Conditions must be enclosed in parentheses
- `else if` is supported for cleaner chaining (syntactic sugar for nested if statements)

### Switch Statements

**Basic switch:**
```hemlock
let x = 2;

switch (x) {
    case 1:
        print("one");
        break;
    case 2:
        print("two");
        break;
    case 3:
        print("three");
        break;
}
```

**Switch with default:**
```hemlock
let color = "blue";

switch (color) {
    case "red":
        print("stop");
        break;
    case "yellow":
        print("slow");
        break;
    case "green":
        print("go");
        break;
    default:
        print("unknown color");
        break;
}
```

**Fall-through behavior:**
```hemlock
let grade = 85;

switch (grade) {
    case 100:
    case 95:
    case 90:
        print("A");
        break;
    case 85:
    case 80:
        print("B");
        break;
    default:
        print("C or below");
        break;
}
```

**Switch with return (in functions):**
```hemlock
fn get_day_name(day: i32): string {
    switch (day) {
        case 1:
            return "Monday";
        case 2:
            return "Tuesday";
        case 3:
            return "Wednesday";
        default:
            return "Unknown";
    }
}
```

**Notes:**
- Supports any value type (integers, strings, booleans, etc.)
- Cases are compared using value equality
- Fall-through is supported (C-style) - cases without `break` continue to next case
- `break` exits the switch statement
- `default` case matches when no other case matches
- `default` can appear anywhere in the switch body
- Expressions are allowed in both switch value and case values

### Bitwise Operators

Hemlock provides bitwise operators for integer manipulation at the bit level. These operators work **only with integer types** (i8-i64, u8-u64) and preserve the original type of the operands.

**Binary bitwise operators:**
- `&` - Bitwise AND
- `|` - Bitwise OR
- `^` - Bitwise XOR (exclusive OR)
- `<<` - Left shift
- `>>` - Right shift

**Unary bitwise operator:**
- `~` - Bitwise NOT (complement)

**Basic operations:**
```hemlock
let a = 12;  // 1100 in binary
let b = 10;  // 1010 in binary

print(a & b);   // 8  (1000) - AND
print(a | b);   // 14 (1110) - OR
print(a ^ b);   // 6  (0110) - XOR
print(a << 2);  // 48 (110000) - left shift by 2
print(a >> 1);  // 6  (110) - right shift by 1
print(~a);      // -13 (two's complement)
```

**With unsigned types:**
```hemlock
let c: u8 = 15;   // 00001111 in binary
let d: u8 = 7;    // 00000111 in binary

print(c & d);     // 7  (00000111)
print(c | d);     // 15 (00001111)
print(c ^ d);     // 8  (00001000)
print(~c);        // 240 (11110000) - in u8
```

**Type preservation:**
```hemlock
// Bitwise operations preserve the type of operands
let x: u8 = 255;
let result = ~x;  // result is u8 with value 0

let y: i32 = 100;
let result2 = y << 2;  // result2 is i32 with value 400
```

**Operator precedence:**
Bitwise operators follow C-style precedence:
1. `~` (unary NOT) - highest, same level as `!` and `-`
2. `<<`, `>>` (shifts) - higher than comparisons, lower than `+`/`-`
3. `&` (bitwise AND) - higher than `^` and `|`
4. `^` (bitwise XOR) - between `&` and `|`
5. `|` (bitwise OR) - lower than `&` and `^`, higher than `&&`
6. `&&`, `||` (logical) - lowest precedence

**Precedence examples:**
```hemlock
// & has higher precedence than |
let result1 = 12 | 10 & 8;  // (10 & 8) | 12 = 8 | 12 = 12

// Shift has higher precedence than bitwise operators
let result2 = 8 | 1 << 2;   // 8 | (1 << 2) = 8 | 4 = 12

// Use parentheses for clarity
let result3 = (5 & 3) | (2 << 1);  // 1 | 4 = 5
```

**Important notes:**
- Bitwise operators only work with integer types (not floats, strings, etc.)
- Type promotion follows standard rules (smaller types promote to larger)
- Right shift (`>>`) is arithmetic for signed types, logical for unsigned
- Shift amounts are not range-checked (behavior is platform-dependent for large shifts)
- Bitwise NOT (`~`) returns the one's complement (all bits flipped)

### Modulo Operator

The modulo operator `%` returns the remainder after integer division. It works **only with integer types** (i8-i64, u8-u64) and follows C-style semantics.

**Basic usage:**
```hemlock
let a = 10 % 3;   // 1
let b = 15 % 4;   // 3
let c = 20 % 5;   // 0
let d = 100 % 7;  // 2
```

**C-style modulo behavior:**
The sign of the result follows the sign of the **dividend** (left operand):
```hemlock
let a = -10 % 3;   // -1 (negative dividend)
let b = 10 % -3;   // 1  (positive dividend)
let c = -10 % -3;  // -1 (negative dividend)
```

**Type support:**
```hemlock
// Works with all integer types
let a: i8 = 10 % 3;       // i8
let b: i32 = 100 % 7;     // i32
let c: i64 = 1000 % 13;   // i64
let d: u8 = 255 % 10;     // u8
let e: u32 = 50000 % 19;  // u32
```

**Type promotion:**
Modulo follows the same type promotion rules as other arithmetic operators:
```hemlock
let a: i8 = 10;
let b: i32 = 3;
let c = a % b;  // i32 (promoted to larger type)
```

**Operator precedence:**
Modulo has the same precedence as multiplication and division (higher than addition/subtraction):
```hemlock
let a = 10 % 3 + 5;  // 6  (1 + 5)
let b = 10 * 3 % 7;  // 2  (30 % 7, left-to-right)
let c = 20 % 3 * 4;  // 8  (2 * 4, left-to-right)
```

**Division by zero:**
Modulo by zero causes a runtime error:
```hemlock
let a = 10 % 0;  // Runtime error: Division by zero
```

**Important notes:**
- Modulo only works with integer types (not floats)
- Result sign follows dividend (C-style semantics)
- Type promotion follows standard rules (smaller types promote to larger)
- Division by zero is checked at runtime and throws an error

---

## Defer Statement

The `defer` statement schedules a function call to be executed when the surrounding function returns, providing explicit resource cleanup without RAII.

### Syntax

```hemlock
defer expression;
```

The expression is typically a function call that performs cleanup (e.g., closing files, releasing resources).

### Basic Usage

```hemlock
fn process_data() {
    print("start");
    defer print("cleanup");
    print("work");
    // "cleanup" executes here, before function returns
}

process_data();
// Output: start, work, cleanup
```

### Multiple Defers (LIFO Order)

When multiple `defer` statements are used, they execute in **LIFO (last-in, first-out)** order:

```hemlock
fn example() {
    defer print("first");
    defer print("second");
    defer print("third");
}

example();
// Output: third, second, first
```

### Defer with Return

Deferred calls execute **before** the function returns, but **after** the return value is evaluated:

```hemlock
fn process(): i32 {
    defer print("cleanup");
    print("work");
    return 42;
    // "cleanup" executes here, then returns 42
}
```

### Defer with Exceptions

Deferred calls execute **even when exceptions are thrown**, making them ideal for cleanup:

```hemlock
fn risky() {
    defer print("cleanup 1");
    defer print("cleanup 2");
    print("work");
    throw "error";
    // Both defers execute here, then exception propagates
}

try {
    risky();
} catch (e) {
    print("Caught: " + e);
}
// Output: work, cleanup 2, cleanup 1, Caught: error
```

### Resource Cleanup Pattern

The primary use case for `defer` is automatic resource cleanup:

```hemlock
fn process_file(filename: string) {
    let file = open(filename, "r");
    defer file.close();  // Guaranteed to execute

    let content = file.read();
    // Process content...

    // File automatically closed when function returns
}
```

### Defer with Closures

Deferred expressions capture the environment at the time of deferral:

```hemlock
fn example() {
    let x = 10;
    defer print("x = " + typeof(x));  // Captures environment
    x = 20;
    // Defer sees x = 20 when it executes
}
```

### Nested Functions

Each function maintains its own defer stack:

```hemlock
fn inner() {
    defer print("inner defer");
    print("inner body");
}

fn outer() {
    defer print("outer defer");
    inner();
    print("outer body");
}

outer();
// Output: inner body, inner defer, outer body, outer defer
```

### Important Notes

- **Explicit over implicit:** `defer` is visible in code, not hidden like RAII
- **LIFO order:** Last defer executes first
- **Always executes:** Runs on normal return, early return, or exception
- **No implicit cleanup:** You must explicitly defer cleanup calls
- **Scope:** Defers are tied to function scope, not block scope

### When to Use Defer

**Good use cases:**
- Closing files and network connections
- Releasing locks or other resources
- Cleanup operations that must always run
- Ensuring paired operations (open/close, lock/unlock)

**Example patterns:**
```hemlock
// File I/O
fn read_config(path: string) {
    let f = open(path, "r");
    defer f.close();
    return f.read();
}

// Multiple cleanup steps
fn complex_operation() {
    let resource1 = acquire_resource1();
    defer release_resource1(resource1);

    let resource2 = acquire_resource2();
    defer release_resource2(resource2);

    // Use resources...
    // Both resources guaranteed to be released in reverse order
}
```

---

## Error Handling

Hemlock supports exception-based error handling with `try`, `catch`, `finally`, and `throw` statements.

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

**Throw statement:**
```hemlock
throw expression;
```

### Semantics

**1. Throw Statement**
- `throw <expr>;` evaluates `<expr>` and throws it as an exception
- Any value can be thrown: strings, numbers, objects, null, etc.
- Execution immediately jumps to the nearest enclosing `catch` block
- If no `catch` exists, propagates up the call stack

```hemlock
throw "file not found";
throw 404;
throw { code: 500, message: "Internal error" };
throw null;
```

**2. Try Block**
- Executes statements sequentially
- If exception thrown, immediately jumps to `catch` (if present) or `finally` (if no catch)
- If no exception, executes `finally` (if present) then continues

**3. Catch Block**
- Receives thrown value in the parameter variable (e.g., `e`)
- Can use the caught value however needed
- Can re-throw: `throw e;`
- Can throw new exception: `throw "different error";`
- Catch parameter is scoped to the catch block only

```hemlock
try {
    throw "oops";
} catch (error) {
    print("Caught: " + error);
    // error only accessible here
}
// error not accessible here
```

**4. Finally Block**
- **Always executes**, regardless of whether exception was thrown
- Runs after try block (if no exception) or after catch block (if exception caught)
- Runs even if try/catch contains `return`, `break`, or `continue`
- If finally block throws, that exception replaces any previous exception

**Execution order example:**
```hemlock
try {
    print("1: try");
    throw "error";
} catch (e) {
    print("2: catch");
} finally {
    print("3: finally");
}
print("4: after");

// Output: 1: try, 2: catch, 3: finally, 4: after
```

**5. Uncaught Exceptions**
- If exception propagates to top of call stack without being caught: **crash**
- Print error message (stack trace to be added)
- Exit with non-zero status code

```hemlock
fn foo() {
    throw "uncaught!";
}

foo();  // Crashes with: Runtime error: uncaught!
```

### Panic - Unrecoverable Errors

Hemlock provides `panic(message?)` for **unrecoverable errors** that should immediately terminate the program.

**Syntax:**
```hemlock
panic();                    // Default message: "panic!"
panic("custom message");    // Custom message
panic(42);                  // Non-string values are printed
```

**Semantics:**
- `panic()` **immediately exits** the program with exit code 1
- Prints error message to stderr in format: `panic: <message>`
- **NOT catchable** with try/catch (unlike `throw`)
- Use for bugs and unrecoverable errors (e.g., invalid state, unreachable code)

**Panic vs Throw:**
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

**When to use panic:**
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
```

### Control Flow Interactions

**Return inside try/catch/finally:**
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

**Break/continue inside try/catch/finally:**
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

**Nested try/catch:**
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

### Examples

**Basic error handling:**
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

**Resource cleanup:**
```hemlock
fn process_file(filename) {
    let file = null;
    try {
        file = open(filename);
        let content = read(file);
        process(content);
    } catch (e) {
        print("Error processing file: " + e);
    } finally {
        if (file != null) {
            close(file);  // Always closes, even on error
        }
    }
}
```

**Re-throwing:**
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

### Current Limitations

- No stack trace on uncaught exceptions (planned)
- Memory allocation failures and internal errors still call `exit()` (intentional - these indicate unrecoverable system failures)
- No custom exception types yet (any value can be thrown)

**Note:** All user-facing runtime errors are now catchable with try/catch, including:
- Array/string indexing out of bounds
- Type conversion errors
- Function arity mismatches
- Stack overflow (infinite recursion)
- Async task errors (join/detach)

---

## Functions

Functions are **first-class values** that can be assigned, passed, and returned:

```hemlock
// Named function syntax
fn add(a: i32, b: i32): i32 {
    return a + b;
}

// Anonymous function
let multiply = fn(x, y) {
    return x * y;
};

// Recursion
fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

// Closures
fn makeAdder(x) {
    return fn(y) {
        return x + y;
    };
}

let add5 = makeAdder(5);
print(add5(3));  // 8
```

**Features:**
- **First-class:** Functions can be assigned to variables, passed as arguments, and returned
- **Lexical scoping:** Functions can read (not write) outer scope variables
- **Closures:** Functions capture their defining environment
- **Recursion:** Fully supported (no tail call optimization yet)
- **Type annotations:** Optional for parameters and return type
- **Optional parameters:** Parameters can have default values
- **Pass-by-value:** All arguments are copied

**Return semantics:**
- Functions with return type annotation MUST return a value
- Functions without return type annotation implicitly return `null` if no return statement
- `return;` without a value returns `null`

**Type checking:**
- Parameter types are checked at call time if annotated
- Return types are checked when function returns if annotated
- Implicit type conversions follow standard promotion rules

**Named vs Anonymous:**
- `fn name(...) {}` desugars to `let name = fn(...) {};`
- Both forms are equivalent

### Optional Parameters

Functions can have optional parameters with default values using the `?:` syntax:

```hemlock
// Basic optional parameter
fn greet(name: string, greeting?: "Hello") {
    print(greeting + " " + name);
}

greet("Alice");              // Hello Alice
greet("Bob", "Hi");          // Hi Bob

// Multiple optional parameters
fn format_name(first: string, middle?: "", last?: "Doe") {
    if (middle == "") {
        return first + " " + last;
    }
    return first + " " + middle + " " + last;
}

print(format_name("John"));                    // John Doe
print(format_name("John", "Q", "Adams"));      // John Q Adams

// Optional parameter with type annotation
fn multiply(x: i32, factor?: 2): i32 {
    return x * factor;
}

print(multiply(5));      // 10
print(multiply(5, 3));   // 15

// Default expressions can be complex
fn power(base: i32, exp?: 2 + 1) {
    let result = 1;
    let i = 0;
    while (i < exp) {
        result = result * base;
        i = i + 1;
    }
    return result;
}

print(power(2));      // 8 (2^3)
print(power(2, 4));   // 16 (2^4)

// Works with closures
fn make_multiplier(factor?: 10) {
    return fn(n) {
        return n * factor;
    };
}

let mult = make_multiplier();
print(mult(5));  // 50

// All optional parameters - 0 args is valid
fn test(a?: 1, b?: 2, c?: 3) {
    return a + b + c;
}

print(test());            // 6
print(test(10));          // 15
print(test(10, 20));      // 33
print(test(10, 20, 30));  // 60
```

**Optional parameter rules:**
- Optional parameters must come after all required parameters
- Syntax: `param?: default_value` or `param: type?: default_value`
- Default expressions are evaluated at call time, not function definition time
- Default expressions are evaluated in the closure environment
- Function calls validate argument count: `required ≤ provided ≤ total`
- Error messages show the valid range: "Function expects 2-4 arguments, got 1"

**Known limitations (v0.1):**
- Closure environments are never freed (memory leak, to be fixed with refcounting in v0.2)
- No pass-by-reference yet (`ref` keyword parsed but not implemented)
- No variadic functions
- No function overloading
- No tail call optimization

---

## Enums

Hemlock supports **C-style enumerations** with explicit or auto-incrementing values:

```hemlock
// Simple enum (auto values: 0, 1, 2, ...)
enum Color {
    RED,
    GREEN,
    BLUE
}

// Enum with explicit values
enum Status {
    OK = 0,
    ERROR = 1,
    PENDING = 2
}

// Mixed auto and explicit values
enum Code {
    A,        // 0
    B = 10,   // 10
    C,        // 11 (auto-increments from last explicit value)
    D = 20,   // 20
    E         // 21
}
```

### Usage

**Accessing enum variants:**
```hemlock
print(Color.RED);     // 0
print(Status.ERROR);  // 1
print(Code.C);        // 11
```

**Type annotations:**
```hemlock
let color: Color = Color.RED;
let status: Status = Status.OK;

// Type checking ensures values match
color = Color.BLUE;  // ✓ OK
```

**Comparisons:**
```hemlock
let status = Status.OK;

if (status == Status.OK) {
    print("Success");
}

if (status != Status.ERROR) {
    print("Not an error");
}
```

**Switch statements:**
```hemlock
let color = Color.GREEN;

switch (color) {
    case Color.RED:
        print("Red");
        break;
    case Color.GREEN:
        print("Green");
        break;
    case Color.BLUE:
        print("Blue");
        break;
}
```

**Function parameters:**
```hemlock
fn process(s: Status): string {
    if (s == Status.OK) {
        return "Success";
    }
    if (s == Status.ERROR) {
        return "Failed";
    }
    return "Waiting";
}

print(process(Status.OK));      // "Success"
print(process(Status.ERROR));   // "Failed"
print(process(Status.PENDING)); // "Waiting"
```

### Implementation Details

- Enum variants are **i32 values** at runtime
- Enums create a **const namespace object** with variant fields
- Type checking validates enum types during assignment and function calls
- Auto-incrementing starts at 0, or continues from last explicit value
- Variant values must be compile-time constants (i32 expressions)

**Example of namespace object:**
```hemlock
enum Status {
    OK,
    ERROR,
    PENDING
}

// Internally creates:
// const Status = {
//     OK: 0,
//     ERROR: 1,
//     PENDING: 2
// }

// So you can access:
print(Status.OK);  // 0
```

**Current Limitations:**
- No enum variant validation at runtime (can assign any i32 to enum-typed variable)
- No discriminated unions or associated data
- No pattern matching (use switch statements)
- Enum values must fit in i32 range

---

## Objects

Hemlock implements JavaScript-style objects with heap allocation, dynamic fields, methods, and duck typing.

### Object Literals
```hemlock
// Anonymous object
let person = { name: "Alice", age: 30, city: "NYC" };
print(person.name);  // "Alice"

// Empty object
let obj = {};

// Nested objects
let user = {
    info: { name: "Bob", age: 25 },
    active: true
};
print(user.info.name);  // "Bob"
```

### Field Access and Assignment
```hemlock
// Access
let x = person.name;

// Modify existing field
person.age = 31;

// Add new field dynamically
person.email = "alice@example.com";
```

### Methods and `self` Keyword
```hemlock
let counter = {
    count: 0,
    increment: fn() {
        self.count = self.count + 1;
    },
    get: fn() {
        return self.count;
    }
};

counter.increment();
counter.increment();
print(counter.get());  // 2
```

**How `self` works:**
- When a function is called as a method (e.g., `obj.method()`), `self` is automatically bound to the object
- `self` is read-only in the current implementation
- Method calls are detected at runtime by checking if the function expression is a property access

### Type Definitions with `define`
```hemlock
define Person {
    name: string,
    age: i32,
    active: bool,
}

// Create object and assign to typed variable
let p = { name: "Alice", age: 30, active: true };
let typed_p: Person = p;  // Duck typing validates structure

print(typeof(typed_p));  // "Person"
```

### Duck Typing
Objects are validated against `define` statements using **structural compatibility** (duck typing):

```hemlock
define Person {
    name: string,
    age: i32,
}

// OK: Has all required fields
let p1: Person = { name: "Alice", age: 30 };

// OK: Extra fields are allowed
let p2: Person = { name: "Bob", age: 25, city: "NYC", active: true };

// ERROR: Missing required field 'age'
let p3: Person = { name: "Carol" };

// ERROR: Wrong type for 'age'
let p4: Person = { name: "Dave", age: "thirty" };
```

**Type checking happens:**
- At assignment time (when assigning to a typed variable)
- Validates all required fields are present
- Validates field types match
- Extra fields are allowed and preserved
- Sets the object's type name for `typeof()`

### Optional Fields
```hemlock
define Person {
    name: string,
    age: i32,
    active?: true,       // Optional with default value
    nickname?: string,   // Optional, defaults to null
}

// Object with only required fields
let p = { name: "Alice", age: 30 };
let typed_p: Person = p;

print(typed_p.active);    // true (default applied)
print(typed_p.nickname);  // null (no default)

// Can override optional fields
let p2: Person = { name: "Bob", age: 25, active: false };
print(p2.active);  // false (overridden)
```

**Optional field syntax:**
- `field?: default_value` - Optional field with default
- `field?: type` - Optional field with type annotation, defaults to null
- Optional fields are added to objects during duck typing if missing

### JSON Serialization

Objects can be serialized to JSON strings using the `.serialize()` method, and JSON strings can be deserialized back to objects using the `.deserialize()` method on strings:

```hemlock
// obj.serialize() - Convert object to JSON string
let obj = { x: 10, y: 20, name: "test" };
let json = obj.serialize();
print(json);  // {"x":10,"y":20,"name":"test"}

// Nested objects
let nested = { inner: { a: 1, b: 2 }, outer: 3 };
print(nested.serialize());  // {"inner":{"a":1,"b":2},"outer":3}

// json.deserialize() - Parse JSON string to object
let json_str = obj.serialize();
let restored = json_str.deserialize();
print(restored.name);  // "test"
```

**Cycle Detection:**
```hemlock
let obj = { x: 10 };
obj.me = obj;  // Create circular reference
obj.serialize();  // ERROR: serialize() detected circular reference
```

**Supported types in JSON:**
- Numbers (i8-i32, u8-u32, f32, f64)
- Booleans
- Strings (with escape sequences)
- Null
- Objects (nested)
- Arrays
- Not supported: functions, pointers, buffers

**Object Methods:**
- `obj.serialize()` - Convert object to JSON string (with cycle detection)

**String Methods (for JSON):**
- `json_string.deserialize()` - Parse JSON string to object/value

### Built-in Functions
- `typeof(value)` - Returns type name string
  - Anonymous objects: `"object"`
  - Typed objects: custom type name (e.g., `"Person"`)

### Implementation Details
- Objects are heap-allocated
- Shallow copy semantics (assignment copies the reference)
- Fields stored as dynamic arrays (name/value pairs)
- Methods are just functions stored in object fields
- Duck typing validates at assignment time
- Type names are stored in objects for `typeof()`

**Current Limitations:**
- No deep copy built-in
- No reference counting (objects are never freed automatically)
- No pass-by-value for objects
- No object spread syntax
- No computed property names
- `self` is read-only (cannot reassign in methods)

---

## Arrays

Hemlock provides **dynamic arrays** with comprehensive methods for data manipulation and processing:

```hemlock
// Array literals
let arr = [1, 2, 3, 4, 5];
print(arr[0]);         // 1
print(arr.length);     // 5

// Mixed types allowed
let mixed = [1, "hello", true, null];
```

**Properties:**
- Dynamic sizing (grow automatically)
- Zero-indexed
- Mixed types allowed (unless using typed arrays)
- `.length` property
- Heap-allocated

### Typed Arrays

Hemlock supports **typed arrays** - arrays with element type constraints enforced at runtime:

```hemlock
// Declare a typed array with element type constraint
let numbers: array<i32> = [1, 2, 3, 4, 5];
let strings: array<string> = ["hello", "world"];
let bools: array<bool> = [true, false, true];

// Declare an explicitly untyped array (allows mixed types)
let mixed: array = ["hello", 42, 3.14, true];

// Fully dynamic array (no type annotation)
let dynamic = [1, "two", 3.0];  // Same as untyped, but not annotated
```

**Type enforcement:**
```hemlock
let arr: array<i32> = [1, 2, 3];

// OK: Adding correct type
arr.push(4);         // ✓ i32
arr[0] = 10;         // ✓ i32
arr.unshift(0);      // ✓ i32
arr.insert(2, 99);   // ✓ i32

// ERROR: Type mismatch
arr.push("string");  // ✗ Runtime error: Type mismatch in typed array
arr[0] = 3.14;       // ✗ Runtime error: Type mismatch in typed array
```

**Supported element types:**
- All primitive numeric types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`
- `bool`, `string`, `rune`
- `ptr`, `buffer`

**Type checking applies to:**
- Direct index assignment (`arr[i] = value`)
- `push(value)` method
- `unshift(value)` method
- `insert(index, value)` method

**Type checking behavior:**
- Type constraints are enforced at runtime
- All array operations that add elements validate the type
- Mixed-type operations are not allowed in typed arrays
- Three syntaxes for arrays:
  - `let arr: array<type> = [...]` - Typed array (strict element type checking)
  - `let arr: array = [...]` - Explicitly untyped array (allows mixed types)
  - `let arr = [...]` - Implicitly untyped array (allows mixed types, no annotation)

**Example - type safety:**
```hemlock
let bytes: array<u8> = [255, 128, 64];
bytes.push(200);     // ✓ OK
bytes.push(-1);      // ✗ Error: u8 cannot be negative (enforced during array literal conversion)
bytes.push(256);     // ✗ Error: out of range for u8

let names: array<string> = ["Alice", "Bob"];
names.push("Charlie");  // ✓ OK
names.push(42);         // ✗ Error: expected string
```

**Implementation notes:**
- Type constraint is stored in the array structure
- Type checking happens at every mutation operation
- No automatic type conversion (strict type matching)
- Type validation uses exact type matching (i32 ≠ u8)

### Array Methods

**Stack & Queue Operations:**
```hemlock
let arr = [1, 2, 3];
arr.push(4);           // Add to end: [1, 2, 3, 4]
let last = arr.pop();  // Remove from end: 4

let first = arr.shift();   // Remove from start: 1
arr.unshift(0);            // Add to start: [0, 2, 3]
```

**Array Manipulation:**
```hemlock
let arr = [1, 2, 4, 5];
arr.insert(2, 3);      // Insert at index: [1, 2, 3, 4, 5]
let removed = arr.remove(0);  // Remove at index: 1

arr.reverse();         // Reverse in-place: [5, 4, 3, 2]
arr.clear();           // Remove all elements: []
```

**Slicing & Extraction:**
```hemlock
let arr = [1, 2, 3, 4, 5];
let sub = arr.slice(1, 4);   // [2, 3, 4] (end exclusive)
let f = arr.first();         // 1 (without removing)
let l = arr.last();          // 5 (without removing)
```

**Search & Find:**
```hemlock
let arr = [10, 20, 30, 40];
let idx = arr.find(30);      // 2 (index of first occurrence)
let idx2 = arr.find(99);     // -1 (not found)
let has = arr.contains(20);  // true
```

**Array Combination:**
```hemlock
let a = [1, 2, 3];
let b = [4, 5, 6];
let combined = a.concat(b);  // [1, 2, 3, 4, 5, 6] (new array)
```

**String Conversion:**
```hemlock
let words = ["hello", "world", "foo"];
let joined = words.join(" ");  // "hello world foo"

let numbers = [1, 2, 3];
let csv = numbers.join(",");   // "1,2,3"

// Works with mixed types
let mixed = [1, "hello", true, null];
print(mixed.join(" | "));  // "1 | hello | true | null"
```

**Method Chaining:**
```hemlock
let result = [1, 2, 3]
    .concat([4, 5, 6])
    .slice(2, 5);  // [3, 4, 5]

let text = ["apple", "banana", "cherry"]
    .slice(0, 2)
    .join(" and ");  // "apple and banana"
```

**Available Array Methods:**
- `push(value)` - Add element to end
- `pop()` - Remove and return last element
- `shift()` - Remove and return first element
- `unshift(value)` - Add element to beginning
- `insert(index, value)` - Insert element at index
- `remove(index)` - Remove and return element at index
- `find(value)` - Find first occurrence, returns index or -1
- `contains(value)` - Check if array contains value
- `slice(start, end)` - Extract subarray (end exclusive, returns new array)
- `join(delimiter)` - Join elements into string
- `concat(other)` - Concatenate with another array (returns new array)
- `reverse()` - Reverse array in-place
- `first()` - Get first element (without removing)
- `last()` - Get last element (without removing)
- `clear()` - Remove all elements
- `map(callback)` - Transform each element, returns new array
- `filter(predicate)` - Keep elements that pass test, returns new array
- `reduce(reducer, initial?)` - Accumulate to single value

### Higher-Order Functions

**map(callback) - Transform Elements:**
```hemlock
// Double each number
let nums = [1, 2, 3, 4, 5];
let doubled = nums.map(fn(x) {
    return x * 2;
});
print(doubled[0]);  // 2
print(doubled[2]);  // 6

// Transform to different type
let strings = [1, 2, 3].map(fn(n) {
    return "num_" + typeof(n);
});
print(strings[0]);  // "num_1"
```

**filter(predicate) - Select Elements:**
```hemlock
// Keep even numbers
let nums = [1, 2, 3, 4, 5, 6];
let evens = nums.filter(fn(x) {
    return x % 2 == 0;
});
print(evens.length);  // 3
print(evens[0]);  // 2

// Filter strings by length
let words = ["apple", "banana", "cherry", "date"];
let long_words = words.filter(fn(w) {
    return w.length > 5;
});
print(long_words[0]);  // "banana"
```

**reduce(reducer, initial?) - Accumulate Values:**
```hemlock
// Sum array
let nums = [1, 2, 3, 4, 5];
let sum = nums.reduce(fn(acc, x) {
    return acc + x;
}, 0);
print(sum);  // 15

// Product (without initial value - uses first element)
let product = [1, 2, 3, 4].reduce(fn(acc, x) {
    return acc * x;
});
print(product);  // 24

// Find maximum
let max = [3, 7, 2, 9, 5].reduce(fn(acc, x) {
    if (x > acc) {
        return x;
    } else {
        return acc;
    }
}, 0);
print(max);  // 9
```

**Chaining Higher-Order Functions:**
```hemlock
// Complex data pipeline
let result = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    .filter(fn(x) { return x % 2 == 1; })  // Get odds
    .map(fn(x) { return x * x; })           // Square them
    .reduce(fn(acc, x) { return acc + x; }, 0);  // Sum
print(result);  // 165 (1+9+25+49+81)
```

### Implementation Details
- Arrays are heap-allocated with dynamic capacity
- Automatic growth when capacity exceeded (doubles capacity)
- Value comparison for `find()` and `contains()` works correctly for primitives and strings
- `join()` converts all elements to strings automatically
- Methods like `slice()` and `concat()` return new arrays (no mutation)
- Methods like `reverse()`, `push()`, `insert()` mutate in-place

**Current Limitations:**
- No reference counting (arrays are never freed automatically)
- No bounds checking on direct index access (use methods for safety)
- Comparing objects/arrays in `find()` uses reference equality

---

## Command-Line Arguments

Hemlock programs can access command-line arguments via a built-in **`args` array** that is automatically populated at program startup.

### The `args` Array

```hemlock
// args[0] is always the script filename
// args[1] through args[n-1] are the actual arguments
print(args[0]);        // "script.hml"
print(args.length);    // Total number of arguments (including script name)
```

**Example usage:**
```bash
./hemlock script.hml hello world "test 123"
```

```hemlock
// In script.hml
print("Script name: " + args[0]);     // "script.hml"
print("Total args: " + typeof(args.length));  // "4"
print("First arg: " + args[1]);       // "hello"
print("Second arg: " + args[2]);      // "world"
print("Third arg: " + args[3]);       // "test 123"
```

### Iteration Pattern

```hemlock
// Skip args[0] (script name) and process actual arguments
let i = 1;
while (i < args.length) {
    print("Argument " + typeof(i) + ": " + args[i]);
    i = i + 1;
}
```

### Properties
- **Always present:** `args` is a global array available in all Hemlock programs
- **Script name included:** `args[0]` always contains the script path/name
- **Type:** `args` is an array of strings
- **Minimum length:** Always at least 1 (the script name)
- **REPL behavior:** In the REPL, `args.length` is 0 (empty array)

### Implementation Details
- `args` is registered as a built-in global variable during environment initialization
- Arguments are passed from `main()` through to `register_builtins()`
- Each argument is stored as a string value in the array
- Arguments with spaces are preserved if quoted in the shell

### Common Use Cases

**Simple argument processing:**
```hemlock
if (args.length < 2) {
    print("Usage: " + args[0] + " <filename>");
} else {
    let filename = args[1];
    // ... process file
}
```

**Named arguments (simple pattern):**
```hemlock
let i = 1;
while (i < args.length) {
    if (args[i] == "--verbose") {
        let verbose = true;
    }
    if (args[i] == "--output") {
        i = i + 1;
        let output_file = args[i];
    }
    i = i + 1;
}
```

---

## Command Execution

Hemlock provides the **`exec()` builtin function** to execute shell commands and capture their output.

### The `exec()` Function

```hemlock
let result = exec("echo hello");
print(result.output);      // "hello\n"
print(result.exit_code);   // 0
```

**Signature:**
- `exec(command: string): object`

**Returns:** An object with two fields:
- `output` (string): The command's stdout output (as a string)
- `exit_code` (i32): The command's exit status code

### Basic Usage

```hemlock
// Simple command
let r = exec("ls -la");
print(r.output);
print("Exit code: " + typeof(r.exit_code));

// Check exit status
let r2 = exec("grep pattern file.txt");
if (r2.exit_code == 0) {
    print("Found: " + r2.output);
} else {
    print("Pattern not found");
}

// Commands with pipes
let r3 = exec("ps aux | grep hemlock");
print(r3.output);
```

### Result Object

The object returned by `exec()` has the following structure:

```hemlock
{
    output: string,      // Command stdout (captured output)
    exit_code: i32       // Process exit status (0 = success)
}
```

**Field details:**
- **`output`**: Contains all text written to stdout by the command
  - Empty string if command produces no output
  - Includes newlines and whitespace as-is
  - Multi-line output preserved
  - Not limited in size (dynamically allocated)

- **`exit_code`**: The command's exit status
  - `0` typically indicates success
  - Non-zero values indicate errors (convention: 1-255)
  - `-1` if command could not be executed or terminated abnormally

### Advanced Examples

**Handling failures:**
```hemlock
let r = exec("ls /nonexistent");
if (r.exit_code != 0) {
    print("Command failed with code: " + typeof(r.exit_code));
    print("Error output: " + r.output);  // Note: stderr not captured
}
```

**Processing multi-line output:**
```hemlock
let r = exec("cat file.txt");
let lines = r.output.split("\n");
let i = 0;
while (i < lines.length) {
    print("Line " + typeof(i) + ": " + lines[i]);
    i = i + 1;
}
```

**Command chaining:**
```hemlock
// Multiple commands with && and ||
let r1 = exec("mkdir -p /tmp/test && touch /tmp/test/file.txt");
if (r1.exit_code == 0) {
    print("Setup complete");
}

// Pipes and redirections work
let r2 = exec("echo 'data' | base64");
print("Base64: " + r2.output);
```

**Exit code patterns:**
```hemlock
// Different exit codes indicate different conditions
let r = exec("test -f myfile.txt");
if (r.exit_code == 0) {
    print("File exists");
} else if (r.exit_code == 1) {
    print("File does not exist");
} else {
    print("Test command failed: " + typeof(r.exit_code));
}
```

### Error Handling

The `exec()` function throws an exception if the command cannot be executed:

```hemlock
try {
    let r = exec("nonexistent_command_xyz");
} catch (e) {
    print("Failed to execute: " + e);
}
```

**When exceptions are thrown:**
- `popen()` fails (e.g., cannot create pipe)
- System resource limits exceeded
- Memory allocation failures

**When exceptions are NOT thrown:**
- Command runs but returns non-zero exit code (check `exit_code` field)
- Command produces no output (returns empty string in `output`)
- Command not found by shell (returns non-zero `exit_code`)

### Implementation Details

**How it works:**
- Uses `popen()` to execute commands via `/bin/sh`
- Captures stdout only (stderr is not captured)
- Output buffered dynamically (starts at 4KB, grows as needed)
- Exit status extracted using `WIFEXITED()` and `WEXITSTATUS()` macros
- Output string is properly null-terminated

**Performance considerations:**
- Creates a new shell process for each call
- Output stored entirely in memory
- No streaming support (waits for command completion)
- Suitable for commands with reasonable output sizes

**Security considerations:**
- ⚠️ **Shell injection risk**: The command is executed by the shell (`/bin/sh`)
- ⚠️ Always validate/sanitize user input before passing to `exec()`
- Commands have full shell access (pipes, redirects, variables, etc.)
- Runs with the same permissions as the Hemlock process

### Limitations

- **No stderr capture**: Only stdout is captured, stderr goes to terminal
- **No streaming**: Must wait for command completion
- **No timeout**: Commands can run indefinitely
- **No signal handling**: Cannot send signals to running commands
- **No process control**: Cannot interact with command after starting

### Use Cases

**Good use cases:**
- Running system utilities (ls, grep, find, etc.)
- Quick data processing with Unix tools
- Checking system state or file existence
- Generating reports from command-line tools
- Automation scripts

**Not recommended for:**
- Long-running services or daemons
- Interactive commands requiring input
- Commands producing gigabytes of output
- Real-time streaming data processing
- Mission-critical error handling (stderr not captured)

---

## Async/Concurrency

Hemlock provides **structured concurrency** with `async fn` syntax, task spawning, and channels for communication. The implementation uses POSIX threads (pthreads) for **TRUE multi-threaded parallelism**.

**Implementation Status**
- ✅ `async fn` - Fully implemented, functions can be spawned as tasks
- ✅ `spawn()` / `join()` / `detach()` - Fully implemented with true parallelism
- ✅ `await` - Automatically joins task handles, returns result
- ✅ Channels - Thread-safe communication with `send()` / `recv()` / `close()`

**What this means:**
- ✅ **Real OS threads** - Each spawned task runs on a separate pthread (POSIX thread)
- ✅ **True parallelism** - Tasks execute simultaneously on multiple CPU cores
- ✅ **Kernel-scheduled** - The OS scheduler distributes tasks across available cores
- ✅ **Thread-safe channels** - Uses pthread mutexes and condition variables for synchronization

**What this is NOT:**
- ❌ **NOT green threads** - Not user-space cooperative multitasking
- ❌ **NOT async/await coroutines** - Not single-threaded event loop like JavaScript/Python asyncio
- ❌ **NOT emulated concurrency** - Not simulated parallelism

This is the **same threading model as C, C++, and Rust** when using OS threads. You get actual parallel execution across multiple cores.

### Async Functions

Functions can be declared as `async` to indicate they're designed for concurrent execution:

```hemlock
async fn compute(n: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
```

**Key points:**
- `async fn` declares an asynchronous function
- Async functions can be spawned as concurrent tasks using `spawn()`
- Async functions can also be called directly (runs synchronously in current thread)
- When spawned, each task runs on its **own OS thread** (not a coroutine!)
- `await task_handle` automatically joins the task and returns its result
- Can use either `await task` or explicit `join(task)` - both are equivalent

### Task Spawning

Use `spawn()` to run async functions **in parallel on separate OS threads**:

```hemlock
async fn factorial(n: i32): i32 {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

// Spawn multiple tasks - these run in PARALLEL on different CPU cores!
let t1 = spawn(factorial, 5);  // Thread 1
let t2 = spawn(factorial, 6);  // Thread 2
let t3 = spawn(factorial, 7);  // Thread 3

// All three are computing simultaneously right now!

// Wait for results
let f5 = join(t1);  // 120
let f6 = join(t2);  // 720
let f7 = join(t3);  // 5040
```

**Built-in functions:**
- `spawn(async_fn, arg1, arg2, ...)` - Create a new task on a new pthread, returns task handle
- `join(task)` - Wait for task completion (blocks until thread finishes), returns result
- `detach(task)` - Fire-and-forget execution (thread runs independently, no join allowed)

### Await Syntax

The `await` keyword provides convenient syntax for waiting on task results:

```hemlock
async fn compute(n: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}

// Spawn multiple tasks
let task1 = spawn(compute, 10);
let task2 = spawn(compute, 20);
let task3 = spawn(compute, 30);

// Await results (automatically joins tasks)
let result1 = await task1;  // Equivalent to join(task1)
let result2 = await task2;  // Equivalent to join(task2)
let result3 = await task3;  // Equivalent to join(task3)

print(result1 + result2 + result3);

// Can also await inline
let result4 = await spawn(compute, 40);
```

**How `await` works:**
- If the expression evaluates to a task handle, `await` automatically calls `join()` on it
- If the expression is any other value, `await` just returns that value unchanged
- `await task` and `join(task)` are functionally equivalent
- Exceptions thrown in awaited tasks are propagated to the caller

### Channels

Channels provide thread-safe communication between tasks:

```hemlock
async fn producer(ch, count: i32) {
    let i = 0;
    while (i < count) {
        ch.send(i * 10);
        i = i + 1;
    }
    ch.close();
    return null;
}

async fn consumer(ch, count: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < count) {
        let val = ch.recv();
        sum = sum + val;
        i = i + 1;
    }
    return sum;
}

// Create channel with buffer size
let ch = channel(10);

// Spawn producer and consumer
let p = spawn(producer, ch, 5);
let c = spawn(consumer, ch, 5);

// Wait for completion
join(p);
let total = join(c);  // 100 (0+10+20+30+40)
```

**Channel methods:**
- `channel(capacity)` - Create buffered channel
- `ch.send(value)` - Send value (blocks if full)
- `ch.recv()` - Receive value (blocks if empty)
- `ch.close()` - Close channel (recv on closed channel returns null)

### Exception Propagation

Exceptions thrown in spawned tasks are propagated when joined:

```hemlock
async fn risky_operation(should_fail: i32): i32 {
    if (should_fail == 1) {
        throw "Task failed!";
    }
    return 42;
}

let t = spawn(risky_operation, 1);
try {
    let result = join(t);
} catch (e) {
    print("Caught: " + e);  // "Caught: Task failed!"
}
```

### Verifying True Parallelism

You can verify that Hemlock's async implementation uses true multi-core parallelism by running CPU-intensive tasks:

```bash
# Run with time to measure CPU vs wall clock time
time ./hemlock my_concurrent_program.hml
```

**Example output showing true parallelism:**
```
real    0m1.981s   # Wall clock time (actual elapsed time)
user    0m7.160s   # CPU time (sum across all cores)
sys     0m0.020s
```

**Interpretation:**
- **Real time:** How long the program took to run (wall clock)
- **User time:** Total CPU time spent across all cores
- **Ratio (user/real):** 7.16 / 1.98 = **3.6x speedup**

If `user time > real time`, this proves **true parallel execution**:
- Single-threaded would show `user ≈ real`
- Green threads would show `user ≈ real` (one core)
- **True parallelism shows `user >> real`** (multiple cores working simultaneously)

The ratio indicates how many cores were utilized: 3.6x means ~3-4 CPU cores were actively computing in parallel.

### Implementation Details

**Threading Model:**
- **1:1 threading** - Each spawned task creates a dedicated OS thread via `pthread_create()`
- **Kernel-scheduled** - The OS kernel schedules threads across available CPU cores
- **Pre-emptive multitasking** - The OS can interrupt and switch between threads
- **No GIL** - Unlike Python, there's no Global Interpreter Lock limiting parallelism

**Synchronization:**
- **Mutexes** - Channels use `pthread_mutex_t` for thread-safe access
- **Condition variables** - Blocking send/recv use `pthread_cond_t` for efficient waiting
- **Lock-free operations** - Task state transitions are atomic

**Performance Characteristics:**
- **True parallelism** - N spawned tasks can utilize N CPU cores simultaneously
- **Proven speedup** - Stress tests show 3.6x-9x CPU time vs wall time (multiple cores working)
- **Verified parallel execution** - CPU time exceeds wall clock time, proving multi-core utilization
- **Thread overhead** - Each task has ~8KB stack + pthread overhead
- **Blocking I/O safe** - Blocking operations in one task don't block others

**Memory & Cleanup:**
- Joined tasks are automatically cleaned up after `join()` returns
- Detached tasks: pthread is cleaned up by OS, but Task struct (~64-96 bytes) currently leaks
- Channels are reference-counted and freed when no longer used

**Current limitations:**
- No `select()` for multiplexing multiple channels (planned)
- No work-stealing scheduler (uses 1 thread per task, can be inefficient for many short tasks)
- No async I/O integration yet (file/network operations still block)
- Channel capacity is fixed at creation time (no dynamic resizing)

---

## File I/O

Hemlock provides a **File object API** for file operations with proper error handling and resource management.

### Opening Files

Use `open(path, mode?)` to open a file:

```hemlock
let f = open("data.txt", "r");     // Read mode (default)
let f2 = open("output.txt", "w");  // Write mode (truncate)
let f3 = open("log.txt", "a");     // Append mode
let f4 = open("data.bin", "r+");   // Read/write mode
```

**Modes:**
- `"r"` - Read (default)
- `"w"` - Write (truncate existing file)
- `"a"` - Append
- `"r+"` - Read and write
- `"w+"` - Read and write (truncate)
- `"a+"` - Read and append

**Important:** Files must be manually closed with `f.close()` to avoid file descriptor leaks.

### File Methods

**Reading:**
```hemlock
let f = open("data.txt", "r");

// Read entire file
let all = f.read();              // Read from current position to EOF

// Read specific number of bytes
let chunk = f.read(1024);        // Read up to 1024 bytes

// Read binary data
let binary = f.read_bytes(256);  // Returns buffer with 256 bytes

f.close();
```

**Writing:**
```hemlock
let f = open("output.txt", "w");

// Write text (returns bytes written)
let written = f.write("Hello, World!\n");  // Returns i32

// Write binary data
let buf = buffer(10);
buf[0] = 65;  // 'A'
let bytes = f.write_bytes(buf);  // Returns i32

f.close();
```

**Seeking:**
```hemlock
let f = open("data.txt", "r");

// Get current position
let pos = f.tell();  // Returns i32

// Move to specific position
f.seek(100);         // Move to byte 100
f.seek(0);           // Reset to beginning

f.close();
```

**Closing:**
```hemlock
let f = open("data.txt", "r");
f.close();
f.close();  // Safe - idempotent, can call multiple times
```

### File Properties (Read-Only)

```hemlock
let f = open("/path/to/file.txt", "r");

print(f.path);    // "/path/to/file.txt" - file path
print(f.mode);    // "r" - open mode
print(f.closed);  // false - whether file is closed

f.close();
print(f.closed);  // true
```

### Error Handling

All file operations include proper error messages with context:

```hemlock
// Errors include filename for better debugging
let f = open("missing.txt", "r");
// Error: Failed to open 'missing.txt': No such file or directory

let f2 = open("data.txt", "r");
f2.close();
f2.read();
// Error: Cannot read from closed file 'data.txt'

let f3 = open("readonly.txt", "r");
f3.write("data");
// Error: Cannot write to file 'readonly.txt' opened in read-only mode
```

### Resource Management Pattern

**Always close files explicitly:**

```hemlock
// Basic pattern
let f = open("data.txt", "r");
let content = f.read();
f.close();

// With error handling
let f = open("data.txt", "r");
try {
    let content = f.read();
    process(content);
} finally {
    f.close();  // Always close, even on error
}
```

### Complete File API

**Methods:**
- `read(size?: i32): string` - Read text (optional size parameter)
- `read_bytes(size: i32): buffer` - Read binary data
- `write(data: string): i32` - Write text, returns bytes written
- `write_bytes(data: buffer): i32` - Write binary data, returns bytes written
- `seek(position: i32): i32` - Seek to position, returns new position
- `tell(): i32` - Get current position
- `close()` - Close file (idempotent)

**Properties (read-only):**
- `file.path: string` - File path
- `file.mode: string` - Open mode
- `file.closed: bool` - Whether file is closed

**Note:** The old global functions (`read_file()`, `write_file()`, `append_file()`, `read_bytes()`, `write_bytes()`, `file_exists()`) have been removed in favor of the File object API. Use `open()` and file methods instead.

---

## Signal Handling

Hemlock provides **POSIX signal handling** for managing system signals like SIGINT (Ctrl+C), SIGTERM, and custom signals. This enables low-level process control and inter-process communication.

### Signal API

**Register Signal Handler:**
```hemlock
signal(signum, handler_fn)
```
- `signum` - Signal number (i32 constant like SIGINT, SIGTERM)
- `handler_fn` - Function to call when signal is received (or `null` to reset to default)
- Returns the previous handler function (or `null` if none)

**Raise Signal:**
```hemlock
raise(signum)
```
- `signum` - Signal number to send to current process
- Returns `null`

### Signal Constants

Hemlock provides standard POSIX signal constants as i32 values:

**Interrupt & Termination:**
- `SIGINT` (2) - Interrupt from keyboard (Ctrl+C)
- `SIGTERM` (15) - Termination request
- `SIGQUIT` (3) - Quit from keyboard (Ctrl+\)
- `SIGHUP` (1) - Hangup detected on controlling terminal
- `SIGABRT` (6) - Abort signal

**User-Defined:**
- `SIGUSR1` (10) - User-defined signal 1
- `SIGUSR2` (12) - User-defined signal 2

**Process Control:**
- `SIGALRM` (14) - Alarm clock timer
- `SIGCHLD` (17) - Child process status change
- `SIGCONT` (18) - Continue if stopped
- `SIGSTOP` (19) - Stop process (cannot be caught)
- `SIGTSTP` (20) - Terminal stop (Ctrl+Z)

**I/O:**
- `SIGPIPE` (13) - Broken pipe
- `SIGTTIN` (21) - Background read from terminal
- `SIGTTOU` (22) - Background write to terminal

### Basic Signal Handling

**Catch Ctrl+C:**
```hemlock
let interrupted = false;

fn handle_interrupt(sig) {
    print("Caught SIGINT!");
    interrupted = true;
}

signal(SIGINT, handle_interrupt);

// Program continues running...
// User presses Ctrl+C -> handle_interrupt() is called
```

**Handler Arguments:**
Signal handlers receive one argument: the signal number (i32)
```hemlock
fn my_handler(signum) {
    print("Received signal: " + typeof(signum));
    // signum contains the signal number (e.g., 2 for SIGINT)
}
```

### Multiple Signals

Different handlers for different signals:
```hemlock
fn handle_int(sig) {
    print("SIGINT received");
}

fn handle_term(sig) {
    print("SIGTERM received");
}

signal(SIGINT, handle_int);
signal(SIGTERM, handle_term);
```

### Resetting to Default

Pass `null` as the handler to reset to default behavior:
```hemlock
// Register custom handler
signal(SIGINT, my_handler);

// Later, reset to default (terminate on SIGINT)
signal(SIGINT, null);
```

### Raising Signals

Send signals to your own process:
```hemlock
let count = 0;

fn increment(sig) {
    count = count + 1;
}

signal(SIGUSR1, increment);

// Trigger handler manually
raise(SIGUSR1);
raise(SIGUSR1);

print(count);  // 2
```

### Graceful Shutdown Pattern

Common pattern for cleanup on termination:
```hemlock
let should_exit = false;

fn handle_shutdown(sig) {
    print("Shutting down gracefully...");
    should_exit = true;
}

signal(SIGINT, handle_shutdown);
signal(SIGTERM, handle_shutdown);

// Main loop
while (!should_exit) {
    // Do work...
    // Check should_exit flag periodically
}

print("Cleanup complete");
```

### Signal Handler Behavior

**Important notes:**
- Handlers are called **synchronously** when the signal is received
- Handlers execute in the current process context
- Signal handlers share the closure environment of the function they're defined in
- Handlers can access and modify outer scope variables (like globals or captured variables)
- Keep handlers simple and quick - avoid long-running operations

**What signals can be caught:**
- Most signals can be caught and handled (SIGINT, SIGTERM, SIGUSR1, etc.)
- **Cannot catch:** SIGKILL (9), SIGSTOP (19) - these always terminate/stop
- Some signals have default behaviors that may differ by system

### Safety Considerations

Signal handling is **inherently unsafe** in Hemlock's philosophy:
- Handlers can be called at any time, interrupting normal execution
- No async-signal-safety guarantees (handlers can call any Hemlock code)
- Race conditions are possible if handler modifies shared state
- The user is responsible for proper synchronization

**Example of potential issues:**
```hemlock
let counter = 0;

fn increment(sig) {
    counter = counter + 1;  // Race condition if called during counter update
}

signal(SIGUSR1, increment);

// Main code also modifies counter
counter = counter + 1;  // Could be interrupted by signal handler
```

**Best practices:**
- Keep handlers simple and fast
- Use atomic flags (simple boolean assignments)
- Avoid complex logic in handlers
- Be aware that handlers can interrupt any operation

### Complete Example

```hemlock
let running = true;
let signal_count = 0;

fn handle_signal(signum) {
    signal_count = signal_count + 1;

    if (signum == SIGINT) {
        print("Interrupt detected (Ctrl+C)");
        running = false;
    }

    if (signum == SIGUSR1) {
        print("User signal 1 received");
    }
}

// Register handlers
signal(SIGINT, handle_signal);
signal(SIGUSR1, handle_signal);

// Simulate some work
let i = 0;
while (running && i < 100) {
    print("Working... " + typeof(i));

    // Trigger SIGUSR1 every 10 iterations
    if (i == 10 || i == 20) {
        raise(SIGUSR1);
    }

    i = i + 1;
}

print("Total signals received: " + typeof(signal_count));
```

---

## Standard Library

Hemlock provides a comprehensive standard library with modules for common programming tasks. All stdlib modules use the `@stdlib/` import prefix and are documented in `stdlib/docs/`.

### Import Syntax

```hemlock
// Import specific functions
import { sin, cos, PI } from "@stdlib/math";
import { now, sleep } from "@stdlib/time";
import { read_file, write_file } from "@stdlib/fs";
import { TcpListener, TcpStream, UdpSocket } from "@stdlib/net";
import { compile, test } from "@stdlib/regex";

// Import all as namespace
import * as math from "@stdlib/math";
import * as fs from "@stdlib/fs";
import * as net from "@stdlib/net";
import * as regex from "@stdlib/regex";

// Use imported functions
let angle = math.PI / 4.0;
let x = math.sin(angle);
let content = fs.read_file("data.txt");
let stream = net.TcpStream("example.com", 80);
let valid = regex.test("^[a-z]+$", "hello", null);
```

### Available Modules

#### 1. **Collections** (`@stdlib/collections`) ⭐
**Status:** Production-ready, fully optimized

Comprehensive data structures for efficient data manipulation:
- **HashMap** - O(1) hash table with djb2 algorithm
- **Queue** - O(1) FIFO with circular buffer
- **Stack** - O(1) LIFO
- **Set** - O(1) unique values (HashMap-based)
- **LinkedList** - O(1) insertion/deletion with bidirectional traversal

```hemlock
import { HashMap, Queue, Stack, Set, LinkedList } from "@stdlib/collections";

let map = HashMap();
map.set("key", "value");
map.set("count", 42);
print(map.get("key"));  // "value"

let queue = Queue();
queue.enqueue("first");
queue.enqueue("second");
print(queue.dequeue());  // "first"

let set = Set();
set.add(10);
set.add(20);
set.add(10);  // Duplicate ignored
print(set.size);  // 2
```

**Documentation:** `stdlib/docs/collections.md`
**Features:** All collections support `.each(callback)` iterators, automatic resizing, optimal performance

---

#### 2. **Math** (`@stdlib/math`)
**Status:** Complete

Mathematical functions and constants:
- **Trigonometry:** sin, cos, tan, asin, acos, atan, atan2
- **Exponential/Log:** sqrt, pow, exp, log, log10, log2
- **Rounding:** floor, ceil, round, trunc
- **Utility:** abs, min, max, clamp
- **Random:** rand, rand_range, seed
- **Constants:** PI, E, TAU, INF, NAN

```hemlock
import { sin, cos, sqrt, pow, PI } from "@stdlib/math";

// Trigonometry
let angle = PI / 4.0;
let x = cos(angle);  // 0.707...
let y = sin(angle);  // 0.707...

// Math operations
let dist = sqrt(pow(3.0, 2.0) + pow(4.0, 2.0));  // 5.0

// Random numbers
import { rand, rand_range, seed } from "@stdlib/math";
seed(42);  // Reproducible
let random = rand();  // 0.0 to 1.0
let dice = rand_range(1.0, 7.0);  // 1.0 to 6.999...
```

**Documentation:** `stdlib/docs/math.md`
**Notes:** All functions return f64, angles are in radians

---

#### 3. **Time** (`@stdlib/time`)
**Status:** Basic

Time measurement and delays:
- `now()` - Unix timestamp in seconds (i64)
- `time_ms()` - Milliseconds since epoch (i64)
- `clock()` - CPU time in seconds (f64)
- `sleep(seconds)` - Pause execution (supports sub-second precision)

```hemlock
import { time_ms, sleep, now } from "@stdlib/time";

// Benchmark code
let start = time_ms();
// ... do work ...
let elapsed = time_ms() - start;
print("Took " + typeof(elapsed) + "ms");

// Rate limiting
fn process_items(items: array): null {
    let i = 0;
    while (i < items.length) {
        process(items[i]);
        sleep(0.1);  // 100ms between items
        i = i + 1;
    }
    return null;
}
```

**Documentation:** `stdlib/docs/time.md`
**Notes:** `sleep()` uses nanosleep() for precision

---

#### 4. **Date & Time** (`@stdlib/datetime`)
**Status:** Complete

Comprehensive date/time manipulation with formatting, parsing, and arithmetic:
- **DateTime class** - High-level date/time object
- **Constructors:** now, from_date, from_utc, parse_iso
- **Formatting:** format (strftime), to_string, to_date_string, to_time_string, to_iso_string
- **Arithmetic:** add_days, add_hours, add_minutes, add_seconds
- **Comparison:** is_before, is_after, is_equal
- **Differences:** diff_days, diff_hours, diff_minutes, diff_seconds
- **Utilities:** weekday_name, month_name
- **Low-level builtins:** localtime, gmtime, mktime, strftime

```hemlock
import { now, from_date, parse_iso } from "@stdlib/datetime";

// Get current date/time
let current = now();
print(current.to_string());  // "2025-01-16 12:30:45"
print(current.format("%B %d, %Y"));  // "January 16, 2025"

// Create from specific date
let birthday = from_date(1990, 5, 15, 14, 30, 0);
print(birthday.format("%B %d, %Y at %I:%M %p"));  // "May 15, 1990 at 02:30 PM"

// Parse ISO date
let meeting = parse_iso("2025-03-20T14:30:00");
print(meeting.to_string());  // "2025-03-20 14:30:00"

// Date arithmetic
let next_week = current.add_days(7);
let days_until = next_week.diff_days(current);
print("Days until: " + typeof(days_until));  // 7

// Comparison
if (meeting.is_after(current)) {
    print("Meeting is in the future");
}

// Format with strftime codes
let formatted = current.format("%A, %B %d, %Y at %I:%M:%S %p");
print(formatted);  // "Thursday, January 16, 2025 at 12:30:45 PM"
```

**Documentation:** `stdlib/docs/datetime.md`
**Notes:** Building on `@stdlib/time`, uses C's strftime for formatting

---

#### 5. **Environment** (`@stdlib/env`)
**Status:** Complete

Environment variables and process control:
- `getenv(name)` - Read environment variable
- `setenv(name, value)` - Set environment variable
- `unsetenv(name)` - Remove environment variable
- `exit(code?)` - Exit process with status code
- `get_pid()` - Get process ID

```hemlock
import { getenv, setenv, exit, get_pid } from "@stdlib/env";

// Configuration from environment
let port = getenv("PORT");
if (port == null) {
    port = "8080";  // Default
}

// Set environment for child processes
let path = getenv("PATH");
setenv("PATH", path + ":/usr/local/bin");

// Process ID
let pid = get_pid();
print("Running as PID: " + typeof(pid));

// Graceful exit
if (error_occurred) {
    exit(1);
}
```

**Documentation:** `stdlib/docs/env.md`
**Notes:** Environment changes affect current process and children only

---

#### 6. **Filesystem** (`@stdlib/fs`)
**Status:** Comprehensive

File and directory operations:

**File operations:**
- `exists(path)` - Check if file/directory exists
- `read_file(path)` - Read entire file as string
- `write_file(path, content)` - Write/overwrite file
- `append_file(path, content)` - Append to file
- `remove_file(path)` - Delete file
- `rename(old, new)` - Rename/move file
- `copy_file(src, dest)` - Copy file

**Directory operations:**
- `make_dir(path, mode?)` - Create directory
- `remove_dir(path)` - Remove empty directory
- `list_dir(path)` - List directory contents

**File information:**
- `is_file(path)` - Check if regular file
- `is_dir(path)` - Check if directory
- `file_stat(path)` - Get file metadata

**Path operations:**
- `cwd()` - Get current directory
- `chdir(path)` - Change directory
- `absolute_path(path)` - Resolve to absolute path

```hemlock
import {
    read_file, write_file, exists,
    list_dir, is_file, is_dir,
    copy_file
} from "@stdlib/fs";

// File operations
if (exists("config.json")) {
    let config = read_file("config.json");
    print("Config loaded");
} else {
    write_file("config.json", "{}");
}

// Directory listing
let files = list_dir(".");
let i = 0;
while (i < files.length) {
    if (is_file(files[i])) {
        print("File: " + files[i]);
    }
    i = i + 1;
}

// Backup
copy_file("important.txt", "important.txt.backup");
```

**Documentation:** `stdlib/docs/fs.md`
**Notes:** All operations throw exceptions on errors (use try/catch)

---

#### 7. **Networking** (`@stdlib/net`)
**Status:** Complete

TCP/UDP networking with ergonomic wrappers over raw socket builtins:
- **TcpListener** - TCP server socket for accepting connections
- **TcpStream** - TCP client/connection with read/write methods
- **UdpSocket** - UDP datagram socket with send_to/recv_from
- **DNS:** resolve() - Hostname to IP resolution

```hemlock
import { TcpListener, TcpStream, UdpSocket, resolve } from "@stdlib/net";

// TCP Server
let listener = TcpListener("0.0.0.0", 8080);
defer listener.close();

while (true) {
    let stream = listener.accept();
    spawn(handle_client, stream);
}

// TCP Client
let stream = TcpStream("example.com", 80);
defer stream.close();

stream.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
let response = stream.read(4096);

// UDP Socket
let sock = UdpSocket("0.0.0.0", 5000);
defer sock.close();

let packet = sock.recv_from(1024);
sock.send_to(packet.address, packet.port, packet.data);  // Echo

// DNS Resolution
let ip = resolve("example.com");  // "93.184.216.34"
```

**Documentation:** `stdlib/docs/net.md`
**Features:** IPv4 support, async-compatible, manual resource management with defer, exception-based errors

---

#### 8. **Regular Expressions** (`@stdlib/regex`)
**Status:** Basic (via FFI)

POSIX Extended Regular Expression pattern matching via FFI to system regex library:
- `compile(pattern, flags)` - Compile reusable regex object
- `test(pattern, text, flags)` - One-shot pattern matching
- `matches()`, `find()` - Convenience aliases
- Case-insensitive matching with `REG_ICASE` flag
- Manual memory management (explicit `.free()` required)

```hemlock
import { compile, test, REG_ICASE } from "@stdlib/regex";

// One-shot matching
if (test("^[a-z]+$", "hello", null)) {
    print("Valid lowercase string");
}

// Case-insensitive matching
if (test("^hello", "HELLO WORLD", REG_ICASE)) {
    print("Matches!");
}

// Compiled regex for reuse
let email_pattern = compile("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$", null);
print(email_pattern.test("user@example.com"));  // true
print(email_pattern.test("invalid"));           // false
email_pattern.free();  // Manual cleanup required
```

**Documentation:** `stdlib/docs/regex.md`
**Notes:** Uses POSIX ERE syntax, manual memory management required

---

### JSON Serialization

Hemlock has built-in JSON support through object/string methods (no separate module needed):

```hemlock
// Object to JSON
let data = { name: "Alice", age: 30, active: true };
let json = data.serialize();
print(json);  // {"name":"Alice","age":30,"active":true}

// JSON to object
let json_str = '{"x":10,"y":20}';
let obj = json_str.deserialize();
print(obj.x);  // 10

// Nested objects work
let nested = { user: { name: "Bob" }, items: [1, 2, 3] };
let json2 = nested.serialize();
let restored = json2.deserialize();
```

**Features:**
- Supports objects, arrays, strings, numbers, booleans, null
- Automatic cycle detection (throws on circular references)
- No explicit import needed (methods on objects/strings)

---

### Module Organization

```
stdlib/
├── README.md              # Module overview
├── collections.hml        # Data structures
├── math.hml               # Mathematical functions
├── time.hml               # Time/date operations
├── env.hml                # Environment variables
├── fs.hml                 # Filesystem operations
├── net.hml                # Networking (TCP/UDP)
├── regex.hml              # Regular expressions (via FFI)
└── docs/
    ├── collections.md     # Collections API reference
    ├── math.md            # Math API reference
    ├── time.md            # Time API reference
    ├── env.md             # Environment API reference
    ├── fs.md              # Filesystem API reference
    ├── net.md             # Networking API reference
    └── regex.md           # Regex API reference
```

### Testing

All stdlib modules have comprehensive test coverage:

```bash
# Run all tests
make test

# Run specific module tests
make test | grep stdlib_collections
make test | grep stdlib_math
make test | grep stdlib_time
make test | grep stdlib_env
make test | grep stdlib_net
make test | grep stdlib_regex
```

**Test locations:**
- `tests/stdlib_collections/` - Collections tests
- `tests/stdlib_math/` - Math tests
- `tests/stdlib_time/` - Time tests
- `tests/stdlib_env/` - Environment tests
- `tests/stdlib_net/` - Networking tests (TCP/UDP)
- `tests/stdlib_regex/` - Regular expression tests

### Future Stdlib Modules

Planned additions:
- **strings** - String utilities (pad, join, is_alpha, reverse, lines, words)
- **path** - Path manipulation (join, basename, dirname, extname, normalize)
- **encoding** - Base64, hex, URL encoding/decoding
- **testing** - Test framework with describe/test/expect/assertions
- **crypto** - Cryptographic functions (via FFI + OpenSSL)
- **compression** - zlib/gzip compression (via FFI)

See `stdlib/README.md`, `STDLIB_ANALYSIS_UPDATED.md`, and `STDLIB_NETWORKING_DESIGN.md` for detailed roadmap.

---

## Implementation Details

### Project Structure
```
hemlock/
├── include/              # Public headers
│   ├── ast.h
│   ├── lexer.h
│   ├── parser.h
│   └── interpreter.h
├── src/                  # Implementation
│   ├── ast.c             # AST node constructors and cleanup
│   ├── lexer.c           # Tokenization
│   ├── parser.c          # Parsing (tokens → AST)
│   ├── main.c            # CLI entry point, REPL
│   └── interpreter/      # Interpreter subsystem (modular)
│       ├── internal.h        # Internal API shared between modules
│       ├── environment.c     # Variable scoping (121 lines)
│       ├── values.c          # Value constructors, data structures (394 lines)
│       ├── types.c           # Type system, conversions, duck typing (440 lines)
│       ├── builtins.c        # Builtin functions, registration (955 lines)
│       ├── io.c              # File I/O, serialization (449 lines)
│       └── runtime.c         # eval_expr, eval_stmt, control flow (865 lines)
├── tests/                # Test suite, ran by tests/run_tests.sh
└── examples/             # Example programs
```

**Modular Design Benefits:**
- **Separation of concerns** - Each module has a single, clear responsibility
- **Faster incremental builds** - Only modified modules recompile
- **Easier navigation** - Find features quickly by module name
- **Testable** - Modules can be tested in isolation
- **Scalable** - New features can be added to specific modules without growing monolithic files

### Compilation Pipeline
1. **Lexer** → tokens
2. **Parser** → AST
3. **Interpreter** → tree-walking execution (current)
4. **Compiler** → C code generation (future)

### Current Runtime
- Tree-walking interpreter
- Tagged union for values (`Value` struct)
- Environment-based variable storage
- No optimization yet

### Future Runtime
- Compile to C code
- Keep runtime library for dynamic features
- Optional `--no-tags` flag for fully static builds

---

## Testing Philosophy

- **Test-driven development** for new features
- Comprehensive test suite in `tests/`
- Test both success and error cases
- Run `make test` before committing

Example test structure:
```bash
tests/
├── primitives/       # Type system tests
├── conversions/      # Implicit conversion tests
├── memory/           # Pointer/buffer tests
├── strings/          # String operation tests
├── control/          # Control flow tests
├── functions/        # Function and closure tests
├── objects/          # Object, method, and serialization tests
├── arrays/           # Array operations tests
├── loops/            # For, while, break, continue tests
├── exceptions/       # Try/catch/finally/throw tests
├── io/               # File I/O tests
├── args/             # Command-line argument tests
└── run_tests.sh      # Test runner
```

---

## Common Patterns

### Error Handling
```hemlock
// Use try/catch for recoverable errors
fn safe_divide(a, b) {
    try {
        if (b == 0) {
            throw "division by zero";
        }
        return a / b;
    } catch (e) {
        print("Error: " + e);
        return null;
    }
}

// Use finally for cleanup
fn process_data(filename) {
    let file = null;
    try {
        file = open(filename);
        // ... process file
    } finally {
        if (file != null) {
            close(file);
        }
    }
}
```

### Memory Patterns
```hemlock
// Pattern: Allocate, use, free
let data = alloc(1024);
// ... use data
free(data);

// Pattern: Safe buffer usage
let buf = buffer(256);
let i = 0;
while (i < buf.length) {
    buf[i] = i;
    i = i + 1;
}
free(buf);
```

### Type Patterns
```hemlock
// Pattern: Let types infer when obvious
let count = 0;
let sum = 0.0;

// Pattern: Annotate when precision matters
let byte: u8 = 255;
let precise: f64 = 3.14159265359;
```

---

## What NOT to Do

### ❌ Don't Add Implicit Behavior
```hemlock
// BAD: Automatic semicolon insertion
let x = 5
let y = 10

// BAD: Automatic memory management
let s = "hello"  // String auto-freed at end of scope? NO!

// BAD: Implicit type conversions that lose precision
let x: i32 = 3.14  // Should truncate or error?
```

### ❌ Don't Hide Complexity
```hemlock
// BAD: Magic behind-the-scenes optimization
let arr = [1, 2, 3]  // Is this stack or heap? User should know!

// BAD: Automatic reference counting
let p = create_thing()  // Does this increment a refcount? NO!
```

### ❌ Don't Break Existing Semantics
- Semicolons are mandatory - don't make them optional
- Manual memory management - don't add GC
- Mutable strings - don't make them immutable
- Runtime type checking - don't remove it

---

## Future Considerations

### Maybe Add (Under Discussion)
- Array/list types (not just buffers) - Note: Basic arrays are implemented
- Pattern matching
- Error types (`Result<T, E>`)

### Probably Never Add
- Garbage collection
- Automatic memory management
- Implicit type conversions that lose data
- Operator overloading (maybe for user types)
- Macros (too complex)

---

## Philosophy on Safety

**Hemlock's take on safety:**

"We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).

The default should guide toward safety, but the escape hatch should always be available."

**Examples:**
- Default to `buffer` in docs, but `ptr` exists for FFI
- Encourage type annotations, but don't require them
- Provide bounds checking, but allow raw pointer math
- Include `sizeof()` and typed allocation to reduce errors

---

## Contributing Guidelines (Future)

When adding features to Hemlock:

1. **Read this document first**
2. **Write tests before implementation**
3. **Keep it explicit** - no magic
4. **Document the unsafe parts** - be honest
5. **Follow existing patterns** - consistency matters
6. **Update CLAUDE.md** - keep this doc current

---

## Version History

- **v0.1** - Primitives, memory management, UTF-8 strings, control flow, functions, closures, recursion, objects, arrays, enums, error handling, file I/O, signal handling, command-line arguments, async/await, structured concurrency, FFI (current)
  - Type system: i8-i64, u8-u64, f32/f64, bool, string, rune, null, ptr, buffer, array, object, enum, file, task, channel, void
  - **64-bit integer support:** i64 and u64 types with full type promotion, conversion, and FFI support
  - **UTF-8 first-class strings:** Full Unicode support (U+0000 to U+10FFFF), codepoint-based indexing and operations, `.length` (codepoints) and `.byte_length` (bytes) properties
  - **Rune type:** Unicode codepoints as distinct 32-bit type, rune literals with escape sequences and Unicode escapes ('\u{XXXX}'), string + rune concatenation, integer ↔ rune conversions
  - Memory: alloc, free, memset, memcpy, realloc, talloc, sizeof
  - Objects: literals, methods, duck typing, optional fields, serialize/deserialize
  - **Enums:** C-style enumerations with auto-incrementing or explicit values, type checking, namespace objects
  - **Strings:** 18 methods including substr, slice, find, contains, split, trim, to_upper, to_lower, starts_with, ends_with, replace, replace_all, repeat, char_at, byte_at, chars, bytes, to_bytes
  - **Arrays:** 18 methods including push, pop, shift, unshift, insert, remove, find, contains, slice, join, concat, reverse, first, last, clear, **map, filter, reduce** (higher-order functions for functional programming)
  - Control flow: if/else, while, for, for-in, break, continue, switch, bitwise operators (&, |, ^, <<, >>, ~), **defer**
  - **Error handling:** try/catch/finally/throw, panic - **all user-facing runtime errors are catchable** (array bounds, type conversions, arity mismatches, stack overflow, async errors)
  - **File I/O:** File object API with methods (read, read_bytes, write, write_bytes, seek, tell, close) and properties (path, mode, closed)
  - **Signal Handling:** POSIX signal handling with signal(signum, handler) and raise(signum), 15 signal constants (SIGINT, SIGTERM, SIGUSR1, SIGUSR2, etc.)
  - Command-line arguments: built-in `args` array
  - **Async/Concurrency:** async/await syntax, spawn/join/detach (supports both fire-and-forget and spawn-then-detach patterns), channels with send/recv/close, pthread-based true parallelism, exception propagation
  - **FFI (Foreign Function Interface):** Call C functions from shared libraries using libffi, support for all primitive types, automatic type conversion
  - **Architecture:** Modular interpreter (environment, values, types, builtins, io, runtime, ffi)
  - **394 tests** - 369 passing + 25 expected error tests (100% test success rate including async, FFI, i64/u64, signals, defer, map/filter/reduce, edge cases)
- **v0.2** - Compiler backend, optimization (planned)
- **v0.3** - Advanced features (planned)

---

## Final Thoughts

Hemlock is about **trust and responsibility**. We trust the programmer to:
- Manage memory correctly
- Use types appropriately
- Handle errors properly
- Understand the tradeoffs

In return, Hemlock provides:
- No hidden costs
- No surprise behavior
- Full control when needed
- Safety tools when wanted

**If you're not sure whether a feature fits Hemlock, ask: "Does this give the programmer more explicit control, or does it hide something?"**

If it hides, it probably doesn't belong in Hemlock.
