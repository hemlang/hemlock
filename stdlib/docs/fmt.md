# @stdlib/fmt - String Formatting

The `fmt` module provides printf-style string formatting and text manipulation utilities.

## Quick Start

```hemlock
import { format, pad_left, thousands } from "@stdlib/fmt";

// Printf-style formatting
let msg = format("Hello, %s! You have %d messages.", ["Alice", 5]);
print(msg);  // "Hello, Alice! You have 5 messages."

// Number formatting
print(thousands(1234567));  // "1,234,567"

// String padding
print(pad_left("42", 5, "0"));  // "00042"
```

## Format Specifiers

| Specifier | Description | Example |
|-----------|-------------|---------|
| `%s` | String | `format("%s", ["hello"])` → `"hello"` |
| `%d`, `%i` | Integer (decimal) | `format("%d", [42])` → `"42"` |
| `%f` | Float (6 decimals) | `format("%f", [3.14])` → `"3.140000"` |
| `%.Nf` | Float with N decimals | `format("%.2f", [3.14159])` → `"3.14"` |
| `%x`, `%X` | Hexadecimal | `format("%x", [255])` → `"ff"` |
| `%o` | Octal | `format("%o", [64])` → `"100"` |
| `%b` | Binary | `format("%b", [10])` → `"1010"` |
| `%e`, `%E` | Scientific notation | `format("%e", [1234])` → `"1.234000e+03"` |
| `%c` | Character (from code) | `format("%c", [65])` → `"A"` |
| `%q` | Quoted string | `format("%q", ["hi"])` → `"\"hi\""` |
| `%%` | Literal percent | `format("100%%", [])` → `"100%"` |

### Width and Flags

| Flag | Description | Example |
|------|-------------|---------|
| `%10s` | Right-pad to 10 chars | `format("[%10s]", ["hi"])` → `"[        hi]"` |
| `%-10s` | Left-pad to 10 chars | `format("[%-10s]", ["hi"])` → `"[hi        ]"` |
| `%05d` | Zero-pad to 5 chars | `format("%05d", [42])` → `"00042"` |
| `%+d` | Show + sign | `format("%+d", [42])` → `"+42"` |
| `% d` | Space for positive | `format("% d", [42])` → `" 42"` |

## API Reference

### format(template, args): string

Format a string with placeholders.

```hemlock
import { format } from "@stdlib/fmt";

print(format("Name: %s, Age: %d", ["Alice", 30]));
// "Name: Alice, Age: 30"

print(format("Pi: %.3f", [3.14159]));
// "Pi: 3.141"

print(format("Hex: 0x%X", [255]));
// "Hex: 0xFF"
```

### sprintf(template, args): string

Alias for `format()`.

### pad_left(s, width, char?): string

Pad string on the left to reach target width.

```hemlock
import { pad_left } from "@stdlib/fmt";

print(pad_left("hello", 10));         // "     hello"
print(pad_left("42", 5, "0"));        // "00042"
print(pad_left("hello", 3));          // "hello" (unchanged)
```

### pad_right(s, width, char?): string

Pad string on the right to reach target width.

```hemlock
import { pad_right } from "@stdlib/fmt";

print(pad_right("hello", 10));         // "hello     "
print(pad_right("hello", 10, "-"));    // "hello-----"
```

### center(s, width, char?): string

Center string within target width.

```hemlock
import { center } from "@stdlib/fmt";

print(center("hello", 11));            // "   hello   "
print(center("hi", 10, "="));          // "====hi===="
```

### truncate(s, max_len, suffix?): string

Truncate string to maximum length with optional suffix.

```hemlock
import { truncate } from "@stdlib/fmt";

print(truncate("Hello, World!", 10));           // "Hello, ..."
print(truncate("Hello, World!", 10, ".."));     // "Hello, W.."
print(truncate("Short", 10));                   // "Short"
```

### wrap(text, width): string

Wrap text to specified width.

```hemlock
import { wrap } from "@stdlib/fmt";

let text = "This is a long text that should be wrapped";
print(wrap(text, 20));
// This is a long text
// that should be
// wrapped
```

### thousands(num, sep?): string

Format number with thousands separator.

```hemlock
import { thousands } from "@stdlib/fmt";

print(thousands(1234567));        // "1,234,567"
print(thousands(1000000, " "));   // "1 000 000"
print(thousands(-9999));          // "-9,999"
```

### bytes_size(bytes, precision?): string

Format bytes as human-readable size.

```hemlock
import { bytes_size } from "@stdlib/fmt";

print(bytes_size(500));           // "500 B"
print(bytes_size(1536));          // "1.5 KB"
print(bytes_size(1048576));       // "1.0 MB"
print(bytes_size(1073741824));    // "1.0 GB"
```

### duration(seconds): string

Format duration in seconds as human-readable.

```hemlock
import { duration } from "@stdlib/fmt";

print(duration(0.5));      // "500.0ms"
print(duration(45));       // "45.0s"
print(duration(125));      // "2m 5s"
print(duration(3725));     // "1h 2m"
print(duration(90061));    // "1d 1h"
```

### ordinal(n): string

Format number as ordinal (1st, 2nd, 3rd, etc.).

```hemlock
import { ordinal } from "@stdlib/fmt";

print(ordinal(1));    // "1st"
print(ordinal(2));    // "2nd"
print(ordinal(3));    // "3rd"
print(ordinal(4));    // "4th"
print(ordinal(11));   // "11th"
print(ordinal(21));   // "21st"
```

### percent(value, precision?): string

Format value as percentage.

```hemlock
import { percent } from "@stdlib/fmt";

print(percent(0.5));         // "50.0%"
print(percent(0.12345, 2));  // "12.34%"
print(percent(1.0));         // "100.0%"
```

## Examples

### Formatting table output

```hemlock
import { format, pad_left, pad_right } from "@stdlib/fmt";

let items = [
    { name: "Apple", price: 1.50, qty: 10 },
    { name: "Banana", price: 0.75, qty: 25 },
    { name: "Orange", price: 2.00, qty: 8 }
];

print(pad_right("Item", 15) + pad_left("Price", 10) + pad_left("Qty", 8));
print("-".repeat(33));

let i = 0;
while (i < items.length) {
    let item = items[i];
    print(pad_right(item["name"], 15) +
          pad_left(format("$%.2f", [item["price"]]), 10) +
          pad_left("" + item["qty"], 8));
    i = i + 1;
}
```

### Log message formatting

```hemlock
import { format } from "@stdlib/fmt";

fn log(level, msg, args) {
    let timestamp = "2024-01-15 10:30:00";  // Use @stdlib/datetime
    let formatted = format(msg, args);
    print(format("[%s] [%s] %s", [timestamp, level, formatted]));
}

log("INFO", "User %s logged in from %s", ["alice", "192.168.1.1"]);
log("ERROR", "Failed to connect after %d retries", [3]);
```

### Progress display

```hemlock
import { format, percent, bytes_size, duration } from "@stdlib/fmt";

fn show_progress(downloaded, total, elapsed) {
    let pct = downloaded / total;
    let speed = downloaded / elapsed;

    print(format("Downloaded: %s / %s (%s)",
        [bytes_size(downloaded), bytes_size(total), percent(pct)]));
    print(format("Speed: %s/s, Elapsed: %s",
        [bytes_size(speed), duration(elapsed)]));
}
```

## See Also

- [@stdlib/decimal](decimal.md) - Number formatting (`to_fixed`, `to_hex`, `parse_int`, StringBuilder)
- [@stdlib/strings](strings.md) - Additional string utilities
- [@stdlib/terminal](terminal.md) - ANSI colors and styles
