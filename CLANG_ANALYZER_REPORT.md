# Clang Static Analyzer Report

**Date:** 2024-12-14
**Files Analyzed:** `src/lsp/*.c`, `runtime/src/*.c`

## Summary

The Clang Static Analyzer found **100+ potential issues** across the codebase. Most issues are in `runtime/src/builtins.c`.

## Critical Issues

### 1. Null Pointer Dereferences (50+ instances)

Many functions extract pointers from `HmlValue` structs but continue execution even after null checks fail:

```c
// Example pattern in builtins.c:494-502
if (command.type != HML_VAL_STRING || !command.as.as_string) {
    // Should return here, but falls through
}
HmlString *cmd_str = command.as.as_string;  // Can be NULL
for (int64_t i = 0; i < cmd_str->length; i++) {  // NULL dereference
```

**Affected functions:**
- `hml_exec()` - lines 502, 517
- `hml_exec_argv()` - lines 604, 622, 631
- `hml_array_*()` - multiple array operations
- `hml_buffer_*()` - buffer operations
- `hml_object_*()` - object operations
- `hml_socket_*()` - network operations (lines 6313-6703)

### 2. Use After Free (20+ instances)

Memory is accessed after being freed in several locations:

```c
// builtins.c:3904-3943 - JSON string parsing
*out++ = '\n';  // Use after free
```

**Affected areas:**
- JSON parsing (lines 3904-3947)
- Object field parsing (lines 4079-4121)
- Array element parsing (lines 4185-4205)
- Poll wrapper (lines 6011-6045)
- FFI callback creation (lines 7119-7148)

### 3. Division by Zero (3 instances)

Modulo operations without zero checks:

- Line 1533: `return hml_val_i32(l % r);`
- Line 1562: `return hml_val_i64(l % r);`
- Line 1730: `return make_int_result(result_type, l % r);`

### 4. Double Free / Free of Released Memory (10+ instances)

```c
// builtins.c:5989-6007 - Poll cleanup
free(pfds);           // First free
// ... error handling ...
free(pfds);           // Double free
```

### 5. Uninitialized Values (5 instances)

- Line 267 (protocol.c): Array subscript with uninitialized index
- Line 1705-1707: Uninitialized `result` in float conversion
- Line 2547: Uninitialized `rune_val` comparison
- Lines 6467, 6551: Uninitialized buffer passed to send/sendto

### 6. Dead Stores (3 instances)

Variables written but never read:
- Line 2394: `pos += len_c;`
- Line 2428: `pos += len_d;`
- Line 2466: `pos += len_e;`

### 7. Null Function Pointer Calls (6 instances)

FFI function pointer calls without null checks:
- Lines 4428-4461: `((HmlFn*)fn_ptr)(...)` where `fn_ptr` may be NULL

## Medium Priority Issues

### Missing Return After Error Check

Multiple functions check for errors but don't return, leading to execution with invalid state:

```c
if (arr->type != HML_VAL_ARRAY || !arr.as.as_array) {
    hml_runtime_error("...");
    // Missing: return hml_val_null();
}
// Continues with null pointer
```

### DNS Resolution Without Null Check

```c
// Line 6430
struct hostent *host = gethostbyname(...);
memcpy(..., host->h_addr_list[0], ...);  // host may be NULL
```

## Recommended Fixes

### 1. Add proper null checks with early returns:

```c
if (command.type != HML_VAL_STRING || !command.as.as_string) {
    hml_runtime_error("exec() requires a string argument");
    return hml_val_null();  // ADD THIS
}
```

### 2. Add division by zero checks:

```c
if (r == 0) {
    hml_runtime_error("Division by zero");
    return hml_val_null();
}
return hml_val_i32(l % r);
```

### 3. Fix memory management in error paths:

```c
char *buf = malloc(size);
if (!buf) {
    return hml_val_null();  // Don't continue
}
// ... use buf ...
free(buf);  // Only free once
```

### 4. Initialize variables before use:

```c
double result = 0.0;  // Initialize
// ... computation ...
return hml_val_f64(result);
```

## Files Requiring Attention

| File | Issues | Severity |
|------|--------|----------|
| `runtime/src/builtins.c` | 95+ | Critical |
| `src/lsp/protocol.c` | 1 | Medium |

## Notes

- Many issues follow the same pattern: missing return after error handling
- The FFI and networking code has the most complex memory management issues
- JSON parsing has significant use-after-free concerns

## Running the Analyzer

```bash
# Analyze specific files with include paths
clang --analyze -I src/include -I src/frontend \
    src/lsp/*.c runtime/src/*.c 2>&1

# Full project analysis (requires scan-build)
scan-build --status-bugs make
```
