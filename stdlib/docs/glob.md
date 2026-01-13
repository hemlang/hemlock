# @stdlib/glob - Glob Pattern Matching

The `glob` module provides functions for matching file paths against glob patterns and finding files in the filesystem.

## Quick Start

```hemlock
import { glob, glob_match, match_path } from "@stdlib/glob";

// Find all .hml files recursively
let files = glob("**/*.hml", "src");

// Match a string against a pattern
print(glob_match("*.txt", "file.txt"));   // true
print(glob_match("*.txt", "file.md"));    // false

// Match paths with ** support
print(match_path("src/**/*.hml", "src/utils/helper.hml"));  // true
```

## Pattern Syntax

| Pattern | Meaning |
|---------|---------|
| `*` | Matches any characters except `/` |
| `?` | Matches exactly one character except `/` |
| `[abc]` | Matches any character in the set |
| `[a-z]` | Matches any character in the range |
| `[!abc]` | Matches any character NOT in the set |
| `**` | Matches zero or more directories |

## API Reference

### glob_match(pattern, text): bool

Match a string against a glob pattern.

**Parameters:**
- `pattern: string` - Glob pattern (supports `*`, `?`, `[abc]`, `[!abc]`)
- `text: string` - String to match

**Returns:** `bool` - True if text matches pattern

```hemlock
import { glob_match } from "@stdlib/glob";

print(glob_match("*.txt", "file.txt"));    // true
print(glob_match("*.txt", "file.md"));     // false
print(glob_match("file.*", "file.tar.gz")); // true
print(glob_match("f?le.txt", "file.txt")); // true
print(glob_match("f?le.txt", "fle.txt"));  // false
print(glob_match("[abc].txt", "a.txt"));   // true
print(glob_match("[abc].txt", "d.txt"));   // false
print(glob_match("[!abc].txt", "d.txt"));  // true
print(glob_match("[a-z].txt", "m.txt"));   // true
```

### match_path(pattern, path): bool

Match a path against a glob pattern with `**` support.

**Parameters:**
- `pattern: string` - Glob pattern (supports `**`)
- `path: string` - Path to match

**Returns:** `bool` - True if path matches pattern

```hemlock
import { match_path } from "@stdlib/glob";

// ** matches zero or more directories
print(match_path("**/*.txt", "file.txt"));          // true
print(match_path("**/*.txt", "src/file.txt"));      // true
print(match_path("**/*.txt", "a/b/c/file.txt"));    // true

// ** at the end
print(match_path("src/**", "src/foo"));             // true
print(match_path("src/**", "src/a/b/c"));           // true

// ** in the middle
print(match_path("src/**/test.txt", "src/test.txt"));         // true
print(match_path("src/**/test.txt", "src/a/b/test.txt"));     // true

// Without **
print(match_path("src/*.txt", "src/file.txt"));     // true
print(match_path("src/*.txt", "src/a/file.txt"));   // false
```

### glob(pattern, base_dir?): array

Find files matching a glob pattern in the filesystem.

**Parameters:**
- `pattern: string` - Glob pattern
- `base_dir: string` - Base directory (default: `"."`)

**Returns:** `array<string>` - Matching file paths

```hemlock
import { glob } from "@stdlib/glob";

// Find all .hml files in current directory
let files = glob("*.hml");

// Find all .hml files recursively in src
let all_src = glob("**/*.hml", "src");

// Find test files
let tests = glob("test_*.hml", "tests");

// Print results
let i = 0;
while (i < files.length) {
    print(files[i]);
    i = i + 1;
}
```

### escape(text): string

Escape special glob characters in a string for literal matching.

**Parameters:**
- `text: string` - String to escape

**Returns:** `string` - Escaped string

```hemlock
import { escape, match } from "@stdlib/glob";

let filename = "file[1].txt";
let pattern = escape(filename);
print(pattern);                        // "file[[]1].txt"
print(glob_match(pattern, filename));       // true
```

### has_magic(pattern): bool

Check if a pattern contains any glob special characters.

**Parameters:**
- `pattern: string` - Pattern to check

**Returns:** `bool` - True if pattern has glob characters

```hemlock
import { has_magic } from "@stdlib/glob";

print(has_magic("*.txt"));     // true
print(has_magic("file.txt"));  // false
print(has_magic("file?.txt")); // true
print(has_magic("[abc]"));     // true
```

### filter(paths, pattern): array

Filter a list of paths by a glob pattern.

**Parameters:**
- `paths: array<string>` - Paths to filter
- `pattern: string` - Glob pattern

**Returns:** `array<string>` - Matching paths

```hemlock
import { filter } from "@stdlib/glob";

let paths = [
    "src/main.hml",
    "src/util.hml",
    "tests/test.hml",
    "README.md"
];

let hml_files = filter(paths, "**/*.hml");
// ["src/main.hml", "src/util.hml", "tests/test.hml"]

let src_files = filter(paths, "src/*.hml");
// ["src/main.hml", "src/util.hml"]
```

### translate(pattern): string

Translate a glob pattern to a regular expression string.

**Parameters:**
- `pattern: string` - Glob pattern

**Returns:** `string` - Regex pattern

```hemlock
import { translate } from "@stdlib/glob";

print(translate("*.txt"));      // "^[^/]*\.txt$"
print(translate("file?.txt"));  // "^file[^/]\.txt$"
print(translate("**/*.txt"));   // "^.*/[^/]*\.txt$"
```

## Examples

### Finding source files

```hemlock
import { glob } from "@stdlib/glob";

// Find all Hemlock source files
let sources = glob("**/*.hml", "src");

let i = 0;
while (i < sources.length) {
    print("Source: " + sources[i]);
    i = i + 1;
}
```

### Matching file extensions

```hemlock
import { glob_match } from "@stdlib/glob";

fn get_file_type(filename) {
    if (glob_match("*.hml", filename)) {
        return "hemlock";
    }
    if (glob_match("*.{js,ts}", filename)) {
        return "javascript";
    }
    if (glob_match("*.py", filename)) {
        return "python";
    }
    return "unknown";
}
```

### Finding test files

```hemlock
import { glob, match_path } from "@stdlib/glob";

// Find all test files
let test_files = glob("**/test_*.hml");

// Or filter by convention
let all_files = glob("**/*.hml");
let tests: array = [];
let i = 0;
while (i < all_files.length) {
    if (match_path("**/test_*.hml", all_files[i]) ||
        match_path("**/tests/**/*.hml", all_files[i])) {
        tests.push(all_files[i]);
    }
    i = i + 1;
}
```

### Safe pattern from user input

```hemlock
import { glob, escape, has_magic } from "@stdlib/glob";

fn find_file(user_input) {
    // If user input contains glob chars, escape them for literal match
    let pattern = user_input;
    if (has_magic(user_input)) {
        pattern = escape(user_input);
    }

    return glob(pattern);
}
```

## See Also

- [@stdlib/fs](fs.md) - File system operations
- [@stdlib/path](path.md) - Path manipulation
