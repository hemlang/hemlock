# String API Reference

Complete reference for Hemlock's string type and all 19 string methods.

---

## Overview

Strings in Hemlock are **UTF-8 encoded, mutable, heap-allocated** sequences with full Unicode support. All operations work with **codepoints** (characters), not bytes.

**Key Features:**
- UTF-8 encoding (U+0000 to U+10FFFF)
- Mutable (can modify characters in place)
- Codepoint-based indexing
- 19 built-in methods
- Automatic concatenation with `+` operator

---

## String Type

**Type:** `string`

**Properties:**
- `.length` - Number of codepoints (characters)
- `.byte_length` - Number of UTF-8 bytes

**Literal Syntax:** Double quotes `"text"`

**Examples:**
```hemlock
let s = "hello";
print(s.length);        // 5 (codepoints)
print(s.byte_length);   // 5 (bytes)

let emoji = "🚀";
print(emoji.length);        // 1 (one codepoint)
print(emoji.byte_length);   // 4 (four UTF-8 bytes)
```

---

## Indexing

Strings support codepoint-based indexing using `[]`:

**Read Access:**
```hemlock
let s = "hello";
let ch = s[0];          // Returns rune 'h'
```

**Write Access:**
```hemlock
let s = "hello";
s[0] = 'H';             // Mutate with rune (now "Hello")
```

**UTF-8 Example:**
```hemlock
let text = "Hi🚀!";
print(text[0]);         // 'H'
print(text[1]);         // 'i'
print(text[2]);         // '🚀' (one codepoint)
print(text[3]);         // '!'
```

---

## Concatenation

Use the `+` operator to concatenate strings and runes:

**String + String:**
```hemlock
let s = "hello" + " " + "world";  // "hello world"
let msg = "Count: " + typeof(42); // "Count: 42"
```

**String + Rune:**
```hemlock
let greeting = "Hello" + '!';      // "Hello!"
let decorated = "Text" + '✓';      // "Text✓"
```

**Rune + String:**
```hemlock
let prefix = '>' + " Message";     // "> Message"
let bullet = '•' + " Item";        // "• Item"
```

**Multiple Concatenations:**
```hemlock
let msg = "Hi " + '👋' + " World " + '🌍';  // "Hi 👋 World 🌍"
```

---

## String Properties

### .length

Get the number of Unicode codepoints (characters).

**Type:** `i32`

**Examples:**
```hemlock
let s = "hello";
print(s.length);        // 5

let emoji = "🚀";
print(emoji.length);    // 1 (one codepoint)

let text = "Hello 🌍!";
print(text.length);     // 8 (7 ASCII + 1 emoji)
```

---

### .byte_length

Get the number of UTF-8 bytes.

**Type:** `i32`

**Examples:**
```hemlock
let s = "hello";
print(s.byte_length);   // 5 (1 byte per ASCII char)

let emoji = "🚀";
print(emoji.byte_length); // 4 (emoji is 4 UTF-8 bytes)

let text = "Hello 🌍!";
print(text.byte_length);  // 11 (7 ASCII + 4 for emoji)
```

---

## String Methods

### Substring & Slicing

#### substr

Extract substring by position and length.

**Signature:**
```hemlock
string.substr(start: i32, length: i32): string
```

**Parameters:**
- `start` - Starting codepoint index (0-based)
- `length` - Number of codepoints to extract

**Returns:** New string

**Examples:**
```hemlock
let s = "hello world";
let sub = s.substr(6, 5);       // "world"
let first = s.substr(0, 5);     // "hello"

// UTF-8 example
let text = "Hi🚀!";
let emoji = text.substr(2, 1);  // "🚀"
```

---

#### slice

Extract substring by range (end exclusive).

**Signature:**
```hemlock
string.slice(start: i32, end: i32): string
```

**Parameters:**
- `start` - Starting codepoint index (0-based)
- `end` - Ending codepoint index (exclusive)

**Returns:** New string

**Examples:**
```hemlock
let s = "hello world";
let sub = s.slice(0, 5);        // "hello"
let world = s.slice(6, 11);     // "world"

// UTF-8 example
let text = "Hi🚀!";
let first_three = text.slice(0, 3);  // "Hi🚀"
```

---

### Search & Find

#### find

Find first occurrence of substring.

**Signature:**
```hemlock
string.find(needle: string): i32
```

**Parameters:**
- `needle` - Substring to search for

**Returns:** Codepoint index of first occurrence, or `-1` if not found

**Examples:**
```hemlock
let s = "hello world";
let pos = s.find("world");      // 6
let pos2 = s.find("foo");       // -1 (not found)
let pos3 = s.find("l");         // 2 (first 'l')
```

---

#### contains

Check if string contains substring.

**Signature:**
```hemlock
string.contains(needle: string): bool
```

**Parameters:**
- `needle` - Substring to search for

**Returns:** `true` if found, `false` otherwise

