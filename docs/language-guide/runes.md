# Runes

Runes represent **Unicode codepoints** (U+0000 to U+10FFFF) as a distinct type for character manipulation in Hemlock. Unlike bytes (u8), runes are full Unicode characters that can represent any character in any language or emoji.

## Overview

```hemlock
let ch = 'A';           // Rune literal
let emoji = '🚀';       // Multi-byte character as single rune
print(ch);              // 'A'
print(emoji);           // U+1F680

let s = "Hello " + '!'; // String + rune concatenation
let r = '>' + " msg";   // Rune + string concatenation
```

## What is a Rune?

A rune is a **32-bit value** representing a Unicode codepoint:

- **Range:** 0 to 0x10FFFF (1,114,111 valid codepoints)
- **Not a numeric type** - Used for character representation
- **Distinct from u8/char** - Runes are full Unicode, u8 is just bytes
- **Returned by string indexing** - `str[0]` returns a rune, not a byte

**Why runes?**
- Hemlock strings are UTF-8 encoded
- A single Unicode character can be 1-4 bytes in UTF-8
- Runes allow working with complete characters, not partial bytes

## Rune Literals

### Basic Syntax

Single quotes denote rune literals:

```hemlock
let a = 'A';            // ASCII character
let b = '0';            // Digit character
let c = '!';            // Punctuation
let d = ' ';            // Space
```

### Multi-byte UTF-8 Characters

Runes can represent any Unicode character:

```hemlock
// Emoji
let rocket = '🚀';      // Emoji (U+1F680)
let heart = '❤';        // Heart (U+2764)
let smile = '😀';       // Grinning face (U+1F600)

// CJK characters
let chinese = '中';     // Chinese (U+4E2D)
let japanese = 'あ';    // Hiragana (U+3042)
let korean = '한';      // Hangul (U+D55C)

// Symbols
let check = '✓';        // Checkmark (U+2713)
let arrow = '→';        // Rightwards arrow (U+2192)
```

### Escape Sequences

Common escape sequences for special characters:

```hemlock
let newline = '\n';     // Newline (U+000A)
let tab = '\t';         // Tab (U+0009)
let backslash = '\\';   // Backslash (U+005C)
let quote = '\'';       // Single quote (U+0027)
let dquote = '"';       // Double quote (U+0022)
let null_char = '\0';   // Null character (U+0000)
let cr = '\r';          // Carriage return (U+000D)
```

**Available escape sequences:**
- `\n` - Newline (line feed)
- `\t` - Horizontal tab
- `\r` - Carriage return
- `\0` - Null character
- `\\` - Backslash
- `\'` - Single quote
- `\"` - Double quote

### Unicode Escapes

Use `\u{XXXXXX}` syntax for Unicode codepoints (up to 6 hex digits):

```hemlock
let rocket = '\u{1F680}';   // 🚀 Emoji via Unicode escape
let heart = '\u{2764}';     // ❤ Heart
let ascii = '\u{41}';       // 'A' via escape
let max = '\u{10FFFF}';     // Maximum Unicode codepoint

// Leading zeros optional
let a = '\u{41}';           // Same as '\u{0041}'
let b = '\u{0041}';
```

**Rules:**
- Range: `\u{0}` to `\u{10FFFF}`
- Hex digits: 1 to 6 digits
- Case insensitive: `\u{1F680}` or `\u{1f680}`
- Values outside valid Unicode range cause error

## String + Rune Concatenation

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

// Method chaining works
let result = ('>' + " Important").to_upper();  // "> IMPORTANT"
```

**How it works:**
- Runes are automatically encoded to UTF-8
- Converted to strings during concatenation
- The string concatenation operator handles this transparently

## Type Conversions

Runes can convert to/from other types.

### Integer ↔ Rune

Convert between integers and runes to work with codepoint values:

```hemlock
// Integer to rune (codepoint value)
let code: rune = 65;            // 'A' (ASCII 65)
let emoji_code: rune = 128640;  // U+1F680 (🚀)

