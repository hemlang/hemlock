# Hemlock FFI Utilities Module

A standard library module providing callback management utilities for the Foreign Function Interface (FFI) in Hemlock.

## Overview

The ffi module provides functions for creating and managing C-callable function pointers from Hemlock functions:

- **Callback creation** - Wrap Hemlock functions as C function pointers using libffi closures
- **Callback cleanup** - Free callback resources when no longer needed

These functions enable integration with C APIs that expect function pointers, such as `qsort`, event loops, signal handlers, and callback-based libraries.

For general FFI documentation (loading shared libraries, declaring extern functions, pointer helpers, structs), see `docs/advanced/ffi.md`.

## Usage

```hemlock
import { callback, callback_free } from "@stdlib/ffi";

fn my_compare(a: ptr, b: ptr): i32 {
    let va = ptr_read_i32(a);
    let vb = ptr_read_i32(b);
    return va - vb;
}

let cmp = callback(my_compare, ["ptr", "ptr"], "i32");
// ... pass cmp to a C function expecting a function pointer ...
callback_free(cmp);
```

Or import all:

```hemlock
import * as ffi from "@stdlib/ffi";
let cb = ffi.callback(my_fn, ["i32"], "void");
```

---

## Functions

### callback(fn, param_types, return_type)
Creates a C-callable function pointer from a Hemlock function. The returned pointer can be passed to any C function that expects a callback, such as `qsort`.

**Parameters:**
- `fn: fn` - A Hemlock function to wrap as a C callback
- `param_types: array` - Array of type name strings describing the callback's parameter types
- `return_type: string` - Type name string describing the callback's return type

**Returns:** `ptr` - A C function pointer that can be passed to extern functions

**Supported type names:**
- `"i8"`, `"i16"`, `"i32"`, `"i64"` - Signed integers
- `"u8"`, `"u16"`, `"u32"`, `"u64"` - Unsigned integers
- `"f32"`, `"f64"` - Floating point
- `"ptr"` - Pointer
- `"void"` - No return value (return type only)
- `"bool"` - Boolean

**Use cases:**
- Passing comparison functions to `qsort`
- Registering event handlers with C libraries
- Providing iteration callbacks to C APIs

```hemlock
import { callback } from "@stdlib/ffi";

// Simple callback that takes an i32 and returns void
fn print_value(val: i32): void {
    print(val);
}

let cb = callback(print_value, ["i32"], "void");
// cb is now a C function pointer of type void(*)(int32_t)
```

### callback_free(cb)
Frees a callback previously created with `callback()`. This releases the libffi closure and associated resources.

**Parameters:**
- `cb: ptr` - A callback pointer returned by `callback()`

**Returns:** `null`

**Use cases:**
- Cleaning up callbacks after they are no longer needed
- Preventing memory leaks in long-running programs

```hemlock
import { callback, callback_free } from "@stdlib/ffi";

let cb = callback(my_fn, ["ptr"], "i32");
// ... use cb ...
callback_free(cb);  // Free when done
```

**Note:** Callbacks are also automatically freed when the program exits, but explicit cleanup is recommended for long-running programs or when creating many callbacks.

---

## Examples

### Sorting with qsort

The classic use case for callbacks is passing a comparison function to C's `qsort`:

```hemlock
import { callback, callback_free } from "@stdlib/ffi";

import "libc.so.6";
extern fn qsort(base: ptr, nmemb: u64, size: u64, compar: ptr): void;

// Comparison function for ascending order
fn compare_ascending(a: ptr, b: ptr): i32 {
    let va = ptr_read_i32(a);
    let vb = ptr_read_i32(b);
    if (va < vb) { return -1; }
    if (va > vb) { return 1; }
    return 0;
}

// Allocate and fill an array of 5 integers
let arr = alloc(20);  // 5 * 4 bytes
ptr_write_i32(arr, 5);
ptr_write_i32(ptr_offset(arr, 1, 4), 2);
ptr_write_i32(ptr_offset(arr, 2, 4), 8);
ptr_write_i32(ptr_offset(arr, 3, 4), 1);
ptr_write_i32(ptr_offset(arr, 4, 4), 9);

// Create callback and sort
let cmp = callback(compare_ascending, ["ptr", "ptr"], "i32");
qsort(arr, 5, 4, cmp);

// Print sorted array
for (let i = 0; i < 5; i++) {
    print(ptr_read_i32(ptr_offset(arr, i, 4)));
}
// Output: 1 2 5 8 9

// Clean up
callback_free(cmp);
free(arr);
```

