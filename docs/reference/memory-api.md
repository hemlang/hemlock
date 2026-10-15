# Memory API Reference

Complete reference for Hemlock's memory management functions and pointer types.

---

## Overview

Hemlock provides **manual memory management** with explicit allocation and deallocation. Memory is managed through two pointer types: raw pointers (`ptr`) and safe buffers (`buffer`).

**Key Principles:**
- Explicit allocation and deallocation
- No garbage collection
- User responsible for calling `free()`
- Internal refcounting for scope/reassignment safety (see below)

### Internal Reference Counting

The runtime uses reference counting internally to manage object lifetimes through scopes. For most local variables, cleanup is automatic.

**Automatic (no `free()` needed):**
- Local variables of refcounted types (buffer, array, object, string) are freed when scope exits
- Old values are released when variables are reassigned
- Container elements are released when containers are freed

**Manual `free()` required:**
- Raw pointers from `alloc()` - always
- Early cleanup before scope ends
- Long-lived/global data

See [Memory Management Guide](../language-guide/memory.md#internal-reference-counting) for details.

---

## Pointer Types

### ptr (Raw Pointer)

**Type:** `ptr`

**Description:** Raw memory address with no bounds checking or tracking.

**Size:** 8 bytes

**Use Cases:**
- Low-level memory operations
- FFI (Foreign Function Interface)
- Maximum performance (no overhead)

**Safety:** Unsafe - no bounds checking, user must track lifetime

**Examples:**
```hemlock
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);
```

---

### buffer (Safe Buffer)

**Type:** `buffer`

**Description:** Safe pointer wrapper with bounds checking.

**Structure:** Pointer + length + capacity + ref_count

**Properties:**
- `.length` - Buffer size (i32)
- `.capacity` - Allocated capacity (i32)

**Use Cases:**
- Most memory allocations
- When safety is important
- Dynamic arrays

**Safety:** Bounds-checked on index access

**Refcounting:** Buffers are internally refcounted. Automatically freed when scope exits or variable is reassigned. Use `free()` for early cleanup or long-lived data.

**Examples:**
```hemlock
let b: buffer = buffer(64);
b[0] = 65;              // Bounds checked
print(b.length);        // 64
free(b);
```

---

## Memory Allocation Functions

### alloc

Allocate raw memory.

**Signature:**
```hemlock
alloc(size: i32): ptr
```

**Parameters:**
- `size` - Number of bytes to allocate

**Returns:** Pointer to allocated memory (`ptr`)

**Examples:**
```hemlock
let p = alloc(1024);        // Allocate 1KB
memset(p, 0, 1024);         // Initialize to zero
free(p);                    // Free when done

// Allocate for structure
let struct_size = 16;
let p2 = alloc(struct_size);
```

**Behavior:**
- Returns uninitialized memory
- Memory must be manually freed
- Returns `null` on allocation failure (caller must check)

**See Also:** `buffer()` for safer alternative

---

### buffer

Allocate safe buffer with bounds checking.

**Signature:**
```hemlock
buffer(size: i32): buffer
```

**Parameters:**
- `size` - Buffer size in bytes

**Returns:** Buffer object

**Examples:**
```hemlock
let buf = buffer(256);
print(buf.length);          // 256
print(buf.capacity);        // 256

// Access with bounds checking
buf[0] = 65;                // 'A'
buf[255] = 90;              // 'Z'
// buf[256] = 0;            // ERROR: out of bounds

free(buf);
```

**Properties:**
- `.length` - Current size (i32)
- `.capacity` - Allocated capacity (i32)

**Behavior:**
- Initializes memory to zero
- Provides bounds checking on index access
- Throws a catchable error for `size <= 0` (same as `alloc()`)
- Returns `null` on allocation failure (caller must check)
- Must be manually freed

---

### free

Free allocated memory.

**Signature:**
```hemlock
free(ptr: ptr | buffer): null
```

**Parameters:**
- `ptr` - Pointer or buffer to free

**Returns:** `null`

**Examples:**
```hemlock
// Free raw pointer
let p = alloc(1024);
free(p);

// Free buffer
let buf = buffer(256);
free(buf);
```

**Behavior:**
- Frees memory allocated by `alloc()` or `buffer()`
- For raw pointers: double-free causes a crash, and freeing invalid
  pointers is undefined behavior (user's responsibility to avoid)
- For buffers, `free()` is validated with catchable errors instead:
  - double-free raises "double free detected on buffer"
  - freeing a slice view raises "cannot free() a buffer slice view"
  - freeing a buffer that still has live slice views raises "Cannot free
    buffer with N live slice views" — release the views first, or the
    views' windows would dangle into freed memory
  - after a successful `free()`, every alias sees `length == 0` and all
    accesses raise catchable bounds errors

See [Memory-Safety Model](../design/memory-safety-model.md) for the
invariants behind these rules.

**Important:** You allocate, you free. No automatic cleanup.

---

### realloc

Resize allocated memory.

**Signature:**
```hemlock
realloc(ptr: ptr, new_size: i32): ptr
```

**Parameters:**
- `ptr` - Pointer to resize
- `new_size` - New size in bytes

**Returns:** Pointer to resized memory (may be different address)

**Examples:**
```hemlock
let p = alloc(100);
// ... use memory ...

// Need more space
p = realloc(p, 200);        // Now 200 bytes
// ... use expanded memory ...

free(p);
```

**Behavior:**
- May move memory to new location
- Preserves existing data (up to minimum of old/new size)
- Old pointer is invalid after successful realloc (use returned pointer)
- If new_size is smaller, data is truncated
- Returns `null` on allocation failure (original pointer remains valid)

**Important:** Always check for `null` and update your pointer variable with the result.

---

## Memory Operations

### memset

Fill memory with byte value.

**Signature:**
```hemlock
memset(ptr: ptr | buffer, byte: i32, size: i32): null
```

**Parameters:**
- `ptr` - Pointer or buffer to memory
- `byte` - Byte value to fill (0-255)
- `size` - Number of bytes to fill

**Returns:** `null`

**Examples:**
```hemlock
let p = alloc(100);

// Zero out memory
memset(p, 0, 100);

// Fill with specific value
memset(p, 0xFF, 100);

// Initialize buffer
let buf = buffer(256);
memset(buf, 65, 256);       // Fill with 'A' (bounds checked)

free(p);
free(buf);
```

**Behavior:**
- Writes byte value to each byte in range
- Byte value is truncated to 8 bits (0-255)
- Raw pointers have no bounds checking (unsafe)
- Buffers validate that `size` bytes fit within the buffer before writing

---

### memcpy

Copy memory from source to destination.

**Signature:**
```hemlock
memcpy(dest: ptr | buffer, src: ptr | buffer, size: i32): null
```

**Parameters:**
- `dest` - Destination pointer or buffer
- `src` - Source pointer or buffer
- `size` - Number of bytes to copy

**Returns:** `null`

**Examples:**
```hemlock
let src = alloc(100);
let dest = alloc(100);

// Initialize source
memset(src, 65, 100);

// Copy to destination
memcpy(dest, src, 100);

// dest now contains same data as src

free(src);
free(dest);
```

**Behavior:**
- Copies byte-by-byte from src to dest
- Raw pointers have no bounds checking (unsafe)
- Buffers validate that `size` bytes fit within both source and destination before copying
- Overlapping regions have undefined behavior (use carefully)

---

## Typed Memory Operations

### sizeof

Get size of type in bytes.

**Signature:**
```hemlock
sizeof(type): i32
```

**Parameters:**
- `type` - Type identifier (e.g., `i32`, `f64`, `ptr`)

**Returns:** Size in bytes (i32)

**Type Sizes:**

| Type | Size (bytes) |
|------|--------------|
| `i8` | 1 |
| `i16` | 2 |
| `i32`, `integer` | 4 |
| `i64` | 8 |
| `u8`, `byte` | 1 |
| `u16` | 2 |
| `u32` | 4 |
| `u64` | 8 |
| `f32` | 4 |
| `f64`, `number` | 8 |
| `bool` | 1 |
| `ptr` | 8 |
| `rune` | 4 |

**Examples:**
```hemlock
let int_size = sizeof(i32);      // 4
let ptr_size = sizeof(ptr);      // 8
let float_size = sizeof(f64);    // 8
let byte_size = sizeof(u8);      // 1
let rune_size = sizeof(rune);    // 4

// Calculate array allocation size
let count = 100;
let total = sizeof(i32) * count; // 400 bytes
```

**Behavior:**
- Returns 0 for unknown types
- Accepts both type identifiers and type strings

---

## Pointer/Buffer Interoperability

All `ptr_read_*`, `ptr_write_*`, and `ptr_deref_*` builtins accept both `ptr` and `buffer` types directly. When a buffer is passed, the operation uses the buffer's underlying data pointer and validates that the whole typed access fits within the buffer.

```hemlock
let buf = buffer(16);

// Write directly to a buffer (no need to extract ptr first)
ptr_write_i32(buf, 42);
ptr_write_f64(ptr_offset(buffer_ptr(buf), 4), 3.14);

// Read directly from a buffer
let val = ptr_read_i32(buf);      // 42
let fval = ptr_deref_i32(buf);    // 42

// Also works with raw pointers as before
let p = alloc(8);
ptr_write_i32(p, 99);
let pval = ptr_read_i32(p);      // 99
free(p);
```

This eliminates the need to call `buffer_ptr()` before every typed read/write operation, making buffer-based code more concise while preserving buffer bounds checks for direct buffer operands.

**Warning — `buffer_ptr()` leaves the safe world.** The returned `ptr` is
the buffer's raw interior address: it does not keep the buffer alive, it
carries no length, and it dangles the moment the buffer is freed or its
last reference is released. Calling `buffer_ptr()` on an already-freed
buffer raises a catchable error, but keeping the pointer past the buffer's
lifetime is on you. Prefer the direct buffer operands above, or the
buffer's typed `read_*`/`write_*` methods, which stay bounds-checked at any
offset. See [Memory-Safety Model](../design/memory-safety-model.md) for the
exact obligations.

---

### talloc

Allocate array of typed values.

**Signature:**
```hemlock
talloc(type, count: i32): ptr
```

**Parameters:**
- `type` - Type to allocate (e.g., `i32`, `f64`, `ptr`)
- `count` - Number of elements (must be positive)

**Returns:** Pointer to allocated array, or `null` on allocation failure

**Examples:**
```hemlock
let arr = talloc(i32, 100);      // Array of 100 i32s (400 bytes)
let floats = talloc(f64, 50);    // Array of 50 f64s (400 bytes)
let bytes = talloc(u8, 1024);    // Array of 1024 bytes

// Always check for allocation failure
if (arr == null) {
    panic("allocation failed");
}

// Use the allocated memory
// ...

free(arr);
free(floats);
free(bytes);
```

**Behavior:**
- Allocates `sizeof(type) * count` bytes
- Returns uninitialized memory
- Memory must be manually freed with `free()`
- Returns `null` on allocation failure (caller must check)
- Panics if count is not positive

---

## Buffer Properties

### .length

Get buffer size.

**Type:** `i32`

**Access:** Read-only

**Examples:**
```hemlock
let buf = buffer(256);
print(buf.length);          // 256

let buf2 = buffer(1024);
print(buf2.length);         // 1024
```

---

### .capacity

Get buffer capacity.

**Type:** `i32`

**Access:** Read-only

**Examples:**
```hemlock
let buf = buffer(256);
print(buf.capacity);        // 256
```

**Note:** Currently, `.length` and `.capacity` are the same for buffers created with `buffer()`.

---

## Buffer Methods

### .slice

Create a zero-copy view into the buffer's memory. The returned view shares the same underlying memory as the parent buffer -- modifications to the original are visible through the view and vice versa.

**Signature:**
```hemlock
buffer.slice(start: i32, end?: i32): buffer
```

**Parameters:**
- `start` - Starting byte offset (0-based, inclusive). Negative values are clamped to 0.
- `end` - Ending byte offset (exclusive). Defaults to `buffer.length` if omitted. Values beyond buffer length are clamped.

**Returns:** Buffer view (zero-copy)

**Examples:**
```hemlock
let buf = buffer(10);
for (let i = 0; i < 10; i++) {
    buf[i] = i + 65;  // A=65, B=66, ...
}

// Basic slice
let view = buf.slice(2, 5);
print(view.length);    // 3
print(view[0]);        // 67 (C)
print(view[1]);        // 68 (D)
print(view[2]);        // 69 (E)

// Zero-copy proof: modifying original is visible through view
buf[3] = 90;           // Change D(68) to Z(90)
print(view[1]);        // 90 (reflects parent change)

// Single-arg slice (start to end)
let tail = buf.slice(7);
print(tail.length);    // 3

// Chained slices (slice of a slice)
let inner = view.slice(1, 3);
print(inner.length);   // 2
print(inner[0]);       // 90 (Z)

// Empty slice
let empty = buf.slice(5, 5);
print(empty.length);   // 0
```

**Behavior:**
- Returns a zero-copy view -- no memory is allocated for the data
- Views hold a reference to the root buffer (prevents use-after-free)
- Chained slices (slice of a slice) track the root owner, not the intermediate view
- Bounds checking is performed relative to the view's range
- Out-of-range `start`/`end` values are clamped to valid bounds
- You **cannot** `free()` a view buffer -- only the root buffer should be freed
- Set views to `null` before freeing the parent buffer to release references

---

## Usage Patterns

### Basic Allocation Pattern

```hemlock
// Allocate
let p = alloc(1024);
if (p == null) {
    panic("allocation failed");
}

// Use
memset(p, 0, 1024);

// Free
free(p);
```

### Safe Buffer Pattern

```hemlock
// Allocate buffer
let buf = buffer(256);
if (buf == null) {
    panic("buffer allocation failed");
}

// Use with bounds checking
let i = 0;
while (i < buf.length) {
    buf[i] = i;
    i = i + 1;
}

// Free
free(buf);
```

### Dynamic Growth Pattern

```hemlock
let size = 100;
let p = alloc(size);
if (p == null) {
    panic("allocation failed");
}

// ... use memory ...

// Need more space - check for failure
let new_p = realloc(p, 200);
if (new_p == null) {
    // Original pointer still valid, clean up
    free(p);
    panic("realloc failed");
}
p = new_p;
size = 200;

// ... use expanded memory ...

free(p);
```

### Memory Copy Pattern

```hemlock
let original = alloc(100);
memset(original, 65, 100);

// Create copy
let copy = alloc(100);
memcpy(copy, original, 100);

free(original);
free(copy);
```

---

## Safety Considerations

**Hemlock memory management is UNSAFE by design:**

### Common Pitfalls

**1. Memory Leaks**
```hemlock
// BAD: Memory leak
fn create_buffer() {
    let p = alloc(1024);
    return null;  // Memory leaked!
}

// GOOD: Proper cleanup
fn create_buffer() {
    let p = alloc(1024);
    // ... use memory ...
    free(p);
    return null;
}
```

**2. Use After Free**
```hemlock
// BAD: Use after free
let p = alloc(100);
free(p);
memset(p, 0, 100);  // CRASH: using freed memory

// GOOD: Don't use after free
let p2 = alloc(100);
memset(p2, 0, 100);
free(p2);
// Don't touch p2 after this
```

**3. Double Free**
```hemlock
// BAD: Double free
let p = alloc(100);
free(p);
free(p);  // CRASH: double free

// GOOD: Free once
let p2 = alloc(100);
free(p2);
```

**4. Buffer Overflow (ptr)**
```hemlock
// BAD: Buffer overflow with ptr
let p = alloc(10);
memset(p, 65, 100);  // CRASH: writing past allocation

// GOOD: Use buffer for bounds checking
let buf = buffer(10);
// buf[100] = 65;       // ERROR: index bounds check fails
// memset(buf, 65, 100); // ERROR: range bounds check fails
```

**5. Dangling Pointers**
```hemlock
// BAD: Dangling pointer
let p1 = alloc(100);
let p2 = p1;
free(p1);
memset(p2, 0, 100);  // CRASH: p2 is dangling

// GOOD: Track ownership carefully
let p = alloc(100);
// ... use p ...
free(p);
// Don't keep other references to p
```

**6. Unchecked Allocation Failure**
```hemlock
// BAD: Not checking for null
let p = alloc(1000000000);  // May fail on low memory
memset(p, 0, 1000000000);   // CRASH: p is null

// GOOD: Always check allocation result
let p2 = alloc(1000000000);
if (p2 == null) {
    panic("out of memory");
}
memset(p2, 0, 1000000000);
free(p2);
```

---

## When to Use What

### Use `buffer()` when:
- You need bounds checking
- Working with dynamic data
- Safety is important
- Learning Hemlock

### Use `alloc()` when:
- Maximum performance needed
- FFI/interfacing with C
- You know exact memory layout
- You're an expert

### Use `realloc()` when:
- Growing/shrinking allocations
- Dynamic arrays
- You need to preserve data

---

## Complete Function Summary

| Function  | Signature                              | Returns  | Description                |
|-----------|----------------------------------------|----------|----------------------------|
| `alloc`   | `(size: i32)`                          | `ptr`    | Allocate raw memory        |
| `buffer`  | `(size: i32)`                          | `buffer` | Allocate safe buffer       |
| `free`    | `(ptr: ptr \| buffer)`                 | `null`   | Free memory                |
| `realloc` | `(ptr: ptr, new_size: i32)`            | `ptr`    | Resize allocation          |
| `memset`  | `(ptr: ptr, byte: i32, size: i32)`     | `null`   | Fill memory                |
| `memcpy`  | `(dest: ptr, src: ptr, size: i32)`     | `null`   | Copy memory                |
| `sizeof`  | `(type)`                               | `i32`    | Get type size in bytes     |
| `talloc`  | `(type, count: i32)`                   | `ptr`    | Allocate typed array       |

### Buffer Methods

| Method    | Signature                              | Returns  | Description                     |
|-----------|----------------------------------------|----------|---------------------------------|
| `.slice`  | `(start: i32, end?: i32)`             | `buffer` | Zero-copy view into buffer      |
| `.write_u8` | `(offset: i32, value: u8)`          | `null`   | Write unsigned 8-bit integer    |
| `.write_i8` | `(offset: i32, value: i8)`          | `null`   | Write signed 8-bit integer      |
| `.write_u16_le` | `(offset: i32, value: u16)`    | `null`   | Write u16, little-endian        |
| `.write_u16_be` | `(offset: i32, value: u16)`    | `null`   | Write u16, big-endian           |
| `.write_i16_le` | `(offset: i32, value: i16)`    | `null`   | Write i16, little-endian        |
| `.write_i16_be` | `(offset: i32, value: i16)`    | `null`   | Write i16, big-endian           |
| `.write_u32_le` | `(offset: i32, value: u32)`    | `null`   | Write u32, little-endian        |
| `.write_u32_be` | `(offset: i32, value: u32)`    | `null`   | Write u32, big-endian           |
| `.write_i32_le` | `(offset: i32, value: i32)`    | `null`   | Write i32, little-endian        |
| `.write_i32_be` | `(offset: i32, value: i32)`    | `null`   | Write i32, big-endian           |
| `.write_u64_le` | `(offset: i32, value: u64)`    | `null`   | Write u64, little-endian        |
| `.write_u64_be` | `(offset: i32, value: u64)`    | `null`   | Write u64, big-endian           |
| `.write_i64_le` | `(offset: i32, value: i64)`    | `null`   | Write i64, little-endian        |
| `.write_i64_be` | `(offset: i32, value: i64)`    | `null`   | Write i64, big-endian           |
| `.write_f32_le` | `(offset: i32, value: f32)`    | `null`   | Write f32, little-endian        |
| `.write_f32_be` | `(offset: i32, value: f32)`    | `null`   | Write f32, big-endian           |
| `.write_f64_le` | `(offset: i32, value: f64)`    | `null`   | Write f64, little-endian        |
| `.write_f64_be` | `(offset: i32, value: f64)`    | `null`   | Write f64, big-endian           |
| `.write_bytes` | `(offset: i32, src: buffer)`    | `null`   | Copy bytes from source buffer   |
| `.read_u8`  | `(offset: i32)`                      | `u8`     | Read unsigned 8-bit integer     |
| `.read_i8`  | `(offset: i32)`                      | `i8`     | Read signed 8-bit integer       |
| `.read_u16_le` | `(offset: i32)`                   | `u16`    | Read u16, little-endian         |
| `.read_u16_be` | `(offset: i32)`                   | `u16`    | Read u16, big-endian            |
| `.read_i16_le` | `(offset: i32)`                   | `i16`    | Read i16, little-endian         |
| `.read_i16_be` | `(offset: i32)`                   | `i16`    | Read i16, big-endian            |
| `.read_u32_le` | `(offset: i32)`                   | `u32`    | Read u32, little-endian         |
| `.read_u32_be` | `(offset: i32)`                   | `u32`    | Read u32, big-endian            |
| `.read_i32_le` | `(offset: i32)`                   | `i32`    | Read i32, little-endian         |
| `.read_i32_be` | `(offset: i32)`                   | `i32`    | Read i32, big-endian            |
| `.read_u64_le` | `(offset: i32)`                   | `u64`    | Read u64, little-endian         |
| `.read_u64_be` | `(offset: i32)`                   | `u64`    | Read u64, big-endian            |
| `.read_i64_le` | `(offset: i32)`                   | `i64`    | Read i64, little-endian         |
| `.read_i64_be` | `(offset: i32)`                   | `i64`    | Read i64, big-endian            |
| `.read_f32_le` | `(offset: i32)`                   | `f32`    | Read f32, little-endian         |
| `.read_f32_be` | `(offset: i32)`                   | `f32`    | Read f32, big-endian            |
| `.read_f64_le` | `(offset: i32)`                   | `f64`    | Read f64, little-endian         |
| `.read_f64_be` | `(offset: i32)`                   | `f64`    | Read f64, big-endian            |
| `.read_bytes` | `(offset: i32, length: i32)`       | `buffer` | Read bytes into new buffer      |

---

## Typed Buffer Read/Write Methods

Buffers provide endian-aware typed read and write methods for building and parsing binary data structures like network packets, file formats, and wire protocols. These methods are bounds-checked and raise runtime errors on out-of-bounds access.

### Write Methods

Write a typed value at a byte offset. The `_le` and `_be` suffixes specify little-endian and big-endian byte order respectively.

```hemlock
let pkt = buffer(64);
let offset = 0;

// Build a packet header
pkt.write_u16_be(offset, 0x0800);    // EtherType: IPv4
offset += 2;
pkt.write_u8(offset, 0x45);          // Version + IHL
offset += 1;
pkt.write_u8(offset, 0x00);          // DSCP/ECN
offset += 1;
pkt.write_u16_be(offset, 40);        // Total length
offset += 2;
pkt.write_u32_be(offset, 0xC0A80001); // Source IP: 192.168.0.1
offset += 4;

// Float values
pkt.write_f32_le(offset, 3.14);
offset += 4;
pkt.write_f64_be(offset, 2.71828);
offset += 8;
```

**Single-byte writes** (`write_u8`, `write_i8`) have no endianness suffix since byte order is irrelevant for single bytes.

### Read Methods

Read a typed value from a byte offset. Endianness suffixes match the write methods.

```hemlock
let pkt = buffer(64);
// ... fill buffer with data ...

// Parse a packet header
let ether_type = pkt.read_u16_be(0);    // 0x0800
let version = pkt.read_u8(2);            // 0x45
let total_len = pkt.read_u16_be(4);      // 40
let src_ip = pkt.read_u32_be(6);         // 0xC0A80001

// Read float values
let pi = pkt.read_f32_le(10);
let e = pkt.read_f64_be(14);
```

### Bulk Operations

```hemlock
let src = buffer(8);
for (let i = 0; i < 8; i++) { src[i] = i + 1; }

let dest = buffer(32);
dest.write_bytes(4, src);          // Copy src into dest at offset 4

let chunk = dest.read_bytes(4, 8); // Read 8 bytes starting at offset 4
print(chunk[0]);                   // 1
```

### Bounds Checking

All typed read/write methods validate that the entire value fits within the buffer. For example, `write_u32_be(offset, val)` checks that `offset + 4 <= buffer.length`.

```hemlock
let buf = buffer(4);
buf.write_u32_be(0, 42);    // OK: 4 bytes fit
// buf.write_u32_be(2, 42); // ERROR: would write past end (offset 2 + 4 > 4)
```

### Use Cases

- **Network protocols:** Build/parse TCP, UDP, DNS, and custom packets
- **Binary file formats:** Read/write image headers, archive formats, etc.
- **Wire protocols:** Serialize/deserialize structured binary messages
- **FFI data exchange:** Prepare buffers for C library calls

---

## See Also

- [Type System](type-system.md) - Pointer and buffer types
- [Built-in Functions](builtins.md) - All built-in functions
- [String API](string-api.md) - String `.to_bytes()` method
