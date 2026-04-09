# @stdlib/decimal - Decimal Formatting, Number Parsing & String Building

The `decimal` module provides utilities for converting numbers to formatted strings, parsing strings into numbers, and efficient incremental string building.

## Quick Start

```hemlock
import { to_fixed, to_hex, parse_int, sb_new, sb_append, sb_to_string } from "@stdlib/decimal";

// Fixed decimal places with rounding
print(to_fixed(3.14159, 2));   // "3.14"
print(to_fixed(9.999, 2));     // "10.00"

// Base conversion
print(to_hex(255));            // "ff"
print(to_bin(10));             // "1010"

// Number parsing
print(parse_int("ff", 16));    // 255
print(parse_float("3.14"));   // 3.14

// String builder
let sb = sb_new();
sb_append(sb, "Hello");
sb_append(sb, " World");
print(sb_to_string(sb));       // "Hello World"
```

---

## Number-to-String Formatting

### to_fixed(num, places?): string

Format a number to a fixed number of decimal places with rounding.

**Parameters:**
- `num` - Number to format
- `places?: i32` - Decimal places, 0-20 (default: 0)

**Returns:** `string` - Formatted number

```hemlock
import { to_fixed } from "@stdlib/decimal";

print(to_fixed(3.14159, 2));   // "3.14"
print(to_fixed(3.145, 2));     // "3.15" (rounds up)
print(to_fixed(3, 4));         // "3.0000"
print(to_fixed(0.1 + 0.2, 2)); // "0.30"
print(to_fixed(-1.5, 0));      // "-2"
print(to_fixed(42));            // "42" (no decimal)
print(to_fixed(1.005, 2));     // "1.01"
```

### to_precision(num, digits): string

Format a number to N significant figures.

**Parameters:**
- `num` - Number to format
- `digits: i32` - Significant digits, 1-21

**Returns:** `string` - Formatted number

```hemlock
import { to_precision } from "@stdlib/decimal";

print(to_precision(3.14159, 3));    // "3.14"
print(to_precision(0.001234, 3));   // "0.00123"
print(to_precision(1234, 2));       // "1200"
print(to_precision(0, 3));          // "0.00"
print(to_precision(123.456, 5));    // "123.45"
```

---

## Base Conversion

### to_hex(num): string

Convert integer to lowercase hexadecimal string (no prefix).

```hemlock
import { to_hex } from "@stdlib/decimal";

print(to_hex(255));     // "ff"
print(to_hex(0));       // "0"
print(to_hex(16));      // "10"
print(to_hex(4096));    // "1000"
print(to_hex(-1));      // "-1"
```

### to_oct(num): string

Convert integer to octal string (no prefix).

```hemlock
import { to_oct } from "@stdlib/decimal";

print(to_oct(8));       // "10"
print(to_oct(64));      // "100"
print(to_oct(0));       // "0"
print(to_oct(511));     // "777"
```

### to_bin(num): string

Convert integer to binary string (no prefix).

```hemlock
import { to_bin } from "@stdlib/decimal";

print(to_bin(10));      // "1010"
print(to_bin(255));     // "11111111"
print(to_bin(0));       // "0"
print(to_bin(1));       // "1"
```

### number_to_string(num, radix?): string

Convert integer to string in any base from 2 to 36.

**Parameters:**
- `num` - Integer to convert
- `radix?: i32` - Base 2-36 (default: 10)

```hemlock
import { number_to_string } from "@stdlib/decimal";

print(number_to_string(255, 16));   // "ff"
print(number_to_string(10, 2));     // "1010"
print(number_to_string(35, 36));    // "z"
print(number_to_string(100));       // "100" (default base 10)
```

---

## Number Parsing

### parse_int(str, radix?): i64

Parse a string into an integer. Supports optional radix and auto-detects `0x`, `0o`, `0b` prefixes when radix is 10.

**Parameters:**
- `str: string` - String to parse
- `radix?: i32` - Base 2-36 (default: 10)

**Returns:** `i64` - Parsed integer

```hemlock
import { parse_int } from "@stdlib/decimal";

// Decimal
print(parse_int("42"));         // 42
print(parse_int("-100"));       // -100
print(parse_int("1_000_000"));  // 1000000 (underscores ignored)

// Explicit radix
print(parse_int("ff", 16));     // 255
print(parse_int("777", 8));     // 511
print(parse_int("1010", 2));    // 10

// Auto-detect prefix (when radix is 10)
print(parse_int("0xff"));       // 255
print(parse_int("0o77"));       // 63
print(parse_int("0b1010"));     // 10
```

### parse_float(str): f64

Parse a string into a floating-point number. Supports scientific notation and numeric separators.

**Parameters:**
- `str: string` - String to parse

**Returns:** `f64` - Parsed float

