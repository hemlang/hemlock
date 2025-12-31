# Strings

Hemlock strings are **UTF-8 first-class mutable sequences** with full Unicode support and a rich set of methods for text processing. Unlike many languages, Hemlock strings are mutable and work natively with Unicode codepoints.

## Overview

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

## Properties

Hemlock strings have these key characteristics:

- **UTF-8 encoded** - Full Unicode support (U+0000 to U+10FFFF)
- **Mutable** - Unlike Python, JavaScript, and Java strings
- **Codepoint-based indexing** - Returns `rune` (Unicode codepoint), not byte
- **Heap-allocated** - With internal capacity tracking
- **Two length properties**:
  - `.length` - Codepoint count (number of characters)
  - `.byte_length` - Byte count (UTF-8 encoding size)

## UTF-8 Behavior

All string operations work with **codepoints** (characters), not bytes:

```hemlock
let text = "Hello🚀World";
print(text.length);        // 11 (codepoints)
print(text.byte_length);   // 15 (bytes, emoji is 4 bytes)

// Indexing uses codepoints
let h = text[0];           // 'H' (rune)
let rocket = text[5];      // '🚀' (rune)
```

**Multi-byte characters count as one:**
```hemlock
"Hello".length;      // 5
"🚀".length;         // 1 (one emoji)
"你好".length;       // 2 (two Chinese characters)
"café".length;       // 4 (é is one codepoint)
```

## String Literals

```hemlock
// Basic strings
let s1 = "hello";
let s2 = "world";

// With escape sequences
let s3 = "Line 1\nLine 2\ttabbed";
let s4 = "Quote: \"Hello\"";
let s5 = "Backslash: \\";

// Unicode characters
let s6 = "🚀 Emoji";
let s7 = "中文字符";
```

## Template Strings (String Interpolation)

Use backticks for template strings with embedded expressions:

```hemlock
let name = "Alice";
let age = 30;

// Basic interpolation
let greeting = `Hello, ${name}!`;           // "Hello, Alice!"
let info = `${name} is ${age} years old`;   // "Alice is 30 years old"

// Expressions in interpolation
let x = 5;
let y = 10;
let sum = `${x} + ${y} = ${x + y}`;         // "5 + 10 = 15"

// Method calls
let upper = `Name: ${name.to_upper()}`;     // "Name: ALICE"

// Nested objects
let person = { name: "Bob", city: "NYC" };
let desc = `${person.name} lives in ${person.city}`;  // "Bob lives in NYC"

// Multi-line (preserves newlines)
let multi = `Line 1
Line 2
Line 3`;
```

**Template string features:**
- Expressions inside `${...}` are evaluated and converted to strings
- Any valid expression can be used (variables, function calls, arithmetic)
- Backtick strings support the same escape sequences as regular strings
- Useful for building dynamic strings without concatenation

### Escaping in Template Strings

To include a literal `${` in a template string, escape the dollar sign:

```hemlock
let price = 100;
let text = `Price: \${price} or ${price}`;
// "Price: ${price} or 100"

// Literal backtick
let code = `Use \` for template strings`;
// "Use ` for template strings"
```

### Complex Expressions

Template strings can contain any valid expression:

```hemlock
// Ternary-like expressions
let age = 25;
let status = `Status: ${age >= 18 ? "adult" : "minor"}`;

// Array access
let items = ["apple", "banana", "cherry"];
let first = `First item: ${items[0]}`;

// Function calls with arguments
fn format_price(p) { return "$" + p; }
let msg = `Total: ${format_price(99.99)}`;  // "Total: $99.99"

// Chained method calls
let name = "alice";
let formatted = `Hello, ${name.to_upper().slice(0, 1)}${name.slice(1)}!`;
// "Hello, Alice!"
```

### Template Strings vs Concatenation

Template strings are often cleaner than concatenation:

```hemlock
// Concatenation (harder to read)
let msg1 = "Hello, " + name + "! You have " + count + " messages.";

// Template string (easier to read)
let msg2 = `Hello, ${name}! You have ${count} messages.`;
```

## Indexing and Mutation

### Reading Characters

Indexing returns a `rune` (Unicode codepoint):

```hemlock
let s = "Hello";
let first = s[0];      // 'H' (rune)
let last = s[4];       // 'o' (rune)

// UTF-8 example
let emoji = "Hi🚀!";
let rocket = emoji[2];  // '🚀' (rune at codepoint index 2)
```

### Writing Characters

Strings are mutable - you can modify individual characters:

```hemlock
let s = "hello";
s[0] = 'H';            // Now "Hello"
s[4] = '!';            // Now "Hell!"

// With Unicode
let msg = "Go!";
msg[0] = '🚀';         // Now "🚀o!"
```

## Concatenation

Use `+` to concatenate strings:

```hemlock
let greeting = "Hello" + " " + "World";  // "Hello World"

// With variables
let name = "Alice";
let msg = "Hi, " + name + "!";  // "Hi, Alice!"

