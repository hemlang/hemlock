# Hemlock Standard Library

The Hemlock standard library provides essential modules and data structures for Hemlock programs.

## Installation

### Core Modules
Most stdlib modules are pure Hemlock and require no external dependencies.

### Optional Dependencies

Some modules require external C libraries:

**For HTTP/WebSocket support:**
```bash
# Ubuntu/Debian
sudo apt-get install libwebsockets-dev

# macOS
brew install libwebsockets

# Arch Linux
sudo pacman -S libwebsockets
```

**Build stdlib C modules:**
```bash
make stdlib
```

This compiles the libwebsockets FFI wrapper (lws_wrapper.so) for HTTP and WebSocket modules.

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

### Networking (`@stdlib/net`)
**Status:** Complete

TCP/UDP networking with ergonomic wrappers:
- **TcpListener** - TCP server socket for accepting connections
- **TcpStream** - TCP client/connection with read/write methods
- **UdpSocket** - UDP datagram socket with send_to/recv_from
- **DNS:** resolve() - Hostname to IP resolution

See [docs/net.md](docs/net.md) for detailed documentation.

### Regular Expressions (`@stdlib/regex`)
**Status:** Basic (via FFI)

POSIX Extended Regular Expression pattern matching:
- `compile(pattern, flags)` - Compile reusable regex object
- `test(pattern, text, flags)` - One-shot pattern matching
- `matches()`, `find()` - Convenience aliases
- Case-insensitive matching with `REG_ICASE` flag
- Manual memory management (explicit `.free()` required)

See [docs/regex.md](docs/regex.md) for detailed documentation.

### HTTP Client (`@stdlib/http`)
**Status:** Production (via libwebsockets FFI)

Production-ready HTTP/HTTPS client using libwebsockets:
- **HTTP methods:** get, post, request
- **Convenience:** fetch, post_json
- **Status helpers:** is_success, is_redirect, is_client_error, is_server_error
- **URL helpers:** url_encode
- **Native libwebsockets FFI** for performance and reliability
- **Full HTTPS/TLS support** via libwebsockets SSL
- **Requires:** libwebsockets-dev package

See [docs/http.md](docs/http.md) for detailed documentation.

### WebSocket (`@stdlib/websocket`)
**Status:** Production (via libwebsockets FFI)

