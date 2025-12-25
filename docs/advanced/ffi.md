# Foreign Function Interface (FFI) in Hemlock

Hemlock provides **FFI (Foreign Function Interface)** to call C functions from shared libraries using libffi, enabling integration with existing C libraries and system APIs.

## Table of Contents

- [Overview](#overview)
- [Current Status](#current-status)
- [Supported Types](#supported-types)
- [Basic Concepts](#basic-concepts)
- [Exporting FFI Functions](#exporting-ffi-functions)
- [Use Cases](#use-cases)
- [Future Development](#future-development)
- [FFI Callbacks](#ffi-callbacks)
- [FFI Structs](#ffi-structs)
- [Exporting Struct Types](#exporting-struct-types)
- [Current Limitations](#current-limitations)
- [Best Practices](#best-practices)

## Overview

The Foreign Function Interface (FFI) allows Hemlock programs to:
- Call C functions from shared libraries (.so, .dylib, .dll)
- Use existing C libraries without writing wrapper code
- Access system APIs directly
- Integrate with third-party native libraries
- Bridge Hemlock with low-level system functionality

**Key capabilities:**
- Dynamic library loading
- C function binding
- Automatic type conversion between Hemlock and C types
- Support for all primitive types
- libffi-based implementation for portability

## Current Status

FFI support is available in Hemlock with the following features:

**Implemented:**
- ✅ Call C functions from shared libraries
- ✅ Support for all primitive types (integers, floats, pointers)
- ✅ Automatic type conversion
- ✅ libffi-based implementation
- ✅ Dynamic library loading
- ✅ **Function pointer callbacks** - Pass Hemlock functions to C
- ✅ **Export extern functions** - Share FFI bindings across modules
- ✅ **Struct passing and return values** - Pass C-compatible structs by value
- ✅ **Complete pointer helpers** - Read/write all types (i8-i64, u8-u64, f32, f64, ptr)
- ✅ **Buffer/pointer conversion** - `buffer_ptr()`, `ptr_to_buffer()`
- ✅ **FFI type sizes** - `ffi_sizeof()` for platform-aware type sizes
- ✅ **Platform types** - `size_t`, `usize`, `isize`, `intptr_t` support

**In Development:**
- 🔄 String marshaling helpers
- 🔄 Error handling improvements

**Test Coverage:**
- FFI tests passing including callback tests
- Basic function calling verified
- Type conversion tested
- qsort callback integration tested

## Supported Types

### Primitive Types

The following Hemlock types can be passed to/from C functions:

| Hemlock Type | C Type | Size | Notes |
|--------------|--------|------|-------|
| `i8` | `int8_t` | 1 byte | Signed 8-bit integer |
| `i16` | `int16_t` | 2 bytes | Signed 16-bit integer |
| `i32` | `int32_t` | 4 bytes | Signed 32-bit integer |
| `i64` | `int64_t` | 8 bytes | Signed 64-bit integer |
| `u8` | `uint8_t` | 1 byte | Unsigned 8-bit integer |
| `u16` | `uint16_t` | 2 bytes | Unsigned 16-bit integer |
| `u32` | `uint32_t` | 4 bytes | Unsigned 32-bit integer |
| `u64` | `uint64_t` | 8 bytes | Unsigned 64-bit integer |
| `f32` | `float` | 4 bytes | 32-bit floating point |
| `f64` | `double` | 8 bytes | 64-bit floating point |
| `ptr` | `void*` | 8 bytes | Raw pointer |

### Type Conversion

**Automatic conversions:**
- Hemlock integers → C integers (with range checking)
- Hemlock floats → C floats
- Hemlock pointers → C pointers
- C return values → Hemlock values

**Example type mappings:**
```hemlock
// Hemlock → C
let i: i32 = 42;         // → int32_t (4 bytes)
let f: f64 = 3.14;       // → double (8 bytes)
let p: ptr = alloc(64);  // → void* (8 bytes)

// C → Hemlock (return values)
// int32_t foo() → i32
// double bar() → f64
// void* baz() → ptr
```

## Basic Concepts

### Shared Libraries

FFI works with compiled shared libraries:

**Linux:** `.so` files
```
libexample.so
/usr/lib/libm.so
```

**macOS:** `.dylib` files
```
libexample.dylib
/usr/lib/libSystem.dylib
```

**Windows:** `.dll` files
```
example.dll
kernel32.dll
```

### Function Signatures

C functions must have known signatures for FFI to work correctly:

```c
// Example C function signatures
int add(int a, int b);
double sqrt(double x);
void* malloc(size_t size);
void free(void* ptr);
```

These can be called from Hemlock once the library is loaded and functions are bound.

### Platform Compatibility

FFI uses **libffi** for portability:
- Works on x86, x86-64, ARM, ARM64
- Handles calling conventions automatically
- Abstracts platform-specific ABI details
- Supports Linux, macOS, Windows (with appropriate libffi)

## Exporting FFI Functions

FFI functions declared with `extern fn` can be exported from modules, allowing you to create reusable library wrappers that can be shared across multiple files.

### Basic Export Syntax

```hemlock
// string_utils.hml - A library module wrapping C string functions
import "libc.so.6";

// Export the extern function directly
export extern fn strlen(s: string): i32;
export extern fn strcmp(s1: string, s2: string): i32;

// You can also export wrapper functions alongside extern functions
export fn string_length(s: string): i32 {
    return strlen(s);
}

export fn strings_equal(a: string, b: string): bool {
    return strcmp(a, b) == 0;
}
```

### Importing Exported FFI Functions

```hemlock
// main.hml - Using the exported FFI functions
import { strlen, string_length, strings_equal } from "./string_utils.hml";

let msg = "Hello, World!";
print(strlen(msg));           // 13 - direct extern call
print(string_length(msg));    // 13 - wrapper function

print(strings_equal("foo", "foo"));  // true
print(strings_equal("foo", "bar"));  // false
```

### Use Cases for Export Extern

**1. Platform Abstraction**
```hemlock
// platform.hml - Abstract platform differences
import "libc.so.6";  // Linux

export extern fn getpid(): i32;
export extern fn getuid(): i32;
export extern fn geteuid(): i32;
```

**2. Library Wrappers**
```hemlock
// crypto_lib.hml - Wrap crypto library functions
import "libcrypto.so";

export extern fn SHA256(data: ptr, len: u64, out: ptr): ptr;
export extern fn MD5(data: ptr, len: u64, out: ptr): ptr;

// Add Hemlock-friendly wrappers
export fn sha256_string(s: string): string {
    // Implementation using the extern function
}
```

**3. Centralized FFI Declarations**
```hemlock
// libc.hml - Central module for libc bindings
import "libc.so.6";

// String functions
export extern fn strlen(s: string): i32;
export extern fn strcpy(dest: ptr, src: string): ptr;
export extern fn strcat(dest: ptr, src: string): ptr;

// Memory functions
export extern fn malloc(size: u64): ptr;
export extern fn realloc(p: ptr, size: u64): ptr;
export extern fn calloc(nmemb: u64, size: u64): ptr;

// Process functions
export extern fn getpid(): i32;
export extern fn getppid(): i32;
export extern fn getenv(name: string): ptr;
```

Then use throughout your project:
```hemlock
import { strlen, malloc, getpid } from "./libc.hml";
```

### Combining with Regular Exports

You can mix exported extern functions with regular function exports:

```hemlock
// math_extended.hml
import "libm.so.6";

// Export raw C functions
export extern fn sin(x: f64): f64;
export extern fn cos(x: f64): f64;
export extern fn tan(x: f64): f64;

// Export Hemlock functions that use them
export fn deg_to_rad(degrees: f64): f64 {
    return degrees * 3.14159265359 / 180.0;
}

export fn sin_degrees(degrees: f64): f64 {
    return sin(deg_to_rad(degrees));
}
```

### Platform-Specific Libraries

When exporting extern functions, remember that library names differ by platform:

```hemlock
// For Linux
import "libc.so.6";

// For macOS (different approach needed)
import "libSystem.B.dylib";
```

Currently, Hemlock's `import "library"` syntax uses static library paths, so platform-specific modules may be needed for cross-platform FFI code.

## Use Cases

### 1. System Libraries

Access standard C library functions:

**Math functions:**
```hemlock
// Call sqrt from libm
let result = sqrt(16.0);  // 4.0
```

**Memory allocation:**
```hemlock
// Call malloc/free from libc
let ptr = malloc(1024);
free(ptr);
```

### 2. Third-Party Libraries

Use existing C libraries:

**Example: Image processing**
```hemlock
// Load libpng or libjpeg
// Process images using C library functions
```

**Example: Cryptography**
```hemlock
// Use OpenSSL or libsodium
// Encryption/decryption via FFI
```

### 3. System APIs

Direct system calls:

**Example: POSIX APIs**
```hemlock
// Call getpid, getuid, etc.
// Access low-level system functionality
```

### 4. Performance-Critical Code

Call optimized C implementations:

```hemlock
// Use highly-optimized C libraries
// SIMD operations, vectorized code
// Hardware-accelerated functions
```

### 5. Hardware Access

Interface with hardware libraries:

```hemlock
// GPIO control on embedded systems
// USB device communication
// Serial port access
```

### 6. Legacy Code Integration

Reuse existing C codebases:

```hemlock
// Call functions from legacy C applications
// Gradually migrate to Hemlock
// Preserve working C code
```

## Future Development

### Planned Features

**1. Struct Support**
```hemlock
// Future: Pass/return C structs
define Point {
    x: f64,
    y: f64,
}

let p = Point { x: 1.0, y: 2.0 };
c_function_with_struct(p);
```

**2. Array/Buffer Handling**
```hemlock
// Future: Better array passing
let arr = [1, 2, 3, 4, 5];
process_array(arr);  // Pass to C function
```

**3. Function Pointer Callbacks** ✅ (Implemented!)
```hemlock
// Pass Hemlock functions to C as callbacks
fn my_compare(a: ptr, b: ptr): i32 {
    let va = ptr_deref_i32(a);
    let vb = ptr_deref_i32(b);
    return va - vb;
}

// Create a C-callable function pointer
let cmp = callback(my_compare, ["ptr", "ptr"], "i32");

// Use with qsort or any C function expecting a callback
qsort(arr, count, elem_size, cmp);

// Clean up when done
callback_free(cmp);
```

**4. String Marshaling**
```hemlock
// Future: Automatic string conversion
let s = "hello";
c_string_function(s);  // Auto-convert to C string
```

**5. Error Handling**
```hemlock
// Future: Better error reporting
try {
    let result = risky_c_function();
} catch (e) {
    print("FFI error: " + e);
}
```

**6. Type Safety**
```hemlock
// Future: Type annotations for FFI
@ffi("libm.so")
fn sqrt(x: f64): f64;

let result = sqrt(16.0);  // Type-checked
```

### Features

**v1.0:**
- ✅ Basic FFI with primitive types
- ✅ Dynamic library loading
- ✅ Function calling
- ✅ Callback support via libffi closures

**Future:**
- Struct support
- Array handling improvements
- Automatic binding generation

## FFI Callbacks

Hemlock supports passing functions to C code as callbacks using libffi closures. This enables integration with C APIs that expect function pointers, such as `qsort`, event loops, and callback-based libraries.

### Creating Callbacks

Use `callback()` to create a C-callable function pointer from a Hemlock function:

```hemlock
// callback(function, param_types, return_type) -> ptr
let cb = callback(my_function, ["ptr", "ptr"], "i32");
```

**Parameters:**
- `function`: A Hemlock function to wrap
- `param_types`: Array of type name strings (e.g., `["ptr", "i32"]`)
- `return_type`: Return type string (e.g., `"i32"`, `"void"`)

**Supported callback types:**
- `"i8"`, `"i16"`, `"i32"`, `"i64"` - Signed integers
- `"u8"`, `"u16"`, `"u32"`, `"u64"` - Unsigned integers
- `"f32"`, `"f64"` - Floating point
- `"ptr"` - Pointer
- `"void"` - No return value
- `"bool"` - Boolean

### Example: qsort

```hemlock
import "libc.so.6";
extern fn qsort(base: ptr, nmemb: u64, size: u64, compar: ptr): void;

// Comparison function for integers (ascending order)
fn compare_ints(a: ptr, b: ptr): i32 {
    let va = ptr_deref_i32(a);
    let vb = ptr_deref_i32(b);
    if (va < vb) { return -1; }
    if (va > vb) { return 1; }
    return 0;
}

// Allocate array of 5 integers
let arr = alloc(20);  // 5 * 4 bytes
ptr_write_i32(arr, 5);
ptr_write_i32(ptr_offset(arr, 1, 4), 2);
ptr_write_i32(ptr_offset(arr, 2, 4), 8);
ptr_write_i32(ptr_offset(arr, 3, 4), 1);
ptr_write_i32(ptr_offset(arr, 4, 4), 9);

// Create callback and sort
let cmp = callback(compare_ints, ["ptr", "ptr"], "i32");
qsort(arr, 5, 4, cmp);

// Array is now sorted: [1, 2, 5, 8, 9]

// Clean up
callback_free(cmp);
free(arr);
```

### Pointer Helper Functions

Hemlock provides comprehensive helper functions for working with raw pointers. These are essential for FFI callbacks and direct memory manipulation.

#### Integer Type Helpers

| Function | Description |
|----------|-------------|
| `ptr_deref_i8(ptr)` | Dereference pointer, read i8 |
| `ptr_deref_i16(ptr)` | Dereference pointer, read i16 |
| `ptr_deref_i32(ptr)` | Dereference pointer, read i32 |
| `ptr_deref_i64(ptr)` | Dereference pointer, read i64 |
| `ptr_deref_u8(ptr)` | Dereference pointer, read u8 |
| `ptr_deref_u16(ptr)` | Dereference pointer, read u16 |
| `ptr_deref_u32(ptr)` | Dereference pointer, read u32 |
| `ptr_deref_u64(ptr)` | Dereference pointer, read u64 |
| `ptr_write_i8(ptr, value)` | Write i8 to pointer location |
| `ptr_write_i16(ptr, value)` | Write i16 to pointer location |
| `ptr_write_i32(ptr, value)` | Write i32 to pointer location |
| `ptr_write_i64(ptr, value)` | Write i64 to pointer location |
| `ptr_write_u8(ptr, value)` | Write u8 to pointer location |
| `ptr_write_u16(ptr, value)` | Write u16 to pointer location |
| `ptr_write_u32(ptr, value)` | Write u32 to pointer location |
| `ptr_write_u64(ptr, value)` | Write u64 to pointer location |

#### Float Type Helpers

| Function | Description |
|----------|-------------|
| `ptr_deref_f32(ptr)` | Dereference pointer, read f32 (float) |
| `ptr_deref_f64(ptr)` | Dereference pointer, read f64 (double) |
| `ptr_write_f32(ptr, value)` | Write f32 to pointer location |
| `ptr_write_f64(ptr, value)` | Write f64 to pointer location |

#### Pointer Type Helpers

| Function | Description |
|----------|-------------|
| `ptr_deref_ptr(ptr)` | Dereference pointer-to-pointer |
| `ptr_write_ptr(ptr, value)` | Write pointer to pointer location |
| `ptr_offset(ptr, index, size)` | Calculate offset: `ptr + index * size` |
| `ptr_read_i32(ptr)` | Read i32 through pointer-to-pointer (for qsort callbacks) |
| `ptr_null()` | Get a null pointer constant |

#### Buffer Conversion Helpers

| Function | Description |
|----------|-------------|
| `buffer_ptr(buffer)` | Get raw pointer from a buffer |
| `ptr_to_buffer(ptr, size)` | Copy data from pointer into a new buffer |

#### FFI Utility Functions

| Function | Description |
|----------|-------------|
| `ffi_sizeof(type_name)` | Get size in bytes of an FFI type |

**Supported type names for `ffi_sizeof`:**
- `"i8"`, `"i16"`, `"i32"`, `"i64"` - Signed integers (1, 2, 4, 8 bytes)
- `"u8"`, `"u16"`, `"u32"`, `"u64"` - Unsigned integers (1, 2, 4, 8 bytes)
- `"f32"`, `"f64"` - Floats (4, 8 bytes)
- `"ptr"` - Pointer (8 bytes on 64-bit)
- `"size_t"`, `"usize"` - Platform-dependent size type
- `"intptr_t"`, `"isize"` - Platform-dependent signed pointer type

#### Example: Working with Different Types

```hemlock
let p = alloc(64);

// Write and read integers
ptr_write_i8(p, 42);
print(ptr_deref_i8(p));  // 42

ptr_write_i64(ptr_offset(p, 1, 8), 9000000000);
print(ptr_deref_i64(ptr_offset(p, 1, 8)));  // 9000000000

// Write and read floats
ptr_write_f64(p, 3.14159);
print(ptr_deref_f64(p));  // 3.14159

// Pointer-to-pointer
let inner = alloc(4);
ptr_write_i32(inner, 999);
ptr_write_ptr(p, inner);
let retrieved = ptr_deref_ptr(p);
print(ptr_deref_i32(retrieved));  // 999

// Get type sizes
print(ffi_sizeof("i64"));  // 8
print(ffi_sizeof("ptr"));  // 8 (on 64-bit)

// Buffer conversion
let buf = buffer(64);
ptr_write_i32(buffer_ptr(buf), 12345);
print(ptr_deref_i32(buffer_ptr(buf)));  // 12345

free(inner);
free(p);
```

### Freeing Callbacks

**Important:** Always free callbacks when done to prevent memory leaks:

```hemlock
let cb = callback(my_fn, ["ptr"], "void");
// ... use callback ...
callback_free(cb);  // Free when done
```

Callbacks are also automatically freed when the program exits.

### Closures in Callbacks

Callbacks capture their closure environment, so they can access outer scope variables:

```hemlock
let multiplier = 10;

fn scale(a: ptr, b: ptr): i32 {
    let va = ptr_deref_i32(a);
    let vb = ptr_deref_i32(b);
    // Can access 'multiplier' from outer scope
    return (va * multiplier) - (vb * multiplier);
}

let cmp = callback(scale, ["ptr", "ptr"], "i32");
```

### Thread Safety

Callback invocations are serialized with a mutex to ensure thread safety, as the Hemlock interpreter is not fully thread-safe. This means:
- Only one callback can execute at a time
- Safe to use with multi-threaded C libraries
- May impact performance if callbacks are called very frequently from multiple threads

### Error Handling in Callbacks

Exceptions thrown in callbacks cannot propagate to C code. Instead:
- A warning is printed to stderr
- The callback returns a default value (0 or NULL)
- The exception is logged but not propagated

```hemlock
fn risky_callback(a: ptr): i32 {
    throw "Something went wrong";  // Warning printed, returns 0
}
```

For robust error handling, validate inputs and avoid throwing in callbacks.

## FFI Structs

Hemlock supports passing structs by value to C functions. Struct types are automatically registered for FFI when you define them with type annotations.

### Defining FFI-Compatible Structs

A struct is FFI-compatible when all fields have explicit type annotations using FFI-compatible types:

```hemlock
// FFI-compatible struct
define Point {
    x: f64,
    y: f64,
}

// FFI-compatible struct with multiple field types
define Rectangle {
    top_left: Point,      // Nested struct
    width: f64,
    height: f64,
}

// NOT FFI-compatible (field without type annotation)
define DynamicObject {
    name,                 // No type - not usable in FFI
    value,
}
```

### Using Structs in FFI

Declare extern functions that use struct types:

```hemlock
// Define the struct type
define Vector2D {
    x: f64,
    y: f64,
}

// Import the C library
import "libmath.so";

// Declare extern function that takes/returns structs
extern fn vector_add(a: Vector2D, b: Vector2D): Vector2D;
extern fn vector_length(v: Vector2D): f64;

// Use it naturally
let a: Vector2D = { x: 3.0, y: 0.0 };
let b: Vector2D = { x: 0.0, y: 4.0 };
let result = vector_add(a, b);
print(result.x);  // 3.0
print(result.y);  // 4.0

let len = vector_length(result);
print(len);       // 5.0
```

### Supported Field Types

Struct fields must use these FFI-compatible types:

| Hemlock Type | C Type | Size |
|--------------|--------|------|
| `i8` | `int8_t` | 1 byte |
| `i16` | `int16_t` | 2 bytes |
| `i32` | `int32_t` | 4 bytes |
| `i64` | `int64_t` | 8 bytes |
| `u8` | `uint8_t` | 1 byte |
| `u16` | `uint16_t` | 2 bytes |
| `u32` | `uint32_t` | 4 bytes |
| `u64` | `uint64_t` | 8 bytes |
| `f32` | `float` | 4 bytes |
| `f64` | `double` | 8 bytes |
| `ptr` | `void*` | 8 bytes |
| `string` | `char*` | 8 bytes |
| `bool` | `int` | varies |
| Nested struct | struct | varies |

### Struct Layout

Hemlock uses the platform's native struct layout rules (matching the C ABI):
- Fields are aligned according to their type
- Padding is inserted as needed
- Total size is padded to align the largest member

```hemlock
// Example: C-compatible layout
define Mixed {
    a: i8,    // offset 0, size 1
              // 3 bytes padding
    b: i32,   // offset 4, size 4
}
// Total size: 8 bytes (with padding)

define Point3D {
    x: f64,   // offset 0, size 8
    y: f64,   // offset 8, size 8
    z: f64,   // offset 16, size 8
}
// Total size: 24 bytes (no padding needed)
```

### Nested Structs

Structs can contain other structs:

```hemlock
define Inner {
    x: i32,
    y: i32,
}

define Outer {
    inner: Inner,
    z: i32,
}

import "mylib.so";
extern fn process_nested(data: Outer): i32;

let obj: Outer = {
    inner: { x: 1, y: 2 },
    z: 3,
};
let result = process_nested(obj);
```

### Struct Return Values

C functions can return structs:

```hemlock
define Point {
    x: f64,
    y: f64,
}

import "libmath.so";
extern fn get_origin(): Point;

let p = get_origin();
print(p.x);  // 0.0
print(p.y);  // 0.0
```

### Limitations

- **Struct fields must have type annotations** - fields without types are not FFI-compatible
- **No arrays in structs** - use pointers instead
- **No unions** - only struct types are supported
- **Callbacks cannot return structs** - use pointers for callback return values

### Exporting Struct Types

You can export struct type definitions from a module using `export define`:

```hemlock
// geometry.hml
export define Vector2 {
    x: f32,
    y: f32,
}

export define Rectangle {
    x: f32,
    y: f32,
    width: f32,
    height: f32,
}

export fn create_rect(x: f32, y: f32, w: f32, h: f32): Rectangle {
    return { x: x, y: y, width: w, height: h };
}
```

**Important:** Exported struct types are registered **globally** when the module is loaded. They become available automatically when you import anything from the module. You do NOT need to (and cannot) explicitly import them by name:

```hemlock
// main.hml

// GOOD - struct types are auto-available after any import from the module
import { create_rect } from "./geometry.hml";
let v: Vector2 = { x: 1.0, y: 2.0 };      // Works - Vector2 is globally available
let r: Rectangle = create_rect(0.0, 0.0, 100.0, 50.0);  // Works

// BAD - cannot explicitly import struct types by name
import { Vector2 } from "./geometry.hml";  // Error: Undefined variable 'Vector2'
```

This behavior exists because struct types are registered in the global type registry when the module loads, rather than being stored as values in the module's export environment. The type becomes available to all code that imports from the module.

## Current Limitations

FFI has the following limitations:

**1. Manual Type Conversion**
- Must manually manage string conversions
- No automatic Hemlock string ↔ C string conversion

**2. Limited Error Handling**
- Basic error reporting
- Exceptions in callbacks cannot propagate to C

**3. Manual Library Loading**
- Must manually load libraries
- No automatic binding generation

**4. Platform-Specific Code**
- Library paths differ by platform
- Must handle .so vs .dylib vs .dll

## Best Practices

While comprehensive FFI documentation is still being developed, here are general best practices:

### 1. Type Safety

```hemlock
// Be explicit about types
let x: i32 = 42;
let result: f64 = c_function(x);
```

### 2. Memory Management

```hemlock
// Remember to free allocated memory
let ptr = c_malloc(1024);
// ... use ptr
c_free(ptr);
```

### 3. Error Checking

```hemlock
// Check return values
let result = c_function();
if (result == null) {
    print("C function failed");
}
```

### 4. Platform Compatibility

```hemlock
// Handle platform differences
// Use appropriate library extensions (.so, .dylib, .dll)
```

## Examples

For working examples, refer to:
- Callback tests: `/tests/ffi_callbacks/` - qsort callback examples
- Stdlib FFI usage: `/stdlib/hash.hml`, `/stdlib/regex.hml`, `/stdlib/crypto.hml`
- Example programs: `/examples/` (if available)

## Getting Help

FFI is a newer feature in Hemlock. For questions or issues:

1. Check test suite for working examples
2. Refer to libffi documentation for low-level details
3. Report bugs or request features via project issues

## Summary

Hemlock's FFI provides:

- ✅ C function calling from shared libraries
- ✅ Primitive type support (i8-i64, u8-u64, f32, f64, ptr)
- ✅ Automatic type conversion
- ✅ libffi-based portability
- ✅ Foundation for native library integration
- ✅ **Function pointer callbacks** - pass Hemlock functions to C
- ✅ **Export extern functions** - share FFI bindings across modules
- ✅ **Struct passing and return** - pass C-compatible structs by value
- ✅ **Export define** - share struct type definitions across modules (auto-imported globally)
- ✅ **Complete pointer helpers** - read/write all types (i8-i64, u8-u64, f32, f64, ptr)
- ✅ **Buffer/pointer conversion** - `buffer_ptr()`, `ptr_to_buffer()` for data marshaling
- ✅ **FFI type sizes** - `ffi_sizeof()` for platform-aware type sizes
- ✅ **Platform types** - `size_t`, `usize`, `isize`, `intptr_t`, `uintptr_t` support

**Current status:** FFI fully featured with primitive types, structs, callbacks, module exports, and complete pointer helper functions

**Future:** String marshaling helpers

**Use cases:** System libraries, third-party libraries, qsort, event loops, callback-based APIs, reusable library wrappers

## Contributing

FFI documentation is being expanded. If you're working with FFI:
- Document your use cases
- Share example code
- Report issues or limitations
- Suggest improvements

The FFI system is designed to be practical and safe while providing low-level access when needed, following Hemlock's philosophy of "explicit over implicit" and "unsafe is a feature, not a bug."
