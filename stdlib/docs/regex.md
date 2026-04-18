# Hemlock Regex Module

Regular expression pattern matching using POSIX regex via FFI.

## Overview

The regex module provides pattern matching functionality through POSIX Extended Regular Expressions (ERE). It uses FFI to interface with the system's regex library (libc), providing both compiled regex objects for reuse and one-shot convenience functions.

**Features:**
- POSIX Extended Regular Expression syntax
- Compiled regex objects for efficient reuse
- Case-insensitive matching support
- One-shot convenience functions
- Manual memory management (explicit free required)

> **Note on flags:** Every function in this module that accepts a `flags` argument (`compile`, `test`, `matches`, `find`, `replace`, `replace_all`) requires it to be passed explicitly. Pass `null` to select the default (`REG_EXTENDED`), or a bitwise OR of the `REG_*` constants. Omitting the argument is not supported in all call contexts — always pass `null` when you want the default behavior.

## Usage

```hemlock
import { compile, test, matches, REG_EXTENDED, REG_ICASE } from "@stdlib/regex";
```

Or import all:

```hemlock
import * as regex from "@stdlib/regex";
let pattern = regex.compile("hello.*world", null);
```

---

## Quick Start

### One-Shot Pattern Matching

```hemlock
import { test } from "@stdlib/regex";

// Test if string matches pattern (pass null for default REG_EXTENDED flags)
if (test("^hello", "hello world", null)) {
    print("Match found!");
}

// Case-insensitive matching
import { REG_ICASE } from "@stdlib/regex";
if (test("HELLO", "hello world", REG_ICASE)) {
    print("Case-insensitive match!");
}
```

### Compiled Regex (Reusable)

```hemlock
import { compile } from "@stdlib/regex";

// Compile pattern once (pass null for default REG_EXTENDED flags)
let email_pattern = compile("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$", null);

// Reuse for multiple tests
print(email_pattern.test("user@example.com"));    // true
print(email_pattern.test("invalid.email"));       // false
print(email_pattern.test("another@test.org"));    // true

// Must manually free when done
email_pattern.free();
```

---

## API Reference

### Functions

#### `compile(pattern, flags)`

Compile a regex pattern into a reusable regex object.

**Parameters:**
- `pattern: string` - The regex pattern to compile
- `flags: i32 | null` - Compilation flags. Pass `null` to use the default (`REG_EXTENDED`), or a bitwise OR of the `REG_*` constants. The argument is **required** and must be passed explicitly (an integer value or `null`).

**Returns:** Regex object with methods `test()`, `matches()`, `find()`, `free()`

**Throws:** Exception if pattern is invalid

**Example:**
```hemlock
import { compile, REG_EXTENDED, REG_ICASE } from "@stdlib/regex";

// Basic pattern (null selects the default REG_EXTENDED)
let pattern = compile("^hello", null);

// Case-insensitive pattern
let pattern2 = compile("WORLD", REG_EXTENDED | REG_ICASE);

// Don't forget to free!
pattern.free();
pattern2.free();
```

---

#### `test(pattern, text, flags)`

Test if text matches pattern (one-shot, compiles and frees automatically).

**Parameters:**
- `pattern: string` - The regex pattern
- `text: string` - The text to test
- `flags: i32 | null` - Compilation flags. Pass `null` for the default (`REG_EXTENDED`) or a bitwise OR of `REG_*` constants. The argument must be passed explicitly.

**Returns:** `bool` - True if text matches pattern

**Example:**
```hemlock
import { test } from "@stdlib/regex";

if (test("^[0-9]+$", "12345", null)) {
    print("String contains only digits");
}
```

---

#### `matches(pattern, text, flags)`

Alias for `test()`. Tests if text matches pattern. The `flags` argument must be passed explicitly (pass `null` for default `REG_EXTENDED`).

**Example:**
```hemlock
import { matches } from "@stdlib/regex";

let is_valid = matches("^[A-Z][a-z]+$", "Hello", null);
print(is_valid);  // true
```

---

#### `find(pattern, text, flags)`

Alias for `test()`. Finds if pattern exists in text. The `flags` argument must be passed explicitly (pass `null` for default `REG_EXTENDED`).

**Example:**
```hemlock
import { find } from "@stdlib/regex";

if (find("error", "This is an error message", null)) {
    print("Found error in message");
}
```

---

#### `replace(pattern, text, replacement, flags)`

Replace the first match of pattern in text (one-shot, compiles and frees automatically).