WebSocket client/server using libwebsockets:
- **Client:** WebSocket(url) - Connect to ws:// or wss:// endpoints
- **Server:** WebSocketServer(host, port) - Accept WebSocket connections
- **Message types:** Text, binary, ping, pong, close
- **SSL/TLS support:** Secure WebSocket (wss://) built-in
- **Requires:** libwebsockets-dev package

See [docs/websocket.md](docs/websocket.md) for detailed documentation.

### JSON (`@stdlib/json`)
**Status:** Comprehensive

Full-featured JSON manipulation library:
- **Parsing:** parse, parse_file, is_valid, validate
- **Serialization:** stringify, stringify_file, pretty, pretty_file
- **Path access:** get, set, has, delete (dot notation: "user.address.city")
- **Type checking:** is_object, is_array, is_string, is_number, is_bool, is_null, type_of
- **Utilities:** clone (deep copy), equals (deep comparison)
- **Building on:** Built-in serialize()/deserialize() with enhanced features

See [docs/json.md](docs/json.md) for detailed documentation.

## Usage

Import modules using the `@stdlib/` prefix:

```hemlock
// Import specific functions
import { HashMap, Queue, Stack } from "@stdlib/collections";
import { sin, cos, PI } from "@stdlib/math";
import { now, sleep } from "@stdlib/time";
import { getenv, exit } from "@stdlib/env";
import { read_file, write_file, exists } from "@stdlib/fs";
import { TcpListener, TcpStream, UdpSocket } from "@stdlib/net";
import { compile, test, REG_ICASE } from "@stdlib/regex";
import { get, post, fetch } from "@stdlib/http";
import { WebSocket, WebSocketServer } from "@stdlib/websocket";
import { parse, stringify, pretty, get, set } from "@stdlib/json";

// Import all as namespace
import * as math from "@stdlib/math";
import * as fs from "@stdlib/fs";
import * as net from "@stdlib/net";
import * as regex from "@stdlib/regex";
import * as http from "@stdlib/http";
import * as ws from "@stdlib/websocket";
import * as json from "@stdlib/json";

// Use imported functions
let angle = math.PI / 4.0;
let result = math.sin(angle);
let content = fs.read_file("data.txt");
let stream = net.TcpStream("example.com", 80);
let is_valid = regex.test("^[a-z]+$", "hello", null);
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

### Networking
```hemlock
import { TcpStream, resolve } from "@stdlib/net";

let ip = resolve("example.com");
let stream = TcpStream(ip, 80);
defer stream.close();

stream.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
let response = stream.read(4096);
```

### Regular Expressions
```hemlock
import { compile, test } from "@stdlib/regex";

// One-shot pattern matching
if (test("^[a-z]+$", "hello", null)) {
    print("Valid lowercase string");
}

// Compiled regex for reuse
let email_pattern = compile("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$", null);
print(email_pattern.test("user@example.com"));  // true
email_pattern.free();  // Manual cleanup required
```

### HTTP Client
```hemlock
import { get, post_json, fetch } from "@stdlib/http";

// Simple GET request
let response = get("http://example.com/api/data", []);
print(response.status_code);  // 200
print(response.body);

// Fetch URL (simplified)
let body = fetch("http://example.com");

// POST JSON
let data = { name: "Alice", age: 30 };
let response2 = post_json("http://api.example.com/users", data);
```

### WebSocket
```hemlock
import { WebSocket } from "@stdlib/websocket";

// Connect to WebSocket server
let ws = WebSocket("ws://echo.websocket.org");
defer ws.close();

// Send and receive messages
ws.send_text("Hello, WebSocket!");
let msg = ws.recv(5000);  // 5 second timeout
if (msg.type == "text") {
    print("Received: " + msg.data);
}
```

### JSON
```hemlock
import { parse, pretty, get, set } from "@stdlib/json";

// Parse and access nested data
let config = parse('{"server":{"host":"localhost","port":8080}}');
let port = get(config, "server.port");  // 8080

// Modify and pretty print
set(config, "server.port", 9000);
print(pretty(config, 2));

// Clone for independent copy
let backup = clone(config);
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
├── net.hml             # Networking module implementation
├── regex.hml           # Regular expressions module (via FFI)
├── http.hml            # HTTP client module (via libwebsockets FFI)
├── websocket.hml       # WebSocket client/server (via libwebsockets FFI)
├── websocket_pure.hml  # WebSocket pure Hemlock implementation (educational)
├── json.hml            # JSON module (pure Hemlock)
├── c/                  # C FFI wrappers (compiled with 'make stdlib')
│   └── lws_wrapper.c   # libwebsockets wrapper for HTTP/WebSocket
└── docs/
    ├── collections.md  # Collections API reference
    ├── math.md         # Math API reference
    ├── time.md         # Time API reference
    ├── env.md          # Environment API reference
    ├── fs.md           # Filesystem API reference
    ├── net.md          # Networking API reference
    ├── regex.md        # Regex API reference
    ├── http.md         # HTTP API reference
    ├── websocket.md    # WebSocket API reference
    └── json.md         # JSON API reference
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
- **encoding** - Base64, hex, URL encoding/decoding
- **testing** - Test framework with describe/test/expect/assertions
- **datetime** - Date/time formatting and parsing
- **crypto** - Cryptographic functions (via FFI + OpenSSL)
- **compression** - zlib/gzip compression (via FFI)

See `STDLIB_ANALYSIS_UPDATED.md` and `STDLIB_NETWORKING_DESIGN.md` for detailed roadmap.

## Module Status

| Module | Status | Docs | Tests | Lines | Quality |
|--------|--------|------|-------|-------|---------|
| collections | ✅ Production | ✅ Complete | ✅ Comprehensive | 760 | Excellent |
| math | ✅ Complete | ✅ Complete | ✅ Good | 50 | High |
| time | ⚠️ Basic | ✅ Complete | ✅ Good | 13 | Good |
| env | ✅ Complete | ✅ Complete | ✅ Good | 14 | High |
| fs | ✅ Comprehensive | ✅ Complete | ⚠️ Partial | 31 | High |
| net | ✅ Complete | ✅ Complete | ✅ Good | 240 | High |
| regex | ⚠️ Basic (FFI) | ✅ Complete | ✅ Good | 152 | Good |
| http | ✅ Production (libwebsockets) | ✅ Complete | ✅ Good | 280 | High |
| websocket | ✅ Production (libwebsockets) | ✅ Complete | ✅ Good | 318 | High |
| json | ✅ Comprehensive | ✅ Complete | ✅ Good | 550+ | High |

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
make test | grep stdlib_regex

# Or run individual test files
./hemlock tests/stdlib_collections/test_hashmap.hml
./hemlock tests/stdlib_math/test_math_constants.hml
./hemlock tests/stdlib_time/test_time.hml
./hemlock tests/stdlib_regex/basic_test.hml
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
