# Hemlock Crypto Module

Comprehensive cryptographic functions via OpenSSL FFI for secure encryption, signing, and random number generation.

## Overview

The `@stdlib/crypto` module provides:
- **Secure Random Bytes**: Cryptographically secure random number generation (RAND_bytes)
- **AES-256-CBC**: Symmetric encryption/decryption with proper padding
- **RSA Signatures**: 2048-bit RSA signing and verification with SHA-256
- **ECDSA Signatures**: P-256 elliptic curve signing and verification with SHA-256
- **Utility Functions**: Hex encoding/decoding for keys and signatures

### System Requirements

**Requires OpenSSL (same as @stdlib/hash):**
- Hemlock links against `libcrypto.so.3` (OpenSSL 3.x)
- On Debian/Ubuntu: `sudo apt-get install libssl-dev` (for building)
- Runtime requires `libcrypto.so.3` (usually pre-installed)
- On macOS: Install OpenSSL via Homebrew

## Usage

```hemlock
// Import specific functions
import { random_bytes, aes_encrypt, aes_decrypt } from "@stdlib/crypto";
import { rsa_generate_key, rsa_sign, rsa_verify } from "@stdlib/crypto";
import { ecdsa_generate_key, ecdsa_sign, ecdsa_verify } from "@stdlib/crypto";

// Or import all as namespace
import * as crypto from "@stdlib/crypto";
let key = crypto.generate_aes_key();
```

---

## Secure Random Bytes

### random_bytes(size: i32): buffer

Generate cryptographically secure random bytes using OpenSSL's RAND_bytes.

```hemlock
import { random_bytes } from "@stdlib/crypto";

// Generate random bytes
let random = random_bytes(32);
print(random.length);  // 32

// Generate secret token
let token = random_bytes(16);
print(buffer_to_hex(token));  // "3a7f2c9e..." (32 hex chars)

// Generate nonce
let nonce = random_bytes(12);
```

**Properties:**
- Uses OpenSSL's CSPRNG (Cryptographically Secure Pseudo-Random Number Generator)
- Suitable for cryptographic keys, IVs, tokens, nonces
- Throws exception if RAND_bytes fails (extremely rare)

**Use cases:**
- ✓ Generating encryption keys
- ✓ Generating IVs (initialization vectors)
- ✓ Session tokens
- ✓ Nonces for protocols
- ✓ Cryptographic salts

**Security notes:**
- DO NOT use `@stdlib/math/rand()` for security - use `random_bytes()`
- OpenSSL's RAND_bytes is properly seeded from system entropy
- Suitable for all cryptographic purposes

---

## AES-256-CBC Encryption

AES (Advanced Encryption Standard) with 256-bit keys in CBC (Cipher Block Chaining) mode. Industry-standard symmetric encryption.

### generate_aes_key(): buffer

Generate a secure 256-bit (32-byte) AES key.

```hemlock
import { generate_aes_key } from "@stdlib/crypto";

let key = generate_aes_key();
print(key.length);  // 32 bytes
```

### generate_iv(): buffer

Generate a secure 128-bit (16-byte) initialization vector (IV).

```hemlock
import { generate_iv } from "@stdlib/crypto";

let iv = generate_iv();
print(iv.length);  // 16 bytes
```

**Important:** Always use a **new random IV** for each encryption, even with the same key.

### aes_encrypt(plaintext: string, key: buffer, iv: buffer): buffer

Encrypt plaintext using AES-256-CBC with PKCS#7 padding.

```hemlock
import { generate_aes_key, generate_iv, aes_encrypt } from "@stdlib/crypto";

let plaintext = "Secret message!";
let key = generate_aes_key();
let iv = generate_iv();

let ciphertext = aes_encrypt(plaintext, key, iv);
print("Encrypted " + typeof(plaintext.length) + " bytes to " + typeof(ciphertext.length));
```

**Parameters:**
- `plaintext`: String to encrypt (UTF-8)
- `key`: 32-byte (256-bit) AES key
- `iv`: 16-byte (128-bit) initialization vector

**Returns:** Buffer containing encrypted data with PKCS#7 padding

**Throws:**
- If key is not 32 bytes
- If IV is not 16 bytes
- If encryption fails

### aes_decrypt(ciphertext: buffer, key: buffer, iv: buffer): string

Decrypt ciphertext using AES-256-CBC, removing PKCS#7 padding.

