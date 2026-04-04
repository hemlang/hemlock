# File I/O in Hemlock

Hemlock provides a **File object API** for file operations with proper error handling and resource management.

## Table of Contents

- [Overview](#overview)
- [Opening Files](#opening-files)
- [File Methods](#file-methods)
- [File Properties](#file-properties)
- [Error Handling](#error-handling)
- [Resource Management](#resource-management)
- [Complete API Reference](#complete-api-reference)
- [Common Patterns](#common-patterns)
- [Best Practices](#best-practices)

## Overview

The File object API provides:

- **Explicit resource management** - Files must be manually closed
- **Multiple open modes** - Read, write, append, read/write
- **Text and binary operations** - Read/write both text and binary data
- **Seeking support** - Random access within files
- **Comprehensive error messages** - Context-aware error reporting

**Important:** Files are not automatically closed. You must call `f.close()` to avoid file descriptor leaks.

## Opening Files

Use `open(path, mode?)` to open a file:

```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");     // Read mode (default)
let f2 = open("output.txt", "w");  // Write mode (truncate)
let f3 = open("log.txt", "a");     // Append mode
let f4 = open("data.bin", "r+");   // Read/write mode
```

### Open Modes

| Mode | Description | File Must Exist | Truncates | Position |
|------|-------------|----------------|-----------|----------|
| `"r"` | Read (default) | Yes | No | Start |
| `"w"` | Write | No (creates) | Yes | Start |
| `"a"` | Append | No (creates) | No | End |
| `"r+"` | Read and write | Yes | No | Start |
| `"w+"` | Read and write | No (creates) | Yes | Start |
| `"a+"` | Read and append | No (creates) | No | End |

### Examples

**Reading an existing file:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("config.json", "r");
// or simply:
let f = open("config.json");  // "r" is default
```

**Creating a new file for writing:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("output.txt", "w");  // Creates or truncates
```

**Appending to a file:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("log.txt", "a");  // Creates if doesn't exist
```

**Read and write mode:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.bin", "r+");  // Existing file, can read/write
```

## File Methods

### Reading

#### read(size?: i32): string

Read text from file (optional size parameter).

**Without size (read all):**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
let all = f.read();  // Read from current position to EOF
f.close();
```

**With size (read specific bytes):**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
let chunk = f.read(1024);  // Read up to 1024 bytes
let next = f.read(1024);   // Read next 1024 bytes
f.close();
```

**Returns:** String containing the read data, or empty string if at EOF

**Example - Reading entire file:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("poem.txt", "r");
let content = f.read();
print(content);
f.close();
```

**Example - Reading in chunks:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("large.txt", "r");
while (true) {
    let chunk = f.read(4096);  // 4KB chunks
    if (chunk == "") { break; }  // EOF reached
    process(chunk);
}
f.close();
```

#### read_bytes(size: i32): buffer

Read binary data (returns buffer).

**Parameters:**
- `size` (i32) - Number of bytes to read

**Returns:** Buffer containing the read bytes

```hemlock
import { open } from "@stdlib/fs";

let f = open("image.png", "r");
let binary = f.read_bytes(256);  // Read 256 bytes
print(binary.length);  // 256 (or less if EOF)

// Access individual bytes
let first_byte = binary[0];
print(first_byte);

f.close();
```

**Example - Reading entire binary file:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.bin", "r");
let size = 10240;  // Expected size
let data = f.read_bytes(size);
f.close();

// Process binary data
let i = 0;
while (i < data.length) {
    let byte = data[i];
    // ... process byte
    i = i + 1;
}
```

### Writing

#### write(data: string): i32

Write text to file (returns bytes written).

**Parameters:**
- `data` (string) - Text to write

**Returns:** Number of bytes written (i32)

```hemlock
import { open } from "@stdlib/fs";

let f = open("output.txt", "w");
let written = f.write("Hello, World!\n");
print("Wrote " + typeof(written) + " bytes");  // "Wrote 14 bytes"
f.close();
```

**Example - Writing multiple lines:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("output.txt", "w");
f.write("Line 1\n");
f.write("Line 2\n");
f.write("Line 3\n");
f.close();
```

**Example - Appending to log file:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("app.log", "a");
f.write("[INFO] Application started\n");
f.write("[INFO] User logged in\n");
f.close();
```

#### write_bytes(data: buffer): i32

Write binary data (returns bytes written).

**Parameters:**
- `data` (buffer) - Binary data to write

**Returns:** Number of bytes written (i32)

```hemlock
import { open } from "@stdlib/fs";

let f = open("output.bin", "w");

// Create binary data
let buf = buffer(10);
buf[0] = 65;  // 'A'
buf[1] = 66;  // 'B'
buf[2] = 67;  // 'C'

let bytes = f.write_bytes(buf);
print("Wrote " + typeof(bytes) + " bytes");

f.close();
```

**Example - Copying binary file:**
```hemlock
import { open } from "@stdlib/fs";

let src = open("input.bin", "r");
let dst = open("output.bin", "w");

let data = src.read_bytes(1024);
while (data.length > 0) {
    dst.write_bytes(data);
    data = src.read_bytes(1024);
}

src.close();
dst.close();
```

### Seeking

#### seek(position: i32): i32

Move to specific position (returns new position).

**Parameters:**
- `position` (i32) - Byte offset from beginning of file

**Returns:** New position (i32)

```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");

// Move to byte 100
f.seek(100);

// Read from position 100
let data = f.read(50);

// Reset to beginning
f.seek(0);

f.close();
```

**Example - Random access:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("records.dat", "r");

// Read record at offset 1000
f.seek(1000);
let record1 = f.read_bytes(100);

// Read record at offset 2000
f.seek(2000);
let record2 = f.read_bytes(100);

f.close();
```

#### tell(): i32

Get current position in file.

**Returns:** Current byte offset (i32)

```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");

let pos1 = f.tell();  // 0 (at start)

f.read(100);
let pos2 = f.tell();  // 100 (after reading 100 bytes)

f.seek(500);
let pos3 = f.tell();  // 500 (after seeking)

f.close();
```

**Example - Measuring read amount:**
```hemlock
let f = open("data.txt", "r");

let start = f.tell();
let content = f.read();
let end = f.tell();

let bytes_read = end - start;
print("Read " + typeof(bytes_read) + " bytes");

f.close();
```

### Closing

#### close()

Close file (idempotent, can call multiple times).

```hemlock
let f = open("data.txt", "r");
// ... use file
f.close();
f.close();  // Safe - no error on second close
```

**Important notes:**
- Always close files when done to avoid file descriptor leaks
- Closing is idempotent - can call multiple times safely
- After closing, all other operations will error
- Use `finally` blocks to ensure files are closed even on errors

## File Properties

File objects have three read-only properties:

### path: string

The file path used to open the file.

```hemlock
import { open } from "@stdlib/fs";

let f = open("/path/to/file.txt", "r");
print(f.path);  // "/path/to/file.txt"
f.close();
```

### mode: string

The mode the file was opened with.

```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
print(f.mode);  // "r"
f.close();

let f2 = open("output.txt", "w");
print(f2.mode);  // "w"
f2.close();
```

### closed: bool

Whether the file is closed.

```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
print(f.closed);  // false

f.close();
print(f.closed);  // true
```

**Example - Checking if file is open:**
```hemlock
let f = open("data.txt", "r");

if (!f.closed) {
    let content = f.read();
    // ... process content
}

f.close();

if (f.closed) {
    print("File is now closed");
}
```

## Error Handling

All file operations include proper error messages with context.

### Common Errors

**File not found:**
```hemlock
let f = open("missing.txt", "r");
// Error: Failed to open 'missing.txt': No such file or directory
```

**Reading from closed file:**
```hemlock
let f = open("data.txt", "r");
f.close();
f.read();
// Error: Cannot read from closed file 'data.txt'
```

**Writing to read-only file:**
```hemlock
let f = open("readonly.txt", "r");
f.write("data");
// Error: Cannot write to file 'readonly.txt' opened in read-only mode
```

**Reading from write-only file:**
```hemlock
let f = open("output.txt", "w");
f.read();
// Error: Cannot read from file 'output.txt' opened in write-only mode
```

### Using try/catch

```hemlock
import { open } from "@stdlib/fs";

try {
    let f = open("data.txt", "r");
    let content = f.read();
    f.close();
    process(content);
} catch (e) {
    print("Error reading file: " + e);
}
```

## Resource Management

### Basic Pattern

Always close files explicitly:

```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
let content = f.read();
f.close();
```

### With Error Handling (Recommended)

Use `finally` to ensure files are closed even on errors:

```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
try {
    let content = f.read();
    process(content);
} finally {
    f.close();  // Always close, even on error
}
```

### Multiple Files

```hemlock
import { open } from "@stdlib/fs";

let src = null;
let dst = null;

try {
    src = open("input.txt", "r");
    dst = open("output.txt", "w");

    let content = src.read();
    dst.write(content);
} finally {
    if (src != null) { src.close(); }
    if (dst != null) { dst.close(); }
}
```

### Helper Function Pattern

```hemlock
import { open } from "@stdlib/fs";

fn with_file(path: string, mode: string, callback) {
    let f = open(path, mode);
    try {
        return callback(f);
    } finally {
        f.close();
    }
}

// Usage:
with_file("data.txt", "r", fn(f) {
    return f.read();
});
```

## Complete API Reference

### Functions

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `open(path, mode?)` | path: string, mode?: string | File | Open file (mode defaults to "r") |

### Methods

| Method | Parameters | Returns | Description |
|--------|-----------|---------|-------------|
| `read(size?)` | size?: i32 | string | Read text (all or specific bytes) |
| `read_bytes(size)` | size: i32 | buffer | Read binary data |
| `write(data)` | data: string | i32 | Write text, returns bytes written |
| `write_bytes(data)` | data: buffer | i32 | Write binary data, returns bytes written |
| `seek(position)` | position: i32 | i32 | Seek to position, returns new position |
| `tell()` | - | i32 | Get current position |
| `close()` | - | null | Close file (idempotent) |

### Properties (read-only)

| Property | Type | Description |
|----------|------|-------------|
| `path` | string | File path |
| `mode` | string | Open mode |
| `closed` | bool | Whether file is closed |

## Common Patterns

### Reading Entire File

```hemlock
import { open } from "@stdlib/fs";

fn read_file(path: string): string {
    let f = open(path, "r");
    try {
        return f.read();
    } finally {
        f.close();
    }
}

let content = read_file("config.json");
```

### Writing Entire File

```hemlock
import { open } from "@stdlib/fs";

fn write_file(path: string, content: string) {
    let f = open(path, "w");
    try {
        f.write(content);
    } finally {
        f.close();
    }
}

write_file("output.txt", "Hello, World!");
```

### Appending to File

```hemlock
import { open } from "@stdlib/fs";

fn append_file(path: string, content: string) {
    let f = open(path, "a");
    try {
        f.write(content);
    } finally {
        f.close();
    }
}

append_file("log.txt", "[INFO] Event occurred\n");
```

### Reading Lines

```hemlock
import { open } from "@stdlib/fs";

fn read_lines(path: string) {
    let f = open(path, "r");
    try {
        let content = f.read();
        return content.split("\n");
    } finally {
        f.close();
    }
}

let lines = read_lines("data.txt");
let i = 0;
while (i < lines.length) {
    print("Line " + typeof(i) + ": " + lines[i]);
    i = i + 1;
}
```

### Processing Large Files in Chunks

```hemlock
import { open } from "@stdlib/fs";

fn process_large_file(path: string) {
    let f = open(path, "r");
    try {
        while (true) {
            let chunk = f.read(4096);  // 4KB chunks
            if (chunk == "") { break; }

            // Process chunk
            process_chunk(chunk);
        }
    } finally {
        f.close();
    }
}
```

### Binary File Copy

```hemlock
import { open } from "@stdlib/fs";

fn copy_file(src_path: string, dst_path: string) {
    let src = null;
    let dst = null;

    try {
        src = open(src_path, "r");
        dst = open(dst_path, "w");

        while (true) {
            let chunk = src.read_bytes(4096);
            if (chunk.length == 0) { break; }

            dst.write_bytes(chunk);
        }
    } finally {
        if (src != null) { src.close(); }
        if (dst != null) { dst.close(); }
    }
}

copy_file("input.dat", "output.dat");
```

### File Truncation

```hemlock
import { open } from "@stdlib/fs";

fn truncate_file(path: string) {
    let f = open(path, "w");  // "w" mode truncates
    f.close();
}

truncate_file("empty_me.txt");
```

### Random Access Read

```hemlock
import { open } from "@stdlib/fs";

fn read_at_offset(path: string, offset: i32, size: i32): string {
    let f = open(path, "r");
    try {
        f.seek(offset);
        return f.read(size);
    } finally {
        f.close();
    }
}

let data = read_at_offset("records.dat", 1000, 100);
```

### File Size

```hemlock
import { open } from "@stdlib/fs";

fn file_size(path: string): i32 {
    let f = open(path, "r");
    try {
        // Seek to end
        let end = f.seek(999999999);  // Large number
        f.seek(0);  // Reset
        return end;
    } finally {
        f.close();
    }
}

let size = file_size("data.txt");
print("File size: " + typeof(size) + " bytes");
```

### Conditional Read/Write

```hemlock
import { open } from "@stdlib/fs";

fn update_file(path: string, condition, new_content: string) {
    let f = open(path, "r+");
    try {
        let content = f.read();

        if (condition(content)) {
            f.seek(0);  // Reset to beginning
            f.write(new_content);
        }
    } finally {
        f.close();
    }
}
```

## Best Practices

### 1. Always Use try/finally

```hemlock
import { open } from "@stdlib/fs";

// Good
let f = open("data.txt", "r");
try {
    let content = f.read();
    process(content);
} finally {
    f.close();
}

// Bad - file might not close on error
let f = open("data.txt", "r");
let content = f.read();
process(content);  // If this throws, file leaks
f.close();
```

### 2. Check File State Before Operations

```hemlock
let f = open("data.txt", "r");

if (!f.closed) {
    let content = f.read();
    // ... use content
}

f.close();
```

### 3. Use Appropriate Modes

```hemlock
// Reading only? Use "r"
let f = open("config.json", "r");

// Completely replacing? Use "w"
let f = open("output.txt", "w");

// Adding to end? Use "a"
let f = open("log.txt", "a");
```

### 4. Handle Errors Gracefully

```hemlock
import { open } from "@stdlib/fs";

fn safe_read_file(path: string): string {
    try {
        let f = open(path, "r");
        try {
            return f.read();
        } finally {
            f.close();
        }
    } catch (e) {
        print("Warning: Could not read " + path + ": " + e);
        return "";
    }
}
```

### 5. Close Files in Reverse Order of Opening

```hemlock
let f1 = null;
let f2 = null;
let f3 = null;

try {
    f1 = open("file1.txt", "r");
    f2 = open("file2.txt", "r");
    f3 = open("file3.txt", "r");

    // ... use files
} finally {
    // Close in reverse order
    if (f3 != null) { f3.close(); }
    if (f2 != null) { f2.close(); }
    if (f1 != null) { f1.close(); }
}
```

### 6. Avoid Reading Large Files Entirely

```hemlock
// Bad for large files
let f = open("huge.log", "r");
let content = f.read();  // Loads entire file into memory
f.close();

// Good - process in chunks
let f = open("huge.log", "r");
try {
    while (true) {
        let chunk = f.read(4096);
        if (chunk == "") { break; }
        process_chunk(chunk);
    }
} finally {
    f.close();
}
```

## Summary

Hemlock's File I/O API provides:

- ✅ Simple, explicit file operations
- ✅ Text and binary support
- ✅ Random access with seek/tell
- ✅ Clear error messages with context
- ✅ Idempotent close operation

Remember:
- Always close files manually
- Use try/finally for resource safety
- Choose appropriate open modes
- Handle errors gracefully
- Process large files in chunks