// Rune to integer (get codepoint value)
let r = 'Z';
let value: i32 = r;             // 90 (ASCII value)

let rocket = '🚀';
let code: i32 = rocket;         // 128640 (U+1F680)
```

**Range checking:**
- Integer to rune: Must be in [0, 0x10FFFF]
- Out of range values cause runtime error
- Rune to integer: Always succeeds (returns codepoint)

### Rune → String

Runes can be explicitly converted to strings:

```hemlock
// Explicit conversion
let ch: string = 'H';           // "H"
let emoji: string = '🚀';       // "🚀"

// Automatic during concatenation
let s = "" + 'A';               // "A"
let s2 = "x" + 'y' + "z";       // "xyz"
```

### u8 (Byte) → Rune

Any u8 value (0-255) can convert to rune:

```hemlock
// ASCII range (0-127)
let byte: u8 = 65;
let rune_val: rune = byte;      // 'A'

// Extended ASCII / Latin-1 (128-255)
let extended: u8 = 200;
let r: rune = extended;         // U+00C8 (È)

// Note: Values 0-127 are ASCII, 128-255 are Latin-1
```

### Chained Conversions

Type conversions can be chained:

```hemlock
// i32 → rune → string
let code: i32 = 128512;         // Grinning face codepoint
let r: rune = code;             // 😀
let s: string = r;              // "😀"

// All in one expression
let emoji: string = 128640;     // Implicit i32 → rune → string (🚀)
```

## Rune Operations

### Printing

How runes are displayed depends on the codepoint:

```hemlock
let ascii = 'A';
print(ascii);                   // 'A' (quoted, printable ASCII)

let emoji = '🚀';
print(emoji);                   // U+1F680 (Unicode notation for non-ASCII)

let tab = '\t';
print(tab);                     // U+0009 (non-printable as hex)

let space = ' ';
print(space);                   // ' ' (printable)
```

**Print format:**
- Printable ASCII (32-126): Quoted character `'A'`
- Non-printable or Unicode: Hex notation `U+XXXX`

### Type Checking

Use `typeof()` to check if a value is a rune:

```hemlock
let r = '🚀';
print(typeof(r));               // "rune"

let s = "text";
let ch = s[0];
print(typeof(ch));              // "rune" (indexing returns runes)

let num = 65;
print(typeof(num));             // "i32"
```

### Comparison

Runes can be compared for equality:

```hemlock
let a = 'A';
let b = 'B';
print(a == a);                  // true
print(a == b);                  // false

// Case sensitive
let upper = 'A';
let lower = 'a';
print(upper == lower);          // false

// Runes can be compared with integers (codepoint values)
print(a == 65);                 // true (implicit conversion)
print('🚀' == 128640);          // true
```

**Comparison operators:**
- `==` - Equal
- `!=` - Not equal
- `<`, `>`, `<=`, `>=` - Codepoint order

```hemlock
print('A' < 'B');               // true (65 < 66)
print('a' > 'Z');               // true (97 > 90)
```

## Working with String Indexing

String indexing returns runes, not bytes:

```hemlock
let s = "Hello🚀";
let h = s[0];                   // 'H' (rune)
let rocket = s[5];              // '🚀' (rune)

print(typeof(h));               // "rune"
print(typeof(rocket));          // "rune"

// Convert to string if needed
let h_str: string = h;          // "H"
let rocket_str: string = rocket; // "🚀"
```

**Important:** String indexing uses codepoint positions, not byte offsets:

```hemlock
let text = "Hi🚀!";
// Codepoint positions: 0='H', 1='i', 2='🚀', 3='!'
// Byte positions:      0='H', 1='i', 2-5='🚀', 6='!'

let r = text[2];                // '🚀' (codepoint 2)
print(typeof(r));               // "rune"
```

## Examples

### Example: Character Classification

```hemlock
fn is_digit(r: rune): bool {
    return r >= '0' && r <= '9';
}

