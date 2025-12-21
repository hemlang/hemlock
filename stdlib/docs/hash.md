# Hemlock Hash Module

Comprehensive hashing and checksum utilities for both non-cryptographic (fast hash tables) and cryptographic (secure hashing) use cases.

## Overview

The `@stdlib/hash` module provides:
- **Non-cryptographic hashes**: djb2, fnv1a, murmur3 (for hash tables, fast checksums)
- **Cryptographic hashes**: SHA-256, SHA-512, MD5 (via OpenSSL FFI)
- **HMAC**: Hash-based message authentication (hmac_sha256, hmac_sha512, hmac_md5)
- **File checksums**: Convenient functions for hashing file contents

### System Requirements

**Cryptographic hash functions require OpenSSL:**
- Hemlock links against `libcrypto` (OpenSSL) at compile time
- On Debian/Ubuntu: `sudo apt-get install libssl-dev` (for building from source)
- Runtime requires `libcrypto.so.3` (usually pre-installed)
- On macOS: Install OpenSSL via Homebrew
- Non-cryptographic hashes (djb2, fnv1a, murmur3) work without OpenSSL

## Usage

```hemlock
import { djb2, fnv1a, murmur3, sha256, sha512, md5 } from "@stdlib/hash";
import { hmac_sha256, hmac_sha512, hmac_md5 } from "@stdlib/hash";
import { file_checksum, file_sha256, file_md5 } from "@stdlib/hash";
```

Or import all:

```hemlock
import * as hash from "@stdlib/hash";
let checksum = hash.sha256("data");
```

---

## Non-Cryptographic Hash Functions

These functions are **fast** and suitable for hash tables, checksums, and non-security applications. They return **i32** values.

### djb2(input: string): i32

DJB2 hash algorithm - fast, simple, with good distribution. Commonly used in hash tables.

```hemlock
let h = djb2("hello world");
print(h);  // -862545276 (i32 hash value)

// Empty string
let h2 = djb2("");
print(h2);  // 5381 (djb2 initial value)
```

**Properties:**
- Very fast (simple multiply and add operations)
- Good distribution for short strings
- Used in Hemlock's HashMap implementation
- Returns i32 (signed 32-bit integer)

**Use cases:**
- Hash tables
- Quick string comparisons
- Non-cryptographic checksums

---

### fnv1a(input: string): i32

FNV-1a hash algorithm - better avalanche properties than djb2 for certain data patterns.

```hemlock
let h = fnv1a("hello world");
print(h);  // -1382160680

// FNV-1a is deterministic
let h2 = fnv1a("hello world");
assert(h == h2, "Same input = same hash");
```

**Properties:**
- Good distribution characteristics
- Better avalanche effect than djb2 (small changes → large hash differences)
- FNV offset basis: 2166136261
- Returns i32

**Use cases:**
- Hash tables requiring better distribution
- String interning
- Fast checksums

---

### murmur3(input: string, seed?: 0): i32

MurmurHash3 (32-bit) - excellent distribution, widely used in production systems.

```hemlock
let h = murmur3("hello world");
print(h);  // -862545276

// With custom seed
let h2 = murmur3("hello world", 42);
print(h2);  // Different hash due to different seed

// Seed changes output
assert(murmur3("test", 0) != murmur3("test", 1));
```

**Properties:**
- Excellent distribution (best among non-crypto hashes)
- Widely used (Redis, Hadoop, Cassandra, etc.)
- Optional seed parameter (default: 0)
- Returns i32

**Use cases:**
- Production hash tables
- Bloom filters
- Distributed systems (consistent hashing)
- Data deduplication

---

## Cryptographic Hash Functions

These functions provide **secure cryptographic hashing** via OpenSSL's libcrypto. They return **hexadecimal strings**.

⚠️ **Note:** These are true cryptographic hashes suitable for security applications.

### sha256(input: string): string

SHA-256 hash (256-bit / 32-byte output). Industry-standard secure hash.

```hemlock
let hash = sha256("hello");
print(hash);
// "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"

assert(hash.length == 64, "SHA-256 produces 64 hex characters");

// Empty string
let empty_hash = sha256("");
print(empty_hash);
// "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

// UTF-8 support
let emoji_hash = sha256("Hello 🌍");
print(emoji_hash);  // Works with any UTF-8 string
```

**Properties:**
- 256-bit (32-byte) output
- Returns 64-character hexadecimal string (lowercase)
- Cryptographically secure
- Widely used and trusted

**Use cases:**
- Password hashing (with salt)
- File integrity verification
- Digital signatures
- Blockchain and cryptocurrency
- Security tokens

---

### sha512(input: string): string

SHA-512 hash (512-bit / 64-byte output). More secure variant of SHA-2 family.

```hemlock
let hash = sha512("hello");
print(hash);
// Returns 128-character hex string

assert(hash.length == 128, "SHA-512 produces 128 hex characters");

// Empty string
let empty_hash = sha512("");
print(empty_hash);
// "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce
//  47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"
```

