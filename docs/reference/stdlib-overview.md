# Standard Library Overview

Hemlock ships with 53 standard library modules covering systems programming, I/O, networking, data formats, concurrency, and more. All modules are imported with the `@stdlib/` prefix.

```hemlock
import { sin, cos, PI } from "@stdlib/math";
import { parse, stringify } from "@stdlib/json";
import { ThreadPool } from "@stdlib/async";
```

---

## Module Categories

### Core Utilities

| Module | Import | Description |
|--------|--------|-------------|
| [`assert`](../../stdlib/docs/assert.md) | `@stdlib/assert` | Assertion utilities for testing and validation |
| [`collections`](../../stdlib/docs/collections.md) | `@stdlib/collections` | HashMap, Queue, Stack, Set, LinkedList, LRUCache |
| [`debug`](../../stdlib/docs/debug.md) | `@stdlib/debug` | Task inspection and stack management |
| [`fmt`](../../stdlib/docs/fmt.md) | `@stdlib/fmt` | Printf-style string formatting |
| [`iter`](../../stdlib/docs/iter.md) | `@stdlib/iter` | Iterator utilities (range, enumerate, zip, chain) |
| [`logging`](../../stdlib/docs/logging.md) | `@stdlib/logging` | Structured logger with levels and file output |
| [`random`](../../stdlib/docs/random.md) | `@stdlib/random` | Random number generation and shuffling |
| [`strings`](../../stdlib/docs/strings.md) | `@stdlib/strings` | Extended string utilities (pad, reverse, lines) |
| [`testing`](../../stdlib/docs/testing.md) | `@stdlib/testing` | BDD-style test framework (describe, test, expect) |

### Math & Science

| Module | Import | Description |
|--------|--------|-------------|
| [`math`](../../stdlib/docs/math.md) | `@stdlib/math` | sin, cos, sqrt, pow, rand, PI, E, divi |
| [`decimal`](../../stdlib/docs/decimal.md) | `@stdlib/decimal` | to_fixed, to_hex, parse_int, parse_float, StringBuilder |
| [`matrix`](../../stdlib/docs/matrix.md) | `@stdlib/matrix` | Dense matrix operations (multiply, transpose, determinant, inverse) |
| [`vector`](../../stdlib/docs/vector.md) | `@stdlib/vector` | Vector similarity search using USearch ANN |

### Memory & Low-Level

| Module | Import | Description |
|--------|--------|-------------|
| [`arena`](../../stdlib/docs/arena.md) | `@stdlib/arena` | Arena (bump) memory allocator |
| [`atomic`](../../stdlib/docs/atomic.md) | `@stdlib/atomic` | Atomic operations (load, store, add, CAS, fence) |
| [`bytes`](../../stdlib/docs/bytes.md) | `@stdlib/bytes` | Byte swapping, endian conversion, buffer I/O |
| [`mmap`](../../stdlib/docs/mmap.md) | `@stdlib/mmap` | Memory-mapped file I/O |

### File System & I/O

| Module | Import | Description |
|--------|--------|-------------|
| [`fs`](../../stdlib/docs/fs.md) | `@stdlib/fs` | File and directory operations (open, read_file, write_file, list_dir) |
| [`async_fs`](../../stdlib/docs/async_fs.md) | `@stdlib/async_fs` | Non-blocking file I/O via thread pool |
| [`glob`](../../stdlib/docs/glob.md) | `@stdlib/glob` | File pattern matching |
| [`path`](../../stdlib/docs/path.md) | `@stdlib/path` | Path manipulation (join, dirname, basename, extname) |

### Concurrency & Async

| Module | Import | Description |
|--------|--------|-------------|
| [`async`](../../stdlib/docs/async.md) | `@stdlib/async` | ThreadPool, parallel_map |
| [`ipc`](../../stdlib/docs/ipc.md) | `@stdlib/ipc` | Inter-process communication (pipes, message queues) |
| [`signal`](../../stdlib/docs/signal.md) | `@stdlib/signal` | Signal constants and handling (SIGINT, SIGTERM, etc.) |

### Networking

| Module | Import | Description |
|--------|--------|-------------|
| [`http`](../../stdlib/docs/http.md) | `@stdlib/http` | HTTP client (get, post, request with headers) |
| [`net`](../../stdlib/docs/net.md) | `@stdlib/net` | TCP/UDP sockets (TcpListener, TcpStream, UdpSocket) |
| [`unix_socket`](../../stdlib/docs/unix_socket.md) | `@stdlib/unix_socket` | Unix domain sockets (AF_UNIX stream/datagram) |
| [`url`](../../stdlib/docs/url.md) | `@stdlib/url` | URL parsing and manipulation |
| [`websocket`](../../stdlib/docs/websocket.md) | `@stdlib/websocket` | WebSocket client |

### Data Formats

