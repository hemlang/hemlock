# Hemlock Standard Library: Encoding

The `@stdlib/encoding` module provides encoding and decoding utilities for common data formats used in network protocols and data interchange.

## Features

- **Base64 encoding/decoding** - Standard Base64 alphabet with padding
- **Base32 encoding/decoding** - RFC 4648 standard alphabet
- **Base58 encoding/decoding** - Bitcoin alphabet (no confusing characters)
- **Hexadecimal encoding/decoding** - Convert binary data to/from hex strings
- **URL encoding/decoding** - Percent-encoding for safe URL transmission

## Import

```hemlock
// Import specific functions
import { base64_encode, base64_decode } from "@stdlib/encoding";
import { base32_encode, base32_decode } from "@stdlib/encoding";
import { base58_encode, base58_decode } from "@stdlib/encoding";
import { hex_encode, hex_decode } from "@stdlib/encoding";
import { url_encode, url_decode } from "@stdlib/encoding";

// Import all functions
import * as encoding from "@stdlib/encoding";
```

---

## Base64 Encoding

Base64 encodes binary data into ASCII text using a 64-character alphabet (A-Z, a-z, 0-9, +, /) with `=` padding.

### base64_encode(input: string): string

Encodes a string to Base64 format.

**Parameters:**
- `input` - String to encode (required)

**Returns:** Base64-encoded string with padding

**Throws:**
- Error if `input` is not a string

**Examples:**
```hemlock
import { base64_encode } from "@stdlib/encoding";

// Basic encoding
let encoded1 = base64_encode("Hello, World!");
print(encoded1);  // "SGVsbG8sIFdvcmxkIQ=="

// Empty string
let encoded2 = base64_encode("");
print(encoded2);  // ""

// Binary data (all bytes 0-255)
let binary = "";
let i = 0;
while (i < 256) {
    let b: rune = i;
    binary = binary + b;
    i = i + 1;
}
let encoded3 = base64_encode(binary);

// Unicode/UTF-8
let encoded4 = base64_encode("Hello 世界 🚀");
print(encoded4);  // UTF-8 bytes encoded to Base64
```

---

### base64_decode(input: string): string

Decodes a Base64-encoded string back to the original string.

**Parameters:**
- `input` - Base64 string to decode (required)

**Returns:** Decoded original string

**Throws:**
- Error if `input` is not a string
- Error if Base64 string length is not a multiple of 4
- Error if Base64 string contains invalid characters
- Error if padding appears in unexpected positions

**Examples:**
```hemlock
import { base64_decode } from "@stdlib/encoding";

// Basic decoding
let decoded1 = base64_decode("SGVsbG8sIFdvcmxkIQ==");
print(decoded1);  // "Hello, World!"

// Round-trip encoding/decoding
let original = "The quick brown fox";
let encoded = base64_encode(original);
let decoded2 = base64_decode(encoded);
print(decoded2 == original);  // true

// Whitespace is automatically removed
let decoded3 = base64_decode("SGVs bG8s IFdv cmxk IQ==");
print(decoded3);  // "Hello, World!"
```

---

## Base32 Encoding

Base32 encodes binary data using a 32-character alphabet (A-Z, 2-7) with `=` padding. It's commonly used in TOTP tokens and file systems where case-insensitivity is needed.

### base32_encode(input: string): string

Encodes a string to Base32 format (RFC 4648).

**Parameters:**
- `input` - String to encode (required)

**Returns:** Base32-encoded string with padding

**Throws:**
- Error if `input` is not a string

**Examples:**
```hemlock
import { base32_encode } from "@stdlib/encoding";

// Basic encoding
let encoded = base32_encode("Hello");
print(encoded);  // "JBSWY3DP"

// Longer text
let encoded2 = base32_encode("Hello, World!");
print(encoded2);  // "JBSWY3DPFQQFO33SNRSCC==="
```

---

### base32_decode(input: string): string

Decodes a Base32-encoded string back to the original string.

