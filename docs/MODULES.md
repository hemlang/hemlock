# Hemlock Module System (Phase 1)

This document describes the ES6-style import/export module system implemented for Hemlock.

## Overview

Hemlock now supports a file-based module system with ES6-style import/export syntax. Modules are:
- **Singletons**: Each module is loaded once and cached
- **File-based**: Modules correspond to .hml files on disk
- **Explicitly imported**: Dependencies are declared with import statements
- **Topologically executed**: Dependencies are executed before dependents

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

Test modules are located in `tests/modules/`:

- `math.hml` - Basic module with exports
- `test_import_named.hml` - Named import test
- `test_import_namespace.hml` - Namespace import test
- `test_import_alias.hml` - Import aliasing test

## Current Limitations

1. **Main Integration**: The module system is implemented but not yet integrated with main.c by default. Files must explicitly use the module API.
2. **No Package Management**: Only file-based modules are supported (no `hemlock_modules/` or package.json)
3. **No Standard Library**: No `std/` prefix for built-in modules yet
4. **No Dynamic Imports**: `import()` as a runtime function is not supported
5. **No Conditional Exports**: Exports must be at top level

## Future Work (Phase 2+)

- Package resolution from `hemlock_modules/`
- Standard library with `std/` prefix
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