// With runes (see Runes documentation)
let s = "Hello" + '!';          // "Hello!"
```

## String Methods

Hemlock provides 19 string methods for comprehensive text manipulation.

### Substring & Slicing

**`substr(start, length)`** - Extract substring by position and length:
```hemlock
let s = "hello world";
let sub = s.substr(6, 5);       // "world" (start at 6, length 5)
let first = s.substr(0, 5);     // "hello"

// UTF-8 example
let text = "Hi🚀!";
let emoji = text.substr(2, 1);  // "🚀" (position 2, length 1)
```

**`slice(start, end)`** - Extract substring by range (end exclusive):
```hemlock
let s = "hello world";
let slice = s.slice(0, 5);      // "hello" (index 0 to 4)
let slice2 = s.slice(6, 11);    // "world"
```

**Difference:**
- `substr(start, length)` - Uses length parameter
- `slice(start, end)` - Uses end index (exclusive)

### Search & Find

**`find(needle)`** - Find first occurrence:
```hemlock
let s = "hello world";
let pos = s.find("world");      // 6 (index of first occurrence)
let pos2 = s.find("foo");       // -1 (not found)
let pos3 = s.find("l");         // 2 (first 'l')
```

**`contains(needle)`** - Check if string contains substring:
```hemlock
let s = "hello world";
let has = s.contains("world");  // true
let has2 = s.contains("foo");   // false
```

### Split & Trim

**`split(delimiter)`** - Split into array of strings:
```hemlock
let csv = "apple,banana,cherry";
let parts = csv.split(",");     // ["apple", "banana", "cherry"]

let words = "one two three".split(" ");  // ["one", "two", "three"]

// Empty delimiter splits by character
let chars = "abc".split("");    // ["a", "b", "c"]
```

**`trim()`** - Remove leading/trailing whitespace:
```hemlock
let s = "  hello  ";
let clean = s.trim();           // "hello"

let s2 = "\t\ntext\n\t";
let clean2 = s2.trim();         // "text"
```

### Case Conversion

**`to_upper()`** - Convert to uppercase:
```hemlock
let s = "hello world";
let upper = s.to_upper();       // "HELLO WORLD"

// Preserves non-ASCII
let s2 = "café";
let upper2 = s2.to_upper();     // "CAFÉ"
```

**`to_lower()`** - Convert to lowercase:
```hemlock
let s = "HELLO WORLD";
let lower = s.to_lower();       // "hello world"
```

### Prefix/Suffix Checking

**`starts_with(prefix)`** - Check if starts with prefix:
```hemlock
let s = "hello world";
let starts = s.starts_with("hello");  // true
let starts2 = s.starts_with("world"); // false
```

**`ends_with(suffix)`** - Check if ends with suffix:
```hemlock
let s = "hello world";
let ends = s.ends_with("world");      // true
let ends2 = s.ends_with("hello");     // false
```

### Replacement

**`replace(old, new)`** - Replace first occurrence:
```hemlock
let s = "hello world";
let s2 = s.replace("world", "there");      // "hello there"

let s3 = "foo foo foo";
let s4 = s3.replace("foo", "bar");         // "bar foo foo" (first only)
```

**`replace_all(old, new)`** - Replace all occurrences:
```hemlock
let s = "foo foo foo";
let s2 = s.replace_all("foo", "bar");      // "bar bar bar"

let s3 = "hello world, world!";
let s4 = s3.replace_all("world", "hemlock"); // "hello hemlock, hemlock!"
```

### Repetition

**`repeat(count)`** - Repeat string n times:
```hemlock
let s = "ha";
let laugh = s.repeat(3);        // "hahaha"

let line = "=".repeat(40);      // "========================================"
```

### Character & Byte Access

**`char_at(index)`** - Get Unicode codepoint at index (returns rune):
```hemlock
let s = "hello";
let char = s.char_at(0);        // 'h' (rune)

// UTF-8 example
let emoji = "🚀";
let rocket = emoji.char_at(0);  // Returns rune U+1F680
```

**`chars()`** - Convert to array of runes (codepoints):
```hemlock
let s = "hello";
let chars = s.chars();          // ['h', 'e', 'l', 'l', 'o'] (array of runes)

// UTF-8 example
let text = "Hi🚀";
let chars2 = text.chars();      // ['H', 'i', '🚀']
```

**`byte_at(index)`** - Get byte value at index (returns u8):
```hemlock
let s = "hello";
let byte = s.byte_at(0);        // 104 (ASCII value of 'h')

// UTF-8 example
let emoji = "🚀";
let first_byte = emoji.byte_at(0);  // 240 (first UTF-8 byte)
```

**`bytes()`** - Convert to array of bytes (u8 values):
```hemlock
let s = "hello";
let bytes = s.bytes();          // [104, 101, 108, 108, 111] (array of u8)

// UTF-8 example
let emoji = "🚀";
let bytes2 = emoji.bytes();     // [240, 159, 154, 128] (4 UTF-8 bytes)
```

**`to_bytes()`** - Convert to buffer for low-level access:
```hemlock
let s = "hello";
let buf = s.to_bytes();         // Returns buffer with UTF-8 bytes
print(buf.length);              // 5
free(buf);                      // Remember to free
```

## Method Chaining

All string methods return new strings, enabling chaining:

```hemlock
let result = "  Hello World  "
    .trim()
    .to_lower()
    .replace("world", "hemlock");  // "hello hemlock"