fn is_upper(r: rune): bool {
    return r >= 'A' && r <= 'Z';
}

fn is_lower(r: rune): bool {
    return r >= 'a' && r <= 'z';
}

print(is_digit('5'));           // true
print(is_upper('A'));           // true
print(is_lower('z'));           // true
```

### Example: Case Conversion

```hemlock
fn to_upper_rune(r: rune): rune {
    if (r >= 'a' && r <= 'z') {
        // Convert to uppercase (subtract 32)
        let code: i32 = r;
        code = code - 32;
        return code;
    }
    return r;
}

fn to_lower_rune(r: rune): rune {
    if (r >= 'A' && r <= 'Z') {
        // Convert to lowercase (add 32)
        let code: i32 = r;
        code = code + 32;
        return code;
    }
    return r;
}

print(to_upper_rune('a'));      // 'A'
print(to_lower_rune('Z'));      // 'z'
```

### Example: Character Iteration

```hemlock
fn print_chars(s: string) {
    let i = 0;
    while (i < s.length) {
        let ch = s[i];
        print("Position " + i + ": " + ch);
        i = i + 1;
    }
}

print_chars("Hi🚀");
// Position 0: H
// Position 1: i
// Position 2: 🚀
```

### Example: Building Strings from Runes

```hemlock
fn repeat_char(ch: rune, count: i32): string {
    let result = "";
    let i = 0;
    while (i < count) {
        result = result + ch;
        i = i + 1;
    }
    return result;
}

let line = repeat_char('=', 40);  // "========================================"
let stars = repeat_char('⭐', 5);  // "⭐⭐⭐⭐⭐"
```

## Common Patterns

### Pattern: Character Filter

```hemlock
fn filter_digits(s: string): string {
    let result = "";
    let i = 0;
    while (i < s.length) {
        let ch = s[i];
        if (ch >= '0' && ch <= '9') {
            result = result + ch;
        }
        i = i + 1;
    }
    return result;
}

let text = "abc123def456";
let digits = filter_digits(text);  // "123456"
```

### Pattern: Character Counting

```hemlock
fn count_char(s: string, target: rune): i32 {
    let count = 0;
    let i = 0;
    while (i < s.length) {
        if (s[i] == target) {
            count = count + 1;
        }
        i = i + 1;
    }
    return count;
}

let text = "hello world";
let l_count = count_char(text, 'l');  // 3
let o_count = count_char(text, 'o');  // 2
```

## Best Practices

1. **Use runes for character operations** - Don't try to work with bytes for text
2. **String indexing returns runes** - Remember that `str[i]` gives you a rune
3. **Unicode-aware comparisons** - Runes handle any Unicode character
4. **Convert when needed** - Runes convert easily to strings and integers
5. **Test with emoji** - Always test character operations with multi-byte characters

## Common Pitfalls

### Pitfall: Rune vs. Byte Confusion

```hemlock
// DON'T: Treat runes as bytes
let r: rune = '🚀';
let b: u8 = r;              // ERROR: Rune codepoint 128640 doesn't fit in u8

// DO: Use appropriate conversions
let r: rune = '🚀';
let code: i32 = r;          // OK: 128640
```

### Pitfall: String Byte Indexing

```hemlock
// DON'T: Assume byte indexing
let s = "🚀";
let byte = s.byte_at(0);    // 240 (first UTF-8 byte, not complete char)

// DO: Use codepoint indexing
let s = "🚀";
let rune = s[0];            // '🚀' (complete character)
let rune2 = s.char_at(0);   // '🚀' (explicit method)
```

## Related Topics

- [Strings](strings.md) - String operations and UTF-8 handling
- [Types](types.md) - Type system and conversions
- [Control Flow](control-flow.md) - Using runes in comparisons

## See Also

- **Unicode Standard**: Unicode codepoints are defined by the Unicode Consortium
- **UTF-8 Encoding**: See [Strings](strings.md) for UTF-8 details
- **Type Conversions**: See [Types](types.md) for conversion rules
