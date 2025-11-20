# Hemlock Filesystem Module

A standard library module providing comprehensive file and directory operations for Hemlock programs.

## Overview

The fs module provides essential filesystem capabilities:

- **File operations** - Read, write, append, copy, rename, delete files
- **Directory operations** - Create, remove, list directories
- **File information** - Check existence, get file stats, distinguish files/directories
- **Path operations** - Get current directory, change directory, resolve absolute paths

## Usage

```hemlock
import { read_file, write_file, exists } from "@stdlib/fs";

if (exists("config.json")) {
    let content = read_file("config.json");
    print(content);
} else {
    write_file("config.json", "{}");
}
```

Or import all:

```hemlock
import * as fs from "@stdlib/fs";
let files = fs.list_dir(".");
```

---

## File Operations

### exists(path)
Checks if a file or directory exists at the given path.

**Parameters:**
- `path: string` - Path to check

**Returns:** `bool` - True if path exists, false otherwise

```hemlock
import { exists } from "@stdlib/fs";

if (exists("/etc/passwd")) {
    print("File exists");
}

if (!exists("missing.txt")) {
    print("File does not exist");
}
```

### read_file(path)
Reads the entire contents of a file as a string.

**Parameters:**
- `path: string` - Path to the file

**Returns:** `string` - Contents of the file

**Throws:** Exception if file doesn't exist or cannot be read

```hemlock
import { read_file } from "@stdlib/fs";

try {
    let content = read_file("data.txt");
    print("File contents: " + content);
} catch (e) {
    print("Error reading file: " + e);
}
```

**Note:** For line-by-line reading or binary files, use the File object API (`open()`).

### write_file(path, content)
Writes a string to a file, creating it if it doesn't exist, or overwriting if it does.

**Parameters:**
- `path: string` - Path to the file
- `content: string` - Content to write

**Returns:** `null`

**Throws:** Exception if file cannot be written

```hemlock
import { write_file } from "@stdlib/fs";

try {
    write_file("output.txt", "Hello, World!");
    print("File written successfully");
} catch (e) {
    print("Error writing file: " + e);
}
```

**Note:** Creates parent directory if needed (on some systems). Overwrites existing files.

### append_file(path, content)
Appends content to a file, creating it if it doesn't exist.

**Parameters:**
- `path: string` - Path to the file
- `content: string` - Content to append

**Returns:** `null`

**Throws:** Exception if file cannot be written

```hemlock
import { append_file } from "@stdlib/fs";

try {
    append_file("log.txt", "New log entry\n");
    append_file("log.txt", "Another entry\n");
    print("Log entries appended");
} catch (e) {
    print("Error appending to file: " + e);
}
```

### remove_file(path)
Deletes a file.

**Parameters:**
- `path: string` - Path to the file

**Returns:** `null`

**Throws:** Exception if file doesn't exist or cannot be deleted

```hemlock
import { remove_file, exists } from "@stdlib/fs";

try {
    if (exists("temp.txt")) {
        remove_file("temp.txt");
        print("File deleted");
    }
} catch (e) {
    print("Error deleting file: " + e);
}
```

**Warning:** Permanently deletes the file. Cannot be undone.

### rename(old_path, new_path)
Renames or moves a file or directory.

**Parameters:**
- `old_path: string` - Current path
- `new_path: string` - New path

**Returns:** `null`

**Throws:** Exception if operation fails

```hemlock
import { rename } from "@stdlib/fs";

try {
    rename("old_name.txt", "new_name.txt");
    print("File renamed");

    // Can also move files
    rename("file.txt", "/tmp/file.txt");
    print("File moved");
} catch (e) {
    print("Error renaming file: " + e);
}
```

**Note:** Can move files across directories. Overwrites destination if it exists.

### copy_file(src, dest)
Copies a file to a new location.

**Parameters:**
- `src: string` - Source file path
- `dest: string` - Destination file path

**Returns:** `null`

**Throws:** Exception if source doesn't exist or copy fails

```hemlock
import { copy_file } from "@stdlib/fs";

try {
    copy_file("original.txt", "backup.txt");
    print("File copied");
} catch (e) {
    print("Error copying file: " + e);
}
```

**Note:** Overwrites destination if it exists. Preserves file contents but not permissions/metadata.

---

## Directory Operations

### make_dir(path, mode?)
Creates a new directory.

**Parameters:**
- `path: string` - Path for the new directory
- `mode: u32` (optional) - Permissions mode (default: 0755)

**Returns:** `null`

**Throws:** Exception if directory already exists or cannot be created