**Parameters:**
- `input` - Base32 string to decode (required, case-insensitive)

**Returns:** Decoded original string

**Throws:**
- Error if `input` is not a string
- Error if Base32 string length is not a multiple of 8
- Error if Base32 string contains invalid characters

**Examples:**
```hemlock
import { base32_decode } from "@stdlib/encoding";

// Basic decoding
let decoded = base32_decode("JBSWY3DP");
print(decoded);  // "Hello"

// Case-insensitive
let decoded2 = base32_decode("jbswy3dp");
print(decoded2);  // "Hello"

// Round-trip
let original = "Secret123";
let encoded = base32_encode(original);
let decoded3 = base32_decode(encoded);
print(decoded3 == original);  // true
```

---

## Base58 Encoding

Base58 uses a 58-character alphabet that excludes easily confused characters (0, O, I, l). It's commonly used in Bitcoin addresses, IPFS hashes, and other systems where human-readability matters.

### base58_encode(input: string): string

Encodes a string to Base58 format (Bitcoin alphabet).

**Parameters:**
- `input` - String to encode (required)

**Returns:** Base58-encoded string (no padding)

**Throws:**
- Error if `input` is not a string

**Examples:**
```hemlock
import { base58_encode } from "@stdlib/encoding";

// Basic encoding
let encoded = base58_encode("Hello");
print(encoded);  // "9Ajdvzr"

// Leading zero bytes become leading '1's
let data = "";
let zero: rune = 0;
data = data + zero + zero + "Hi";
let encoded2 = base58_encode(data);
print(encoded2);  // "11..." (starts with two 1's)
```

---

### base58_decode(input: string): string

Decodes a Base58-encoded string back to the original string.

**Parameters:**
- `input` - Base58 string to decode (required)

**Returns:** Decoded original string

**Throws:**
- Error if `input` is not a string
- Error if Base58 string contains invalid characters (0, O, I, l are not valid)

**Examples:**
```hemlock
import { base58_decode } from "@stdlib/encoding";

// Basic decoding
let decoded = base58_decode("9Ajdvzr");
print(decoded);  // "Hello"

// Round-trip
let original = "Hello, World!";
let encoded = base58_encode(original);
let decoded2 = base58_decode(encoded);
print(decoded2 == original);  // true
```

---

## Hexadecimal Encoding

Hexadecimal encoding represents bytes as pairs of hex digits (0-9, a-f). Each byte becomes two hex characters.

### hex_encode(input: string): string

Encodes a string to hexadecimal representation.

**Parameters:**
- `input` - String to encode (required)

**Returns:** Lowercase hexadecimal string

**Throws:**
- Error if `input` is not a string

**Examples:**
```hemlock
import { hex_encode } from "@stdlib/encoding";

// Basic encoding
let hex1 = hex_encode("Hello");
print(hex1);  // "48656c6c6f"

// Binary data
let data = "";
let b1: rune = 0;
let b2: rune = 255;
data = data + b1 + b2;
let hex2 = hex_encode(data);
print(hex2);  // "00ff"

// Unicode (UTF-8 bytes are hex-encoded)
let hex3 = hex_encode("🚀");
print(hex3);  // UTF-8 encoding of rocket emoji
```

---

### hex_decode(input: string): string

Decodes a hexadecimal string back to the original string.

**Parameters:**
- `input` - Hexadecimal string to decode (required, case-insensitive)

**Returns:** Decoded original string

**Throws:**
- Error if `input` is not a string
- Error if hex string length is not even
- Error if hex string contains invalid characters (not 0-9, a-f, A-F)

**Examples:**
```hemlock
import { hex_decode } from "@stdlib/encoding";

// Basic decoding
let decoded1 = hex_decode("48656c6c6f");
print(decoded1);  // "Hello"

// Case-insensitive
let decoded2 = hex_decode("48656C6C6F");
print(decoded2);  // "Hello" (uppercase works too)

let decoded3 = hex_decode("48656C6c6f");
print(decoded3);  // "Hello" (mixed case works)

// Whitespace is automatically removed
let decoded4 = hex_decode("48 65 6c 6c 6f");
print(decoded4);  // "Hello"

// Round-trip
let original = "The quick brown fox";
let encoded = hex_encode(original);
let decoded5 = hex_decode(encoded);
print(decoded5 == original);  // true
```

