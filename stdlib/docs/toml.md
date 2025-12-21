# @stdlib/toml - TOML Parser and Serializer

Provides parsing and serialization for TOML (Tom's Obvious, Minimal Language), a popular configuration file format designed for human readability.

## Import

```hemlock
import { parse, stringify, parse_file, write_file } from "@stdlib/toml";
import { get, set } from "@stdlib/toml";
```

## Core Functions

### parse(input: string): object

Parse a TOML string into an object.

```hemlock
let config = parse(`
title = "My App"
version = 1

[database]
server = "localhost"
port = 5432
`);

print(config["title"]);           // "My App"
print(config["database"]["port"]); // 5432
```

### stringify(obj: object): string

Convert an object to a TOML string.

```hemlock
let config = {
    title: "My App",
    debug: true,
    database: {
        host: "localhost",
        port: 5432
    }
};

let toml = stringify(config);
// title = "My App"
// debug = true
//
// [database]
// host = "localhost"
// port = 5432
```

### parse_file(path: string): object

Parse a TOML file.

```hemlock
let config = parse_file("config.toml");
print(config["server"]["host"]);
```

### write_file(path: string, obj: object)

Write an object to a TOML file.

```hemlock
let config = { name: "app", version: 1 };
write_file("config.toml", config);
```

## Helper Functions

### get(obj: object, path: string): any

Get a value using a dotted key path.

```hemlock
let config = parse(`
[database]
host = "localhost"

[database.connection]
timeout = 30
`);

print(get(config, "database.host"));             // "localhost"
print(get(config, "database.connection.timeout")); // 30
```

### set(obj: object, path: string, value: any)

Set a value using a dotted key path.

```hemlock
let config = {};
set(config, "database.host", "localhost");
set(config, "database.port", 5432);

print(stringify(config));
// [database]
// host = "localhost"
// port = 5432
```

## TOML Features Supported

### Basic Types

```toml
# Strings
title = "Hello"
path = 'C:\Users\name'  # Literal string (no escapes)

# Numbers
integer = 42
negative = -17
float = 3.14159
exponent = 1e10
hex = 0xDEADBEEF
octal = 0o755
binary = 0b11010110

# Booleans
enabled = true
disabled = false
```

### Arrays

```toml
# Simple arrays
ports = [8001, 8002, 8003]
names = ["alpha", "beta", "gamma"]

# Mixed types
data = [1, "two", 3.0]

# Multiline arrays
colors = [
    "red",
    "green",
    "blue"
]
```

### Tables (Sections)

```toml
[server]
host = "localhost"
port = 8080

[server.ssl]
enabled = true
cert = "/path/to/cert.pem"
```

### Inline Tables

```toml
point = { x = 1, y = 2 }
person = { name = "Alice", age = 30 }
```

### Array of Tables

```toml
[[products]]
name = "Hammer"
price = 9.99

[[products]]
name = "Nail"
price = 0.05
```

### Dotted Keys

```toml
# These are equivalent:
physical.color = "orange"
physical.shape = "round"

[physical]
color = "orange"
shape = "round"
```

### Comments

```toml
# This is a comment
key = "value"  # Inline comment
```

### Multiline Strings

```toml
# Basic multiline
description = """
This is a long
multiline string.
"""

# Literal multiline (no escapes)
regex = '''
\d{3}-\d{4}
'''
```

## Examples

### Configuration File

```hemlock
import { parse_file, get } from "@stdlib/toml";

let config = parse_file("app.toml");

let db_host = get(config, "database.host");
let db_port = get(config, "database.port");
let log_level = get(config, "logging.level");

print("Connecting to " + db_host + ":" + db_port);
```

### Creating Configuration

```hemlock
import { stringify, write_file, set } from "@stdlib/toml";

let config = {};
set(config, "app.name", "MyApp");
set(config, "app.version", "1.0.0");
set(config, "database.host", "localhost");
set(config, "database.port", 5432);
set(config, "logging.level", "info");

write_file("config.toml", config);
```

### Parsing Cargo.toml (Rust-style)

```hemlock
import { parse } from "@stdlib/toml";

let cargo = parse(`
[package]
name = "myproject"
version = "0.1.0"
edition = "2021"

[dependencies]
serde = "1.0"
tokio = { version = "1", features = ["full"] }
`);

print("Package: " + cargo["package"]["name"]);
print("Version: " + cargo["package"]["version"]);
```

### Working with Arrays of Tables

```hemlock
import { parse } from "@stdlib/toml";

let config = parse(`
[[servers]]
host = "alpha"
dc = "east"

[[servers]]
host = "beta"
dc = "west"
`);

let servers = config["servers"];
let i = 0;
while (i < servers.length) {
    print("Server: " + servers[i]["host"] + " in " + servers[i]["dc"]);
    i = i + 1;
}
// Server: alpha in east
// Server: beta in west
```

## Error Handling

```hemlock
import { parse } from "@stdlib/toml";

try {
    let config = parse(`
        invalid =
    `);
} catch (e) {
    print("Parse error: " + e);
    // Parse error: TOML parse error at line 2, col 19: expected value, got newline
}
```

## Supported Escape Sequences

In basic strings (`"..."`):
- `\n` - Newline
- `\t` - Tab
- `\r` - Carriage return
- `\\` - Backslash
- `\"` - Double quote

Literal strings (`'...'`) have no escape sequences - backslashes are literal.

## Limitations

- Date/time values are parsed as strings (no native datetime type)
- Unicode escape sequences (`\uXXXX`) not yet supported
- Some edge cases in TOML 1.0 spec may not be fully supported

## See Also

- [TOML Specification](https://toml.io/)
- [@stdlib/json](./json.md) - JSON parsing and serialization