| Module | Import | Description |
|--------|--------|-------------|
| [`json`](../../stdlib/docs/json.md) | `@stdlib/json` | JSON parse, stringify, pretty, path access |
| [`toml`](../../stdlib/docs/toml.md) | `@stdlib/toml` | TOML parsing and generation |
| [`yaml`](../../stdlib/docs/yaml.md) | `@stdlib/yaml` | YAML parsing and generation |
| [`csv`](../../stdlib/docs/csv.md) | `@stdlib/csv` | CSV parsing and generation |

### Encoding & Cryptography

| Module | Import | Description |
|--------|--------|-------------|
| [`encoding`](../../stdlib/docs/encoding.md) | `@stdlib/encoding` | Base64, Base32, hex, URL encoding |
| [`hash`](../../stdlib/docs/hash.md) | `@stdlib/hash` | SHA-1, SHA-256, SHA-512, MD5, CRC32, DJB2 |
| [`crypto`](../../stdlib/docs/crypto.md) | `@stdlib/crypto` | AES encryption, RSA signing, random_bytes |
| [`compression`](../../stdlib/docs/compression.md) | `@stdlib/compression` | gzip, gunzip, deflate |

### Text Processing

| Module | Import | Description |
|--------|--------|-------------|
| [`regex`](../../stdlib/docs/regex.md) | `@stdlib/regex` | Regular expressions (POSIX ERE) |
| [`jinja`](../../stdlib/docs/jinja.md) | `@stdlib/jinja` | Jinja2-compatible template rendering |

### Date, Time & Versioning

| Module | Import | Description |
|--------|--------|-------------|
| [`datetime`](../../stdlib/docs/datetime.md) | `@stdlib/datetime` | DateTime class, formatting, parsing |
| [`time`](../../stdlib/docs/time.md) | `@stdlib/time` | Timestamps, sleep, clock measurement |
| [`semver`](../../stdlib/docs/semver.md) | `@stdlib/semver` | Semantic version parsing and comparison |
| [`uuid`](../../stdlib/docs/uuid.md) | `@stdlib/uuid` | UUID v4 and v7 generation |

### System & Environment

| Module | Import | Description |
|--------|--------|-------------|
| [`args`](../../stdlib/docs/args.md) | `@stdlib/args` | Command-line argument parsing |
| [`env`](../../stdlib/docs/env.md) | `@stdlib/env` | Environment variables, exit, get_pid |
| [`os`](../../stdlib/docs/os.md) | `@stdlib/os` | Platform detection, CPU count, hostname |
| [`process`](../../stdlib/docs/process.md) | `@stdlib/process` | fork, exec, wait, kill |
| [`shell`](../../stdlib/docs/shell.md) | `@stdlib/shell` | Shell command execution and argument escaping |

### Terminal & UI

| Module | Import | Description |
|--------|--------|-------------|
| [`terminal`](../../stdlib/docs/terminal.md) | `@stdlib/terminal` | ANSI colors, styles, and cursor control |
| [`termios`](../../stdlib/docs/termios.md) | `@stdlib/termios` | Raw terminal input and key detection |

### Database

| Module | Import | Description |
|--------|--------|-------------|
| [`sqlite`](../../stdlib/docs/sqlite.md) | `@stdlib/sqlite` | SQLite database, query, exec, transactions |

### FFI & Interop

| Module | Import | Description |
|--------|--------|-------------|
| [`ffi`](../../stdlib/docs/ffi.md) | `@stdlib/ffi` | FFI callback management and type constants |

### Utility

| Module | Import | Description |
|--------|--------|-------------|
| [`retry`](../../stdlib/docs/retry.md) | `@stdlib/retry` | Retry logic with exponential backoff |

---

## Quick Examples

### Read and parse a config file

```hemlock
import { parse_file } from "@stdlib/yaml";
import { get } from "@stdlib/yaml";

let config = parse_file("config.yaml");
let db_host = get(config, "database.host");
print("Connecting to " + db_host);
```

### HTTP request with JSON

```hemlock
import { http_get } from "@stdlib/http";
import { parse } from "@stdlib/json";

let resp = http_get("https://api.example.com/data");
let data = parse(resp.body);
print(data["name"]);
```

### Concurrent processing

```hemlock
import { ThreadPool, parallel_map } from "@stdlib/async";

let pool = ThreadPool(4);
let results = parallel_map(pool, fn(x) { return x * x; }, [1, 2, 3, 4, 5]);
pool.shutdown();
```

### Hash and encode

```hemlock
import { sha256 } from "@stdlib/hash";
import { base64_encode } from "@stdlib/encoding";

let digest = sha256("hello world");
let encoded = base64_encode(digest);
print(encoded);
```

### Template rendering

```hemlock
import { render } from "@stdlib/jinja";

let html = render(`
<h1>{{ title }}</h1>
<ul>
{% for item in items %}<li>{{ item }}</li>
{% endfor %}</ul>
`, { title: "Menu", items: ["Home", "About", "Contact"] });
```

---

## See Also

- [Built-in Functions Reference](./builtins.md) — functions available without imports
- [Migration Guide (v2.0)](../migration-2.0.md) — builtins moved to stdlib in v2.0
- Individual module docs in [`stdlib/docs/`](../../stdlib/docs/)