**Examples:**
```hemlock
let s = "hello world";
let has = s.contains("world");  // true
let has2 = s.contains("foo");   // false
```

---

### Split & Join

#### split

Split string into array by delimiter.

**Signature:**
```hemlock
string.split(delimiter: string): array
```

**Parameters:**
- `delimiter` - String to split on

**Returns:** Array of strings

**Examples:**
```hemlock
let csv = "a,b,c";
let parts = csv.split(",");     // ["a", "b", "c"]

let path = "/usr/local/bin";
let dirs = path.split("/");     // ["", "usr", "local", "bin"]

let text = "hello world foo";
let words = text.split(" ");    // ["hello", "world", "foo"]
```

---

#### trim

Remove leading and trailing whitespace.

**Signature:**
```hemlock
string.trim(): string
```

**Returns:** New string with whitespace removed

**Examples:**
```hemlock
let s = "  hello  ";
let clean = s.trim();           // "hello"

let text = "\n\t  world  \n";
let clean2 = text.trim();       // "world"
```

---

### Case Conversion

#### to_upper

Convert string to uppercase.

**Signature:**
```hemlock
string.to_upper(): string
```

**Returns:** New string in uppercase

**Examples:**
```hemlock
let s = "hello world";
let upper = s.to_upper();       // "HELLO WORLD"

let mixed = "HeLLo";
let upper2 = mixed.to_upper();  // "HELLO"
```

---

#### to_lower

Convert string to lowercase.

**Signature:**
```hemlock
string.to_lower(): string
```

**Returns:** New string in lowercase

**Examples:**
```hemlock
let s = "HELLO WORLD";
let lower = s.to_lower();       // "hello world"

let mixed = "HeLLo";
let lower2 = mixed.to_lower();  // "hello"
```

---

### Prefix & Suffix

#### starts_with

Check if string starts with prefix.

**Signature:**
```hemlock
string.starts_with(prefix: string): bool
```

**Parameters:**
- `prefix` - Prefix to check

**Returns:** `true` if string starts with prefix, `false` otherwise

**Examples:**
```hemlock
let s = "hello world";
let starts = s.starts_with("hello");  // true
let starts2 = s.starts_with("world"); // false
```

---

#### ends_with

Check if string ends with suffix.

**Signature:**
```hemlock
string.ends_with(suffix: string): bool
```

**Parameters:**
- `suffix` - Suffix to check

**Returns:** `true` if string ends with suffix, `false` otherwise

**Examples:**
```hemlock
let s = "hello world";
let ends = s.ends_with("world");      // true
let ends2 = s.ends_with("hello");     // false
```

---

### Replacement

#### replace

Replace first occurrence of substring.

**Signature:**
```hemlock
string.replace(old: string, new: string): string
```

**Parameters:**
- `old` - Substring to replace
- `new` - Replacement string

**Returns:** New string with first occurrence replaced

**Examples:**
```hemlock
let s = "hello world";
let s2 = s.replace("world", "there");  // "hello there"

let text = "foo foo foo";
let text2 = text.replace("foo", "bar"); // "bar foo foo" (only first)
```

---

#### replace_all

Replace all occurrences of substring.

**Signature:**
```hemlock
string.replace_all(old: string, new: string): string
```

**Parameters:**
- `old` - Substring to replace
- `new` - Replacement string

**Returns:** New string with all occurrences replaced

**Examples:**
```hemlock
let text = "foo foo foo";
let text2 = text.replace_all("foo", "bar"); // "bar bar bar"

let s = "hello world hello";
let s2 = s.replace_all("hello", "hi");      // "hi world hi"
```

---

### Repetition

#### repeat

Repeat string n times.

**Signature:**
```hemlock
string.repeat(count: i32): string
```

**Parameters:**
- `count` - Number of repetitions

**Returns:** New string repeated count times

**Examples:**
```hemlock
let s = "ha";
let repeated = s.repeat(3);     // "hahaha"

let line = "-";
let separator = line.repeat(40); // "----------------------------------------"
```

---

### Character Access

#### char_at

Get Unicode codepoint at index.

**Signature:**
```hemlock
string.char_at(index: i32): rune
```

**Parameters:**
- `index` - Codepoint index (0-based)

**Returns:** Rune (Unicode codepoint)

**Examples:**
```hemlock
let s = "hello";
let ch = s.char_at(0);          // 'h'
let ch2 = s.char_at(1);         // 'e'

// UTF-8 example
let emoji = "🚀";
let ch3 = emoji.char_at(0);     // U+1F680 (rocket)
```

---

#### chars

Convert string to array of runes.

**Signature:**
```hemlock
string.chars(): array
```

**Returns:** Array of runes (codepoints)

**Examples:**
```hemlock
let s = "hello";
let chars = s.chars();          // ['h', 'e', 'l', 'l', 'o']

// UTF-8 example
let text = "Hi🚀!";
let chars2 = text.chars();      // ['H', 'i', '🚀', '!']
```

