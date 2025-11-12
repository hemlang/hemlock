# Hemlock 1.0 Refactor Summary

This document summarizes the changes made in the Hemlock 1.0 refactor to modernize the file I/O API and introduce standard library modules.

---

## Overview

The refactor achieves the following goals:
1. ✅ Refactored file I/O to use object-oriented File methods
2. ✅ Created `stdlib/json.hml` module for JSON operations
3. ✅ Created `stdlib/fs.hml` module for filesystem operations
4. ✅ Removed global file I/O functions (replaced with File methods and stdlib modules)
5. ✅ Added helper builtins for stdlib implementation
6. ✅ Updated all 216 tests - **all passing!**

---

## Part 1: File I/O Refactor

### File Object Methods

The `open()` function now returns a `File` object with methods:

```hemlock
let f = open("data.txt", "r");  // Returns File object

// Methods:
f.read()           // Read entire file from current position → string
f.write(data)      // Write string to file → i32 (bytes written)
f.seek(position)   // Move file pointer → i32 (new position)
f.tell()           // Get current position → i32
f.close()          // Close the file

// Properties (read-only):
f.path             // string - file path
f.mode             // string - open mode ("r", "w", "a", etc.)
f.closed           // bool - whether file is closed
```

### Example Usage

**Before (old API):**
```hemlock
write_file("output.txt", "Hello, World!");
let content = read_file("output.txt");
```

**After (new API):**
```hemlock
let f = open("output.txt", "w");
f.write("Hello, World!");
f.close();

let fr = open("output.txt", "r");
let content = fr.read();
fr.close();
```

### Removed Global Functions

The following global functions have been **removed**:
- ❌ `read_file(path)` → Use `open()` + `f.read()` + `f.close()`
- ❌ `write_file(path, content)` → Use `open()` + `f.write()` + `f.close()`
- ❌ `append_file(path, content)` → Use `open(path, "a")` + `f.write()` + `f.close()`
- ❌ `read_bytes(path)` → Binary I/O not in v1.0 (future feature)
- ❌ `write_bytes(path, data)` → Binary I/O not in v1.0 (future feature)

### Kept as Builtins

These functions remain as **internal builtins** for stdlib use:
- ✅ `open(path, mode)` - Returns File object
- ✅ `file_exists(path)` - Used by `stdlib/fs.hml`
- ✅ `serialize(obj)` - Used by `stdlib/json.hml`
- ✅ `deserialize(json)` - Used by `stdlib/json.hml`

---

## Part 2: stdlib/json Module

### Module Location
`stdlib/json.hml`

### Functions

```hemlock
import { parse, stringify, parse_file, stringify_file, try_parse } from "std/json";

// Parse JSON string to object
let data = parse('{"name": "Alice", "age": 30}');
print(data.name);  // Alice

// Convert object to JSON string
let json = stringify({ status: "success", count: 42 });

// Parse JSON file
let config = parse_file("config.json");

// Write object to JSON file
stringify_file({ version: "1.0" }, "output.json");

// Try parse (returns null on error instead of throwing)
let result = try_parse(invalid_json);  // null if invalid
```

### Implementation Notes
- Uses `deserialize()` and `serialize()` internally (still builtins)
- Provides try/catch wrappers for safer error handling
- File operations use new File API internally

---

## Part 3: stdlib/fs Module

### Module Location
`stdlib/fs.hml`

### Functions

```hemlock
import * as fs from "std/fs";

// File existence
if (fs.exists("config.json")) {
    print("Config exists");
}

// Read/write files (convenience wrappers)
let content = fs.read_file("input.txt");
fs.write_file("output.txt", content);
fs.append_file("log.txt", "New entry\n");

// Directory operations
fs.make_dir("new_directory", 0o755);
let files = fs.list_dir(".");
for (let i = 0; i < files.length; i = i + 1) {
    print(files[i]);
}
fs.remove_dir("old_directory");

// File type checking
if (fs.is_file("data.txt")) {
    print("Regular file");
}
if (fs.is_dir("/etc")) {
    print("Directory");
}

// File metadata
let info = fs.file_stat("myfile.txt");
print("Size: " + info.size + " bytes");
print("Modified: " + info.mtime);
print("Is file: " + info.is_file);
print("Is dir: " + info.is_dir);
```

### Implementation Notes
- Uses FFI to call C library functions (`mkdir`, `rmdir`, `stat`, `opendir`, `readdir`, `closedir`)
- Imports `libc.so.6` for system calls
- Uses helper builtins for low-level operations

---

## Part 4: Helper Builtins (Internal)

These builtins are prefixed with `__` to indicate they're internal and not intended for direct user use:

```c
// Read integers from pointers (for struct field access)
__read_u32(ptr) → u32
__read_u64(ptr) → i32  // Note: truncated due to lack of u64 type

// Extract filename from dirent struct
__dirent_name(dirent_ptr) → string

// Convert Hemlock string to null-terminated C string pointer
__string_to_cstr(str) → ptr  // malloc'd, must be free'd
```

