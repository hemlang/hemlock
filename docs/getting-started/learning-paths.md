# Learning Paths

Different goals require different knowledge. Pick the path that matches what you want to build.

---

## Path 1: Quick Scripts & Automation

**Goal:** Write scripts to automate tasks, process files, and get things done.

**Time to productivity:** Fast - you can start writing useful scripts immediately.

### What You'll Learn

1. **[Quick Start](quick-start.md)** - Your first program, basic syntax
2. **[Strings](../language-guide/strings.md)** - Text processing, splitting, searching
3. **[Arrays](../language-guide/arrays.md)** - Lists, filtering, transforming data
4. **[File I/O](../advanced/file-io.md)** - Reading and writing files
5. **[Command-Line Args](../advanced/command-line-args.md)** - Getting input from users

### Skip For Now

- Memory management (automatic for scripts)
- Async/concurrency (overkill for simple scripts)
- FFI (only needed for C interop)

### Example Project: File Renamer

```hemlock
import { list_dir, rename } from "@stdlib/fs";

// Rename all .txt files to .md
let files = list_dir(".");
for (file in files) {
    if (file.ends_with(".txt")) {
        let new_name = file.replace(".txt", ".md");
        rename(file, new_name);
        print(`Renamed: ${file} -> ${new_name}`);
    }
}
```

---

## Path 2: Data Processing & Analysis

**Goal:** Parse data, transform it, generate reports.

**Time to productivity:** Fast - Hemlock's string and array methods make this easy.

### What You'll Learn

1. **[Quick Start](quick-start.md)** - Basics
2. **[Strings](../language-guide/strings.md)** - Parsing, splitting, formatting
3. **[Arrays](../language-guide/arrays.md)** - map, filter, reduce for data transformation
4. **[Objects](../language-guide/objects.md)** - Structured data
5. **Standard Library:**
   - **[@stdlib/json](../../stdlib/docs/json.md)** - JSON parsing
   - **[@stdlib/csv](../../stdlib/docs/csv.md)** - CSV files
   - **[@stdlib/fs](../../stdlib/docs/fs.md)** - File operations

### Example Project: CSV Analyzer

```hemlock
import { read_file } from "@stdlib/fs";
import { parse } from "@stdlib/csv";

let data = parse(read_file("sales.csv"));

// Calculate total sales
let total = 0;
for (row in data) {
    total = total + f64(row.amount);
}

print(`Total sales: $${total}`);

// Find top seller
let top = data[0];
for (row in data) {
    if (f64(row.amount) > f64(top.amount)) {
        top = row;
    }
}

print(`Top sale: ${top.product} - $${top.amount}`);
```

---

## Path 3: Web & Network Programming

**Goal:** Build HTTP clients, work with APIs, create servers.

**Time to productivity:** Medium - requires understanding async basics.

### What You'll Learn

1. **[Quick Start](quick-start.md)** - Basics
2. **[Functions](../language-guide/functions.md)** - Callbacks and closures
3. **[Error Handling](../language-guide/error-handling.md)** - try/catch for network errors
4. **[Async & Concurrency](../advanced/async-concurrency.md)** - spawn, await, channels
5. **Standard Library:**
   - **[@stdlib/http](../../stdlib/docs/http.md)** - HTTP requests
   - **[@stdlib/json](../../stdlib/docs/json.md)** - JSON for APIs
   - **[@stdlib/net](../../stdlib/docs/net.md)** - TCP/UDP sockets
   - **[@stdlib/url](../../stdlib/docs/url.md)** - URL parsing

### Example Project: API Client

```hemlock
import { http_get, http_post } from "@stdlib/http";
import { parse, stringify } from "@stdlib/json";

// GET request
let response = http_get("https://api.example.com/users");
let users = parse(response.body);

for (user in users) {
    print(`${user.name}: ${user.email}`);
}

// POST request
let new_user = { name: "Alice", email: "alice@example.com" };
let result = http_post("https://api.example.com/users", {
    body: stringify(new_user),
    headers: { "Content-Type": "application/json" }
});

print(`Created user with ID: ${parse(result.body).id}`);
```

---

## Path 4: Systems Programming

**Goal:** Write low-level code, work with memory, interface with C libraries.

**Time to productivity:** Longer - requires understanding memory management.

### What You'll Learn

1. **[Quick Start](quick-start.md)** - Basics
2. **[Types](../language-guide/types.md)** - Understanding i32, u8, ptr, etc.
3. **[Memory Management](../language-guide/memory.md)** - alloc, free, buffers
4. **[FFI](../advanced/ffi.md)** - Calling C functions
5. **[Signals](../advanced/signals.md)** - Signal handling

### Key Concepts

**Memory Safety Checklist:**
- [ ] Every `alloc()` has a matching `free()`
- [ ] Use `buffer()` unless you need raw `ptr`
- [ ] Set pointers to `null` after freeing
- [ ] Use `try/finally` to guarantee cleanup

**Type Mapping for FFI:**
| Hemlock | C |
|---------|---|
| `i8` | `char` / `int8_t` |
| `i32` | `int` |
| `i64` | `long` (64-bit) |
| `u8` | `unsigned char` |
| `f64` | `double` |
| `ptr` | `void*` |

### Example Project: Custom Memory Pool

