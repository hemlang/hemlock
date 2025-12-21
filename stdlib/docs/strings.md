# Hemlock Strings Module

A standard library module providing advanced string utilities beyond the 18 built-in string methods.

## Overview

The strings module provides utilities for:

- **Padding & Alignment** - pad_left, pad_right, center
- **Character Type Checking** - is_alpha, is_digit, is_alnum, is_whitespace
- **String Manipulation** - reverse, lines, words
- **Case Conversion** - snake_case, camel_case, pascal_case, kebab_case
- **URL/Identifier** - slugify, truncate

These functions complement the built-in string methods (substr, slice, find, split, trim, to_upper, to_lower, etc.) with commonly-needed string operations.

## Usage

```hemlock
import { pad_left, pad_right, center } from "@stdlib/strings";
import { is_alpha, is_digit, is_alnum, is_whitespace } from "@stdlib/strings";
import { reverse, lines, words } from "@stdlib/strings";
import { snake_case, camel_case, pascal_case, kebab_case } from "@stdlib/strings";
import { slugify, truncate } from "@stdlib/strings";
```

Or import all:

```hemlock
import * as strings from "@stdlib/strings";
let result = strings.reverse("hello");
```

---

## Padding & Alignment

### pad_left(str, width, fill?)

Pad string on the left to reach target width.

**Parameters:**
- `str` - Input string
- `width: i32` - Target width (in Unicode codepoints, not bytes)
- `fill?: string` - Fill character (default: space `" "`)

**Returns:** `string` - Padded string

**Notes:**
- If string length >= width, returns original string unchanged
- Fill must be exactly one character (one Unicode codepoint)
- Padding length is calculated in codepoints (emoji = 1 character)

```hemlock
import { pad_left } from "@stdlib/strings";

let s1 = pad_left("42", 5);
print(s1);  // "   42"

let s2 = pad_left("42", 5, "0");
print(s2);  // "00042"

let s3 = pad_left("test", 10, "*");
print(s3);  // "******test"

// Unicode fill character
let s4 = pad_left("Hi", 5, "🚀");
print(s4);  // "🚀🚀🚀Hi"
print(s4.length);  // 5 (codepoints)
```

---

### pad_right(str, width, fill?)

Pad string on the right to reach target width.

**Parameters:**
- `str` - Input string
- `width: i32` - Target width (in Unicode codepoints)
- `fill?: string` - Fill character (default: space `" "`)

**Returns:** `string` - Padded string

```hemlock
import { pad_right } from "@stdlib/strings";

let s1 = pad_right("42", 5);
print(s1);  // "42   "

let s2 = pad_right("42", 5, "0");
print(s2);  // "42000"

// Unicode fill
let s3 = pad_right("test", 10, "─");
print(s3);  // "test──────"
```

---

### center(str, width, fill?)

Center string within target width.

**Parameters:**
- `str` - Input string
- `width: i32` - Target width (in Unicode codepoints)
- `fill?: string` - Fill character (default: space `" "`)

**Returns:** `string` - Centered string

**Notes:**
- If total padding is odd, extra padding goes on the right
- Useful for creating headers, banners, and aligned text

```hemlock
import { center } from "@stdlib/strings";

let s1 = center("Title", 10);
print(s1);  // "  Title   " (2 left, 3 right)

let s2 = center("X", 5, "*");
print(s2);  // "**X**"

// Create banner
let banner = center("IMPORTANT", 40, "=");
print(banner);  // "===============IMPORTANT================"

// Unicode fill
let s3 = center("test", 10, "─");
print(s3);  // "───test───"
```

---

## Character Type Checking

All character type functions:
- Return `false` for empty strings
- Work with ASCII characters only (a-z, A-Z, 0-9)
- Check **all** characters in the string (not just first)
- Return `bool` result

### is_alpha(str)

Check if string contains only alphabetic characters (a-z, A-Z).

**Parameters:**
- `str` - Input string

**Returns:** `bool` - True if all characters are alphabetic

```hemlock
import { is_alpha } from "@stdlib/strings";

print(is_alpha("hello"));         // true
print(is_alpha("HELLO"));         // true
print(is_alpha("HelloWorld"));    // true

print(is_alpha("hello123"));      // false
print(is_alpha("hello world"));   // false (space)
print(is_alpha("hello!"));        // false
print(is_alpha(""));              // false (empty)
```