**Properties:**
- 512-bit (64-byte) output
- Returns 128-character hexadecimal string
- More secure than SHA-256 (larger output space)
- Suitable for high-security applications

**Use cases:**
- High-security applications
- Long-term data integrity
- Cryptographic protocols
- Certificate authorities

---

### md5(input: string): string

MD5 hash (128-bit / 16-byte output).

⚠️ **WARNING:** MD5 is cryptographically broken. Use only for legacy compatibility, NOT for security.

```hemlock
let hash = md5("hello");
print(hash);
// "5d41402abc4b2a76b9719d911017c592"

assert(hash.length == 32, "MD5 produces 32 hex characters");

// Known MD5 hash
let test_hash = md5("The quick brown fox jumps over the lazy dog");
assert(test_hash == "9e107d9d372bb6826bd81d3542a419d6");
```

**Properties:**
- 128-bit (16-byte) output
- Returns 32-character hexadecimal string
- ⚠️ **INSECURE** - collision attacks are practical
- Fast but not recommended for security

**Use cases:**
- ✓ Legacy system compatibility
- ✓ Non-security checksums (where speed matters)
- ✗ **NOT for passwords**
- ✗ **NOT for security tokens**
- ✗ **NOT for digital signatures**

**Alternatives:** Use SHA-256 or SHA-512 for security applications.

---

## HMAC (Hash-based Message Authentication Code)

HMAC provides message authentication using a secret key combined with a hash function. Use HMAC when you need to verify both the integrity AND authenticity of a message.

### hmac_sha256(key: string, message: string): string

HMAC using SHA-256 hash function.

```hemlock
import { hmac_sha256 } from "@stdlib/hash";

// Basic HMAC
let mac = hmac_sha256("secret-key", "Hello, World!");
print(mac);  // 64-character hex string

// Verify a message (compare MACs)
let expected = hmac_sha256("secret-key", "Hello, World!");
let received_mac = "...";  // MAC from sender
if (received_mac == expected) {
    print("Message is authentic");
}
```

**Properties:**
- 256-bit (32-byte) output as 64-character hex string
- Secure for authentication when using strong keys
- Key can be any length (hashed if > 64 bytes)

**Use cases:**
- API authentication (HMAC signatures)
- Webhook verification
- JWT signing (HS256)
- Message integrity verification

---

### hmac_sha512(key: string, message: string): string

HMAC using SHA-512 hash function for higher security.

```hemlock
import { hmac_sha512 } from "@stdlib/hash";

let mac = hmac_sha512("secret-key", "message");
print(mac);  // 128-character hex string
```

**Properties:**
- 512-bit (64-byte) output as 128-character hex string
- Higher security margin than SHA-256
- Key can be any length (hashed if > 128 bytes)

---

### hmac_md5(key: string, message: string): string

HMAC using MD5 hash function. **For legacy compatibility only.**

```hemlock
import { hmac_md5 } from "@stdlib/hash";

let mac = hmac_md5("key", "message");
print(mac);  // 32-character hex string
```

⚠️ **WARNING:** MD5 is cryptographically broken. Use `hmac_sha256` for new applications.

---

## File Checksum Functions

Convenient functions for computing hashes of file contents.

### file_checksum(path: string, hash_fn): string

Generic file checksum using any hash function.

```hemlock
import { sha256, djb2, file_checksum } from "@stdlib/hash";

// SHA-256 checksum of a file
let checksum = file_checksum("data.txt", sha256);
print(checksum);  // Hex string (64 chars for SHA-256)

// Fast non-crypto checksum
let fast_checksum = file_checksum("data.txt", djb2);
print(fast_checksum);  // String representation of i32
```

**Parameters:**
- `path`: File path (string)
- `hash_fn`: Hash function (djb2, fnv1a, murmur3, sha256, sha512, md5)

**Returns:** String (hex for crypto hashes, numeric string for non-crypto)

---

### Convenience Functions

Pre-configured file checksum functions for common use cases:

```hemlock
import {
    file_sha256,
    file_sha512,
    file_md5,
    file_djb2,
    file_fnv1a,
    file_murmur3
} from "@stdlib/hash";

// Cryptographic checksums
let sha256_sum = file_sha256("data.txt");
let sha512_sum = file_sha512("important.pdf");
let md5_sum = file_md5("legacy.zip");  // Legacy only!

// Fast non-crypto checksums
let fast_sum = file_djb2("config.json");
let better_sum = file_murmur3("database.db");
```

**Functions:**
- `file_sha256(path: string): string` - SHA-256 of file
- `file_sha512(path: string): string` - SHA-512 of file
- `file_md5(path: string): string` - MD5 of file (legacy only)
- `file_djb2(path: string): string` - DJB2 of file
- `file_fnv1a(path: string): string` - FNV-1a of file
- `file_murmur3(path: string): string` - MurmurHash3 of file

---

## Complete Example: Verify File Integrity

