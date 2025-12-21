# @stdlib/path - Path Manipulation

The `path` module provides cross-platform utilities for working with file and directory paths.

## Quick Start

```hemlock
import { join, dirname, basename, extname } from "@stdlib/path";

let full_path = join("/home", "user", "file.txt");
print(full_path);  // /home/user/file.txt

print(dirname(full_path));   // /home/user
print(basename(full_path));  // file.txt
print(extname(full_path));   // .txt
```

## Constants

### SEP

The path separator for the current platform (`/` on Unix).

```hemlock
import { SEP } from "@stdlib/path";
print(SEP);  // /
```

### DELIMITER

The path delimiter for environment variables like PATH (`:` on Unix).

```hemlock
import { DELIMITER } from "@stdlib/path";
print(DELIMITER);  // :
```

## API Reference

### join(a, b): string

Join two path segments with the platform separator.

**Parameters:**
- `a: string` - First path segment
- `b: string` - Second path segment

**Returns:** `string` - Joined and normalized path

```hemlock
import { join } from "@stdlib/path";

print(join("foo", "bar"));       // foo/bar
print(join("/home", "user"));    // /home/user
print(join("a/", "/b"));         // a/b
```

### join_all(parts): string

Join multiple path segments with the platform separator.

**Parameters:**
- `parts: array<string>` - Path segments to join

**Returns:** `string` - Joined and normalized path

```hemlock
import { join_all } from "@stdlib/path";

print(join_all(["foo", "bar", "baz"]));       // foo/bar/baz
print(join_all(["/home", "user", "docs"]));   // /home/user/docs
print(join_all(["a/", "/b/", "/c"]));         // a/b/c
print(join_all([]));                           // .
```

### dirname(path): string

Get the directory name of a path.

**Parameters:**
- `path: string` - Input path

**Returns:** `string` - Directory portion of the path

```hemlock
import { dirname } from "@stdlib/path";

print(dirname("/home/user/file.txt"));  // /home/user
print(dirname("/home/user/"));          // /home
print(dirname("file.txt"));             // .
print(dirname("/"));                     // /
```

### basename(path, suffix?): string

Get the base name (final component) of a path.

**Parameters:**
- `path: string` - Input path
- `suffix: string` - Optional suffix to remove (default: "")

**Returns:** `string` - Base name of the path

```hemlock
import { basename } from "@stdlib/path";

print(basename("/home/user/file.txt"));           // file.txt
print(basename("/home/user/file.txt", ".txt"));   // file
print(basename("/home/user/"));                   // user
print(basename("file.txt"));                      // file.txt
```

### extname(path): string

Get the extension of a path (including the dot).

**Parameters:**
- `path: string` - Input path

**Returns:** `string` - Extension including dot, or empty string

```hemlock
import { extname } from "@stdlib/path";

print(extname("file.txt"));       // .txt
print(extname("file.tar.gz"));    // .gz
print(extname(".hidden"));        // (empty - hidden file)
print(extname("file"));           // (empty - no extension)
print(extname("file."));          // (empty - trailing dot)
```

### normalize(path): string

Normalize a path, resolving `.` and `..` segments.

**Parameters:**
- `path: string` - Input path

**Returns:** `string` - Normalized path

```hemlock
import { normalize } from "@stdlib/path";

print(normalize("/home/user/../admin/./docs"));  // /home/admin/docs
print(normalize("./foo/bar/../baz"));            // foo/baz
print(normalize("///foo//bar//"));               // /foo/bar/
print(normalize(""));                             // .
```

### is_absolute(path): bool

Check if a path is absolute.

**Parameters:**
- `path: string` - Input path

**Returns:** `bool` - True if path is absolute

```hemlock
import { is_absolute } from "@stdlib/path";

print(is_absolute("/home/user"));   // true
print(is_absolute("./relative"));   // false
print(is_absolute("file.txt"));     // false
```

### resolve(path): string

Resolve a path to an absolute path (relative to current working directory).

**Parameters:**
- `path: string` - Path to resolve

**Returns:** `string` - Resolved absolute path

```hemlock
import { resolve } from "@stdlib/path";

print(resolve("foo/bar"));     // /current/working/dir/foo/bar
print(resolve("/home/user"));  // /home/user (already absolute)
```

### resolve_all(paths): string

Resolve a sequence of paths to an absolute path. Processes from right to left, stopping when an absolute path is found.

**Parameters:**
- `paths: array<string>` - Path segments (last absolute path wins)

**Returns:** `string` - Resolved absolute path

```hemlock
import { resolve_all } from "@stdlib/path";

print(resolve_all(["foo", "bar"]));            // /current/working/dir/foo/bar
print(resolve_all(["/home", "user", "docs"])); // /home/user/docs
print(resolve_all(["foo", "/bar", "baz"]));    // /bar/baz
```

### relative(source, target): string