---

## URL Encoding (Percent Encoding)

URL encoding (RFC 3986) encodes special characters as `%XX` where XX is the hexadecimal byte value. Safe characters (A-Z, a-z, 0-9, `-`, `_`, `.`, `~`) are not encoded.

### url_encode(input: string): string

Encodes a string for safe use in URLs using percent-encoding.

**Parameters:**
- `input` - String to encode (required)

**Returns:** URL-encoded string

**Throws:**
- Error if `input` is not a string

**Encoding rules:**
- **Safe characters (unreserved):** `A-Z`, `a-z`, `0-9`, `-`, `_`, `.`, `~` → not encoded
- **Space:** Encoded as `+` (form-urlencoded style)
- **All other characters:** Encoded as `%XX` where XX is the hex byte value

**Examples:**
```hemlock
import { url_encode } from "@stdlib/encoding";

// Safe characters pass through unchanged
let safe = url_encode("ABCxyz123-_.~");
print(safe);  // "ABCxyz123-_.~"

// Space becomes +
let space = url_encode("Hello World");
print(space);  // "Hello+World"

// Special characters become %XX
let special = url_encode("hello@example.com");
print(special);  // "hello%40example.com"

// Query string encoding
let query = url_encode("key=value&foo=bar");
print(query);  // "key%3Dvalue%26foo%3Dbar"

// Unicode (UTF-8 bytes are percent-encoded)
let unicode = url_encode("Hello 世界");
print(unicode);  // "Hello+%E4%B8%96%E7%95%8C"

// Emoji
let emoji = url_encode("Hello 🚀");
print(emoji);  // "Hello+%F0%9F%9A%80"
```

---

### url_decode(input: string): string

Decodes a URL-encoded string back to the original string.

**Parameters:**
- `input` - URL-encoded string to decode (required)

**Returns:** Decoded original string

**Throws:**
- Error if `input` is not a string
- Error if percent sequence is incomplete (e.g., `%2` instead of `%20`)
- Error if percent sequence contains invalid hex digits

**Decoding rules:**
- `+` → space
- `%20` → space
- `%XX` → byte value XX (hex)

**Examples:**
```hemlock
import { url_decode } from "@stdlib/encoding";

// Basic decoding
let decoded1 = url_decode("Hello+World");
print(decoded1);  // "Hello World"

// %20 also decodes to space
let decoded2 = url_decode("Hello%20World");
print(decoded2);  // "Hello World"

// Percent-encoded characters
let decoded3 = url_decode("hello%40example.com");
print(decoded3);  // "hello@example.com"

// Complex URL
let decoded4 = url_decode("key%3Dvalue%26foo%3Dbar");
print(decoded4);  // "key=value&foo=bar"

// Unicode
let decoded5 = url_decode("Hello+%E4%B8%96%E7%95%8C");
print(decoded5);  // "Hello 世界"

// Round-trip
let original = "Hello, World! 100% sure?";
let encoded = url_encode(original);
let decoded6 = url_decode(encoded);
print(decoded6 == original);  // true
```

---

## Complete Example

```hemlock
import {
    base64_encode, base64_decode,
    hex_encode, hex_decode,
    url_encode, url_decode
} from "@stdlib/encoding";

// Base64 example
let data = "Hello, World!";
let b64 = base64_encode(data);
print("Base64: " + b64);
print("Decoded: " + base64_decode(b64));

// Hex example
let hex = hex_encode(data);
print("\nHex: " + hex);
print("Decoded: " + hex_decode(hex));

// URL example
let url_data = "hello@example.com?query=value";
let url_enc = url_encode(url_data);
print("\nURL Encoded: " + url_enc);
print("Decoded: " + url_decode(url_enc));

// Working with binary data
let binary = "";
let i = 0;
while (i < 10) {
    let b: rune = i * 25;
    binary = binary + b;
    i = i + 1;
}

print("\nBinary data (10 bytes):");
print("  Base64: " + base64_encode(binary));
print("  Hex: " + hex_encode(binary));
```