```hemlock
import { make_dir } from "@stdlib/fs";

try {
    // Create with default permissions (0755)
    make_dir("new_directory");

    // Create with custom permissions (0700 - owner only)
    let mode: u32 = 448;  // 0700 in octal = 448 in decimal
    make_dir("private_dir", mode);

    print("Directories created");
} catch (e) {
    print("Error creating directory: " + e);
}
```

**Note:** Does not create parent directories (non-recursive). Use a loop to create nested directories.

### remove_dir(path)
Removes an empty directory.

**Parameters:**
- `path: string` - Path to the directory

**Returns:** `null`

**Throws:** Exception if directory doesn't exist, is not empty, or cannot be deleted

```hemlock
import { remove_dir } from "@stdlib/fs";

try {
    remove_dir("empty_directory");
    print("Directory removed");
} catch (e) {
    print("Error removing directory: " + e);
}
```

**Warning:** Only removes empty directories. Use recursive deletion logic for non-empty directories.

### list_dir(path)
Lists all entries in a directory (files and subdirectories).

**Parameters:**
- `path: string` - Path to the directory

**Returns:** `array<string>` - Array of entry names (not full paths)

**Throws:** Exception if directory doesn't exist or cannot be read

```hemlock
import { list_dir } from "@stdlib/fs";

try {
    let entries = list_dir(".");
    print("Files in current directory:");

    let i = 0;
    while (i < entries.length) {
        print("  " + entries[i]);
        i = i + 1;
    }
} catch (e) {
    print("Error listing directory: " + e);
}
```

**Note:** Returns entry names only, not full paths. Does not include "." or ".." entries.

---

## File Information

### is_file(path)
Checks if path is a regular file.

**Parameters:**
- `path: string` - Path to check

**Returns:** `bool` - True if path is a file, false otherwise

```hemlock
import { is_file, is_dir } from "@stdlib/fs";

if (is_file("data.txt")) {
    print("Path is a file");
}

if (is_dir("/tmp")) {
    print("Path is a directory");
}
```

### is_dir(path)
Checks if path is a directory.

**Parameters:**
- `path: string` - Path to check

**Returns:** `bool` - True if path is a directory, false otherwise

```hemlock
import { is_dir } from "@stdlib/fs";

if (is_dir(".")) {
    print("Current directory exists");
}
```

### file_stat(path)
Gets detailed information about a file or directory.

**Parameters:**
- `path: string` - Path to examine

**Returns:** `object` - Stat object with file information

**Throws:** Exception if path doesn't exist

**Stat object fields:**
- `size: i64` - File size in bytes
- `atime: i64` - Last access time (Unix timestamp)
- `mtime: i64` - Last modification time (Unix timestamp)
- `ctime: i64` - Creation/status change time (Unix timestamp)
- `mode: u32` - File mode (permissions and type)
- `is_file: bool` - True if regular file
- `is_dir: bool` - True if directory

```hemlock
import { file_stat } from "@stdlib/fs";

try {
    let info = file_stat("data.txt");

    print("Size: " + typeof(info.size) + " bytes");
    print("Modified: " + typeof(info.mtime));
    print("Is file: " + typeof(info.is_file));
    print("Is directory: " + typeof(info.is_dir));
} catch (e) {
    print("Error getting file info: " + e);
}
```

---

## Path Operations

### cwd()
Gets the current working directory.

**Parameters:** None

**Returns:** `string` - Absolute path of current directory

**Throws:** Exception if current directory cannot be determined

```hemlock
import { cwd } from "@stdlib/fs";

try {
    let current = cwd();
    print("Current directory: " + current);
} catch (e) {
    print("Error getting current directory: " + e);
}
```

### chdir(path)
Changes the current working directory.

**Parameters:**
- `path: string` - Path to change to

**Returns:** `null`

**Throws:** Exception if directory doesn't exist or cannot be accessed

```hemlock
import { chdir, cwd } from "@stdlib/fs";

try {
    print("Before: " + cwd());

    chdir("/tmp");
    print("After: " + cwd());

    // Change back
    chdir("..");
} catch (e) {
    print("Error changing directory: " + e);
}
```

**Warning:** Affects current process only. Changes persist for remainder of program execution.

### absolute_path(path)
Resolves a relative path to an absolute path.

**Parameters:**
- `path: string` - Relative or absolute path

**Returns:** `string` - Absolute path with symlinks resolved

**Throws:** Exception if path doesn't exist or cannot be resolved

```hemlock
import { absolute_path } from "@stdlib/fs";

try {
    let abs = absolute_path("../data/file.txt");
    print("Absolute path: " + abs);

    // Resolve current directory
    let here = absolute_path(".");
    print("Current directory (absolute): " + here);
} catch (e) {
    print("Error resolving path: " + e);
}
```

**Note:** Resolves `.` and `..`, follows symlinks, returns canonicalized absolute path.