---

### Byte Access

#### byte_at

Get byte value at index.

**Signature:**
```hemlock
string.byte_at(index: i32): u8
```

**Parameters:**
- `index` - Byte index (0-based, NOT codepoint index)

**Returns:** Byte value (u8)

**Examples:**
```hemlock
let s = "hello";
let byte = s.byte_at(0);        // 104 (ASCII 'h')
let byte2 = s.byte_at(1);       // 101 (ASCII 'e')

// UTF-8 example
let emoji = "🚀";
let byte3 = emoji.byte_at(0);   // 240 (first UTF-8 byte)
```

---

#### bytes

Convert string to array of bytes.

**Signature:**
```hemlock
string.bytes(): array
```

**Returns:** Array of u8 bytes

**Examples:**
```hemlock
let s = "hello";
let bytes = s.bytes();          // [104, 101, 108, 108, 111]

// UTF-8 example
let emoji = "🚀";
let bytes2 = emoji.bytes();     // [240, 159, 154, 128] (4 UTF-8 bytes)
```

---

#### to_bytes

Convert string to buffer.

**Signature:**
```hemlock
string.to_bytes(): buffer
```

**Returns:** Buffer containing UTF-8 bytes

**Examples:**
```hemlock
let s = "hello";
let buf = s.to_bytes();
print(buf.length);              // 5

// UTF-8 example
let emoji = "🚀";
let buf2 = emoji.to_bytes();
print(buf2.length);             // 4
```

**Note:** This is a legacy method. Prefer `.bytes()` for most use cases.

---

### JSON Deserialization

#### deserialize

Parse JSON string to value.

**Signature:**
```hemlock
string.deserialize(): any
```

**Returns:** Parsed value (object, array, number, string, bool, or null)

**Examples:**
```hemlock
let json = '{"x":10,"y":20}';
let obj = json.deserialize();
print(obj.x);                   // 10
print(obj.y);                   // 20

let arr_json = '[1,2,3]';
let arr = arr_json.deserialize();
print(arr[0]);                  // 1

let num_json = '42';
let num = num_json.deserialize();
print(num);                     // 42
```

**Supported Types:**
- Objects: `{"key": value}`
- Arrays: `[1, 2, 3]`
- Numbers: `42`, `3.14`
- Strings: `"text"`
- Booleans: `true`, `false`
- Null: `null`

**See Also:** Object `.serialize()` method

---

## Method Chaining

String methods can be chained for concise operations:

**Examples:**
```hemlock
let result = "  Hello World  "
    .trim()
    .to_lower()
    .replace("world", "hemlock");  // "hello hemlock"

let processed = "foo,bar,baz"
    .split(",")
    .join(" | ");                  // "foo | bar | baz"

let cleaned = "  HELLO  "
    .trim()
    .to_lower();                   // "hello"
```

---

## Complete Method Summary

| Method         | Signature                                    | Returns   | Description                           |
|----------------|----------------------------------------------|-----------|---------------------------------------|
| `substr`       | `(start: i32, length: i32)`                  | `string`  | Extract substring by position/length  |
| `slice`        | `(start: i32, end: i32)`                     | `string`  | Extract substring by range            |
| `find`         | `(needle: string)`                           | `i32`     | Find first occurrence (-1 if not found)|
| `contains`     | `(needle: string)`                           | `bool`    | Check if contains substring           |
| `split`        | `(delimiter: string)`                        | `array`   | Split into array                      |
| `trim`         | `()`                                         | `string`  | Remove whitespace                     |
| `to_upper`     | `()`                                         | `string`  | Convert to uppercase                  |
| `to_lower`     | `()`                                         | `string`  | Convert to lowercase                  |
| `starts_with`  | `(prefix: string)`                           | `bool`    | Check if starts with prefix           |
| `ends_with`    | `(suffix: string)`                           | `bool`    | Check if ends with suffix             |
| `replace`      | `(old: string, new: string)`                 | `string`  | Replace first occurrence              |
| `replace_all`  | `(old: string, new: string)`                 | `string`  | Replace all occurrences               |
| `repeat`       | `(count: i32)`                               | `string`  | Repeat string n times                 |
| `char_at`      | `(index: i32)`                               | `rune`    | Get codepoint at index                |
| `byte_at`      | `(index: i32)`                               | `u8`      | Get byte at index                     |
| `chars`        | `()`                                         | `array`   | Convert to array of runes             |
| `bytes`        | `()`                                         | `array`   | Convert to array of bytes             |
| `to_bytes`     | `()`                                         | `buffer`  | Convert to buffer (legacy)            |
| `deserialize`  | `()`                                         | `any`     | Parse JSON string                     |

---

## See Also

- [Type System](type-system.md) - String type details
- [Array API](array-api.md) - Array methods for split() results
- [Operators](operators.md) - String concatenation operator
