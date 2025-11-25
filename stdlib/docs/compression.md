# @stdlib/compression - Compression and Archive Utilities

Provides zlib/gzip compression/decompression via FFI and tar archive reading/writing in pure Hemlock.

## Overview

This module provides:
- **Deflate/Inflate**: Raw zlib compression format
- **Gzip/Gunzip**: Gzip format with header and checksum
- **Tar Archives**: Create and read POSIX tar archives

## Installation Requirements

Requires zlib library installed on the system:
```bash
# Ubuntu/Debian
sudo apt-get install zlib1g-dev

# macOS (usually pre-installed)
brew install zlib
```

## Import

```hemlock
// Import specific functions
import { compress, decompress, gzip, gunzip } from "@stdlib/compression";
import { TarWriter, TarReader } from "@stdlib/compression";

// Import all
import * as compression from "@stdlib/compression";
```

## Compression Constants

### Compression Levels

| Constant | Value | Description |
|----------|-------|-------------|
| `Z_NO_COMPRESSION` | 0 | No compression |
| `Z_BEST_SPEED` | 1 | Fastest compression |
| `Z_BEST_COMPRESSION` | 9 | Best compression ratio |
| `Z_DEFAULT_COMPRESSION` | -1 | Default level (6) |
| `LEVEL_NONE` | 0 | No compression |
| `LEVEL_FASTEST` | 1 | Fastest speed |
| `LEVEL_FAST` | 3 | Fast compression |
| `LEVEL_DEFAULT` | 6 | Balanced (default) |
| `LEVEL_BEST` | 9 | Best ratio |

### Tar File Types

| Constant | Value | Description |
|----------|-------|-------------|
| `TAR_TYPE_FILE` | '0' | Regular file |
| `TAR_TYPE_HARDLINK` | '1' | Hard link |
| `TAR_TYPE_SYMLINK` | '2' | Symbolic link |
| `TAR_TYPE_CHARDEV` | '3' | Character device |
| `TAR_TYPE_BLOCKDEV` | '4' | Block device |
| `TAR_TYPE_DIRECTORY` | '5' | Directory |
| `TAR_TYPE_FIFO` | '6' | FIFO/pipe |

## Functions

### compress(data, level?) -> buffer

Compress a string using raw deflate format.

**Parameters:**
- `data: string` - Data to compress
- `level?: i32` - Compression level 0-9 (default: 6)

**Returns:** `buffer` - Compressed data

**Example:**
```hemlock
import { compress, decompress } from "@stdlib/compression";

let original = "Hello, World! This is a test string to compress.";
let compressed = compress(original);
print("Compressed size: " + typeof(compressed.length));

let restored = decompress(compressed);
print(restored);  // "Hello, World! This is a test string to compress."
```

### decompress(data, max_size?) -> string

Decompress raw deflate data.

**Parameters:**
- `data: buffer` - Compressed data
- `max_size?: i64` - Maximum output size (default: 10MB)

**Returns:** `string` - Decompressed data

**Throws:** Exception if decompression fails or data is corrupted

### deflate_compress(data, level?) -> buffer

Alias for `compress()`. Compress using raw deflate format.

### inflate_decompress(data, max_size?) -> string

Alias for `decompress()`. Decompress raw deflate data.

### gzip(data, level?) -> buffer

Compress data to gzip format (with header and checksum).

**Parameters:**
- `data: string` - Data to compress
- `level?: i32` - Compression level 0-9 (default: 6)

**Returns:** `buffer` - Gzip-compressed data

**Example:**
```hemlock
import { gzip, gunzip } from "@stdlib/compression";

let data = "Some text data to compress with gzip format.";
let compressed = gzip(data, 9);  // Best compression

// Verify gzip magic bytes
print(compressed[0]);  // 31 (0x1f)
print(compressed[1]);  // 139 (0x8b)

let restored = gunzip(compressed);
print(restored);  // Original string
```

### gunzip(data, max_size?) -> string

Decompress gzip data.

**Parameters:**
- `data: buffer` - Gzip-compressed data
- `max_size?: i64` - Maximum output size (default: 10MB)