```hemlock
// Simple bump allocator
let pool_size = 1024 * 1024;  // 1MB
let pool = alloc(pool_size);
let pool_offset = 0;

fn pool_alloc(size: i32): ptr {
    if (pool_offset + size > pool_size) {
        throw "Pool exhausted";
    }
    let p = pool + pool_offset;
    pool_offset = pool_offset + size;
    return p;
}

fn pool_reset() {
    pool_offset = 0;
}

fn pool_destroy() {
    free(pool);
}

// Use it
let a = pool_alloc(100);
let b = pool_alloc(200);
memset(a, 0, 100);
memset(b, 0, 200);

pool_reset();  // Reuse all memory
pool_destroy();  // Clean up
```

---

## Path 5: Parallel & Concurrent Programs

**Goal:** Run code on multiple CPU cores, build responsive applications.

**Time to productivity:** Medium - async syntax is straightforward, but reasoning about parallelism takes practice.

### What You'll Learn

1. **[Quick Start](quick-start.md)** - Basics
2. **[Functions](../language-guide/functions.md)** - Closures (important for async)
3. **[Async & Concurrency](../advanced/async-concurrency.md)** - Full deep dive
4. **[Atomics](../advanced/atomics.md)** - Lock-free programming

### Key Concepts

**Hemlock's async model:**
- `async fn` - Define a function that can run on another thread
- `spawn(fn, args...)` - Start running it, returns a task handle
- `join(task)` or `await task` - Wait for it to finish, get result
- `channel(size)` - Create a queue for sending data between tasks

**Important:** Tasks receive *copies* of values. If you pass a pointer, you're responsible for ensuring the memory stays valid until the task completes.

### Example Project: Parallel File Processor

```hemlock
import { list_dir, read_file } from "@stdlib/fs";

async fn process_file(path: string): i32 {
    let content = read_file(path);
    let lines = content.split("\n");
    return lines.length;
}

// Process all files in parallel
let files = list_dir("data/");
let tasks = [];

for (file in files) {
    if (file.ends_with(".txt")) {
        let task = spawn(process_file, "data/" + file);
        tasks.push({ name: file, task: task });
    }
}

// Collect results
let total_lines = 0;
for (item in tasks) {
    let count = join(item.task);
    print(`${item.name}: ${count} lines`);
    total_lines = total_lines + count;
}

print(`Total: ${total_lines} lines`);
```

---

## What to Learn First (Any Path)

No matter your goal, start with these fundamentals:

### Week 1: Core Basics
1. **[Quick Start](quick-start.md)** - Write and run your first program
2. **[Syntax](../language-guide/syntax.md)** - Variables, operators, control flow
3. **[Functions](../language-guide/functions.md)** - Define and call functions

### Week 2: Data Handling
4. **[Strings](../language-guide/strings.md)** - Text manipulation
5. **[Arrays](../language-guide/arrays.md)** - Collections and iteration
6. **[Objects](../language-guide/objects.md)** - Structured data

### Week 3: Robustness
7. **[Error Handling](../language-guide/error-handling.md)** - try/catch/throw
8. **[Modules](../language-guide/modules.md)** - Import/export, using stdlib

### Then: Pick Your Path Above

---

## Cheat Sheet: Coming From Other Languages

### From Python

| Python | Hemlock | Notes |
|--------|---------|-------|
| `x = 42` | `let x = 42;` | Semicolons required |
| `def fn():` | `fn name() { }` | Braces required |
| `if x:` | `if (x) { }` | Parens and braces required |
| `for i in range(10):` | `for (let i = 0; i < 10; i++) { }` | C-style for loops |
| `for item in list:` | `for (item in array) { }` | For-in works same |
| `list.append(x)` | `array.push(x);` | Different method name |
| `len(s)` | `s.length` or `len(s)` | Both work |
| Automatic memory | Manual for `ptr` | Most types auto-cleanup |

### From JavaScript

| JavaScript | Hemlock | Notes |
|------------|---------|-------|
| `let x = 42` | `let x = 42;` | Same (semicolons required) |
| `const x = 42` | `let x = 42;` | No const keyword |
| `function fn()` | `fn name() { }` | Different keyword |
| `() => x` | `fn() { return x; }` | No arrow functions |
| `async/await` | `async/await` | Same syntax |
| `Promise` | `spawn/join` | Different model |
| Automatic GC | Manual for `ptr` | Most types auto-cleanup |

### From C/C++

| C | Hemlock | Notes |
|---|---------|-------|
| `int x = 42;` | `let x: i32 = 42;` | Type after colon |
| `malloc(n)` | `alloc(n)` | Same concept |
| `free(p)` | `free(p)` | Same |
| `char* s = "hi"` | `let s = "hi";` | Strings are managed |
| `#include` | `import { } from` | Module imports |
| Manual everything | Auto for most types | Only `ptr` needs manual |

---

## Getting Help

- **[Glossary](../glossary.md)** - Definitions of programming terms
- **[Examples](../../examples/)** - Complete working programs
- **[Tests](../../tests/)** - See how features are used
- **GitHub Issues** - Ask questions, report bugs

---

## Difficulty Levels

Throughout the docs, you'll see these markers:

| Marker | Meaning |
|--------|---------|
| **Beginner** | No prior programming experience needed |
| **Intermediate** | Assumes basic programming knowledge |
| **Advanced** | Requires understanding of systems concepts |

If something marked "Beginner" confuses you, check the [Glossary](../glossary.md) for term definitions.
