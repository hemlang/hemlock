# Hemlock Memory-Mapped File I/O Module

A standard library module providing memory-mapped file I/O for high-performance data access in Hemlock.

## Overview

The mmap module maps files directly into process memory, enabling:

- **Zero-copy file access** - Read/write file data without explicit read/write syscalls
- **Kernel-managed caching** - The OS page cache handles buffering automatically
- **Shared memory** - Multiple processes or tasks can share mapped regions
- **Anonymous mappings** - Large temporary memory allocations backed by the OS
- **Random access** - Efficient random access patterns on large files

All mapped regions are returned as raw pointers (`ptr`) and accessed via `ptr_read_*`/`ptr_write_*` builtins.

## Usage

```hemlock
import { mmap_open, mmap_close, mmap_size } from "@stdlib/mmap";

let p = mmap_open("data.bin", "r");
let size = mmap_size(p);

// Read first 4 bytes as i32
let val = ptr_read_i32(p);
print(val);

mmap_close(p);
```

Or use the high-level wrapper:

```hemlock
import { MappedFile } from "@stdlib/mmap";

let f = MappedFile("data.bin", "r");
print("Size: " + f.size);
let byte = f.read_u8(0);
f.close();
```

---

## Core Functions

### mmap_open(path, mode?)

Maps a file into memory.

**Parameters:**
- `path: string` - Path to the file to map
- `mode: string` - Access mode: `"r"` (read-only, default) or `"rw"` (read-write)

**Returns:** `ptr` - Pointer to the mapped region, or `null` on failure

```hemlock
import { mmap_open, mmap_close } from "@stdlib/mmap";

// Read-only mapping (default)
let p = mmap_open("config.bin");

// Read-write mapping
let p2 = mmap_open("output.bin", "rw");

mmap_close(p);
mmap_close(p2);
```

**Notes:**
- The file must exist and be non-empty
- Read-write mode maps with `MAP_SHARED`, so changes are visible to other processes
- The underlying file descriptor is managed automatically

### mmap_open_anon(size)

Creates an anonymous memory mapping not backed by any file.

**Parameters:**
- `size: i32|i64` - Size of the mapping in bytes (must be positive)

**Returns:** `ptr` - Pointer to the mapped region, or `null` on failure

```hemlock
import { mmap_open_anon, mmap_close } from "@stdlib/mmap";

// Allocate 1MB of anonymous memory
let p = mmap_open_anon(1048576);
ptr_write_i32(p, 42);
print(ptr_read_i32(p));  // 42
mmap_close(p);
```

**Notes:**
- Anonymous mappings are always read-write
- Memory is zero-initialized by the kernel
- Useful for large allocations where you want OS-level memory management

### mmap_sync(ptr)

Flushes changes in a read-write mapped region to the underlying file on disk.

**Parameters:**
- `ptr: ptr` - Pointer to a mapped region (must have been returned by `mmap_open`)

**Returns:** `bool` - `true` on success, `false` on failure

```hemlock
import { mmap_open, mmap_sync, mmap_close } from "@stdlib/mmap";

let p = mmap_open("data.bin", "rw");
ptr_write_i32(p, 999);
mmap_sync(p);   // Ensure write is flushed to disk
mmap_close(p);
```

### mmap_close(ptr)

Unmaps a memory-mapped region and releases all associated resources.

**Parameters:**
- `ptr: ptr` - Pointer to a mapped region

**Returns:** `bool` - `true` on success, `false` on failure

```hemlock
import { mmap_open, mmap_close } from "@stdlib/mmap";

let p = mmap_open("data.bin");
// ... use the mapping ...
mmap_close(p);  // Always close when done
```

**Notes:**
- For file-backed mappings, the underlying file descriptor is closed automatically
- Accessing the pointer after `mmap_close` is undefined behavior

### mmap_size(ptr)

Returns the size in bytes of a mapped region.

**Parameters:**
- `ptr: ptr` - Pointer to a mapped region

**Returns:** `i64` - Size of the mapping in bytes

```hemlock
import { mmap_open, mmap_size, mmap_close } from "@stdlib/mmap";

let p = mmap_open("data.bin");
let size = mmap_size(p);
print("File size: " + size + " bytes");
mmap_close(p);
```

---

## Advanced Functions

### mmap_advise(ptr, advice)

Provides usage hints to the kernel to optimize access patterns via `madvise()`.

**Parameters:**
- `ptr: ptr` - Pointer to a mapped region
- `advice: i32` - One of the `MADV_*` constants

**Returns:** `bool` - `true` on success, `false` on failure

```hemlock
import { mmap_open, mmap_advise, mmap_close, MADV_SEQUENTIAL } from "@stdlib/mmap";

let p = mmap_open("large_log.bin");
mmap_advise(p, MADV_SEQUENTIAL);  // Hint: we'll read sequentially
// ... read through the file ...
mmap_close(p);
```

### mmap_protect(ptr, prot)

Changes the memory protection flags on a mapped region via `mprotect()`.

**Parameters:**
- `ptr: ptr` - Pointer to a mapped region
- `prot: i32` - Bitwise OR of `PROT_*` constants

**Returns:** `bool` - `true` on success, `false` on failure

