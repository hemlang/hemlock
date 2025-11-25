# Investigation Summary: "Cannot promote to type" Error in @stdlib/crypto

**Date:** 2025-11-24
**Task:** Investigate "Cannot promote to type" error in @stdlib/crypto AES functions
**Status:** ✅ PRIMARY ISSUE FIXED, Secondary issue identified

---

## Root Cause Analysis

### Primary Issue: Buffer Return Type Annotations

**Symptom:**
- Tests failed with "Runtime error: Cannot promote to type"
- `generate_aes_key()` and `generate_iv()` were returning `null` instead of buffers
- AES encryption/decryption failed because key and IV were null

**Root Cause:**
Hemlock has a **critical bug** where functions with explicit `: buffer` return type annotations return `null` instead of the actual buffer value.

**Functions Affected:**
```hemlock
// BEFORE (broken):
export fn generate_aes_key(): buffer {
    return random_bytes(32);  // Returns null!
}

export fn generate_iv(): buffer {
    return random_bytes(16);  // Returns null!
}
```

**Fix Applied:**
Removed the `: buffer` return type annotations:
```hemlock
// AFTER (working):
export fn generate_aes_key() {
    return random_bytes(32);  // Returns buffer correctly
}

export fn generate_iv() {
    return random_bytes(16);  // Returns buffer correctly
}
```

**Verification:**
```bash
$ ./hemlock test_random_bytes.hml
random_bytes result type: buffer ✓
generate_aes_key result type: buffer ✓
generate_iv result type: buffer ✓
```

---

## Investigation Process

### Step 1: Module Caching Confusion
Initially suspected Hemlock was caching the old version of crypto.hml because debug prints weren't executing. Learned that:
- Hemlock HAS a module cache (src/module.c)
- Cache is per-process, reloads on each `./hemlock` invocation
- The real issue was exceptions being thrown before debug code executed

### Step 2: Exception Discovery
Added try/catch to reveal the actual error:
```
ERROR: Cannot convert type to target type
```

This exception was being thrown silently, making it appear that the function returned early.

### Step 3: Parameter Type Investigation
Checked argument types being passed to `aes_decrypt()`:
```
ciphertext type: buffer ✓
key type: null ✗  ← PROBLEM!
iv type: null ✗   ← PROBLEM!
```

### Step 4: Traced to Return Type Annotations
Discovered `generate_aes_key()` and `generate_iv()` had `: buffer` return type annotations that caused them to return null.

### Step 5: Fix and Verification
Removed return type annotations, verified functions now return buffers correctly.

---

## Hemlock Bug Report

**Bug:** Functions with `: buffer` return type annotation return `null`

**Severity:** Critical - breaks any function trying to return a buffer with explicit type

**Reproducible:**
```hemlock
export fn test(): buffer {
    let b = buffer(10);
    return b;  // Returns null instead of buffer!
}

let result = test();
print(typeof(result));  // "null" instead of "buffer"
```

**Workaround:** Remove explicit return type annotation, rely on type inference

**Impact:** Affects all @stdlib functions that explicitly annotate buffer returns

---

## Secondary Issue: AES Decryption Failure

**Status:** ❌ UNRESOLVED

**Symptom:**
After fixing the null buffer issue, AES encryption works but decryption fails:
```
ERROR: EVP_DecryptFinal_ex() failed (possibly wrong key/iv or corrupted data)
```

**Test Case:**
```bash
$ ./hemlock test_aes_minimal.hml
Generating key and IV... Done
Encrypting 'Hello'... Done
Decrypting... ERROR: EVP_DecryptFinal_ex() failed
```

**Analysis:**
- Key and IV generation works (verified with hex output)
- Encryption completes successfully
- Decryption fails at padding removal stage (EVP_DecryptFinal_ex)
- Same key/IV variables used for both operations
- Suggests possible buffer-to-ptr conversion issue or byte order problem

**Needs Investigation:**
1. How Hemlock FFI passes buffer arguments to OpenSSL functions
2. Whether buffer data is correctly preserved during encryption
3. Possible alignment or endianness issues in __read_u32 pattern

---

## Files Modified

- `stdlib/crypto.hml`: Removed `: buffer` return type annotations (lines 101, 106)
- `test_*.hml`: Created diagnostic test files

## Commits

- `952323a`: Fix buffer return type annotations in @stdlib/crypto
- `9b5c50d`: Clean up test files and document investigation

---

## Next Steps

1. ✅ PRIMARY TASK COMPLETE: "Cannot promote to type" error fixed
2. ⏭️ OPTIONAL: Investigate AES decryption failure (separate issue)
3. ⏭️ OPTIONAL: Report Hemlock buffer return type bug to maintainers
4. ⏭️ OPTIONAL: Audit all @stdlib functions for similar return type annotation issues

---

## Testing Commands

```bash
# Verify buffer return type fix
./hemlock test_random_bytes.hml

# Test key/IV generation
./hemlock test_key_iv.hml

# Minimal AES test (shows decryption issue)
./hemlock test_aes_minimal.hml

# Full test suite (still has failures due to decryption issue)
./hemlock tests/stdlib_crypto/test_aes.hml
```
