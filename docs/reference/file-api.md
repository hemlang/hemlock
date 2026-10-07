# File API Reference

Complete reference for Hemlock's File I/O system.

---

## Overview

Hemlock provides a **File object API** for file operations with proper error handling and resource management. Files must be manually opened and closed.

**Key Features:**
- File object with methods
- Read/write text and binary data
- Seeking and positioning
- Proper error messages
- Manual resource management (no RAII)

---

## File Type

**Type:** `file`

**Description:** File handle for I/O operations

**Properties (Read-Only):**
- `.path` - File path (string)
- `.mode` - Open mode (string)
- `.closed` - Whether file is closed (bool)

---

## Opening Files

### open

Open a file for reading, writing, or both.

**Signature:**
```hemlock
open(path: string, mode?: string): file
```

**Parameters:**
- `path` - File path (relative or absolute)
- `mode` (optional) - Open mode (default: `"r"`)

**Returns:** File object

**Modes:**
- `"r"` - Read (default)
- `"w"` - Write (truncate existing file)
- `"a"` - Append
- `"r+"` - Read and write
- `"w+"` - Read and write (truncate)
- `"a+"` - Read and append

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

// Read mode (default)
let f = open("data.txt");
let f_read = open("data.txt", "r");

// Write mode (truncate)
let f_write = open("output.txt", "w");

// Append mode
let f_append = open("log.txt", "a");

// Read/write mode
let f_rw = open("data.bin", "r+");

// Read/write (truncate)
let f_rw_trunc = open("output.bin", "w+");

// Read/append
let f_ra = open("log.txt", "a+");
```

**Error Handling:**
```hemlock
import { open } from "@stdlib/fs";

try {
    let f = open("missing.txt", "r");
} catch (e) {
    print("Failed to open:", e);
    // Error: Failed to open 'missing.txt': No such file or directory
}
```

**Important:** Files must be closed manually with `f.close()` to avoid file descriptor leaks.

---

## File Methods

### Reading

#### read

Read text from file.

**Signature:**
```hemlock
file.read(size?: i32): string
```

**Parameters:**
- `size` (optional) - Number of bytes to read (if omitted, reads to EOF)

**Returns:** String with file contents

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");

// Read entire file
let all = f.read();
print(all);

// Read specific number of bytes
let chunk = f.read(1024);

f.close();
```

**Behavior:**
- Reads from current file position
- Returns empty string at EOF
- Advances file position

**Errors:**
- Reading from closed file
- Reading from write-only file

---

#### read_bytes

Read binary data from file.

**Signature:**
```hemlock
file.read_bytes(size: i32): buffer
```

**Parameters:**
- `size` - Number of bytes to read

**Returns:** Buffer with binary data

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.bin", "r");

// Read 256 bytes
let binary = f.read_bytes(256);
print(binary.length);       // 256

// Process binary data
let i = 0;
while (i < binary.length) {
    print(binary[i]);
    i = i + 1;
}

f.close();
```

**Behavior:**
- Reads exact number of bytes
- Returns buffer (not string)
- Advances file position

---

### Writing

#### write

Write text to file.

**Signature:**
```hemlock
file.write(data: string): i32
```

**Parameters:**
- `data` - String to write

**Returns:** Number of bytes written (i32)

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("output.txt", "w");

// Write text
let written = f.write("Hello, World!\n");
print("Wrote", written, "bytes");

// Multiple writes
f.write("Line 1\n");
f.write("Line 2\n");
f.write("Line 3\n");

f.close();
```

**Behavior:**
- Writes at current file position
- Returns number of bytes written
- Advances file position

**Errors:**
- Writing to closed file
- Writing to read-only file

---

#### write_bytes

Write binary data to file.

**Signature:**
```hemlock
file.write_bytes(data: buffer): i32
```

**Parameters:**
- `data` - Buffer to write

**Returns:** Number of bytes written (i32)

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("output.bin", "w");

// Create buffer
let buf = buffer(10);
buf[0] = 65;  // 'A'
buf[1] = 66;  // 'B'
buf[2] = 67;  // 'C'

