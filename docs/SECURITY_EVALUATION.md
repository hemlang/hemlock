# Hemlock Runtime Security Evaluation

**Date:** 2026-01-28
**Version:** 1.8.6
**Evaluator:** Automated Security Review

---

## Executive Summary

The Hemlock runtime implements a "systems scripting" language with explicit unsafe-by-design semantics. This evaluation identifies security vulnerabilities, mitigations present, and recommendations for improvement.

**Overall Risk Assessment:**
- **With sandbox enabled:** MEDIUM risk
- **Without sandbox:** HIGH risk (all unsafe features accessible)

---

## Table of Contents

1. [Critical Vulnerabilities](#critical-vulnerabilities)
2. [High Severity Issues](#high-severity-issues)
3. [Medium Severity Issues](#medium-severity-issues)
4. [Security Features (Positive)](#security-features-positive)
5. [By-Design Unsafe Features](#by-design-unsafe-features)
6. [Recommendations](#recommendations)

---

## Critical Vulnerabilities

### 1. Command Injection in `exec()` - CRITICAL

**Location:** `runtime/src/builtins_process.c:18-126`

**Issue:** The `hml_exec()` function uses `popen()` which passes commands through a shell, enabling command injection attacks.

```c
// Line 52: Direct shell execution
FILE *pipe = popen(ccmd, "r");
```

**Attack Vector:**
```hemlock
let user_input = "; rm -rf /";
exec("echo " + user_input);  // Executes: echo ; rm -rf /
```

**Current Mitigation:** Warning message printed for shell metacharacters (lines 30-42), but does NOT prevent execution.

**Safe Alternative Available:** `exec_argv()` (line 131) uses `fork()/execvp()` bypassing the shell entirely.

**Recommendation:** Deprecate `exec()` or require explicit opt-in for shell execution.

---

### 2. Missing Alignment Check for Atomic Operations - CRITICAL

**Location:** `runtime/src/atomics.c:45-52, 137-144`

**Issue:** Atomic operations cast raw pointers to `_Atomic` types without verifying alignment:

```c
HmlValue hml_atomic_load_i32(HmlValue ptr) {
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t value = atomic_load(p);  // UNDEFINED BEHAVIOR if misaligned!
    return hml_val_i32(value);
}
```

**Impact:**
- **x86/x64:** May work but with performance penalty
- **ARM/SPARC:** Bus error, crash, or silent data corruption
- **All platforms:** Undefined behavior per C11 standard

**Recommendation:** Add alignment check before atomic operations:
```c
if ((uintptr_t)ptr.as.as_ptr % _Alignof(_Atomic int32_t) != 0) {
    hml_runtime_error("Misaligned atomic pointer");
}
```

---

## High Severity Issues

### 3. Unbounded Pointer Arithmetic

**Location:** `runtime/src/builtins_memory.c:15-39`

**Issue:** Raw pointer operations have NO bounds checking:

```c
HmlValue hml_ptr_get(HmlValue ptr, HmlValue index) {
    int idx = hml_to_i32(index);
    // NO BOUNDS CHECKING - can read arbitrary memory!
    return hml_val_u8(((unsigned char *)p)[idx]);
}
```

**Impact:** Arbitrary memory read/write, potentially exposing:
- Sensitive data (passwords, keys)
- Internal runtime structures
- Other process memory regions

**Note:** This is intentional per Hemlock's design philosophy ("unsafe by design"). Use `buffer` type for bounds-checked access.

---

### 4. Integer Overflow in Array Capacity Doubling

**Location:** `runtime/src/builtins_array.c:35-36`

**Issue:** Array capacity doubling lacks overflow check:

```c
if (a->length >= a->capacity) {
    int new_cap = (a->capacity == 0) ? 8 : a->capacity * 2;
    // Missing: Check if a->capacity * 2 overflows
    ...
}
```

**Attack Vector:** Repeated pushes to a very large array could cause integer overflow, resulting in a small allocation and subsequent heap buffer overflow.

**Recommendation:** Add overflow check:
```c
if (a->capacity > INT_MAX / 2) {
    hml_runtime_error("Array capacity overflow");
}
```

---

### 5. FFI Library Path Traversal - Partial Mitigation

**Location:** `runtime/src/builtins_ffi.c:164-188`

**Issue:** Path validation has gaps:

```c
// Checks for ".." but could be bypassed with symlinks
if (strstr(path, "..")) {
    return "Library path contains directory traversal (..)";
}

// Only warns about /tmp, doesn't block
if (strncmp(path, "/tmp/", 5) == 0 || ...) {
    fprintf(stderr, "Warning: Loading FFI library from world-writable location...");
}
```

**Bypasses:**
- Symlinks: `/safe/path/symlink -> /malicious/lib.so`
- Hardlinks to malicious libraries
- Race conditions (TOCTOU)

**Recommendation:**
- Implement library directory whitelist
- Resolve symlinks before validation
- Consider code signing verification

---

## Medium Severity Issues

### 6. Signal Handler Race Condition

**Location:** `runtime/src/builtins_process.c:788-812`

**Issue:** Signal handler array is accessed without synchronization in the C signal handler:

```c
static void hml_c_signal_handler(int signum) {
    HmlValue handler = g_signal_handlers[signum];  // No lock
    // ...calling Hemlock function from signal context
}
```

**Impact:**
- Calling Hemlock functions from signal context is async-signal-unsafe
- Potential race between signal delivery and handler modification

**Recommendation:** Use signal-safe operations only; queue signals for main thread processing.

---

### 7. Missing Null Check After realloc()

**Location:** `runtime/src/builtins_memory.c:119-121`

**Issue:** `realloc()` failure returns null, but doesn't free original memory:

```c
void *new_ptr = realloc(ptr.as.as_ptr, new_size);
if (!new_ptr) {
    return hml_val_null();  // Original ptr still valid but may leak
}
```

**Impact:** Memory leak on allocation failure.

---

### 8. Double-Free Protection Inconsistency

**Location:** `runtime/include/hemlock_value.h:118`

**Issue:** Atomic `freed` flag exists for arrays/buffers but is not consistently used:

```c
_Atomic int freed;   // Present in HmlArray, HmlBuffer
// Missing in HmlTask, HmlChannel
```

**Recommendation:** Extend double-free protection to all heap types.

---

## Security Features (Positive)

### Sandbox Mode - STRONG

**Location:** `runtime/include/hemlock_runtime.h:31-37`

The runtime implements a comprehensive sandbox with granular controls:

```c
#define HML_SANDBOX_RESTRICT_FFI         0x0001  // Block dynamic library loading
#define HML_SANDBOX_RESTRICT_NETWORK     0x0002  // Block network operations
#define HML_SANDBOX_RESTRICT_PROCESS     0x0004  // Block process spawning
#define HML_SANDBOX_RESTRICT_FILE_WRITE  0x0008  // Block file writes
#define HML_SANDBOX_RESTRICT_FILE_READ   0x0010  // Block file reads
#define HML_SANDBOX_RESTRICT_SIGNALS     0x0020  // Block signal operations
#define HML_SANDBOX_RESTRICT_ALL         0x003F  // All restrictions
```

**Coverage verified in:**
- `builtins_process.c:20,133,662` - Process operations
- `builtins_ffi.c:309,705` - FFI loading/calling
- `builtins_socket.c:14` - Network operations
- `builtins_io.c:48-53` (via builtins_core.c) - File I/O
- `builtins_core.c:48,53` - Path-based file access

### Integer Overflow Protection - GOOD

Comprehensive overflow checks in critical paths:

- `builtins_memory.c:225-228` - `talloc()` multiplication overflow
- `builtins_string.c:351-356` - `replace_all()` result size
- `builtins_string.c:395-401` - `repeat()` result size
- `builtins_process.c:76-83` - Command output buffer growth

### Reference Counting - GOOD

Proper retain-before-release pattern prevents most use-after-free:

```c
// builtins_array.c:100-103
hml_retain(&val);                    // Retain FIRST
hml_release(&a->elements[idx]);      // Then release old
a->elements[idx] = val;              // Then assign
```

### Null Pointer Validation - GOOD

Consistent null checks before pointer dereference across all builtins (28+ occurrences verified).

### Type Checking - GOOD

Runtime type validation before operations in all builtin functions.

---

## By-Design Unsafe Features

These are intentional per Hemlock's design philosophy and documented:

| Feature | Risk | Safe Alternative |
|---------|------|------------------|
| Raw `ptr` type | Arbitrary memory access | Use `buffer` type |
| `alloc()`/`free()` | Double-free, use-after-free | Reference counting, `defer` |
| `exec()` | Command injection | Use `exec_argv()` |
| FFI | Native code execution | Sandbox mode |
| Pointer arithmetic | Buffer overflow | Use `buffer` bounds checking |

---

## Recommendations

### Priority 1 (Critical)

1. **Add alignment checks to atomic operations**
   - File: `runtime/src/atomics.c`
   - All `hml_atomic_*` functions need alignment verification

2. **Add overflow check to array capacity doubling**
   - File: `runtime/src/builtins_array.c:35`
   - Check before `capacity * 2`

### Priority 2 (High)

3. **Strengthen FFI path validation**
   - Implement library directory whitelist
   - Resolve symlinks before validation
   - Option to require code signatures

4. **Fix signal handler thread safety**
   - Queue signals for main thread
   - Use only async-signal-safe operations

5. **Document `exec()` deprecation**
   - Add prominent warnings in documentation
   - Consider renaming to `exec_shell()` with scary name

### Priority 3 (Medium)

6. **Extend double-free protection**
   - Add `freed` flag to `HmlTask`, `HmlChannel`

7. **Audit all `realloc()` calls**
   - Ensure proper handling of null return
   - Free original memory on failure

8. **Consider optional bounds-checked pointer mode**
   - Store allocation size with pointers
   - Enable with compile flag for debugging

---

## Testing Recommendations

1. Run with AddressSanitizer: `CFLAGS="-fsanitize=address"`
2. Run with UndefinedBehaviorSanitizer: `CFLAGS="-fsanitize=undefined"`
3. Fuzz test FFI, exec, and string operations
4. Test on ARM to catch alignment issues
5. Stress test concurrent operations with ThreadSanitizer

---

## Conclusion

The Hemlock runtime demonstrates security awareness with sandbox mode, overflow checks, and type validation. However, its intentionally unsafe design requires users to understand risks. The sandbox mode is **essential** for running any untrusted code.

**For production use:**
- Always enable sandbox for untrusted input
- Prefer `exec_argv()` over `exec()`
- Use `buffer` type instead of raw `ptr` when possible
- Ensure atomic operations use properly aligned memory