**Returns:** `string` - Decompressed data

**Throws:**
- Exception if magic bytes are invalid
- Exception if decompression fails

### gzip_file(input_path, output_path, level?) -> null

Compress a file to gzip format.

**Parameters:**
- `input_path: string` - Path to input file
- `output_path: string` - Path to output .gz file
- `level?: i32` - Compression level (default: 6)

**Example:**
```hemlock
import { gzip_file, gunzip_file } from "@stdlib/compression";

// Compress file
gzip_file("data.txt", "data.txt.gz");

// Decompress file
gunzip_file("data.txt.gz", "data_restored.txt");
```

### gunzip_file(input_path, output_path, max_size?) -> null

Decompress a gzip file.

**Parameters:**
- `input_path: string` - Path to .gz file
- `output_path: string` - Path to output file
- `max_size?: i64` - Maximum output size (default: 10MB)

### compress_bound(source_len) -> i64

Calculate maximum compressed size for a given input size.

**Parameters:**
- `source_len: i64` - Input data length

**Returns:** `i64` - Maximum compressed output size

### crc32(data) -> u32

Calculate CRC32 checksum of buffer data.

**Parameters:**
- `data: buffer` - Data to checksum

**Returns:** `u32` - CRC32 checksum

**Example:**
```hemlock
import { crc32 } from "@stdlib/compression";

let data = buffer(5);
data[0] = 72; data[1] = 101; data[2] = 108; data[3] = 108; data[4] = 111;  // "Hello"
let checksum = crc32(data);
print(checksum);
```

### adler32(data) -> u32

Calculate Adler-32 checksum of buffer data.

**Parameters:**
- `data: buffer` - Data to checksum

**Returns:** `u32` - Adler-32 checksum

## Tar Archives

### TarWriter() -> object

Create a new tar archive writer.

**Methods:**

#### writer.add_file(name, content, mode?) -> null
Add a file to the archive.
- `name: string` - File path in archive
- `content: string` - File content
- `mode?: i32` - File permissions (default: 0644 = 420)

#### writer.add_directory(name, mode?) -> null
Add a directory to the archive.
- `name: string` - Directory path (trailing / added if missing)
- `mode?: i32` - Directory permissions (default: 0755 = 493)

#### writer.build() -> buffer
Build the tar archive and return as buffer.

#### writer.count() -> i32
Get the number of entries added.

**Example:**
```hemlock
import { TarWriter, TarReader } from "@stdlib/compression";

// Create tar archive
let writer = TarWriter();
writer.add_directory("myproject/");
writer.add_file("myproject/README.md", "# My Project\n\nWelcome!");
writer.add_file("myproject/main.hml", "print(\"Hello!\");");

let tar_data = writer.build();
print("Archive size: " + typeof(tar_data.length));

// Save to file
let f = open("myproject.tar", "w");
f.write_bytes(tar_data);
f.close();
```

### TarReader(data) -> object

Create a tar archive reader from buffer data.

**Parameters:**
- `data: buffer` - Tar archive data

**Methods:**

#### reader.entries() -> array
Get all entries as array of TarEntry objects.

#### reader.count() -> i32
Get the number of entries.

#### reader.get(name) -> object | null
Get entry by exact name, or null if not found.

#### reader.list() -> array
Get array of all entry names.

#### reader.contains(name) -> bool
Check if archive contains entry with given name.

**TarEntry Object:**
```hemlock
{
    name: string,      // File/directory name
    content: string,   // File content (empty for directories)
    size: i64,         // Content size in bytes
    mode: i32,         // File permissions
    mtime: i64,        // Modification time
    type: rune,        // Entry type ('0'=file, '5'=dir, etc.)
}
```

