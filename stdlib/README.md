# Hemlock Standard Library

Hemlock ships with 53 standard library modules. Each module has a matching implementation in `stdlib/<module>.hml`, API documentation in `stdlib/docs/<module>.md`, and should be imported with the `@stdlib/` prefix.

```hemlock
import { HashMap } from "@stdlib/collections";
import { read_file } from "@stdlib/fs";
import { parse, stringify } from "@stdlib/json";
```

For a categorized index, see [Standard Library Overview](../docs/reference/stdlib-overview.md).

## Installation and Optional Dependencies

Most modules are pure Hemlock and require no external dependencies beyond a built Hemlock interpreter/compiler.

Some modules rely on optional native libraries:

| Feature area | Modules | Dependency | Notes |
|--------------|---------|------------|-------|
| HTTP/WebSocket | `@stdlib/http`, `@stdlib/websocket` | `libwebsockets` | Build `stdlib/c/lws_wrapper.so` with `make stdlib`. If the library is missing, the build skips the wrapper and related tests may be skipped. |
| Vector search | `@stdlib/vector` | USearch C library (`libusearch_c`) | Optional ANN/vector-similarity support. See [Installation: USearch](../docs/getting-started/installation.md#usearch-vector-similarity-search). |

Common install commands for HTTP/WebSocket support:

```bash
# Ubuntu/Debian
sudo apt-get install libwebsockets-dev

# macOS
brew install libwebsockets

# Arch Linux
sudo pacman -S libwebsockets
```

Build optional stdlib C helpers from the repository root:

```bash
make stdlib
```

## Module Index

The module list below is intentionally generated from the checked-in stdlib inventory during documentation audits. If you add or remove a module, update this table and make sure `python3 tests/check_docs.py` still passes.

| Module | Import | Documentation |
|--------|--------|---------------|
| `arena` | `@stdlib/arena` | [docs/arena.md](docs/arena.md) |
| `args` | `@stdlib/args` | [docs/args.md](docs/args.md) |
| `assert` | `@stdlib/assert` | [docs/assert.md](docs/assert.md) |
| `async` | `@stdlib/async` | [docs/async.md](docs/async.md) |
| `async_fs` | `@stdlib/async_fs` | [docs/async_fs.md](docs/async_fs.md) |
| `atomic` | `@stdlib/atomic` | [docs/atomic.md](docs/atomic.md) |
| `bytes` | `@stdlib/bytes` | [docs/bytes.md](docs/bytes.md) |
| `collections` | `@stdlib/collections` | [docs/collections.md](docs/collections.md) |
| `compression` | `@stdlib/compression` | [docs/compression.md](docs/compression.md) |
| `crypto` | `@stdlib/crypto` | [docs/crypto.md](docs/crypto.md) |
| `csv` | `@stdlib/csv` | [docs/csv.md](docs/csv.md) |
| `datetime` | `@stdlib/datetime` | [docs/datetime.md](docs/datetime.md) |
| `debug` | `@stdlib/debug` | [docs/debug.md](docs/debug.md) |
| `decimal` | `@stdlib/decimal` | [docs/decimal.md](docs/decimal.md) |
| `encoding` | `@stdlib/encoding` | [docs/encoding.md](docs/encoding.md) |
| `env` | `@stdlib/env` | [docs/env.md](docs/env.md) |
| `ffi` | `@stdlib/ffi` | [docs/ffi.md](docs/ffi.md) |
| `fmt` | `@stdlib/fmt` | [docs/fmt.md](docs/fmt.md) |
| `fs` | `@stdlib/fs` | [docs/fs.md](docs/fs.md) |
| `glob` | `@stdlib/glob` | [docs/glob.md](docs/glob.md) |
| `hash` | `@stdlib/hash` | [docs/hash.md](docs/hash.md) |
| `http` | `@stdlib/http` | [docs/http.md](docs/http.md) |
| `ipc` | `@stdlib/ipc` | [docs/ipc.md](docs/ipc.md) |
| `iter` | `@stdlib/iter` | [docs/iter.md](docs/iter.md) |
| `jinja` | `@stdlib/jinja` | [docs/jinja.md](docs/jinja.md) |
| `json` | `@stdlib/json` | [docs/json.md](docs/json.md) |
| `logging` | `@stdlib/logging` | [docs/logging.md](docs/logging.md) |
| `math` | `@stdlib/math` | [docs/math.md](docs/math.md) |
| `matrix` | `@stdlib/matrix` | [docs/matrix.md](docs/matrix.md) |
| `mmap` | `@stdlib/mmap` | [docs/mmap.md](docs/mmap.md) |
| `net` | `@stdlib/net` | [docs/net.md](docs/net.md) |
| `os` | `@stdlib/os` | [docs/os.md](docs/os.md) |
| `path` | `@stdlib/path` | [docs/path.md](docs/path.md) |
| `process` | `@stdlib/process` | [docs/process.md](docs/process.md) |
| `random` | `@stdlib/random` | [docs/random.md](docs/random.md) |
| `regex` | `@stdlib/regex` | [docs/regex.md](docs/regex.md) |
| `retry` | `@stdlib/retry` | [docs/retry.md](docs/retry.md) |
| `semver` | `@stdlib/semver` | [docs/semver.md](docs/semver.md) |
| `shell` | `@stdlib/shell` | [docs/shell.md](docs/shell.md) |
| `signal` | `@stdlib/signal` | [docs/signal.md](docs/signal.md) |
| `sqlite` | `@stdlib/sqlite` | [docs/sqlite.md](docs/sqlite.md) |
| `strings` | `@stdlib/strings` | [docs/strings.md](docs/strings.md) |
| `terminal` | `@stdlib/terminal` | [docs/terminal.md](docs/terminal.md) |
| `termios` | `@stdlib/termios` | [docs/termios.md](docs/termios.md) |
| `testing` | `@stdlib/testing` | [docs/testing.md](docs/testing.md) |
| `time` | `@stdlib/time` | [docs/time.md](docs/time.md) |
| `toml` | `@stdlib/toml` | [docs/toml.md](docs/toml.md) |
| `unix_socket` | `@stdlib/unix_socket` | [docs/unix_socket.md](docs/unix_socket.md) |
| `url` | `@stdlib/url` | [docs/url.md](docs/url.md) |
| `uuid` | `@stdlib/uuid` | [docs/uuid.md](docs/uuid.md) |
| `vector` | `@stdlib/vector` | [docs/vector.md](docs/vector.md) |
| `websocket` | `@stdlib/websocket` | [docs/websocket.md](docs/websocket.md) |
| `yaml` | `@stdlib/yaml` | [docs/yaml.md](docs/yaml.md) |

## Usage Patterns

### Import specific names

```hemlock
import { sin, cos, PI } from "@stdlib/math";
import { read_file, write_file } from "@stdlib/fs";
```

### Import a module namespace

```hemlock
import * as json from "@stdlib/json";

let value = json.parse("{\"ok\":true}");
print(json.stringify(value));
```

### Combine modules

```hemlock
import { read_file } from "@stdlib/fs";
import { parse } from "@stdlib/json";

let config = parse(read_file("config.json"));
print(config.name);
```

## Directory Structure

```text
stdlib/
├── *.hml       # stdlib module implementations
├── c/          # optional C helpers used by selected FFI-backed modules
└── docs/       # module API references, one Markdown file per module
```

## Contributing

When adding or changing a stdlib module:

1. Update `stdlib/<module>.hml`.
2. Add or update tests under `tests/stdlib_<module>/`.
3. Add or update `stdlib/docs/<module>.md`.
4. Update [docs/reference/stdlib-overview.md](../docs/reference/stdlib-overview.md) and this README if the module inventory changes.
5. Run `python3 tests/check_docs.py` to verify docs coverage and links.
6. Run the relevant Hemlock tests, then `make test` when practical.

## Documentation Quality Checks

Use the documentation audit script before submitting doc or stdlib changes:

```bash
python3 tests/check_docs.py
```

The script verifies that every `stdlib/*.hml` module has matching documentation, every stdlib doc has a matching implementation, and relative Markdown links resolve.
