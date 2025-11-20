# Hemlock Standard Library

The Hemlock standard library provides essential modules and data structures for Hemlock programs.

## Available Modules

### Collections (`@stdlib/collections`) ⭐
**Status:** Production-ready, fully optimized

Provides high-performance data structures:
- **HashMap** - O(1) hash table with djb2 algorithm
- **Queue** - O(1) FIFO with circular buffer
- **Stack** - O(1) LIFO
- **Set** - O(1) unique values (HashMap-based)
- **LinkedList** - Bidirectional doubly-linked list

See [docs/collections.md](docs/collections.md) for detailed documentation.

### Math (`@stdlib/math`)
**Status:** Complete

Mathematical functions and constants:
- 21 functions: trigonometry, exponential/log, rounding, utility
- 5 constants: PI, E, TAU, INF, NAN
- Random number generation with seeding

See [docs/math.md](docs/math.md) for detailed documentation.

### Time (`@stdlib/time`)
**Status:** Basic

Time measurement and delays:
- `now()` - Unix timestamp (i64)
- `time_ms()` - Milliseconds since epoch (i64)
- `clock()` - CPU time (f64)
- `sleep(seconds)` - Sub-second precision delays

See [docs/time.md](docs/time.md) for detailed documentation.

### Environment (`@stdlib/env`)
**Status:** Complete

Environment variables and process control:
- `getenv()`, `setenv()`, `unsetenv()` - Environment variable management
- `exit()` - Process termination with exit codes
- `get_pid()` - Process ID

See [docs/env.md](docs/env.md) for detailed documentation.

### Filesystem (`@stdlib/fs`)
**Status:** Comprehensive

File and directory operations:
- **File ops:** read_file, write_file, append_file, remove_file, rename, copy_file, exists
- **Directory ops:** make_dir, remove_dir, list_dir
- **File info:** is_file, is_dir, file_stat
- **Path ops:** cwd, chdir, absolute_path

See [docs/fs.md](docs/fs.md) for detailed documentation.

## Usage

Import modules using the `@stdlib/` prefix:

```hemlock
// Import specific functions
import { HashMap, Queue, Stack } from "@stdlib/collections";
import { sin, cos, PI } from "@stdlib/math";
import { now, sleep } from "@stdlib/time";
import { getenv, exit } from "@stdlib/env";
import { read_file, write_file, exists } from "@stdlib/fs";

// Import all as namespace
import * as math from "@stdlib/math";
import * as fs from "@stdlib/fs";

// Use imported functions
let angle = math.PI / 4.0;
let result = math.sin(angle);
let content = fs.read_file("data.txt");
```

## Quick Examples

### Collections
```hemlock
import { HashMap } from "@stdlib/collections";

let map = HashMap();
map.set("name", "Alice");
map.set("age", 30);
print(map.get("name"));  // "Alice"
```

### Math
```hemlock
import { sqrt, pow } from "@stdlib/math";

let distance = sqrt(pow(3.0, 2.0) + pow(4.0, 2.0));
print(distance);  // 5.0
```

### Time
```hemlock
import { time_ms, sleep } from "@stdlib/time";

let start = time_ms();
sleep(1.0);
let elapsed = time_ms() - start;
print("Elapsed: " + typeof(elapsed) + "ms");
```

### Environment
```hemlock
import { getenv } from "@stdlib/env";

let home = getenv("HOME");
if (home != null) {
    print("Home directory: " + home);
}
```

### Filesystem
```hemlock
import { read_file, write_file, exists } from "@stdlib/fs";

if (exists("config.json")) {
    let config = read_file("config.json");
    print("Config loaded");
} else {
    write_file("config.json", "{}");
}
```

## Directory Structure

```
stdlib/
├── README.md           # This file
├── collections.hml     # Collections module implementation
├── math.hml            # Math module implementation
├── time.hml            # Time module implementation
├── env.hml             # Environment module implementation
├── fs.hml              # Filesystem module implementation
└── docs/
    ├── collections.md  # Collections API reference
    ├── math.md         # Math API reference
    ├── time.md         # Time API reference
    ├── env.md          # Environment API reference
    └── fs.md           # Filesystem API reference
```

## JSON Serialization

Hemlock has built-in JSON support (no module import needed):

```hemlock
// Object to JSON
let data = { name: "Alice", age: 30 };
let json = data.serialize();
print(json);  // {"name":"Alice","age":30}

// JSON to object
let parsed = json.deserialize();
print(parsed.name);  // "Alice"
```