```hemlock
import { mmap_open, mmap_protect, mmap_close, PROT_READ, PROT_NONE } from "@stdlib/mmap";

let p = mmap_open("secret.bin", "rw");
// ... read/write data ...

// Make the region inaccessible (guard page)
mmap_protect(p, PROT_NONE);

// Restore read access
mmap_protect(p, PROT_READ);

mmap_close(p);
```

---

## Constants

### Protection Flags (PROT_*)

| Constant | Description |
|----------|-------------|
| `PROT_NONE` | No access allowed |
| `PROT_READ` | Read access |
| `PROT_WRITE` | Write access |
| `PROT_EXEC` | Execute access |

### Madvise Hints (MADV_*)

| Constant | Description |
|----------|-------------|
| `MADV_NORMAL` | No special treatment (default) |
| `MADV_SEQUENTIAL` | Expect sequential page references |
| `MADV_RANDOM` | Expect random page references |
| `MADV_WILLNEED` | Pages will be needed soon (prefetch) |
| `MADV_DONTNEED` | Pages are not needed (allow kernel to free) |

---

## High-Level Wrapper

### MappedFile(path, mode?)

Creates a high-level memory-mapped file object with bounds-checked read/write methods.

**Parameters:**
- `path: string` - File path to map
- `mode: string` - `"r"` (read-only, default) or `"rw"` (read-write)

**Returns:** Object with the following properties and methods:

| Property/Method | Type | Description |
|----------------|------|-------------|
| `ptr` | `ptr` | Raw pointer to the mapped region |
| `size` | `i64` | Size of the mapping in bytes |
| `read_u8(offset)` | `u8` | Read a byte at offset |
| `read_i32(offset)` | `i32` | Read an i32 at offset |
| `read_i64(offset)` | `i64` | Read an i64 at offset |
| `read_f64(offset)` | `f64` | Read an f64 at offset |
| `write_u8(offset, val)` | `void` | Write a byte at offset (rw only) |
| `write_i32(offset, val)` | `void` | Write an i32 at offset (rw only) |
| `sync()` | `bool` | Flush changes to disk |
| `advise(hint)` | `bool` | Set access pattern hint |
| `close()` | `bool` | Unmap and release resources |

```hemlock
import { MappedFile, MADV_SEQUENTIAL } from "@stdlib/mmap";

let f = MappedFile("measurements.bin", "r");
f.advise(MADV_SEQUENTIAL);

// Read through the file
for (let i = 0; i < f.size; i += 4) {
    let val = f.read_i32(i);
    if (val > 1000) {
        print("Found large value at offset " + i + ": " + val);
    }
}

f.close();
```

---

## Examples

### Reading a Binary File

```hemlock
import { mmap_open, mmap_size, mmap_close } from "@stdlib/mmap";

let p = mmap_open("image.raw");
let size = mmap_size(p);

// Read header (first 4 bytes = width, next 4 = height)
let width = ptr_read_i32(p);
let height = ptr_read_i32(ptr_offset(p, 4, 1));
print("Image: " + width + "x" + height);

// Read pixel data starting at offset 8
for (let i = 0; i < width * height; i++) {
    let pixel = ptr_read_u8(ptr_offset(p, 8 + i, 1));
    // process pixel...
}

mmap_close(p);
```

### Modifying a File In-Place

```hemlock
import { mmap_open, mmap_size, mmap_sync, mmap_close } from "@stdlib/mmap";

let p = mmap_open("counters.bin", "rw");
let size = mmap_size(p);

// Increment a counter at offset 0
let current = ptr_read_i32(p);
ptr_write_i32(p, current + 1);

// Flush to disk
mmap_sync(p);
mmap_close(p);
```

### Anonymous Shared Memory Between Tasks

```hemlock
import { mmap_open_anon, mmap_close } from "@stdlib/mmap";

// Allocate shared memory region
let shared = mmap_open_anon(4096);

// Writer task
let writer = spawn(fn() {
    ptr_write_i32(shared, 42);
});

join(writer);

// Reader sees the write (shared memory)
let val = ptr_read_i32(shared);
print("Read from shared memory: " + val);  // 42

mmap_close(shared);
```

### Processing a Large File with Sequential Access Hint

```hemlock
import { mmap_open, mmap_size, mmap_advise, mmap_close, MADV_SEQUENTIAL } from "@stdlib/mmap";

let p = mmap_open("access.log");
let size = mmap_size(p);

// Tell the kernel we'll read sequentially
mmap_advise(p, MADV_SEQUENTIAL);

// Process file byte by byte
let line_count = 0;
for (let i: i64 = 0; i < size; i++) {
    let byte = ptr_read_u8(ptr_offset(p, i, 1));
    if (byte == 10) {  // newline
        line_count++;
    }
}
print("Lines: " + line_count);

mmap_close(p);
```

---

## When to Use mmap

**Use mmap when:**
- Reading/writing large files where random access is needed
- Sharing data between processes or tasks
- Processing binary file formats (headers, records, etc.)
- You want the OS to handle caching and paging

**Use regular file I/O (`@stdlib/fs`) when:**
- Reading entire small files as strings
- Appending to log files
- Line-by-line text processing
- Portability is more important than performance

---

## See Also

- **Filesystem module** (`@stdlib/fs`) - Regular file I/O operations
- **Memory management** - `alloc()`, `free()`, `ptr_read_*`, `ptr_write_*` builtins
- **Atomic module** (`@stdlib/atomic`) - Atomic operations for shared memory synchronization

---

## License

Part of the Hemlock standard library.