These are used by `stdlib/fs.hml` for FFI operations.

---

## Updated Global Namespace

### Global Functions (After Refactor)

**Core (12 functions):**
```hemlock
// Memory
alloc(size), free(ptr), buffer(size), memset(ptr, byte, size),
memcpy(dest, src, size), realloc(ptr, size)

// Type
typeof(value)

// File I/O
open(path, mode?)  // Returns File object with methods

// Output
print(...values)

// Concurrency
spawn(fn, ...args), join(task), detach(task), channel(capacity)
```

**Internal Builtins (not typically used directly):**
```hemlock
// These remain for stdlib use:
serialize(obj), deserialize(json), file_exists(path)

// Helper builtins (double underscore = internal):
__read_u32(ptr), __read_u64(ptr), __dirent_name(ptr), __string_to_cstr(str)
```

### Methods on Objects

```hemlock
// String (15 methods)
substr, slice, find, contains, split, trim, to_upper, to_lower,
starts_with, ends_with, replace, replace_all, repeat, char_at, to_bytes

// Array (15 methods)
push, pop, shift, unshift, insert, remove, find, contains, slice,
join, concat, reverse, first, last, clear

// File (5 methods) ← NEW!
read, write, seek, tell, close

// Channel (3 methods)
send, recv, close
```

---

## Migration Guide

### File I/O Migration

**Pattern 1: Read file**
```hemlock
// Before
let content = read_file("data.txt");

// After
let f = open("data.txt", "r");
let content = f.read();
f.close();

// Or use stdlib:
import { read_file } from "std/fs";
let content = read_file("data.txt");
```

**Pattern 2: Write file**
```hemlock
// Before
write_file("output.txt", "Hello!");

// After
let f = open("output.txt", "w");
f.write("Hello!");
f.close();

// Or use stdlib:
import { write_file } from "std/fs";
write_file("output.txt", "Hello!");
```

**Pattern 3: Append to file**
```hemlock
// Before
append_file("log.txt", "Entry\n");

// After
let f = open("log.txt", "a");
f.write("Entry\n");
f.close();

// Or use stdlib:
import { append_file } from "std/fs";
append_file("log.txt", "Entry\n");
```

### JSON Migration

```hemlock
// Before
let obj = deserialize('{"x": 10}');
let json = serialize(obj);

// After
import { parse, stringify } from "std/json";
let obj = parse('{"x": 10}');
let json = stringify(obj);
```

### Filesystem Operations Migration

```hemlock
// Before
if (file_exists("config.json")) { ... }

// After
import { exists } from "std/fs";
if (exists("config.json")) { ... }
```

---

## Implementation Details

### C Code Changes

**Modified Files:**
- `src/interpreter/io.c` - Updated `call_file_method()` to use new method signatures
- `src/interpreter/runtime.c` - Added `file.path` property support
- `src/interpreter/builtins.c` - Added helper builtins, kept serialize/deserialize/file_exists
- `src/interpreter/internal.h` - Updated function declarations

**New Files:**
- `stdlib/json.hml` - JSON module
- `stdlib/fs.hml` - Filesystem module

### Test Updates

Updated **7 test files** in `tests/io/`:
- `open_read.hml` - Now uses `f.read()` instead of `f.read_text(size)`
- `open_write.hml` - Now uses new File API for reading results
- `write_read.hml` - Replaced `read_file()`/`write_file()` with File methods
- `seek_tell.hml` - Replaced `write_file()` with File methods
- `file_exists.hml` - Replaced `write_file()` with File methods
- `append.hml` - Replaced `append_file()` with `open(..., "a")`
- `binary.hml` - Updated to use strings (binary I/O deferred to future version)

**Result:** All 216 tests passing! ✅

---

## Future Work

### Not Included in v1.0

The following features are deferred to future versions:

1. **Binary File I/O**
   - `f.read_bytes(size)` / `f.write_bytes(buffer)`
   - Binary mode support
   - Buffer-based file operations

2. **Advanced File Operations**
   - File permissions/ownership management
   - Symbolic link operations
   - File locking

3. **Enhanced Error Handling**
   - More granular error codes
   - Better error messages from FFI operations

4. **64-bit Support**
   - Full `u64`/`i64` types (currently truncated to `i32`)
   - Larger file size support

---

## Breaking Changes

⚠️ **This refactor includes breaking changes:**

1. **Removed global functions:** `read_file()`, `write_file()`, `append_file()`, `read_bytes()`, `write_bytes()`
2. **Changed File method signature:** `f.read_text(size)` → `f.read()` (reads entire file)
3. **File properties added:** `f.path`, `f.mode`, `f.closed`

Users must update their code to use:
- New File methods for direct file operations
- `stdlib/json` module for JSON operations
- `stdlib/fs` module for filesystem operations

---

## Success Metrics

✅ **All objectives achieved:**
- File I/O refactored to object-oriented API
- Standard library modules created and tested
- Global namespace cleaned up
- All 216 tests passing
- Code compiles without warnings
- Documentation updated

🎉 **Hemlock 1.0 Refactor Complete!**