### Descending Sort

```hemlock
import { callback, callback_free } from "@stdlib/ffi";

import "libc.so.6";
extern fn qsort(base: ptr, nmemb: u64, size: u64, compar: ptr): void;

// Reverse comparison for descending order
fn compare_descending(a: ptr, b: ptr): i32 {
    let va = ptr_read_i32(a);
    let vb = ptr_read_i32(b);
    if (va > vb) { return -1; }
    if (va < vb) { return 1; }
    return 0;
}

let arr = alloc(12);  // 3 * 4 bytes
ptr_write_i32(arr, 10);
ptr_write_i32(ptr_offset(arr, 1, 4), 30);
ptr_write_i32(ptr_offset(arr, 2, 4), 20);

let cmp = callback(compare_descending, ["ptr", "ptr"], "i32");
qsort(arr, 3, 4, cmp);

for (let i = 0; i < 3; i++) {
    print(ptr_read_i32(ptr_offset(arr, i, 4)));
}
// Output: 30 20 10

callback_free(cmp);
free(arr);
```

### Closures in Callbacks

Callbacks capture their closure environment, allowing access to outer scope variables:

```hemlock
import { callback, callback_free } from "@stdlib/ffi";

let threshold = 5;

fn filter_compare(a: ptr, b: ptr): i32 {
    let va = ptr_read_i32(a);
    let vb = ptr_read_i32(b);
    // Access 'threshold' from outer scope
    let a_above = va > threshold;
    let b_above = vb > threshold;
    if (a_above && !b_above) { return -1; }
    if (!a_above && b_above) { return 1; }
    return va - vb;
}

let cmp = callback(filter_compare, ["ptr", "ptr"], "i32");
// ... use cmp with qsort ...
callback_free(cmp);
```

### Multiple Callbacks

You can create multiple callbacks simultaneously for different purposes:

```hemlock
import { callback, callback_free } from "@stdlib/ffi";

fn compare_i32(a: ptr, b: ptr): i32 {
    return ptr_read_i32(a) - ptr_read_i32(b);
}

fn compare_f64(a: ptr, b: ptr): i32 {
    let va = ptr_read_f64(a);
    let vb = ptr_read_f64(b);
    if (va < vb) { return -1; }
    if (va > vb) { return 1; }
    return 0;
}

let cmp_int = callback(compare_i32, ["ptr", "ptr"], "i32");
let cmp_float = callback(compare_f64, ["ptr", "ptr"], "i32");

// Use each callback as needed...

// Clean up all callbacks when done
callback_free(cmp_int);
callback_free(cmp_float);
```

---

## Important Notes

### Thread Safety

Callback invocations are serialized with a mutex to ensure thread safety, as the Hemlock interpreter is not fully thread-safe. This means:
- Only one callback can execute at a time
- Safe to use with multi-threaded C libraries
- May impact performance if callbacks are called very frequently from multiple threads

### Error Handling

Exceptions thrown inside callbacks cannot propagate to C code. Instead:
- A warning is printed to stderr
- The callback returns a default value (0 or NULL)
- The exception is logged but not propagated

```hemlock
import { callback, callback_free } from "@stdlib/ffi";

fn risky_callback(a: ptr): i32 {
    throw "Something went wrong";  // Warning printed, returns 0
}

let cb = callback(risky_callback, ["ptr"], "i32");
// If C code calls this callback, it will return 0
callback_free(cb);
```

For robust error handling, validate inputs and avoid throwing inside callbacks.

### Memory Ownership

- Each call to `callback()` allocates a libffi closure. Always call `callback_free()` when the callback is no longer needed.
- Callbacks keep a reference to the wrapped Hemlock function, so the function will not be garbage-collected while the callback exists.
- Creating many callbacks without freeing them will leak memory.

---

## API Reference

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `callback` | `(fn, param_types: array, return_type: string)` | `ptr` | Create a C-callable function pointer |
| `callback_free` | `(cb: ptr)` | `null` | Free a callback's resources |

---

## See Also

- **FFI documentation** (`docs/advanced/ffi.md`) - Full FFI guide including library loading, extern functions, structs, and pointer helpers
- **Memory management** - `alloc()`, `free()`, `ptr_read_*`, `ptr_write_*`, `ptr_offset()` builtins
- **Async module** (`@stdlib/async`) - For Hemlock-native concurrency (prefer channels over callbacks for Hemlock-to-Hemlock communication)

---

## License

Part of the Hemlock standard library.