**Parameters:**
- `pattern: string` - The regex pattern
- `text: string` - The text to search
- `replacement: string` - The replacement string
- `flags: i32 | null` - Compilation flags. Pass `null` for default (`REG_EXTENDED`). Must be passed explicitly.

**Returns:** `string` - Text with first match replaced

**Example:**
```hemlock
import { replace } from "@stdlib/regex";

let result = replace("[0-9]+", "abc 123 def 456", "NUM", null);
print(result);  // "abc NUM def 456"
```

---

#### `replace_all(pattern, text, replacement, flags)`

Replace all matches of pattern in text (one-shot, compiles and frees automatically).

**Parameters:**
- `pattern: string` - The regex pattern
- `text: string` - The text to search
- `replacement: string` - The replacement string
- `flags: i32 | null` - Compilation flags. Pass `null` for default (`REG_EXTENDED`). Must be passed explicitly.

**Returns:** `string` - Text with all matches replaced

**Example:**
```hemlock
import { replace_all } from "@stdlib/regex";

let result = replace_all("[0-9]+", "abc 123 def 456", "NUM", null);
print(result);  // "abc NUM def NUM"
```

---

#### `error_message(errcode)`

Get a human-readable error message for a regex error code.

**Parameters:**
- `errcode: i32` - Regex error code (e.g., from a failed compilation)

**Returns:** `string` - Error description

**Example:**
```hemlock
import { error_message, REG_EBRACK } from "@stdlib/regex";

print(error_message(REG_EBRACK));  // Description of bracket error
```

---

### Regex Object

Returned by `compile()`. Represents a compiled regex pattern.

**Properties:**
- `pattern: string` - The original pattern string (read-only)

**Methods:**

#### `regex.test(text)`

Test if text matches the compiled pattern.

**Parameters:**
- `text: string` - The text to test

**Returns:** `bool` - True if text matches

**Example:**
```hemlock
let pattern = compile("^[a-z]+$", null);
print(pattern.test("hello"));   // true
print(pattern.test("Hello"));   // false (capital H)
print(pattern.test("hello123")); // false (has digits)
pattern.free();
```

---

#### `regex.matches(text)`

Alias for `regex.test(text)`.

---

#### `regex.find(text)`

Alias for `regex.test(text)`.

---

#### `regex.find_all(text, max_matches)`

Find all matches in the text and return an array of match objects.

**Parameters:**
- `text: string` - The text to search
- `max_matches: i32 | null` - Maximum number of matches to return. Pass `null` for no limit. Must be passed explicitly.

**Returns:** `array` - Array of match objects with `{ start: i32, end: i32, text: string }`

**Example:**
```hemlock
let pattern = compile("[0-9]+", null);
let matches = pattern.find_all("abc 123 def 456 ghi 789", null);
for (m in matches) {
    print(m.text);  // "123", "456", "789"
}
pattern.free();
```

---

#### `regex.replace(text, replacement)`

Replace the first match in the text with the replacement string.

**Parameters:**
- `text: string` - The text to search
- `replacement: string` - The replacement string

**Returns:** `string` - Text with first match replaced

**Example:**
```hemlock
let pattern = compile("[0-9]+", null);
let result = pattern.replace("abc 123 def 456", "NUM");
print(result);  // "abc NUM def 456"
pattern.free();
```

---

#### `regex.replace_all(text, replacement)`

Replace all matches in the text with the replacement string.

**Parameters:**
- `text: string` - The text to search
- `replacement: string` - The replacement string

**Returns:** `string` - Text with all matches replaced

**Example:**
```hemlock
let pattern = compile("[0-9]+", null);
let result = pattern.replace_all("abc 123 def 456", "NUM");
print(result);  // "abc NUM def NUM"
pattern.free();
```

---

#### `regex.free()`

Free the compiled regex. **Must be called manually** to avoid memory leaks.

**Returns:** `null`

**Example:**
```hemlock
let pattern = compile("test", null);
pattern.test("testing");
pattern.free();  // Required!
```

**Important:** Attempting to use a freed regex will throw an exception.

---

## Constants

### Compilation Flags

Flags for `compile()` and one-shot functions. The flags argument must always be passed explicitly — pass `null` to select the default (`REG_EXTENDED`), or a bitwise OR of the `REG_*` constants below.