// Write buffer
let written = f.write_bytes(buf);
print("Wrote", written, "bytes");

f.close();
```

**Behavior:**
- Writes buffer contents to file
- Returns number of bytes written
- Advances file position

---

### Seeking

#### seek

Move file position to specific byte offset.

**Signature:**
```hemlock
file.seek(position: i32): i32
```

**Parameters:**
- `position` - Byte offset from beginning of file

**Returns:** New file position (i32)

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");

// Jump to byte 100
f.seek(100);

// Read from that position
let chunk = f.read(50);

// Reset to beginning
f.seek(0);

// Read from start
let all = f.read();

f.close();
```

**Behavior:**
- Sets file position to absolute offset
- Returns new position
- Seeking past EOF is allowed (creates hole in file when writing)

---

#### tell

Get current file position.

**Signature:**
```hemlock
file.tell(): i32
```

**Returns:** Current byte offset from beginning of file (i32)

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");

print(f.tell());        // 0 (at start)

f.read(100);
print(f.tell());        // 100 (after reading)

f.seek(50);
print(f.tell());        // 50 (after seeking)

f.close();
```

---

### Closing

#### close

Close file (idempotent).

**Signature:**
```hemlock
file.close(): null
```

**Returns:** `null`

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
let content = f.read();
f.close();

// Safe to call multiple times
f.close();  // No error
f.close();  // No error
```

**Behavior:**
- Closes file handle
- Flushes any pending writes
- Idempotent (safe to call multiple times)
- Sets `.closed` property to `true`

**Important:** Always close files when done to avoid file descriptor leaks.

---

## File Properties

### .path

Get file path.

**Type:** `string`

**Access:** Read-only

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("/path/to/file.txt", "r");
print(f.path);          // "/path/to/file.txt"
f.close();
```

---

### .mode

Get open mode.

**Type:** `string`

**Access:** Read-only

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
print(f.mode);          // "r"
f.close();

let f2 = open("output.txt", "w");
print(f2.mode);         // "w"
f2.close();
```

---

### .closed

Check if file is closed.

**Type:** `bool`

**Access:** Read-only

**Examples:**
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
print(f.closed);        // false

f.close();
print(f.closed);        // true
```

---

## Error Handling

All file operations include proper error messages with context:

### File Not Found
```hemlock
import { open } from "@stdlib/fs";

let f = open("missing.txt", "r");
// Error: Failed to open 'missing.txt': No such file or directory
```

### Reading from Closed File
```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
f.close();
f.read();
// Error: Cannot read from closed file 'data.txt'
```

### Writing to Read-Only File
```hemlock
import { open } from "@stdlib/fs";

let f = open("readonly.txt", "r");
f.write("data");
// Error: Cannot write to file 'readonly.txt' opened in read-only mode
```

### Using try/catch
```hemlock
import { open } from "@stdlib/fs";

let f = null;
try {
    f = open("data.txt", "r");
    let content = f.read();
    print(content);
} catch (e) {
    print("File error:", e);
} finally {
    if (f != null && !f.closed) {
        f.close();
    }
}
```

---

## Resource Management Patterns

### Basic Pattern

```hemlock
import { open } from "@stdlib/fs";

let f = open("data.txt", "r");
let content = f.read();
f.close();
```

### With Error Handling

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

### Safe Pattern

```hemlock
import { open } from "@stdlib/fs";

let f = null;
try {
    f = open("data.txt", "r");
    let content = f.read();
    // ... process content ...
} catch (e) {
    print("Error:", e);
} finally {
    if (f != null && !f.closed) {
        f.close();
    }
}
```

---

## Usage Examples

### Read Entire File

```hemlock
import { open } from "@stdlib/fs";

fn read_file(filename: string): string {
    let f = open(filename, "r");
    let content = f.read();
    f.close();
    return content;
}

let text = read_file("data.txt");
print(text);
```

### Write Text File

```hemlock
import { open } from "@stdlib/fs";