## Future Modules

Planned additions to the standard library:
- **strings** - String utilities (pad, join, is_alpha, reverse, lines, words)
- **path** - Path manipulation (join, basename, dirname, extname, normalize)
- **json** - Formalized JSON module (wrapper around serialize/deserialize)
- **encoding** - Base64, hex, URL encoding/decoding
- **testing** - Test framework with describe/test/expect/assertions
- **datetime** - Date/time formatting and parsing
- **http** - HTTP client (via FFI + libcurl)
- **regex** - Regular expressions (via FFI + PCRE)
- **crypto** - Cryptographic functions (via FFI + OpenSSL)
- **compression** - zlib/gzip compression (via FFI)

See `STDLIB_ANALYSIS_UPDATED.md` for detailed roadmap and implementation plan.

## Module Status

| Module | Status | Docs | Tests | Lines | Quality |
|--------|--------|------|-------|-------|---------|
| collections | ✅ Production | ✅ Complete | ✅ Comprehensive | 760 | Excellent |
| math | ✅ Complete | ✅ Complete | ✅ Good | 50 | High |
| time | ⚠️ Basic | ✅ Complete | ✅ Good | 13 | Good |
| env | ✅ Complete | ✅ Complete | ✅ Good | 14 | High |
| fs | ✅ Comprehensive | ✅ Complete | ⚠️ Partial | 31 | High |

**Legend:**
- ✅ Complete/Excellent
- ⚠️ Basic/Partial
- ❌ Missing

## Contributing

When adding new stdlib modules:
1. Create the `.hml` file in `stdlib/`
2. Implement the module with comprehensive error handling
3. Add comprehensive tests in `tests/stdlib_<module>/`
4. Document the module in `stdlib/docs/<module>.md` (use existing docs as template)
5. Update this README with the new module
6. Update `CLAUDE.md` stdlib section

## Testing

All stdlib modules have comprehensive test coverage:

```bash
# Run all tests
make test

# Run specific module tests
make test | grep stdlib_collections
make test | grep stdlib_math
make test | grep stdlib_time
make test | grep stdlib_env

# Or run individual test files
./hemlock tests/stdlib_collections/test_hashmap.hml
./hemlock tests/stdlib_math/test_math_constants.hml
./hemlock tests/stdlib_time/test_time.hml
```

**Current test statistics:**
- Total Hemlock tests: 372 (all passing)
- Collections tests: Comprehensive (100+ items, edge cases, performance)
- Math tests: Constants and function coverage
- Time tests: Basic functionality
- Environment tests: Variable management
- Filesystem tests: File operations (needs expansion)

## Performance

### Collections Module Performance

Recent optimizations (PR #49) dramatically improved performance:

**HashMap:**
- djb2 hash algorithm (much better distribution than simple sum)
- Native modulo operator (O(1) vs O(n) pure-Hemlock implementation)
- Efficient type casting for float-to-int conversions
- Load factor: 0.75 with automatic resizing

**Queue:**
- Circular buffer implementation (O(1) enqueue/dequeue, was O(n))
- Automatic capacity doubling when full
- Zero data movement during enqueue/dequeue

**Set:**
- Uses HashMap internally (O(1) operations, was O(n) linear search)
- Dramatically faster add/delete/has operations

**LinkedList:**
- Bidirectional traversal (chooses head vs tail based on proximity)
- O(n/2) average access instead of O(n)

## Documentation

### Module Documentation

Each module has comprehensive documentation in `stdlib/docs/`:
- **Full API reference** - All functions, parameters, return types
- **Usage examples** - Practical code examples for each function
- **Error handling** - Exception behavior and patterns
- **Performance notes** - Time/space complexity where relevant
- **Platform notes** - Cross-platform considerations
- **Security considerations** - Best practices for safe usage

### CLAUDE.md Integration

The main language documentation (`CLAUDE.md`) now includes a complete Standard Library section with:
- Import syntax examples
- Overview of all modules
- Quick reference for common operations
- Links to detailed module documentation

## Design Philosophy

Hemlock's stdlib follows the language's core principles:

1. **Explicit over implicit** - All operations require explicit imports
2. **No magic** - Functions do exactly what they say
3. **Manual memory management** - User controls lifecycle
4. **Performance conscious** - O(1) operations where possible
5. **Error transparency** - Exceptions for errors, no silent failures
6. **Systems focus** - Essential tools for systems scripting

## License

Part of the Hemlock programming language.