- **`REG_EXTENDED`** (1) - Use extended regex syntax (applied when `null` is passed)
- **`REG_ICASE`** (2) - Case-insensitive matching
- **`REG_NOSUB`** (4) - Don't report match positions (not used in basic API)
- **`REG_NEWLINE`** (8) - Treat newline as special character

**Combining flags:**
```hemlock
import { compile, REG_EXTENDED, REG_ICASE, REG_NEWLINE } from "@stdlib/regex";

// Multiple flags using bitwise OR
let flags = REG_EXTENDED | REG_ICASE | REG_NEWLINE;
let pattern = compile("^hello", flags);
```

### Match Flags

Flags for `regexec()` (not exposed in basic API):

- **`REG_NOTBOL`** (1) - String is not beginning of line
- **`REG_NOTEOL`** (2) - String is not end of line

### Error Codes

Error codes returned by regex functions:

- `REG_NOMATCH` - No match found
- `REG_BADPAT` - Invalid regex pattern
- `REG_ECOLLATE` - Invalid collation element
- `REG_ECTYPE` - Invalid character class
- `REG_EESCAPE` - Trailing backslash
- `REG_ESUBREG` - Invalid back reference
- `REG_EBRACK` - Brackets [] not balanced
- `REG_EPAREN` - Parentheses () not balanced
- `REG_EBRACE` - Braces {} not balanced
- `REG_BADBR` - Invalid repetition count
- `REG_ERANGE` - Invalid range in []
- `REG_ESPACE` - Out of memory
- `REG_BADRPT` - Invalid use of repetition operator

---

## Pattern Syntax

### POSIX Extended Regular Expression (ERE) Syntax

**Basic Characters:**
- `.` - Match any character except newline
- `^` - Match start of string
- `$` - Match end of string
- `\` - Escape special characters

**Character Classes:**
- `[abc]` - Match any of a, b, or c
- `[^abc]` - Match anything except a, b, or c
- `[a-z]` - Match any lowercase letter
- `[A-Z]` - Match any uppercase letter
- `[0-9]` - Match any digit

**Predefined Classes:**
- `[[:alnum:]]` - Alphanumeric characters
- `[[:alpha:]]` - Alphabetic characters
- `[[:digit:]]` - Digits
- `[[:lower:]]` - Lowercase letters
- `[[:upper:]]` - Uppercase letters
- `[[:space:]]` - Whitespace characters
- `[[:punct:]]` - Punctuation characters

**Quantifiers:**
- `*` - Match 0 or more times
- `+` - Match 1 or more times
- `?` - Match 0 or 1 time
- `{n}` - Match exactly n times
- `{n,}` - Match n or more times
- `{n,m}` - Match between n and m times

**Grouping:**
- `(pattern)` - Group subpattern
- `|` - Alternation (OR)

**Examples:**
```hemlock
import { test } from "@stdlib/regex";

// Email validation (simplified)
test("^[a-zA-Z0-9]+@[a-zA-Z0-9]+\\.[a-z]+$", "user@example.com", null);  // true

// Phone number (US format)
test("^[0-9]{3}-[0-9]{3}-[0-9]{4}$", "555-123-4567", null);  // true

// Hexadecimal color code
test("^#[0-9a-fA-F]{6}$", "#FF5733", null);  // true

// URL protocol
test("^(http|https)://", "https://example.com", null);  // true

// Whitespace
test("^[[:space:]]+$", "   \t\n", null);  // true
```

---

## Examples

### Email Validation

```hemlock
import { compile, REG_EXTENDED } from "@stdlib/regex";

fn is_valid_email(email: string): bool {
    // Simplified email regex (null selects default REG_EXTENDED flags)
    let pattern = compile("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$", null);
    let valid = pattern.test(email);
    pattern.free();
    return valid;
}

print(is_valid_email("user@example.com"));     // true
print(is_valid_email("invalid.email"));        // false
print(is_valid_email("test@domain.co.uk"));    // true
```

### URL Protocol Extraction

```hemlock
import { test, REG_ICASE } from "@stdlib/regex";

fn has_https(url: string): bool {
    return test("^https://", url, REG_ICASE);
}

print(has_https("https://example.com"));  // true
print(has_https("http://example.com"));   // false
print(has_https("HTTPS://Example.com"));  // true (case-insensitive)
```

### Phone Number Validation

```hemlock
import { compile } from "@stdlib/regex";