---

### is_digit(str)

Check if string contains only digit characters (0-9).

**Parameters:**
- `str` - Input string

**Returns:** `bool` - True if all characters are digits

```hemlock
import { is_digit } from "@stdlib/strings";

print(is_digit("123"));      // true
print(is_digit("0"));        // true
print(is_digit("999"));      // true

print(is_digit("123a"));     // false
print(is_digit("12.3"));     // false (decimal point)
print(is_digit("1 2 3"));    // false (spaces)
print(is_digit(""));         // false (empty)
```

---

### is_alnum(str)

Check if string contains only alphanumeric characters (a-z, A-Z, 0-9).

**Parameters:**
- `str` - Input string

**Returns:** `bool` - True if all characters are alphanumeric

```hemlock
import { is_alnum } from "@stdlib/strings";

print(is_alnum("hello123"));  // true
print(is_alnum("ABC123"));    // true
print(is_alnum("test"));      // true
print(is_alnum("123"));       // true
print(is_alnum("a1b2c3"));    // true

print(is_alnum("hello world"));  // false (space)
print(is_alnum("test!"));        // false (punctuation)
print(is_alnum("a-b"));          // false (hyphen)
print(is_alnum(""));             // false (empty)
```

**Common use case - validating identifiers:**

```hemlock
import { is_alnum } from "@stdlib/strings";

fn is_valid_identifier(name: string): bool {
    if (name.length == 0) {
        return false;
    }
    // First char must be alpha, rest can be alnum
    let first = name.slice(0, 1);
    if (!is_alpha(first)) {
        return false;
    }
    return is_alnum(name);
}

print(is_valid_identifier("myVar123"));  // true
print(is_valid_identifier("123var"));    // false
```

---

### is_whitespace(str)

Check if string contains only whitespace characters (space, tab, newline, carriage return).

**Parameters:**
- `str` - Input string

**Returns:** `bool` - True if all characters are whitespace

**Recognized whitespace:**
- Space (U+0020)
- Tab (U+0009)
- Newline (U+000A)
- Carriage return (U+000D)

```hemlock
import { is_whitespace } from "@stdlib/strings";

print(is_whitespace(" "));       // true
print(is_whitespace("   "));     // true
print(is_whitespace("\t"));      // true
print(is_whitespace("\n"));      // true
print(is_whitespace("\r"));      // true
print(is_whitespace(" \t\n"));   // true

print(is_whitespace("hello"));   // false
print(is_whitespace(" a "));     // false
print(is_whitespace(""));        // false (empty)
```

---

## String Manipulation

### reverse(str)

Reverse a string (works with Unicode codepoints).

**Parameters:**
- `str` - Input string

**Returns:** `string` - Reversed string

**Notes:**
- Reverses by Unicode codepoints (not bytes)
- Correctly handles emojis and multi-byte characters
- O(n) time complexity where n = string length

```hemlock
import { reverse } from "@stdlib/strings";

print(reverse("hello"));       // "olleh"
print(reverse("abc"));         // "cba"
print(reverse("12345"));       // "54321"

// Single character
print(reverse("a"));           // "a"

// Empty string
print(reverse(""));            // ""

// Palindrome
print(reverse("racecar"));     // "racecar"

// With spaces
print(reverse("hello world")); // "dlrow olleh"

// Unicode / emojis
print(reverse("🚀🌍"));        // "🌍🚀"
print(reverse("Hi🚀"));        // "🚀iH"
```

**Use case - palindrome checking:**

```hemlock
import { reverse } from "@stdlib/strings";

fn is_palindrome(s: string): bool {
    let normalized = s.to_lower();
    return normalized == reverse(normalized);
}

print(is_palindrome("racecar"));  // true
print(is_palindrome("Racecar"));  // true
print(is_palindrome("hello"));    // false
```

---

### lines(str)

Split string into lines by newline characters.

**Parameters:**
- `str` - Input string

**Returns:** `array` - Array of strings (one per line)

**Notes:**
- Splits on `\n` (newline) character
- Empty lines are preserved as empty strings
- Does not remove newlines from individual lines (they are split points)
- Returns array with one element (the string itself) if no newlines

