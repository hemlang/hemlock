# Bundling & Packaging

Hemlock provides built-in tools to bundle multi-file projects into single distributable files and create self-contained executables.

## Overview

| Command | Output | Use Case |
|---------|--------|----------|
| `--bundle` | `.hmlc` or `.hmlb` | Distribute bytecode (requires Hemlock to run) |
| `--package` | Executable | Standalone binary (no dependencies) |
| `--compile` | `.hmlc` | Compile single file (no import resolution) |

## Bundling

The bundler resolves all `import` statements from an entry point and flattens them into a single file.

### Basic Usage

```bash
# Bundle app.hml and all its imports into app.hmlc
hemlock --bundle app.hml

# Specify output path
hemlock --bundle app.hml -o dist/app.hmlc

# Create compressed bundle (.hmlb) - smaller file size
hemlock --bundle app.hml --compress -o app.hmlb

# Verbose output (shows resolved modules)
hemlock --bundle app.hml --verbose
```

### Output Formats

**`.hmlc` (Uncompressed)**
- Serialized AST format
- Fast to load and execute
- Default output format

**`.hmlb` (Compressed)**
- zlib-compressed `.hmlc`
- Smaller file size (typically 50-70% reduction)
- Slightly slower startup due to decompression

### Running Bundled Files

```bash
# Run uncompressed bundle
hemlock app.hmlc

# Run compressed bundle
hemlock app.hmlb

# Pass arguments
hemlock app.hmlc arg1 arg2
```

### Example: Multi-Module Project

```
myapp/
├── main.hml
├── lib/
│   ├── math.hml
│   └── utils.hml
└── config.hml
```

```hemlock
// main.hml
import { add, multiply } from "./lib/math.hml";
import { log } from "./lib/utils.hml";
import { VERSION } from "./config.hml";

log(`App v${VERSION}`);
print(add(2, 3));
```

```bash
hemlock --bundle myapp/main.hml -o myapp.hmlc
hemlock myapp.hmlc  # Runs with all dependencies bundled
```

### stdlib Imports

The bundler automatically resolves `@stdlib/` imports:

```hemlock
import { HashMap } from "@stdlib/collections";
import { now } from "@stdlib/time";
```

When bundled, stdlib modules are included in the output.

## Packaging

Packaging creates a self-contained executable by embedding the bundled bytecode into a copy of the Hemlock interpreter.

### Basic Usage

```bash
# Create executable from app.hml
hemlock --package app.hml

# Specify output name
hemlock --package app.hml -o myapp

# Skip compression (faster startup, larger file)
hemlock --package app.hml --no-compress

# Verbose output
hemlock --package app.hml --verbose
```

### Running Packaged Executables

```bash
# The packaged executable runs directly
./myapp

# Arguments are passed to the script
./myapp arg1 arg2
```

### Package Format

Packaged executables use the HMLP format:

```
[hemlock binary][HMLB/HMLC payload][payload_size:u64][HMLP magic:u32]
```

When a packaged executable runs:
1. It checks for an embedded payload at the end of the file
2. If found, it decompresses and executes the payload
3. If not found, it behaves as a normal Hemlock interpreter

### Compression Options

| Flag | Format | Startup | Size |
|------|--------|---------|------|
| (default) | HMLB | Normal | Smaller |
| `--no-compress` | HMLC | Faster | Larger |

For CLI tools where startup time matters, use `--no-compress`.

## Inspecting Bundles

Use `--info` to inspect bundled or compiled files:

```bash
hemlock --info app.hmlc
```

Output:
```
=== File Info: app.hmlc ===
Size: 12847 bytes
Format: HMLC (compiled AST)
Version: 1
Flags: 0x0001 [DEBUG]
Strings: 42
Statements: 156
```

```bash
hemlock --info app.hmlb
```

Output:
```
=== File Info: app.hmlb ===
Size: 5234 bytes
Format: HMLB (compressed bundle)
Version: 1
Uncompressed: 12847 bytes
Compressed: 5224 bytes
Ratio: 59.3% reduction
```

## Native Compilation

For true native executables (no interpreter), use the Hemlock compiler:

```bash
# Compile to native executable via C
hemlockc app.hml -o app

# Keep generated C code
hemlockc app.hml -o app --keep-c

# Emit C only (don't compile)
hemlockc app.hml -c -o app.c

# Optimization level
hemlockc app.hml -o app -O2
```

The compiler generates C code and invokes GCC to produce a native binary. This requires:
- The Hemlock runtime library (`libhemlock_runtime`)
- A C compiler (GCC by default)

### Compiler Options

| Option | Description |
|--------|-------------|
| `-o <file>` | Output executable name |
| `-c` | Emit C code only |
| `--emit-c <file>` | Write C to specified file |
| `-k, --keep-c` | Keep generated C after compilation |
| `-O<level>` | Optimization level (0-3) |
| `--cc <path>` | C compiler to use |
| `--runtime <path>` | Path to runtime library |
| `-v, --verbose` | Verbose output |

## Comparison

| Approach | Portability | Startup | Size | Dependencies |
|----------|-------------|---------|------|--------------|
| `.hml` | Source only | Parse time | Smallest | Hemlock |
| `.hmlc` | Hemlock-only | Fast | Small | Hemlock |
| `.hmlb` | Hemlock-only | Fast | Smaller | Hemlock |
| `--package` | Standalone | Fast | Larger | None |
| `hemlockc` | Native | Fastest | Varies | Runtime libs |

## Best Practices

1. **Development**: Run `.hml` files directly for fast iteration
2. **Distribution (with Hemlock)**: Bundle with `--compress` for smaller files
3. **Distribution (standalone)**: Package for zero-dependency deployment
4. **Performance-critical**: Use `hemlockc` for native compilation

## Troubleshooting

### "Cannot find stdlib"

The bundler looks for stdlib in:
1. `./stdlib` (relative to executable)
2. `../stdlib` (relative to executable)
3. `/usr/local/lib/hemlock/stdlib`

Ensure Hemlock is properly installed or run from the source directory.

### Circular Dependencies

```
Error: Circular dependency detected when loading 'path/to/module.hml'
```

Refactor your imports to break the cycle. Consider using a shared module for common types.

### Large Package Size

- Use default compression (don't use `--no-compress`)
- The packaged size includes the full interpreter (~500KB-1MB base)
- For minimal size, use `hemlockc` for native compilation