Get the relative path from one path to another.

**Parameters:**
- `source: string` - Source path
- `target: string` - Target path

**Returns:** `string` - Relative path from source to target

```hemlock
import { relative } from "@stdlib/path";

print(relative("/home/user", "/home/user/docs"));      // docs
print(relative("/home/user/docs", "/home/user"));      // ..
print(relative("/home/user", "/var/log"));             // ../../var/log
print(relative("/home/user", "/home/user"));           // (empty)
```

### parse(path): object

Parse a path into its components.

**Parameters:**
- `path: string` - Input path

**Returns:** `object` - `{ root, dir, base, ext, name }`

```hemlock
import { parse } from "@stdlib/path";

let p = parse("/home/user/file.txt");
print(p.root);  // /
print(p.dir);   // /home/user
print(p.base);  // file.txt
print(p.ext);   // .txt
print(p.name);  // file
```

### format(pathObject): string

Format a path object into a path string.

**Parameters:**
- `pathObject: object` - `{ root?, dir?, base?, ext?, name? }`

**Returns:** `string` - Formatted path

```hemlock
import { format } from "@stdlib/path";

let path = format({
    dir: "/home/user",
    base: "file.txt"
});
print(path);  // /home/user/file.txt

// name + ext (base takes precedence if both provided)
let path2 = format({
    root: "/",
    name: "file",
    ext: ".txt"
});
print(path2);  // /file.txt
```

### has_trailing_sep(path): bool

Check if path has a trailing separator.

```hemlock
import { has_trailing_sep } from "@stdlib/path";

print(has_trailing_sep("/home/user/"));   // true
print(has_trailing_sep("/home/user"));    // false
```

### ensure_trailing_sep(path): string

Ensure path has a trailing separator.

```hemlock
import { ensure_trailing_sep } from "@stdlib/path";

print(ensure_trailing_sep("/home/user"));   // /home/user/
print(ensure_trailing_sep("/home/user/"));  // /home/user/
```

### remove_trailing_sep(path): string

Remove trailing separator from path.

```hemlock
import { remove_trailing_sep } from "@stdlib/path";

print(remove_trailing_sep("/home/user/"));   // /home/user
print(remove_trailing_sep("/home/user"));    // /home/user
print(remove_trailing_sep("/"));             // /
```

### matches(path, pattern): bool

Check if a path matches a simple glob-like pattern.

**Supported wildcards:**
- `*` - Matches any number of characters
- `?` - Matches exactly one character

**Parameters:**
- `path: string` - Path to match
- `pattern: string` - Pattern to match against

**Returns:** `bool` - True if path matches pattern

```hemlock
import { matches } from "@stdlib/path";

print(matches("file.txt", "*.txt"));        // true
print(matches("file.txt", "file.*"));       // true
print(matches("file.txt", "f??e.txt"));     // true
print(matches("data.json", "*.txt"));       // false
print(matches("src/main.c", "src/*.c"));    // true
```

## Examples

### Building file paths

```hemlock
import { join, dirname, basename, extname } from "@stdlib/path";

let project_dir = "/home/user/project";
let src_dir = join(project_dir, "src");
let main_file = join(src_dir, "main.hml");

print(main_file);  // /home/user/project/src/main.hml

// Get components
print(dirname(main_file));   // /home/user/project/src
print(basename(main_file));  // main.hml
print(extname(main_file));   // .hml
```

### Changing file extensions

```hemlock
import { dirname, basename, join } from "@stdlib/path";

fn change_ext(path, new_ext) {
    let dir = dirname(path);
    let base = basename(path);

    // Find and remove old extension
    let dot_idx = base.find(".");
    if (dot_idx >= 0) {
        base = base.slice(0, dot_idx);
    }

    return join(dir, base + new_ext);
}

print(change_ext("/src/file.ts", ".js"));  // /src/file.js
```

### Resolving relative imports

```hemlock
import { dirname, resolve } from "@stdlib/path";

let current_file = "/home/user/project/src/utils/helper.hml";
let relative_import = "../models/user.hml";

let resolved = resolve(dirname(current_file), relative_import);
print(resolved);  // /home/user/project/src/models/user.hml
```

### Finding matching files

```hemlock
import { matches } from "@stdlib/path";
import { list_dir } from "@stdlib/fs";

let files = list_dir(".");
let i = 0;
while (i < files.length) {
    if (matches(files[i], "*.hml")) {
        print("Hemlock file: " + files[i]);
    }
    i = i + 1;
}
```

## Error Handling

All functions throw descriptive errors for invalid input:

```hemlock
import { join } from "@stdlib/path";

try {
    join(123, "path");  // Not a string
} catch (e) {
    print(e);  // join() requires string arguments
}
```

## See Also

- [@stdlib/fs](fs.md) - File system operations
- [@stdlib/glob](glob.md) - Advanced glob pattern matching