---

## Examples

### File Backup System

```hemlock
import { exists, copy_file } from "@stdlib/fs";
import { now } from "@stdlib/time";

fn backup_file(filepath: string): bool {
    if (!exists(filepath)) {
        print("File not found: " + filepath);
        return false;
    }

    let timestamp = now();
    let backup_path = filepath + ".backup." + typeof(timestamp);

    try {
        copy_file(filepath, backup_path);
        print("Backup created: " + backup_path);
        return true;
    } catch (e) {
        print("Backup failed: " + e);
        return false;
    }
}

backup_file("important.txt");
```

### Directory Tree Listing

```hemlock
import { list_dir, is_dir } from "@stdlib/fs";

fn list_recursive(path: string, indent: string): null {
    try {
        let entries = list_dir(path);

        let i = 0;
        while (i < entries.length) {
            let name = entries[i];
            let full_path = path + "/" + name;

            print(indent + name);

            if (is_dir(full_path)) {
                list_recursive(full_path, indent + "  ");
            }

            i = i + 1;
        }
    } catch (e) {
        print(indent + "Error: " + e);
    }

    return null;
}

print("Directory tree:");
list_recursive(".", "");
```

### Configuration File Manager

```hemlock
import { exists, read_file, write_file } from "@stdlib/fs";

fn load_config(path: string, defaults: object): object {
    if (!exists(path)) {
        print("Config not found, creating default");
        let json = defaults.serialize();
        write_file(path, json);
        return defaults;
    }

    try {
        let json = read_file(path);
        let config = json.deserialize();
        return config;
    } catch (e) {
        print("Error loading config: " + e);
        return defaults;
    }
}

fn save_config(path: string, config: object): bool {
    try {
        let json = config.serialize();
        write_file(path, json);
        return true;
    } catch (e) {
        print("Error saving config: " + e);
        return false;
    }
}

// Usage
let defaults = { port: 8080, debug: false };
let config = load_config("config.json", defaults);
config.debug = true;
save_config("config.json", config);
```

### Log Rotation

```hemlock
import { file_stat, rename, remove_file, exists } from "@stdlib/fs";

fn rotate_log(log_path: string, max_size: i64): null {
    if (!exists(log_path)) {
        return null;
    }

    try {
        let info = file_stat(log_path);

        if (info.size >= max_size) {
            // Rotate: log.txt -> log.txt.1 -> log.txt.2 -> ...
            let i = 4;
            while (i > 0) {
                let old_name = log_path + "." + typeof(i);
                let new_name = log_path + "." + typeof(i + 1);

                if (exists(old_name)) {
                    if (i == 4) {
                        remove_file(old_name);  // Delete oldest
                    } else {
                        rename(old_name, new_name);
                    }
                }

                i = i - 1;
            }

            rename(log_path, log_path + ".1");
            print("Log rotated");
        }
    } catch (e) {
        print("Error rotating log: " + e);
    }

    return null;
}

// Check before each write
rotate_log("app.log", 10485760);  // 10 MB
```

### Recursive Directory Creation

```hemlock
import { exists, make_dir, is_dir } from "@stdlib/fs";

fn make_dirs(path: string): bool {
    if (exists(path)) {
        return is_dir(path);
    }

    // Find the last slash
    let parts = path.split("/");
    if (parts.length <= 1) {
        try {
            make_dir(path);
            return true;
        } catch (e) {
            return false;
        }
    }

    // Create parent first
    let parent_parts = parts.slice(0, parts.length - 1);
    let parent = parent_parts.join("/");

    if (!make_dirs(parent)) {
        return false;
    }

    // Then create this directory
    try {
        make_dir(path);
        return true;
    } catch (e) {
        return false;
    }
}

// Usage
if (make_dirs("/tmp/app/data/logs")) {
    print("Directory tree created");
}
```

### File Search

```hemlock
import { list_dir, is_dir, is_file } from "@stdlib/fs";

fn find_files(dir: string, pattern: string): array {
    let results = [];

    try {
        let entries = list_dir(dir);

        let i = 0;
        while (i < entries.length) {
            let name = entries[i];
            let full_path = dir + "/" + name;

            if (is_file(full_path) && name.contains(pattern)) {
                results.push(full_path);
            }

            if (is_dir(full_path)) {
                let sub_results = find_files(full_path, pattern);
                let j = 0;
                while (j < sub_results.length) {
                    results.push(sub_results[j]);
                    j = j + 1;
                }
            }

            i = i + 1;
        }
    } catch (e) {
        print("Error searching directory: " + e);
    }

    return results;
}

// Find all .txt files
let txt_files = find_files(".", ".txt");
print("Found " + typeof(txt_files.length) + " text files");
```