```hemlock
import { parse_float } from "@stdlib/decimal";

print(parse_float("3.14"));        // 3.14
print(parse_float("-0.5"));        // -0.5
print(parse_float(".5"));          // 0.5
print(parse_float("1_000.5"));     // 1000.5
print(parse_float("1.5e3"));       // 1500
print(parse_float("2.5E-1"));      // 0.25
```

---

## StringBuilder

An efficient pattern for building strings incrementally using an array of parts, avoiding repeated concatenation overhead.

### sb_new(): object

Create a new StringBuilder.

```hemlock
import { sb_new } from "@stdlib/decimal";

let sb = sb_new();
```

### sb_append(sb, val): object

Append a value to the builder. Any value is converted to string. Returns the builder for chaining.

```hemlock
import { sb_new, sb_append, sb_to_string } from "@stdlib/decimal";

let sb = sb_new();
sb_append(sb, "Name: ");
sb_append(sb, "Alice");
sb_append(sb, ", Age: ");
sb_append(sb, 30);
print(sb_to_string(sb));  // "Name: Alice, Age: 30"
```

### sb_prepend(sb, val): object

Prepend a value to the beginning of the builder.

```hemlock
import { sb_new, sb_append, sb_prepend, sb_to_string } from "@stdlib/decimal";

let sb = sb_new();
sb_append(sb, "World");
sb_prepend(sb, "Hello ");
print(sb_to_string(sb));  // "Hello World"
```

### sb_to_string(sb): string

Convert the builder contents to a single string.

### sb_join(sb, sep): string

Convert the builder contents to a string with a separator between parts.

```hemlock
import { sb_new, sb_append, sb_join } from "@stdlib/decimal";

let sb = sb_new();
sb_append(sb, "one");
sb_append(sb, "two");
sb_append(sb, "three");
print(sb_join(sb, ", "));   // "one, two, three"
print(sb_join(sb, " | "));  // "one | two | three"
```

### sb_count(sb): i32

Get the number of parts in the builder.

```hemlock
import { sb_new, sb_append, sb_count } from "@stdlib/decimal";

let sb = sb_new();
print(sb_count(sb));        // 0
sb_append(sb, "hello");
print(sb_count(sb));        // 1
sb_append(sb, "world");
print(sb_count(sb));        // 2
```

### sb_clear(sb): object

Clear all parts from the builder. Returns the builder for chaining.

```hemlock
import { sb_new, sb_append, sb_clear, sb_count, sb_to_string } from "@stdlib/decimal";

let sb = sb_new();
sb_append(sb, "hello");
sb_clear(sb);
print(sb_count(sb));         // 0
print(sb_to_string(sb));     // ""
```

---

## Examples

### Formatting a price list

```hemlock
import { to_fixed, sb_new, sb_append, sb_to_string } from "@stdlib/decimal";
import { pad_left, pad_right } from "@stdlib/strings";

let items = [
    { name: "Coffee", price: 4.5 },
    { name: "Sandwich", price: 8.99 },
    { name: "Cake", price: 12.0 },
];

let sb = sb_new();
for (item in items) {
    sb_append(sb, pad_right(item.name, 15));
    sb_append(sb, "$" + to_fixed(item.price, 2));
    sb_append(sb, "\n");
}
print(sb_to_string(sb));
// Coffee         $4.50
// Sandwich       $8.99
// Cake           $12.00
```

### Hex dump

```hemlock
import { to_hex } from "@stdlib/decimal";
import { pad_left } from "@stdlib/strings";

fn hex_byte(b: i32): string {
    return pad_left(to_hex(b), 2, "0");
}

print(hex_byte(0));    // "00"
print(hex_byte(15));   // "0f"
print(hex_byte(255));  // "ff"
```

### Parsing user input

```hemlock
import { parse_int, parse_float } from "@stdlib/decimal";

// Parse config values
let port = parse_int("8080");
let timeout = parse_float("30.5");
let color = parse_int("0xFF8800");

print(port);     // 8080
print(timeout);  // 30.5
print(color);    // 16746496
```

### Building CSV output

```hemlock
import { sb_new, sb_append, sb_join, sb_clear, sb_to_string } from "@stdlib/decimal";

let rows = [
    ["Name", "Age", "City"],
    ["Alice", "30", "NYC"],
    ["Bob", "25", "LA"],
];

let output = sb_new();
for (row in rows) {
    let line = sb_new();
    for (cell in row) {
        sb_append(line, cell);
    }
    sb_append(output, sb_join(line, ","));
    sb_append(output, "\n");
}
print(sb_to_string(output));
// Name,Age,City
// Alice,30,NYC
// Bob,25,LA
```

---

## See Also

- [@stdlib/fmt](fmt.md) - Printf-style formatting (`format()`, `thousands()`, `percent()`)
- [@stdlib/strings](strings.md) - String manipulation (`pad_left`, `reverse`, `is_digit`)
- [@stdlib/encoding](encoding.md) - Base64 and hex encoding for byte data