```hemlock
import { lines } from "@stdlib/strings";

// Basic line splitting
let l1 = lines("hello\nworld");
print(l1.length);   // 2
print(l1[0]);       // "hello"
print(l1[1]);       // "world"

// Multiple lines
let l2 = lines("line1\nline2\nline3");
print(l2.length);   // 3

// Empty lines are preserved
let l3 = lines("a\n\nb");
print(l3.length);   // 3
print(l3[1]);       // "" (empty line)

// Single line (no newline)
let l4 = lines("hello");
print(l4.length);   // 1
print(l4[0]);       // "hello"
```

**Use case - processing multi-line input:**

```hemlock
import { lines } from "@stdlib/strings";

let text = "Alice\nBob\nCarol\nDave";
let names = lines(text);

let i = 0;
while (i < names.length) {
    print("User " + typeof(i + 1) + ": " + names[i]);
    i = i + 1;
}
// Output:
// User 1: Alice
// User 2: Bob
// User 3: Carol
// User 4: Dave
```

---

### words(str)

Split string into words by whitespace.

**Parameters:**
- `str` - Input string

**Returns:** `array` - Array of strings (non-empty words)

**Notes:**
- Splits on space character only (not tabs or newlines)
- Filters out empty strings (multiple spaces create single split)
- Trims each word (leading/trailing whitespace removed)
- Returns empty array for empty string or whitespace-only string

```hemlock
import { words } from "@stdlib/strings";

// Basic word splitting
let w1 = words("hello world");
print(w1.length);   // 2
print(w1[0]);       // "hello"
print(w1[1]);       // "world"

// Multiple words
let w2 = words("the quick brown fox");
print(w2.length);   // 4

// Extra spaces (filtered out)
let w3 = words("hello  world");
print(w3.length);   // 2 (not 3)

// Leading/trailing spaces
let w4 = words("  hello world  ");
print(w4.length);   // 2

// Single word
let w5 = words("hello");
print(w5.length);   // 1

// Empty string
let w6 = words("");
print(w6.length);   // 0

// Only spaces
let w7 = words("   ");
print(w7.length);   // 0
```

**Use case - word counting:**

```hemlock
import { words } from "@stdlib/strings";

fn word_count(text: string): i32 {
    return words(text).length;
}

let text = "The quick brown fox jumps over the lazy dog";
print(word_count(text));  // 9
```

---

## Case Conversion

These functions convert between different casing conventions commonly used in programming.

### snake_case(str)

Convert string to snake_case (lowercase with underscores).

**Parameters:**
- `str` - Input string (camelCase, PascalCase, kebab-case, or with spaces)

**Returns:** `string` - snake_case string

```hemlock
import { snake_case } from "@stdlib/strings";

print(snake_case("helloWorld"));     // "hello_world"
print(snake_case("HelloWorld"));     // "hello_world"
print(snake_case("hello-world"));    // "hello_world"
print(snake_case("Hello World"));    // "hello_world"
print(snake_case("XMLParser"));      // "x_m_l_parser"
```

---

### camel_case(str)

Convert string to camelCase (first word lowercase, subsequent words capitalized).

**Parameters:**
- `str` - Input string (snake_case, kebab-case, or with spaces)

**Returns:** `string` - camelCase string

```hemlock
import { camel_case } from "@stdlib/strings";

print(camel_case("hello_world"));    // "helloWorld"
print(camel_case("hello-world"));    // "helloWorld"
print(camel_case("Hello World"));    // "helloWorld"
print(camel_case("HELLO_WORLD"));    // "helloWorld"
```

---

### pascal_case(str)

Convert string to PascalCase (UpperCamelCase - all words capitalized).

**Parameters:**
- `str` - Input string (snake_case, kebab-case, or with spaces)

**Returns:** `string` - PascalCase string

```hemlock
import { pascal_case } from "@stdlib/strings";

print(pascal_case("hello_world"));   // "HelloWorld"
print(pascal_case("hello-world"));   // "HelloWorld"
print(pascal_case("hello world"));   // "HelloWorld"
```

---

### kebab_case(str)

Convert string to kebab-case (lowercase with hyphens).

**Parameters:**
- `str` - Input string (camelCase, PascalCase, or with spaces/underscores)