let processed = "foo,bar,baz"
    .split(",")
    .join(" | ")
    .to_upper();                    // "FOO | BAR | BAZ"
```

## Complete Method Reference

| Method | Parameters | Returns | Description |
|--------|-----------|---------|-------------|
| `substr(start, length)` | i32, i32 | string | Extract substring by position and length |
| `slice(start, end)` | i32, i32 | string | Extract substring by range (end exclusive) |
| `find(needle)` | string | i32 | Find first occurrence (-1 if not found) |
| `contains(needle)` | string | bool | Check if contains substring |
| `split(delimiter)` | string | array | Split into array of strings |
| `trim()` | - | string | Remove leading/trailing whitespace |
| `to_upper()` | - | string | Convert to uppercase |
| `to_lower()` | - | string | Convert to lowercase |
| `starts_with(prefix)` | string | bool | Check if starts with prefix |
| `ends_with(suffix)` | string | bool | Check if ends with suffix |
| `replace(old, new)` | string, string | string | Replace first occurrence |
| `replace_all(old, new)` | string, string | string | Replace all occurrences |
| `repeat(count)` | i32 | string | Repeat string n times |
| `char_at(index)` | i32 | rune | Get codepoint at index |
| `byte_at(index)` | i32 | u8 | Get byte value at index |
| `chars()` | - | array | Convert to array of runes |
| `bytes()` | - | array | Convert to array of u8 bytes |
| `to_bytes()` | - | buffer | Convert to buffer (must free) |

## Examples

### Example: Text Processing

```hemlock
fn process_input(text: string): string {
    return text
        .trim()
        .to_lower()
        .replace_all("  ", " ");  // Normalize whitespace
}

let input = "  HELLO   WORLD  ";
let clean = process_input(input);  // "hello world"
```

### Example: CSV Parser

```hemlock
fn parse_csv_line(line: string): array {
    let trimmed = line.trim();
    let fields = trimmed.split(",");

    let result = [];
    let i = 0;
    while (i < fields.length) {
        result.push(fields[i].trim());
        i = i + 1;
    }

    return result;
}

let csv = "apple, banana , cherry";
let fields = parse_csv_line(csv);  // ["apple", "banana", "cherry"]
```

### Example: Word Counter

```hemlock
fn count_words(text: string): i32 {
    let words = text.trim().split(" ");
    return words.length;
}

let sentence = "The quick brown fox";
let count = count_words(sentence);  // 4
```

### Example: String Validation

```hemlock
fn is_valid_email(email: string): bool {
    if (!email.contains("@")) {
        return false;
    }

    if (!email.contains(".")) {
        return false;
    }

    if (email.starts_with("@") || email.ends_with("@")) {
        return false;
    }

    return true;
}

print(is_valid_email("user@example.com"));  // true
print(is_valid_email("invalid"));            // false
```

## Memory Management

Strings are heap-allocated with internal reference counting:

- **Creation**: Allocated on heap with capacity tracking
- **Concatenation**: Creates new string (old strings unchanged)
- **Methods**: Most methods return new strings
- **Lifetime**: Strings are refcounted and automatically freed when scope exits

**Automatic cleanup:**
```hemlock
fn create_strings() {
    let s = "hello";
    let s2 = s + " world";  // New allocation
}  // Both s and s2 are automatically freed when function returns
```

**Note:** Local string variables are automatically cleaned up when they go out of scope. Use `free()` only for early cleanup before scope ends or for long-lived/global data. See [Memory Management](memory.md#internal-reference-counting) for details.

## Best Practices

1. **Use codepoint indexing** - Strings use codepoint positions, not byte offsets
2. **Test with Unicode** - Always test string operations with multi-byte characters
3. **Prefer immutable operations** - Use methods that return new strings rather than mutation
4. **Check bounds** - String indexing does not bounds-check (returns null/error on invalid)
5. **Normalize input** - Use `trim()` and `to_lower()` for user input

## Common Pitfalls

### Pitfall: Byte vs. Codepoint Confusion

```hemlock
let emoji = "🚀";
print(emoji.length);        // 1 (codepoint)
print(emoji.byte_length);   // 4 (bytes)

// Don't mix byte and codepoint operations
let byte = emoji.byte_at(0);  // 240 (first byte)
let char = emoji.char_at(0);  // '🚀' (full codepoint)
```

### Pitfall: Mutation Surprises

```hemlock
let s1 = "hello";
let s2 = s1;       // Shallow copy
s1[0] = 'H';       // Mutates s1
print(s2);         // Still "hello" (strings are value types)
```

## Related Topics

- [Runes](runes.md) - Unicode codepoint type used in string indexing
- [Arrays](arrays.md) - String methods often return or work with arrays
- [Types](types.md) - String type details and conversions

## See Also

- **UTF-8 Encoding**: See CLAUDE.md section "Strings"
- **Type Conversions**: See [Types](types.md) for string conversions
- **Memory**: See [Memory](memory.md) for string allocation details