**Example:**
```hemlock
import { TarReader, TAR_TYPE_FILE, TAR_TYPE_DIRECTORY } from "@stdlib/compression";

// Read tar file
let f = open("archive.tar", "r");
let content = f.read();
f.close();

// Convert to buffer
let bytes = content.bytes();
let buf = buffer(bytes.length);
let i = 0;
while (i < bytes.length) {
    buf[i] = bytes[i];
    i = i + 1;
}

// Parse archive
let reader = TarReader(buf);

// List contents
print("Files in archive:");
let names = reader.list();
i = 0;
while (i < names.length) {
    print("  " + names[i]);
    i = i + 1;
}

// Get specific file
let readme = reader.get("myproject/README.md");
if (readme != null) {
    print("README content:");
    print(readme.content);
}

// Iterate all entries
let entries = reader.entries();
i = 0;
while (i < entries.length) {
    let entry = entries[i];
    if (entry.type == TAR_TYPE_FILE) {
        print("File: " + entry.name + " (" + typeof(entry.size) + " bytes)");
    } else if (entry.type == TAR_TYPE_DIRECTORY) {
        print("Dir: " + entry.name);
    }
    i = i + 1;
}
```

## Combined Example: tar.gz Archives

```hemlock
import { gzip, gunzip, TarWriter, TarReader } from "@stdlib/compression";

// Create compressed archive
fn create_tarball(output_path: string, files: array) {
    let writer = TarWriter();

    let i = 0;
    while (i < files.length) {
        let file = files[i];
        writer.add_file(file.name, file.content);
        i = i + 1;
    }

    let tar_data = writer.build();

    // Convert buffer to string for gzip
    let tar_str = "";
    i = 0;
    while (i < tar_data.length) {
        let ch: rune = tar_data[i];
        tar_str = tar_str + ch;
        i = i + 1;
    }

    let compressed = gzip(tar_str, 9);

    let f = open(output_path, "w");
    f.write_bytes(compressed);
    f.close();

    free(tar_data);
    free(compressed);
}

// Extract compressed archive
fn extract_tarball(input_path: string): array {
    let f = open(input_path, "r");
    let content = f.read();
    f.close();

    // Convert to buffer
    let bytes = content.bytes();
    let compressed = buffer(bytes.length);
    let i = 0;
    while (i < bytes.length) {
        compressed[i] = bytes[i];
        i = i + 1;
    }

    // Decompress
    let tar_str = gunzip(compressed);

    // Convert to buffer
    let tar_bytes = tar_str.bytes();
    let tar_data = buffer(tar_bytes.length);
    i = 0;
    while (i < tar_bytes.length) {
        tar_data[i] = tar_bytes[i];
        i = i + 1;
    }

    // Parse tar
    let reader = TarReader(tar_data);
    let entries = reader.entries();

    free(compressed);
    free(tar_data);

    return entries;
}

// Usage
let files = [
    { name: "hello.txt", content: "Hello, World!" },
    { name: "data.json", content: "{\"key\": \"value\"}" },
];

create_tarball("archive.tar.gz", files);

let extracted = extract_tarball("archive.tar.gz");
print("Extracted " + typeof(extracted.length) + " files");
```

## Error Handling

All functions throw exceptions on errors:

```hemlock
import { compress, decompress, gunzip } from "@stdlib/compression";

// Invalid compression level
try {
    compress("data", 15);  // Level must be 0-9
} catch (e) {
    print("Error: " + e);
}

// Corrupted data
try {
    let bad_data = buffer(10);
    decompress(bad_data);
} catch (e) {
    print("Decompression error: " + e);
}

// Invalid gzip data
try {
    let not_gzip = buffer(10);
    not_gzip[0] = 0;  // Wrong magic byte
    gunzip(not_gzip);
} catch (e) {
    print("Gunzip error: " + e);
}
```

## Performance Notes

- **Compression level 6** provides good balance of speed and ratio
- **Level 1** is fastest but larger output
- **Level 9** is slowest but smallest output
- **Tar archives** are not compressed by default - combine with gzip for .tar.gz
- **Memory usage**: Decompression allocates output buffer up to `max_size`

## Limitations

- Maximum decompressed size defaults to 10MB (configurable via `max_size`)
- Tar archives support POSIX ustar format only
- No support for extended tar headers (pax, GNU extensions)
- Symbolic/hard links are parsed but target paths not validated
- File timestamps are not preserved (set to 0)

## See Also

- [zlib manual](https://www.zlib.net/manual.html)
- [POSIX tar format](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/pax.html)