**Returns:** `string` - kebab-case string

```hemlock
import { kebab_case } from "@stdlib/strings";

print(kebab_case("helloWorld"));     // "hello-world"
print(kebab_case("HelloWorld"));     // "hello-world"
print(kebab_case("hello_world"));    // "hello-world"
print(kebab_case("Hello World"));    // "hello-world"
```

---

## URL/Identifier Utilities

### slugify(str)

Convert string to a URL-friendly slug.

**Parameters:**
- `str` - Input string

**Returns:** `string` - URL-safe slug (lowercase, hyphens, ASCII only)

**Notes:**
- Converts to lowercase
- Replaces spaces, dashes, underscores with hyphens
- Removes non-ASCII characters
- Collapses multiple hyphens into one
- Removes leading/trailing hyphens

```hemlock
import { slugify } from "@stdlib/strings";

print(slugify("Hello World!"));      // "hello-world"
print(slugify("  Foo   Bar  "));     // "foo-bar"
print(slugify("My Blog Post 2024")); // "my-blog-post-2024"
print(slugify("Héllo Wörld"));       // "hllo-wrld" (non-ASCII removed)
print(slugify("@#$%Test!!!"));       // "test"
```

**Use case - URL generation:**

```hemlock
import { slugify } from "@stdlib/strings";

fn article_url(title: string): string {
    return "/blog/" + slugify(title);
}

print(article_url("My First Post!"));  // "/blog/my-first-post"
```

---

### truncate(str, max_len, suffix?)

Truncate string to maximum length with optional suffix.

**Parameters:**
- `str` - Input string
- `max_len: i32` - Maximum length (including suffix)
- `suffix?: string` - Suffix to add if truncated (default: "...")

**Returns:** `string` - Truncated string

```hemlock
import { truncate } from "@stdlib/strings";

print(truncate("Hello World", 8));        // "Hello..."
print(truncate("Hello", 10));             // "Hello" (no truncation)
print(truncate("Hello World", 8, ".."));  // "Hello.."
print(truncate("Hello World", 5, ""));    // "Hello"
```

**Use case - display text:**

```hemlock
import { truncate } from "@stdlib/strings";

fn display_title(title: string): string {
    return truncate(title, 30);
}

print(display_title("A Very Long Article Title That Goes On"));
// "A Very Long Article Title T..."
```

---

## Error Handling

All functions throw exceptions for invalid input:

```hemlock
import { pad_left, is_alpha, reverse } from "@stdlib/strings";

// Invalid fill character length
try {
    pad_left("test", 10, "ab");  // Must be single character
} catch (e) {
    print("Error: " + e);  // "pad_left() fill must be single character"
}

// Invalid type
try {
    is_alpha(123);  // Must be string
} catch (e) {
    print("Error: " + e);  // "is_alpha() requires string argument"
}

// Invalid type
try {
    reverse(null);
} catch (e) {
    print("Error: " + e);  // "reverse() requires string argument"
}
```

**Error messages:**
- Clear indication of which function failed
- Description of the problem (type mismatch, invalid length, etc.)
- All errors are catchable with try/catch

---

## Performance Notes

### Time Complexity

- **pad_left, pad_right, center:** O(n) where n = padding length
- **is_alpha, is_digit, is_alnum, is_whitespace:** O(n) where n = string length
- **reverse:** O(n) where n = string length
- **lines:** O(n) where n = string length
- **words:** O(n) where n = string length

### Memory Usage

- **Padding functions:** Allocate new string with padding
- **Character type functions:** Use `.chars()` which allocates array of runes
- **reverse:** Allocates new string (does not modify in-place)
- **lines, words:** Allocate new array of strings

### Optimization Tips

```hemlock
// Bad - repeated checking in loop
let i = 0;
while (i < items.length) {
    if (is_digit(items[i])) {  // O(m) check per iteration
        // ...
    }
    i = i + 1;
}

// Better - check once, cache result if needed
let i = 0;
while (i < items.length) {
    let item = items[i];
    let is_num = is_digit(item);  // Cache result
    if (is_num) {
        // Use is_num multiple times
    }
    i = i + 1;
}
```

---

## Unicode Support

All functions are **Unicode-aware** and work with UTF-8 encoded strings:

```hemlock
import { reverse, pad_left, pad_right, center } from "@stdlib/strings";

// Reverse with emojis
print(reverse("Hello 🌍"));      // "🌍 olleH"
print(reverse("🚀🌟💫"));        // "💫🌟🚀"

// Reverse with CJK characters
print(reverse("你好"));          // "好你"
print(reverse("こんにちは"));    // "はちにんこ"

// Padding with Unicode
let s1 = pad_left("test", 10, "█");
print(s1);  // "██████test"

let s2 = pad_right("test", 10, "░");
print(s2);  // "test░░░░░░"

// Centering with Unicode
let s3 = center("中", 5, "─");
print(s3);  // "──中──"
```

**Important notes:**
- String length is measured in **Unicode codepoints** (not bytes)
- Emoji count as 1 character: `"🚀".length == 1`
- CJK characters count as 1 character each
- Padding width is in codepoints

---

## Complete Example

```hemlock
import { pad_left, pad_right, center } from "@stdlib/strings";
import { is_alpha, is_digit, is_alnum } from "@stdlib/strings";
import { reverse, lines, words } from "@stdlib/strings";

// Create formatted table
fn format_table(data: array): null {
    let header = center("USER DATA", 40, "=");
    print(header);

    let i = 0;
    while (i < data.length) {
        let item = data[i];
        let id_str = pad_left(typeof(item.id), 5);
        let name_str = pad_right(item.name, 20);
        print(id_str + " | " + name_str);
        i = i + 1;
    }

    return null;
}

// Validate user input
fn validate_username(name: string): bool {
    // Must be 3-20 characters, alphanumeric only
    if (name.length < 3 || name.length > 20) {
        return false;
    }
    return is_alnum(name);
}

// Parse multi-line config
fn parse_config(text: string): array {
    let config_lines = lines(text);
    let result: array = [];

    let i = 0;
    while (i < config_lines.length) {
        let line = config_lines[i].trim();
        if (line.length > 0) {
            let parts = words(line);
            if (parts.length == 2) {
                result.push({ key: parts[0], value: parts[1] });
            }
        }
        i = i + 1;
    }

    return result;
}

// Example usage
let users = [
    { id: 1, name: "Alice" },
    { id: 42, name: "Bob" },
    { id: 999, name: "Carol" }
];

format_table(users);

print(validate_username("alice123"));  // true
print(validate_username("ab"));        // false (too short)
print(validate_username("alice!"));    // false (not alnum)

let config_text = "host localhost\nport 8080\nssl true";
let config = parse_config(config_text);
print(config.length);  // 3
```

---

## See Also

**Built-in string methods (18 total):**
- Substring: `substr`, `slice`
- Search: `find`, `contains`
- Transform: `split`, `trim`, `to_upper`, `to_lower`, `replace`, `replace_all`, `repeat`
- Prefix/suffix: `starts_with`, `ends_with`
- Character access: `char_at`, `byte_at`, `chars`, `bytes`, `to_bytes`

**Related stdlib modules:**
- `@stdlib/regex` - Pattern matching with regular expressions
- `@stdlib/json` - JSON parsing includes string escaping/unescaping
- `@stdlib/collections` - Data structures for storing processed strings

---

## Testing

The strings module has comprehensive test coverage:

```bash
# Run all strings tests
./hemlock tests/stdlib_strings/test_padding.hml
./hemlock tests/stdlib_strings/test_char_type.hml
./hemlock tests/stdlib_strings/test_manipulation.hml
./hemlock tests/stdlib_strings/test_errors.hml
./hemlock tests/stdlib_strings/test_unicode.hml
```

**Test categories:**
- Padding & alignment (45+ assertions)
- Character type checking (35+ assertions)
- String manipulation (40+ assertions)
- Error handling (5+ assertions)
- Unicode support (10+ assertions)

---

## Design Philosophy

The strings module follows Hemlock's design principles:

1. **Explicit over implicit** - All functions have clear names and purposes
2. **No magic** - Functions do exactly what their names suggest
3. **Error transparency** - Exceptions for invalid input, not silent failures
4. **Unicode-aware** - Full UTF-8 support for modern text processing
5. **Complement built-ins** - Extends rather than duplicates built-in methods

---

## License

Part of the Hemlock programming language standard library.
