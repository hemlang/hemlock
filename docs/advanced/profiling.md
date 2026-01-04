# Profiling

Hemlock includes a built-in profiler for **CPU time analysis**, **memory tracking**, and **leak detection**. The profiler helps identify performance bottlenecks and memory issues in your programs.

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Profiling Modes](#profiling-modes)
- [Output Formats](#output-formats)
- [Leak Detection](#leak-detection)
- [Understanding Reports](#understanding-reports)
- [Flamegraph Generation](#flamegraph-generation)
- [Best Practices](#best-practices)

---

## Overview

The profiler is accessed via the `profile` subcommand:

```bash
hemlock profile [OPTIONS] <FILE>
```

**Key features:**
- **CPU profiling** - Measure time spent in each function (self-time and total-time)
- **Memory profiling** - Track all allocations with source locations
- **Leak detection** - Identify memory that was never freed
- **Multiple output formats** - Text, JSON, and flamegraph-compatible output
- **Per-function memory stats** - See which functions allocate the most memory

---

## Quick Start

### Profile CPU time (default)

```bash
hemlock profile script.hml
```

### Profile memory allocations

```bash
hemlock profile --memory script.hml
```

### Detect memory leaks

```bash
hemlock profile --leaks script.hml
```

### Generate flamegraph data

```bash
hemlock profile --flamegraph script.hml > profile.folded
flamegraph.pl profile.folded > profile.svg
```

---

## Profiling Modes

### CPU Profiling (default)

Measures time spent in each function, distinguishing between:
- **Self time** - Time spent executing the function's own code
- **Total time** - Self time plus time spent in called functions

```bash
hemlock profile script.hml
hemlock profile --cpu script.hml  # Explicit
```

**Example output:**
```
=== Hemlock Profiler Report ===

Total time: 1.234ms
Functions called: 5 unique

--- Top 5 by Self Time ---

Function                        Self      Total   Calls
--------                        ----      -----   -----
expensive_calc              0.892ms    0.892ms     100  (72.3%)
process_data                0.234ms    1.126ms      10  (19.0%)
helper                      0.067ms    0.067ms     500  (5.4%)
main                        0.041ms    1.234ms       1  (3.3%)
```

---

### Memory Profiling

Tracks all memory allocations (`alloc`, `buffer`, `talloc`, `realloc`) with source locations.

```bash
hemlock profile --memory script.hml
```

**Example output:**
```
=== Hemlock Profiler Report ===

Total time: 0.543ms
Functions called: 3 unique
Total allocations: 15 (4.2KB)

--- Top 3 by Self Time ---

Function                        Self      Total   Calls      Alloc      Count
--------                        ----      -----   -----      -----      -----
allocator                   0.312ms    0.312ms      10      3.2KB         10  (57.5%)
buffer_ops                  0.156ms    0.156ms       5       1KB          5  (28.7%)
main                        0.075ms    0.543ms       1        0B          0  (13.8%)

--- Top 10 Allocation Sites ---

Location                                      Total    Count
--------                                      -----    -----
src/data.hml:42                               1.5KB        5
src/data.hml:67                               1.0KB       10
src/main.hml:15                               512B         1
```

---

### Call Count Mode

Minimal overhead mode that only counts function calls (no timing).

```bash
hemlock profile --calls script.hml
```

---

## Output Formats

### Text (default)

Human-readable summary with tables.

```bash
hemlock profile script.hml
```

---

### JSON

Machine-readable format for integration with other tools.

```bash
hemlock profile --json script.hml
```

**Example output:**
```json
{
  "total_time_ns": 1234567,
  "function_count": 5,
  "total_alloc_bytes": 4096,
  "total_alloc_count": 15,
  "functions": [
    {
      "name": "expensive_calc",
      "source_file": "script.hml",
      "line": 10,
      "self_time_ns": 892000,
      "total_time_ns": 892000,
      "call_count": 100,
      "alloc_bytes": 0,
      "alloc_count": 0
    }
  ],
  "alloc_sites": [
    {
      "source_file": "script.hml",
      "line": 42,
      "total_bytes": 1536,
      "alloc_count": 5,
      "current_bytes": 0
    }
  ]
}
```

---

### Flamegraph

Generates collapsed stack format compatible with [flamegraph.pl](https://github.com/brendangregg/FlameGraph).

```bash
hemlock profile --flamegraph script.hml > profile.folded

# Generate SVG with flamegraph.pl
flamegraph.pl profile.folded > profile.svg
```

**Example folded output:**
```
main;process_data;expensive_calc 892
main;process_data;helper 67
main;process_data 234
main 41
```

---

## Leak Detection

The `--leaks` flag shows only allocations that were never freed, making it easy to identify memory leaks.

```bash
hemlock profile --leaks script.hml
```

**Example program with leaks:**
```hemlock
fn leaky() {
    let p1 = alloc(100);    // Leak - never freed
    let p2 = alloc(200);    // OK - freed below
    free(p2);
}

fn clean() {
    let b = buffer(64);
    free(b);                // Properly freed
}

leaky();
clean();
```

**Output with --leaks:**
```
=== Hemlock Profiler Report ===

Total time: 0.034ms
Functions called: 2 unique
Total allocations: 3 (388B)

--- Top 2 by Self Time ---

Function                        Self      Total   Calls      Alloc      Count
--------                        ----      -----   -----      -----      -----
leaky                       0.021ms    0.021ms       1       300B          2  (61.8%)
clean                       0.013ms    0.013ms       1        88B          1  (38.2%)

--- Memory Leaks (1 site) ---

Location                                     Leaked      Total    Count
--------                                     ------      -----    -----
script.hml:2                                   100B       100B        1
```

The leak report shows:
- **Leaked** - Bytes currently unfreed at program exit
- **Total** - Total bytes ever allocated at this site
- **Count** - Number of allocations at this site

---

## Understanding Reports

### Function Statistics

| Column | Description |
|--------|-------------|
| Function | Function name |
| Self | Time in function excluding callees |
| Total | Time including all called functions |
| Calls | Number of times function was called |
| Alloc | Total bytes allocated by this function |
| Count | Number of allocations by this function |
| (%) | Percentage of total program time |

### Allocation Sites

| Column | Description |
|--------|-------------|
| Location | Source file and line number |
| Total | Total bytes allocated at this location |
| Count | Number of allocations |
| Leaked | Bytes still allocated at program exit (--leaks only) |

### Time Units

The profiler automatically selects appropriate units:
- `ns` - Nanoseconds (< 1us)
- `us` - Microseconds (< 1ms)
- `ms` - Milliseconds (< 1s)
- `s` - Seconds

---

## Command Reference

```
hemlock profile [OPTIONS] <FILE>

OPTIONS:
    --cpu           CPU/time profiling (default)
    --memory        Memory allocation profiling
    --calls         Call count only (minimal overhead)
    --leaks         Show only unfreed allocations (implies --memory)
    --json          Output in JSON format
    --flamegraph    Output in flamegraph-compatible format
    --top N         Show top N entries (default: 20)
```

---

## Flamegraph Generation

Flamegraphs visualize where your program spends time, with wider bars indicating more time spent.

### Generate a Flamegraph

1. Install flamegraph.pl:
   ```bash
   git clone https://github.com/brendangregg/FlameGraph
   ```

2. Profile your program:
   ```bash
   hemlock profile --flamegraph script.hml > profile.folded
   ```

3. Generate SVG:
   ```bash
   ./FlameGraph/flamegraph.pl profile.folded > profile.svg
   ```

4. Open `profile.svg` in a browser for an interactive visualization.

### Reading Flamegraphs

- **X-axis**: Percentage of total time (width = time proportion)
- **Y-axis**: Call stack depth (bottom = entry point, top = leaf functions)
- **Color**: Random, for visual distinction only
- **Click**: Zoom into a function to see its callees

---

## Best Practices

### 1. Profile Representative Workloads

Profile with realistic data and usage patterns. Small test cases may not reveal real bottlenecks.

```bash
# Good: Profile with production-like data
hemlock profile --memory process_large_file.hml large_input.txt

# Less useful: Tiny test case
hemlock profile quick_test.hml
```

### 2. Use --leaks During Development

Run leak detection regularly to catch memory leaks early:

```bash
hemlock profile --leaks my_program.hml
```

### 3. Compare Before and After

Profile before and after optimizations to measure impact:

```bash
# Before optimization
hemlock profile --json script.hml > before.json

# After optimization
hemlock profile --json script.hml > after.json

# Compare results
```

### 4. Use --top for Large Programs

Limit output to focus on the most significant functions:

```bash
hemlock profile --top 10 large_program.hml
```

### 5. Combine with Flamegraphs

For complex call patterns, flamegraphs provide better visualization than text output:

```bash
hemlock profile --flamegraph complex_app.hml > app.folded
flamegraph.pl app.folded > app.svg
```

---

## Profiler Overhead

The profiler adds some overhead to program execution:

| Mode | Overhead | Use Case |
|------|----------|----------|
| `--calls` | Minimal | Just counting function calls |
| `--cpu` | Low | General performance profiling |
| `--memory` | Moderate | Memory analysis and leak detection |

For the most accurate results, profile multiple times and look for consistent patterns.

---

## See Also

- [Memory Management](../language-guide/memory.md) - Pointers and buffers
- [Memory API](../reference/memory-api.md) - alloc, free, buffer functions
- [Async/Concurrency](async-concurrency.md) - Profiling async code