```hemlock
import { aes_decrypt } from "@stdlib/crypto";

// Decrypt using same key and IV as encryption
let decrypted = aes_decrypt(ciphertext, key, iv);
print(decrypted);  // "Secret message!"
```

**Parameters:**
- `ciphertext`: Buffer containing encrypted data
- `key`: Same 32-byte key used for encryption
- `iv`: Same 16-byte IV used for encryption

**Returns:** Decrypted plaintext string

**Throws:**
- If key or IV is wrong size
- If decryption fails (wrong key, corrupted data, or wrong IV)
- "EVP_DecryptFinal_ex() failed" if padding is invalid

### Complete AES Example

```hemlock
import {
    generate_aes_key,
    generate_iv,
    aes_encrypt,
    aes_decrypt,
    buffer_to_hex
} from "@stdlib/crypto";

// Generate key and IV
let key = generate_aes_key();
let iv = generate_iv();

print("Key: " + buffer_to_hex(key));
print("IV:  " + buffer_to_hex(iv));

// Encrypt
let message = "This is a secret message!";
let encrypted = aes_encrypt(message, key, iv);

print("Encrypted (" + typeof(encrypted.length) + " bytes): " + buffer_to_hex(encrypted));

// Decrypt
let decrypted = aes_decrypt(encrypted, key, iv);
print("Decrypted: " + decrypted);

// Verify
if (decrypted == message) {
    print("✓ Encryption/decryption successful!");
}
```

**Output:**
```
Key: f3a8b2c7d4e1f9... (64 hex chars)
IV:  9e4d3c2b1a0f8e... (32 hex chars)
Encrypted (32 bytes): 7a2f4c8d3b1e...
Decrypted: This is a secret message!
✓ Encryption/decryption successful!
```

---

## RSA Signatures

RSA (Rivest-Shamir-Adleman) digital signatures with 2048-bit keys using SHA-256 hashing.

### rsa_generate_key(): RSAKeyPair

Generate a 2048-bit RSA key pair for signing and verification.

```hemlock
import { rsa_generate_key, rsa_free_keys } from "@stdlib/crypto";

let keypair = rsa_generate_key();
defer rsa_free_keys(keypair);  // Always free keys when done

print(typeof(keypair));  // "RSAKeyPair"
```

**Returns:** RSAKeyPair object with `private_key` and `public_key` fields

**Important:** RSA key generation is computationally expensive (may take 100ms-1s). Keys must be freed with `rsa_free_keys()` to avoid memory leaks.

### rsa_sign(data: string, keypair: RSAKeyPair): buffer

Sign data with RSA private key using SHA-256 digest.

```hemlock
import { rsa_sign } from "@stdlib/crypto";

let data = "Important message";
let signature = rsa_sign(data, keypair);

print("Signature length: " + typeof(signature.length));  // ~256 bytes for 2048-bit RSA
```

**Parameters:**
- `data`: String to sign
- `keypair`: RSA key pair (uses private_key)

