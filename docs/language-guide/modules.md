# Hemlock Module System

This document describes the ES6-style import/export module system implemented for Hemlock.

## Overview

Hemlock supports a file-based module system with ES6-style import/export syntax. Modules are:
- **Singletons**: Each module is loaded once and cached
- **File-based**: Modules correspond to .hml files on disk
- **Explicitly imported**: Dependencies are declared with import statements
- **Topologically executed**: Dependencies are executed before dependents

For package management and third-party dependencies, see [hpm (Hemlock Package Manager)](https://github.com/hemlang/hpm).

## Syntax

### Export Statements

**Inline Named Exports:**
```hemlock
export fn add(a, b) {
    return a + b;
}

export const PI = 3.14159;
export let counter = 0;
```

**Export List:**
```hemlock
fn add(a, b) { return a + b; }
fn subtract(a, b) { return a - b; }

export { add, subtract };
```

**Export Extern (FFI Functions):**
```hemlock
import "libc.so.6";

// Export FFI functions for use in other modules
export extern fn strlen(s: string): i32;
export extern fn getpid(): i32;
```

See [FFI Documentation](../advanced/ffi.md#exporting-ffi-functions) for more details on exporting FFI functions.

**Export Define (Struct Types):**
```hemlock
// Export struct type definitions
export define Vector2 {
    x: f32,
    y: f32,
}

export define Rectangle {
    x: f32,
    y: f32,
    width: f32,
    height: f32,
}
```

**Important:** Exported struct types are registered globally when the module is loaded. They become available automatically when you import anything from the module - you do NOT need to (and cannot) explicitly import them by name:

```hemlock
// GOOD - struct types are auto-available after any import
import { some_function } from "./my_module.hml";
let v: Vector2 = { x: 1.0, y: 2.0 };  // Works!

// BAD - cannot explicitly import struct types
import { Vector2 } from "./my_module.hml";  // Error: Undefined variable 'Vector2'
```

See [FFI Documentation](../advanced/ffi.md#exporting-struct-types) for more details on exporting struct types.

**Re-exports:**
```hemlock
// Re-export from another module
export { add, subtract } from "./math.hml";
```

### Import Statements

**Named Imports:**
```hemlock
import { add, subtract } from "./math.hml";
print(add(1, 2));  // 3
```

**Namespace Import:**
```hemlock
import * as math from "./math.hml";
print(math.add(1, 2));  // 3
print(math.PI);  // 3.14159
```

**Aliasing:**
```hemlock
import { add as sum, subtract as diff } from "./math.hml";
print(sum(1, 2));  // 3
```

## Module Resolution

### Path Types

**Relative Paths:**
```hemlock
import { foo } from "./module.hml";       // Same directory
import { bar } from "../parent.hml";      // Parent directory
import { baz } from "./sub/nested.hml";   // Subdirectory
```

**Absolute Paths:**
```hemlock
import { foo } from "/absolute/path/to/module.hml";
```

**Extension Handling:**
- `.hml` extension can be omitted - it will be added automatically
- `./math` resolves to `./math.hml`

## Features

### Circular Dependency Detection

The module system detects circular dependencies and reports an error:

```
Error: Circular dependency detected when loading '/path/to/a.hml'
```

### Module Caching

Modules are loaded once and cached. Multiple imports of the same module return the same instance:

```hemlock
// counter.hml
export let count = 0;
export fn increment() {
    count = count + 1;
}

// a.hml
import { count, increment } from "./counter.hml";
increment();
print(count);  // 1

// b.hml
import { count } from "./counter.hml";  // Same instance!
print(count);  // Still 1 (shared state)
```

### Import Immutability

Imported bindings cannot be reassigned:

```hemlock
import { add } from "./math.hml";
add = fn() { };  // ERROR: cannot reassign imported binding
```

## Implementation Details

### Architecture

**Files:**
- `include/module.h` - Module system API
- `src/module.c` - Module loading, caching, and execution
- Parser support in `src/parser.c`
- Runtime support in `src/interpreter/runtime.c`

**Key Components:**
1. **ModuleCache**: Maintains loaded modules indexed by absolute path
2. **Module**: Represents a loaded module with its AST and exports
3. **Path Resolution**: Resolves relative/absolute paths to canonical paths
4. **Topological Execution**: Executes modules in dependency order

### Module Loading Process

1. **Parse Phase**: Tokenize and parse the module file
2. **Dependency Resolution**: Recursively load imported modules
3. **Cycle Detection**: Check if module is already being loaded
4. **Caching**: Store module in cache by absolute path
5. **Execution Phase**: Execute in topological order (dependencies first)

### API

```c
// High-level API
int execute_file_with_modules(const char *file_path,
                               int argc, char **argv,
                               ExecutionContext *ctx);

// Low-level API
ModuleCache* module_cache_new(const char *initial_dir);
void module_cache_free(ModuleCache *cache);
Module* load_module(ModuleCache *cache, const char *module_path, ExecutionContext *ctx);
void execute_module(Module *module, ModuleCache *cache, ExecutionContext *ctx);
```

## Testing

Test modules are located in `tests/modules/` and `tests/parity/modules/`:

- `math.hml` - Basic module with exports
- `test_import_named.hml` - Named import test
- `test_import_namespace.hml` - Namespace import test
- `test_import_alias.hml` - Import aliasing test
- `export_extern.hml` - Export extern FFI function test (Linux)

## Package Imports (hpm)

With [hpm](https://github.com/hemlang/hpm) installed, you can import third-party packages from GitHub:

```hemlock
// Import from package root (uses "main" from package.json)
import { app, router } from "hemlang/sprout";

// Import from subpath
import { middleware } from "hemlang/sprout/middleware";

// Standard library (built into Hemlock)
import { HashMap } from "@stdlib/collections";
```

Packages are installed to `hem_modules/` and resolved using GitHub `owner/repo` syntax.

```bash
# Install a package
hpm install hemlang/sprout

# Install with version constraint
hpm install hemlang/sprout@^1.0.0
```

See the [hpm documentation](https://github.com/hemlang/hpm) for full details.

## Current Limitations

1. **No Dynamic Imports**: `import()` as a runtime function is not supported
2. **No Conditional Exports**: Exports must be at top level
3. **Static Library Paths**: FFI library imports use static paths (platform-specific)

## Future Work

- Dynamic imports with `import()` function
- Conditional exports
- Module metadata (`import.meta`)
- Tree shaking and dead code elimination

## Examples

See `tests/modules/` for working examples of the module system.

Example module structure:
```
project/
├── main.hml
├── lib/
│   ├── math.hml
│   ├── string.hml
│   └── index.hml (barrel module)
└── utils/
    └── helpers.hml
```

Example usage:
```hemlock
// lib/math.hml
export fn add(a, b) { return a + b; }
export fn multiply(a, b) { return a * b; }

// lib/index.hml (barrel)
export { add, multiply } from "./math.hml";

// main.hml
import { add } from "./lib/index.hml";
print(add(2, 3));  // 5
```