---

## Error Handling

All functions include comprehensive error checking:

```hemlock
import { base64_encode, base64_decode, hex_decode, url_decode } from "@stdlib/encoding";

// Type checking
try {
    base64_encode(123);  // ERROR: not a string
} catch (e) {
    print("Error: " + e);  // "base64_encode() requires string argument"
}

// Invalid Base64
try {
    base64_decode("ABC");  // ERROR: not multiple of 4
} catch (e) {
    print("Error: " + e);  // "Invalid Base64 string: length must be multiple of 4"
}

// Invalid hex
try {
    hex_decode("XYZ");  // ERROR: invalid characters
} catch (e) {
    print("Error: " + e);  // "Invalid hex string: invalid character"
}

// Invalid URL encoding
try {
    url_decode("Hello%2");  // ERROR: incomplete percent sequence
} catch (e) {
    print("Error: " + e);  // "Invalid URL encoding: incomplete percent sequence"
}
```

---

## Implementation Notes

### Base64

- Uses standard Base64 alphabet: `A-Z`, `a-z`, `0-9`, `+`, `/`
- Padding character: `=`
- Whitespace in input is automatically removed during decoding
- Supports binary data (all byte values 0-255)

### Base32

- Uses RFC 4648 standard alphabet: `A-Z`, `2-7`
- Padding character: `=`
- Case-insensitive decoding (accepts both uppercase and lowercase)
- 8 output characters per 5 input bytes
- ~60% size increase

### Base58

- Uses Bitcoin alphabet: `123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz`
- Excludes confusing characters: `0` (zero), `O` (uppercase o), `I` (uppercase i), `l` (lowercase L)
- No padding (variable-length output)
- Leading zero bytes in input become leading `1` characters
- ~37% size increase on average

### Hexadecimal

- Output is lowercase (`a-f`)
- Decoding is case-insensitive (accepts `A-F` and `a-f`)
- Whitespace in input is automatically removed during decoding
- Two hex digits per byte

### URL Encoding

- Follows RFC 3986 unreserved character set
- Space encoded as `+` (form-urlencoded style)
- Alternative: `%20` also decodes to space
- UTF-8 bytes are percent-encoded for non-ASCII characters

---

## Performance Considerations

- All functions process data character-by-character or byte-by-byte
- String concatenation is used for building results (O(n²) in worst case)
- For large data (>100KB), consider processing in chunks if memory is constrained
- Base64: ~33% size increase (4 output chars per 3 input bytes)
- Hex: 2x size increase (2 hex digits per byte)
- URL encoding: Variable size (1x for safe chars, 3x for encoded chars)

---

## Testing

Comprehensive test suite available in `tests/stdlib_encoding/`:

```bash
# Run all encoding tests
./hemlock tests/stdlib_encoding/test_base64.hml
./hemlock tests/stdlib_encoding/test_hex.hml
./hemlock tests/stdlib_encoding/test_url.hml
./hemlock tests/stdlib_encoding/test_validation.hml
```

Test coverage:
- ✅ 11 Base64 tests (empty, single char, padding, binary, UTF-8, round-trip)
- ✅ 13 Hex tests (empty, case-insensitive, binary, whitespace, round-trip)
- ✅ 18 URL tests (safe chars, space, special chars, Unicode, emoji, round-trip)
- ✅ 10 Validation tests (type checking, invalid input validation)

---

## See Also

- **@stdlib/net** - Networking module (uses URL encoding for HTTP requests)
- **@stdlib/json** - JSON module (uses Base64 for binary data in JSON)
- **@stdlib/fs** - File system module (for reading/writing binary files)