fn validate_phone(phone: string): bool {
    // Match formats: 555-123-4567 or (555) 123-4567
    let pattern = compile("^(\\([0-9]{3}\\) |[0-9]{3}-)[0-9]{3}-[0-9]{4}$", null);
    let valid = pattern.test(phone);
    pattern.free();
    return valid;
}

print(validate_phone("555-123-4567"));    // true
print(validate_phone("(555) 123-4567"));  // true
print(validate_phone("555.123.4567"));    // false
```

### Log Line Filtering

```hemlock
import { compile, REG_ICASE } from "@stdlib/regex";

fn filter_logs(lines: array<string>, pattern: string): array<string> {
    let regex = compile(pattern, REG_ICASE);
    let filtered: array<string> = [];

    let i = 0;
    while (i < lines.length) {
        if (regex.test(lines[i])) {
            filtered.push(lines[i]);
        }
        i = i + 1;
    }

    regex.free();
    return filtered;
}

let logs = [
    "INFO: Starting application",
    "ERROR: Connection failed",
    "INFO: Processing data",
    "ERROR: Invalid input",
];

let errors = filter_logs(logs, "error");
// Returns: ["ERROR: Connection failed", "ERROR: Invalid input"]
```

### Password Strength Validation

```hemlock
import { test } from "@stdlib/regex";

fn is_strong_password(password: string): bool {
    // At least 8 chars, contains uppercase, lowercase, digit, special char
    let has_length = test("^.{8,}$", password, null);
    let has_upper = test("[A-Z]", password, null);
    let has_lower = test("[a-z]", password, null);
    let has_digit = test("[0-9]", password, null);
    let has_special = test("[[:punct:]]", password, null);

    return has_length && has_upper && has_lower && has_digit && has_special;
}

print(is_strong_password("Passw0rd!"));    // true
print(is_strong_password("weak"));         // false
print(is_strong_password("NoSpecial1"));   // false
```

---

## Memory Management

**Important:** Compiled regex objects must be manually freed to avoid memory leaks.

### Pattern: Always Free

```hemlock
let pattern = compile("test", null);
defer pattern.free();  // Guaranteed to free

// Use pattern...
pattern.test("testing");
```

### Pattern: Try/Finally

```hemlock
let pattern = compile("test", null);
try {
    let result = pattern.test("testing");
    // ... process result
} finally {
    pattern.free();  // Always freed, even on exception
}
```

### Pattern: One-Shot Functions (No Free Needed)

```hemlock
// One-shot functions handle memory automatically.
// Remember to pass flags explicitly (null for default REG_EXTENDED).
if (test("pattern", "text", null)) {
    // No need to free - handled internally
}
```

---

## Error Handling

Pattern compilation errors throw exceptions:

```hemlock
import { compile } from "@stdlib/regex";

try {
    let pattern = compile("[invalid", null);  // Unbalanced bracket
} catch (e) {
    print("Regex error: " + e);
    // "Regex compilation failed: error code 7" (REG_EBRACK)
}
```

Using a freed regex throws an exception:

```hemlock
let pattern = compile("test", null);
pattern.free();

try {
    pattern.test("testing");  // Error!
} catch (e) {
    print(e);  // "Regex has been freed"
}
```

---

## Limitations

1. **No capture groups:** The current API only supports match/no-match testing and full-match extraction. Extracting individual capture group substrings is not yet supported.

2. **POSIX ERE only:** Uses POSIX Extended Regular Expressions, not Perl-compatible (PCRE) syntax. Some advanced features like lookahead/lookbehind are not available.

3. **Manual memory management:** Must explicitly call `.free()` on compiled regex objects.

---

## Future Enhancements

Planned additions:
- Capture group extraction
- Split string by regex pattern
- Optional PCRE support for advanced features

---

## Performance Tips

1. **Reuse compiled patterns:** If testing the same pattern multiple times, compile once and reuse:
   ```hemlock
   // Good: Compile once
   let pattern = compile("test", null);
   pattern.test(text1);
   pattern.test(text2);
   pattern.free();

   // Bad: Compile every time
   test("test", text1, null);
   test("test", text2, null);
   ```

2. **Use simple patterns when possible:** Complex patterns with many alternations or nested groups are slower.

3. **Anchor patterns:** Use `^` and `$` to anchor patterns when you know the match location.

---

## See Also

- **String methods:** `string.find()`, `string.contains()`, `string.starts_with()`, `string.ends_with()`
- **FFI documentation:** For advanced users who want to extend the regex API
- **POSIX regex documentation:** `man 3 regex` for detailed regex syntax