### Temporary File Creation

```hemlock
import { write_file, remove_file, exists } from "@stdlib/fs";
import { get_pid } from "@stdlib/env";
import { now } from "@stdlib/time";

fn create_temp_file(prefix: string): string {
    let pid = get_pid();
    let timestamp = now();
    let temp_path = "/tmp/" + prefix + "." + typeof(pid) + "." + typeof(timestamp);

    let counter = 0;
    let final_path = temp_path;

    while (exists(final_path)) {
        counter = counter + 1;
        final_path = temp_path + "." + typeof(counter);
    }

    try {
        write_file(final_path, "");
        return final_path;
    } catch (e) {
        print("Error creating temp file: " + e);
        return "";
    }
}

fn cleanup_temp_file(path: string): null {
    if (exists(path)) {
        try {
            remove_file(path);
        } catch (e) {
            print("Error removing temp file: " + e);
        }
    }
    return null;
}

// Usage
let temp = create_temp_file("myapp");
print("Temp file: " + temp);
// ... use temp file ...
cleanup_temp_file(temp);
```

---

## Error Handling

All fs functions use exception-based error handling:

```hemlock
import { read_file, write_file } from "@stdlib/fs";

try {
    let content = read_file("config.json");
    // Process content...
    write_file("output.json", content);
} catch (e) {
    print("Filesystem error: " + e);
    // e contains descriptive error message with filename
}
```

**Common error scenarios:**
- **File not found:** Reading non-existent file
- **Permission denied:** Insufficient permissions
- **Directory not empty:** Removing non-empty directory
- **Disk full:** Writing when disk is full
- **Invalid path:** Malformed path or illegal characters

---

## Security Considerations

### Path Traversal

**⚠️ WARNING:** Validate user-provided paths to prevent directory traversal attacks:

```hemlock
import { absolute_path } from "@stdlib/fs";

fn is_safe_path(user_path: string, base_dir: string): bool {
    try {
        let abs_base = absolute_path(base_dir);
        let abs_user = absolute_path(base_dir + "/" + user_path);

        // Check if user path is within base directory
        return abs_user.starts_with(abs_base);
    } catch (e) {
        return false;
    }
}

// Validate before using
let user_input = "../../etc/passwd";  // Malicious
if (is_safe_path(user_input, "/var/www/uploads")) {
    // Safe to use
} else {
    print("Invalid path detected");
}
```

### File Permissions

**⚠️ WARNING:** Be careful with file permissions:

```hemlock
import { make_dir } from "@stdlib/fs";

// Create directory with restrictive permissions (owner only)
let mode: u32 = 448;  // 0700 in octal
make_dir("private_data", mode);
```

**Common permission modes:**
- `0755` (493 decimal) - Owner: rwx, Group: rx, Others: rx
- `0700` (448 decimal) - Owner: rwx, Group: none, Others: none
- `0644` (420 decimal) - Owner: rw, Group: r, Others: r

---

## Platform Notes

### Path Separators

- **Unix/Linux:** `/` (forward slash)
- **Windows:** `\` (backslash) or `/` (also supported)

```hemlock
// Cross-platform path construction
let path = "dir" + "/" + "file.txt";  // Works on both Unix and Windows
```

### Case Sensitivity

- **Unix/Linux:** Case-sensitive (`File.txt` ≠ `file.txt`)
- **Windows:** Case-insensitive (`File.txt` == `file.txt`)
- **macOS:** Case-insensitive by default (configurable)

---

## Performance Tips

1. **Batch operations:** Use `list_dir()` once instead of multiple `exists()` calls
2. **Buffer writes:** Accumulate content before calling `write_file()` or `append_file()`
3. **Check existence:** Use `exists()` before reading to avoid exceptions in tight loops
4. **Stat caching:** Cache `file_stat()` results if checking multiple attributes

```hemlock
import { file_stat } from "@stdlib/fs";

// Good: Single stat call
let info = file_stat("large_file.dat");
if (info.size > 1000000 && info.is_file) {
    // Process large file
}

// Bad: Multiple stat calls
if (is_file("large_file.dat")) {
    let info = file_stat("large_file.dat");
    if (info.size > 1000000) {
        // Stat called twice
    }
}
```

---

## Testing

Run the fs module tests:

```bash
# Run all fs tests
make test | grep fs

# Or run individual test
./hemlock tests/fs/test_file_operations.hml
```

---

## See Also

- **File Object API** - Use `open()` for advanced file operations (reading line-by-line, binary I/O)
- **Path module** (planned) - Higher-level path manipulation functions
- **Environment module** (`@stdlib/env`) - Current working directory via `cwd()`

---

## License

Part of the Hemlock standard library.