```hemlock
import { file_sha256 } from "@stdlib/hash";

// Compute checksum of downloaded file
let downloaded_file = "download.zip";
let expected_checksum = "a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3";

let actual_checksum = file_sha256(downloaded_file);

if (actual_checksum == expected_checksum) {
    print("✓ File integrity verified!");
} else {
    print("✗ File corrupted or tampered!");
    print("Expected: " + expected_checksum);
    print("Actual:   " + actual_checksum);
}
```

---

## Complete Example: Hash Table with MurmurHash3

```hemlock
import { murmur3 } from "@stdlib/hash";

// Simple hash table implementation
fn create_hash_table(size: i32) {
    let buckets = [];
    let i = 0;
    while (i < size) {
        buckets.push([]);
        i = i + 1;
    }
    return buckets;
}

fn hash_insert(table, key: string, value) {
    let hash = murmur3(key);
    let bucket_index = (hash % table.length);
    if (bucket_index < 0) {
        bucket_index = bucket_index + table.length;
    }
    table[bucket_index].push({key: key, value: value});
    return null;
}

// Usage
let table = create_hash_table(16);
hash_insert(table, "name", "Alice");
hash_insert(table, "age", 30);
```

---

## Performance Characteristics

### Non-Cryptographic Hashes

| Function | Speed | Distribution | Use Case |
|----------|-------|--------------|----------|
| djb2 | ⚡⚡⚡ Very Fast | Good | Simple hash tables |
| fnv1a | ⚡⚡⚡ Very Fast | Better | Hash tables, string interning |
| murmur3 | ⚡⚡ Fast | Excellent | Production systems |

### Cryptographic Hashes

| Function | Speed | Security | Output Size |
|----------|-------|----------|-------------|
| SHA-256 | ⚡ Medium | ✓ Secure | 256-bit (64 hex) |
| SHA-512 | ⚡ Medium | ✓ Very Secure | 512-bit (128 hex) |
| MD5 | ⚡⚡ Fast | ✗ Broken | 128-bit (32 hex) |

---

## Error Handling

All hash functions validate input types and throw exceptions on errors:

```hemlock
import { sha256, file_sha256 } from "@stdlib/hash";

// Type validation
try {
    sha256(123);  // Error: requires string
} catch (e) {
    print("Error: " + e);
}

// File not found
try {
    file_sha256("/nonexistent/file.txt");
} catch (e) {
    print("File error: " + e);
}
```

---

## Implementation Details

### Non-Crypto Hash Algorithms
- **djb2**: `hash = hash * 33 + byte` (using bit shifts for speed)
- **fnv1a**: `hash = (hash XOR byte) * FNV_PRIME` with offset basis 2166136261
- **murmur3**: 32-bit MurmurHash3 with finalization mix, processes 4-byte chunks

### Crypto Hash Functions
- **OpenSSL FFI**: Uses OpenSSL's libcrypto.so.3 via FFI
- **Memory management**: Manual allocation and cleanup with proper error handling
- **Byte reading**: Uses `__read_u32()` internal builtin for efficient memory access
- **Empty strings**: Special handling to ensure alloc(positive_size)

### Return Types
- **Non-crypto**: i32 (signed 32-bit integer)
- **Crypto**: string (hexadecimal, lowercase)
- **File checksums**: string (format depends on hash function)

---

## Testing

The hash module includes comprehensive tests:

```bash
# Run all hash tests
./hemlock tests/stdlib_hash/test_non_crypto.hml
./hemlock tests/stdlib_hash/test_crypto.hml
./hemlock tests/stdlib_hash/test_file_checksum.hml
```

**Test coverage:**
- Non-crypto: djb2, fnv1a, murmur3 with various inputs
- Crypto: SHA-256, SHA-512, MD5 with known test vectors
- File checksums: All hash functions with test files
- Edge cases: Empty strings, UTF-8, long strings

---

## Security Considerations

### DO use for security:
- ✓ SHA-256 for passwords (with salt)
- ✓ SHA-512 for high-security applications
- ✓ SHA-256/512 for file integrity verification
- ✓ SHA-256/512 for digital signatures

### DO NOT use for security:
- ✗ MD5 (cryptographically broken)
- ✗ djb2, fnv1a, murmur3 (not designed for security)

### Best Practices:
1. **Always use salt** when hashing passwords
2. **Use SHA-256 or SHA-512** for security applications
3. **Use murmur3** for hash tables (best distribution)
4. **Verify checksums** for downloaded files
5. **Never roll your own crypto** - use these proven implementations

---

## Changelog

### v0.1 (Initial Release)
- Non-cryptographic hashes: djb2, fnv1a, murmur3
- Cryptographic hashes: SHA-256, SHA-512, MD5 (via OpenSSL FFI)
- File checksum functions
- Comprehensive test suite
- UTF-8 support for all functions

---

## See Also

- `@stdlib/collections` - HashMap uses djb2 internally
- `@stdlib/encoding` - Base64, hex encoding for binary data
- `@stdlib/fs` - File operations for use with checksums

---

## Contributing

The hash module is part of Hemlock's standard library. For bugs or feature requests, see the main Hemlock repository.