**Returns:** Buffer containing RSA signature (PKCS#1 v1.5 padding, ~256 bytes)

**Throws:**
- If keypair is invalid
- If signing fails

### rsa_verify(data: string, signature: buffer, keypair: RSAKeyPair): bool

Verify RSA signature with public key using SHA-256 digest.

```hemlock
import { rsa_verify } from "@stdlib/crypto";

let valid = rsa_verify(data, signature, keypair);

if (valid) {
    print("✓ Signature is valid");
} else {
    print("✗ Signature is invalid");
}
```

**Parameters:**
- `data`: Original string that was signed
- `signature`: RSA signature buffer
- `keypair`: RSA key pair (uses public_key)

**Returns:** `true` if signature is valid, `false` otherwise

**Security:** Signature verification is constant-time safe (OpenSSL handles this).

### rsa_free_keys(keypair: RSAKeyPair): null

Free RSA key pair memory. Always call when done with keys.

```hemlock
rsa_free_keys(keypair);
```

### Complete RSA Example

```hemlock
import {
    rsa_generate_key,
    rsa_sign,
    rsa_verify,
    rsa_free_keys,
    buffer_to_hex
} from "@stdlib/crypto";

// Generate key pair (slow operation)
print("Generating RSA-2048 key pair...");
let keypair = rsa_generate_key();
defer rsa_free_keys(keypair);

// Sign a message
let message = "Authenticate this message";
let signature = rsa_sign(message, keypair);

print("Message: " + message);
print("Signature (" + typeof(signature.length) + " bytes): " + buffer_to_hex(signature).substr(0, 64) + "...");

// Verify signature
let valid = rsa_verify(message, signature, keypair);
print("Signature valid: " + typeof(valid));  // true

// Tamper with message
let tampered = "Authenticate this messag3";  // Changed last character
let still_valid = rsa_verify(tampered, signature, keypair);
print("Tampered message valid: " + typeof(still_valid));  // false
```

---

## ECDSA Signatures

ECDSA (Elliptic Curve Digital Signature Algorithm) with P-256 curve (secp256r1) using SHA-256 hashing. Smaller keys than RSA with equivalent security.

### ecdsa_generate_key(): ECDSAKeyPair

Generate a P-256 ECDSA key pair for signing and verification.

```hemlock
import { ecdsa_generate_key, ecdsa_free_keys } from "@stdlib/crypto";

let keypair = ecdsa_generate_key();
defer ecdsa_free_keys(keypair);

print(typeof(keypair));  // "ECDSAKeyPair"
```

**Returns:** ECDSAKeyPair object with `private_key` and `public_key` fields

**Note:** ECDSA key generation is much faster than RSA (~10ms vs ~1000ms).

### ecdsa_sign(data: string, keypair: ECDSAKeyPair): buffer

Sign data with ECDSA private key using SHA-256 digest.

```hemlock
import { ecdsa_sign } from "@stdlib/crypto";

let data = "Important message";
let signature = ecdsa_sign(data, keypair);

print("Signature length: " + typeof(signature.length));  // ~70-72 bytes (DER-encoded)
```

**Parameters:**
- `data`: String to sign
- `keypair`: ECDSA key pair (uses private_key)

**Returns:** Buffer containing ECDSA signature (DER-encoded, ~70 bytes)

**Note:** ECDSA signatures are much smaller than RSA (~70 bytes vs ~256 bytes).

### ecdsa_verify(data: string, signature: buffer, keypair: ECDSAKeyPair): bool

Verify ECDSA signature with public key using SHA-256 digest.

```hemlock
import { ecdsa_verify } from "@stdlib/crypto";

let valid = ecdsa_verify(data, signature, keypair);

if (valid) {
    print("✓ Signature is valid");
} else {
    print("✗ Signature is invalid");
}
```

**Parameters:**
- `data`: Original string that was signed
- `signature`: ECDSA signature buffer (DER-encoded)
- `keypair`: ECDSA key pair (uses public_key)

**Returns:** `true` if signature is valid, `false` otherwise

### ecdsa_free_keys(keypair: ECDSAKeyPair): null

Free ECDSA key pair memory. Always call when done with keys.

```hemlock
ecdsa_free_keys(keypair);
```

### Complete ECDSA Example

```hemlock
import {
    ecdsa_generate_key,
    ecdsa_sign,
    ecdsa_verify,
    ecdsa_free_keys,
    buffer_to_hex
} from "@stdlib/crypto";

// Generate key pair (fast operation)
print("Generating ECDSA P-256 key pair...");
let keypair = ecdsa_generate_key();
defer ecdsa_free_keys(keypair);

// Sign a message
let message = "Authenticate this message";
let signature = ecdsa_sign(message, keypair);

print("Message: " + message);
print("Signature (" + typeof(signature.length) + " bytes): " + buffer_to_hex(signature));

// Verify signature
let valid = ecdsa_verify(message, signature, keypair);
print("Signature valid: " + typeof(valid));  // true

// Tamper with message
let tampered = "Authenticate this messag3";
let still_valid = ecdsa_verify(tampered, signature, keypair);
print("Tampered message valid: " + typeof(still_valid));  // false
```

---

## Utility Functions

### buffer_to_hex(buf: buffer): string

Convert buffer to hexadecimal string representation.

```hemlock
import { buffer_to_hex } from "@stdlib/crypto";

let bytes = random_bytes(16);
let hex = buffer_to_hex(bytes);
print(hex);  // "3a7f2c9e4d1b8f6a..."
print(hex.length);  // 32 (2 hex chars per byte)
```

**Use cases:**
- Display keys, IVs, signatures
- Store binary data as text
- Debug cryptographic operations

### hex_to_buffer(hex: string): buffer

Convert hexadecimal string to buffer.

```hemlock
import { hex_to_buffer } from "@stdlib/crypto";

let hex = "48656c6c6f";  // "Hello" in hex
let buf = hex_to_buffer(hex);
print(buf.length);  // 5
```

**Note:** Hex string must have even length (each byte = 2 hex chars).

---

## Comparison: RSA vs ECDSA

| Feature | RSA-2048 | ECDSA P-256 |
|---------|----------|-------------|
| **Key generation** | ~1000ms (slow) | ~10ms (fast) |
| **Signature size** | ~256 bytes | ~70 bytes |
| **Verification speed** | Medium | Fast |
| **Security level** | 112-bit | 128-bit |
| **Use cases** | Legacy systems, wide compatibility | Modern systems, blockchain, IoT |

**Recommendation:**
- Use **ECDSA** for new systems (smaller keys, faster, stronger)
- Use **RSA** for compatibility with legacy systems

---

## Complete Example: Secure File Storage

Encrypt a file with AES and sign it with ECDSA to ensure authenticity and integrity.

```hemlock
import { generate_aes_key, generate_iv, aes_encrypt, aes_decrypt } from "@stdlib/crypto";
import { ecdsa_generate_key, ecdsa_sign, ecdsa_verify, ecdsa_free_keys } from "@stdlib/crypto";
import { buffer_to_hex, hex_to_buffer } from "@stdlib/crypto";
import { write_file, read_file } from "@stdlib/fs";

// Step 1: Generate keys
let aes_key = generate_aes_key();
let aes_iv = generate_iv();
let signing_key = ecdsa_generate_key();
defer ecdsa_free_keys(signing_key);

print("Keys generated");

// Step 2: Encrypt data
let plaintext = "Sensitive financial data: $1,000,000 transaction";
let ciphertext = aes_encrypt(plaintext, aes_key, aes_iv);

print("Data encrypted (" + typeof(ciphertext.length) + " bytes)");

// Step 3: Sign ciphertext
let ciphertext_hex = buffer_to_hex(ciphertext);
let signature = ecdsa_sign(ciphertext_hex, signing_key);

print("Data signed (" + typeof(signature.length) + " bytes)");

// Step 4: Save to file (in real app, you'd save key/IV securely)
write_file("encrypted.bin", ciphertext_hex);
write_file("signature.bin", buffer_to_hex(signature));

print("Files saved");

// Step 5: Load and verify
let loaded_ciphertext_hex = read_file("encrypted.bin");
let loaded_signature_hex = read_file("signature.bin");

let loaded_ciphertext = hex_to_buffer(loaded_ciphertext_hex);
let loaded_signature = hex_to_buffer(loaded_signature_hex);

// Step 6: Verify signature
let valid = ecdsa_verify(loaded_ciphertext_hex, loaded_signature, signing_key);

if (!valid) {
    panic("Signature verification failed! File may be tampered.");
}

print("✓ Signature verified");

// Step 7: Decrypt
let decrypted = aes_decrypt(loaded_ciphertext, aes_key, aes_iv);

print("Decrypted: " + decrypted);

if (decrypted == plaintext) {
    print("✓ File integrity verified!");
}
```

---

## Security Best Practices

### DO:
1. ✓ **Always use random IVs** - Generate new IV for each AES encryption
2. ✓ **Store keys securely** - Never hardcode keys in source code
3. ✓ **Use defer for cleanup** - Always free key pairs with defer
4. ✓ **Verify signatures** - Check return value of verify functions
5. ✓ **Use ECDSA for new projects** - Smaller, faster, stronger than RSA
6. ✓ **Combine encryption + signing** - Encrypt data, then sign ciphertext

### DON'T:
1. ✗ **Never reuse IVs** - Each encryption needs a fresh IV
2. ✗ **Never reuse keys across applications** - Separate keys for each purpose
3. ✗ **Never roll your own crypto** - Use these proven implementations
4. ✗ **Never ignore verification failures** - Throw or panic on invalid signatures
5. ✗ **Never use ECB mode** - This module uses CBC (secure)
6. ✗ **Never store private keys unencrypted** - Protect keys at rest

### Common Pitfalls:
- **IV reuse**: Catastrophic for security - always generate new IV
- **Key storage**: Keys in source code can be extracted - use environment variables or key management systems
- **No authentication**: Encryption alone doesn't prevent tampering - use signatures or HMAC
- **Memory leaks**: Always free keys with `defer` or explicit free calls

---

## Performance Characteristics

### Symmetric Encryption (AES-256-CBC)
- **Encryption**: ~50 MB/s (depends on message size)
- **Key generation**: ~1ms (random bytes)
- **IV generation**: ~0.1ms (random bytes)

### Asymmetric Signatures (2048-bit RSA)
- **Key generation**: ~1000ms (very slow, do once)
- **Signing**: ~5ms
- **Verification**: ~0.5ms

### Asymmetric Signatures (ECDSA P-256)
- **Key generation**: ~10ms (100x faster than RSA)
- **Signing**: ~1ms
- **Verification**: ~2ms

**Recommendations:**
- Use **AES for bulk data** (fast, efficient)
- Use **ECDSA for signatures** (small keys, fast)
- Cache key pairs - don't regenerate for each operation

---

## Error Handling

All crypto functions validate inputs and throw exceptions on errors:

```hemlock
import { aes_encrypt, generate_aes_key, generate_iv } from "@stdlib/crypto";

// Wrong key size
try {
    let bad_key = random_bytes(16);  // Too small
    aes_encrypt("data", bad_key, generate_iv());
} catch (e) {
    print("Error: " + e);  // "aes_encrypt() requires 32-byte (256-bit) key"
}

// Wrong IV size
try {
    aes_encrypt("data", generate_aes_key(), random_bytes(8));  // Too small
} catch (e) {
    print("Error: " + e);  // "aes_encrypt() requires 16-byte (128-bit) iv"
}

// Decryption failure (wrong key or corrupted data)
try {
    let ciphertext = aes_encrypt("data", key1, iv);
    aes_decrypt(ciphertext, key2, iv);  // Different key
} catch (e) {
    print("Error: " + e);  // "EVP_DecryptFinal_ex() failed..."
}
```

---

## Implementation Details

### OpenSSL EVP API
- Uses high-level EVP (Envelope) API for all operations
- Thread-safe (each operation uses separate contexts)
- Automatic memory management for OpenSSL objects
- Proper error checking after each OpenSSL call

### Memory Management
- All keys/buffers must be manually freed
- Use `defer` to ensure cleanup on all code paths
- OpenSSL contexts are freed automatically
- No memory leaks in normal operation

### Algorithms
- **AES**: 256-bit keys, CBC mode, PKCS#7 padding
- **RSA**: 2048-bit keys, PKCS#1 v1.5 padding, SHA-256 digest
- **ECDSA**: P-256 curve (secp256r1), DER-encoded signatures, SHA-256 digest
- **Random**: OpenSSL's RAND_bytes (properly seeded CSPRNG)

### FFI Details
- Direct calls to OpenSSL's libcrypto.so.3
- Uses `extern fn` declarations for C functions
- Manual marshaling of data between Hemlock and C
- Proper handling of pointer types and sizes

---

## Testing

The crypto module includes comprehensive tests:

```bash
# Run all crypto tests
./hemlock tests/stdlib_crypto/test_random.hml
./hemlock tests/stdlib_crypto/test_aes.hml
./hemlock tests/stdlib_crypto/test_rsa.hml
./hemlock tests/stdlib_crypto/test_ecdsa.hml
./hemlock tests/stdlib_crypto/test_integration.hml
```

**Test coverage:**
- Random bytes generation and validation
- AES encryption/decryption with various message sizes
- AES error cases (wrong key/IV size, corrupted data)
- RSA key generation, signing, verification
- ECDSA key generation, signing, verification
- Signature tampering detection
- Integration test: encrypted + signed file storage

---

## Changelog

### v0.1 (Initial Release)
- Secure random bytes generation (RAND_bytes)
- AES-256-CBC encryption/decryption with PKCS#7 padding
- RSA-2048 signing and verification with SHA-256
- ECDSA P-256 signing and verification with SHA-256
- Utility functions: buffer_to_hex, hex_to_buffer
- Comprehensive test suite
- Full error handling and validation

---

## See Also

- `@stdlib/hash` - SHA-256, SHA-512 for hashing (used internally for signatures)
- `@stdlib/encoding` - Base64 encoding for storing binary data as text
- `@stdlib/fs` - File operations for encrypted file storage

---

## References

- **AES**: NIST FIPS 197 (Advanced Encryption Standard)
- **RSA**: PKCS#1 v2.2 (RSA Cryptography Standard)
- **ECDSA**: FIPS 186-4 (Digital Signature Standard)
- **OpenSSL**: https://www.openssl.org/docs/
- **P-256 Curve**: Also known as secp256r1 or prime256v1

---

## Contributing

The crypto module is part of Hemlock's standard library. For bugs or feature requests, see the main Hemlock repository.

**Security issues:** Please report security vulnerabilities privately to the maintainers.