fn write_file(filename: string, content: string) {
    let f = open(filename, "w");
    f.write(content);
    f.close();
}

write_file("output.txt", "Hello, World!\n");
```

### Append to File

```hemlock
import { open } from "@stdlib/fs";

fn append_file(filename: string, line: string) {
    let f = open(filename, "a");
    f.write(line + "\n");
    f.close();
}

append_file("log.txt", "Log entry 1");
append_file("log.txt", "Log entry 2");
```

### Read Binary File

```hemlock
import { open } from "@stdlib/fs";

fn read_binary(filename: string, size: i32): buffer {
    let f = open(filename, "r");
    let data = f.read_bytes(size);
    f.close();
    return data;
}

let binary = read_binary("data.bin", 256);
print("Read", binary.length, "bytes");
```

### Write Binary File

```hemlock
import { open } from "@stdlib/fs";

fn write_binary(filename: string, data: buffer) {
    let f = open(filename, "w");
    f.write_bytes(data);
    f.close();
}

let buf = buffer(10);
buf[0] = 65;
write_binary("output.bin", buf);
```

### Read File Line by Line

```hemlock
import { open } from "@stdlib/fs";

fn read_lines(filename: string): array {
    let f = open(filename, "r");
    let content = f.read();
    f.close();
    return content.split("\n");
}

let lines = read_lines("data.txt");
let i = 0;
while (i < lines.length) {
    print("Line", i, ":", lines[i]);
    i = i + 1;
}
```

### Copy File

```hemlock
import { open } from "@stdlib/fs";

fn copy_file(src: string, dest: string) {
    let f_in = open(src, "r");
    let f_out = open(dest, "w");

    let content = f_in.read();
    f_out.write(content);

    f_in.close();
    f_out.close();
}

copy_file("input.txt", "output.txt");
```

### Read File in Chunks

```hemlock
import { open } from "@stdlib/fs";

fn process_chunks(filename: string) {
    let f = open(filename, "r");

    while (true) {
        let chunk = f.read(1024);  // Read 1KB at a time
        if (chunk.length == 0) {
            break;  // EOF
        }

        // Process chunk
        print("Processing", chunk.length, "bytes");
    }

    f.close();
}

process_chunks("large_file.txt");
```

---

## Complete Method Summary

| Method        | Signature                | Returns   | Description                  |
|---------------|--------------------------|-----------|------------------------------|
| `read`        | `(size?: i32)`           | `string`  | Read text                    |
| `read_bytes`  | `(size: i32)`            | `buffer`  | Read binary data             |
| `write`       | `(data: string)`         | `i32`     | Write text                   |
| `write_bytes` | `(data: buffer)`         | `i32`     | Write binary data            |
| `seek`        | `(position: i32)`        | `i32`     | Set file position            |
| `tell`        | `()`                     | `i32`     | Get file position            |
| `close`       | `()`                     | `null`    | Close file (idempotent)      |

---

## Complete Property Summary

| Property  | Type     | Access     | Description              |
|-----------|----------|------------|--------------------------|
| `.path`   | `string` | Read-only  | File path                |
| `.mode`   | `string` | Read-only  | Open mode                |
| `.closed` | `bool`   | Read-only  | Whether file is closed   |

---

## Migration from Old API

**Old API (Removed):**
- `read_file(path)` - Use `open(path, "r").read()`
- `write_file(path, data)` - Use `open(path, "w").write(data)`
- `append_file(path, data)` - Use `open(path, "a").write(data)`
- `file_exists(path)` - No replacement yet

**Migration Example:**
```hemlock
import { read_file, write_file, open } from "@stdlib/fs";

// Old (v0.0)
let content = read_file("data.txt");
write_file("output.txt", content);

// New (v0.1)
let f = open("data.txt", "r");
let content = f.read();
f.close();

let f2 = open("output.txt", "w");
f2.write(content);
f2.close();
```

---

## See Also

- [Built-in Functions](builtins.md) - `open()` function
- [Memory API](memory-api.md) - Buffer type
- [String API](string-api.md) - String methods for text processing
