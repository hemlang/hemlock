# @stdlib/yaml - YAML Parser and Serializer

Provides parsing and serialization for YAML (YAML Ain't Markup Language), a human-friendly data serialization format commonly used for configuration files.

## Import

```hemlock
import { parse, stringify, parse_file, write_file } from "@stdlib/yaml";
import { parse_all } from "@stdlib/yaml";
import { get, set } from "@stdlib/yaml";
```

## Core Functions

### parse(input: string): any

Parse a YAML string into a value. Returns the first document.

```hemlock
let config = parse(`
name: My App
version: 1
database:
  host: localhost
  port: 5432
`);

print(config["name"]);             // "My App"
print(config["database"]["port"]); // 5432
```

### parse_all(input: string): array

Parse a multi-document YAML string. Returns an array of documents.

```hemlock
let docs = parse_all(`
---
name: doc1
---
name: doc2
`);

print(docs.length);       // 2
print(docs[0]["name"]);   // "doc1"
print(docs[1]["name"]);   // "doc2"
```

### stringify(value, indent?: 2): string

Convert a value to a YAML string.

```hemlock
let config = {
    name: "My App",
    debug: true,
    database: {
        host: "localhost",
        port: 5432
    }
};

let yaml = stringify(config);
// name: My App
// debug: true
// database:
//   host: localhost
//   port: 5432
```

Custom indentation:

```hemlock
let yaml = stringify(config, 4);  // 4-space indent
```

### parse_file(path: string): any

Parse a YAML file.

```hemlock
let config = parse_file("config.yaml");
print(config["server"]["host"]);
```

### write_file(path: string, value)

Write a value to a YAML file.

```hemlock
let config = { name: "app", version: 1 };
write_file("config.yaml", config);
```

## Helper Functions

### get(obj, path: string): any

Get a value using a dotted key path. Supports array indexing.

```hemlock
let config = parse(`
database:
  host: localhost
  connection:
    timeout: 30
servers:
  - name: alpha
  - name: beta
`);

print(get(config, "database.host"));               // "localhost"
print(get(config, "database.connection.timeout"));  // 30
print(get(config, "servers.0.name"));               // "alpha"
```

### set(obj, path: string, value)

Set a value using a dotted key path.

```hemlock
let config = {};
set(config, "database.host", "localhost");
set(config, "database.port", 5432);

print(stringify(config));
// database:
//   host: localhost
//   port: 5432
```

## YAML Features Supported

### Scalars

```yaml
# Strings
plain: hello world
double: "escaped \n string"
single: 'no escapes except '''

# Numbers
integer: 42
negative: -17
float: 3.14
hex: 0xFF
octal: 0o77

# Booleans
enabled: true
disabled: false

# Null
empty: null
tilde: ~
```

### Mappings

```yaml
# Block mapping
name: Alice
age: 30

# Nested
database:
  host: localhost
  port: 5432

# Flow mapping
point: {x: 1, y: 2}
```

### Sequences

```yaml
# Block sequence
colors:
  - red
  - green
  - blue

# Flow sequence
ports: [8001, 8002, 8003]

# Nested sequences
matrix:
  - [1, 2, 3]
  - [4, 5, 6]
```

### Block Scalars

```yaml
# Literal (preserves newlines)
description: |
  This is a long
  multiline string.

# Folded (joins lines with spaces)
summary: >
  This is a long
  folded string.

# Chomping indicators
strip: |-
  no trailing newline
keep: |+
  trailing newlines kept

```

### Multi-Document

```yaml
---
name: first
---
name: second
...
```

### Comments

```yaml
# Full line comment
name: value  # Inline comment
```

### Quoted Strings

```yaml
double: "supports \n escapes"
single: 'only '' escape'
```

## Escape Sequences (Double-Quoted)

- `\n` - Newline
- `\t` - Tab
- `\r` - Carriage return
- `\\` - Backslash
- `\"` - Double quote
- `\/` - Forward slash
- `\0` - Null
- `\a` - Bell
- `\b` - Backspace
- `\f` - Form feed
- `\v` - Vertical tab
- `\e` - Escape
- `\xNN` - Hex byte
- `\uXXXX` - Unicode (4-digit)
- `\UXXXXXXXX` - Unicode (8-digit)

## Examples

### Configuration File

```hemlock
import { parse_file, get } from "@stdlib/yaml";

let config = parse_file("app.yaml");

let db_host = get(config, "database.host");
let db_port = get(config, "database.port");
let log_level = get(config, "logging.level");

print("Connecting to " + db_host + ":" + db_port);
```

### Docker Compose Style

```hemlock
import { parse } from "@stdlib/yaml";

let compose = parse(`
version: "3"
services:
  web:
    image: nginx
    ports:
      - "80:80"
  db:
    image: postgres
    environment:
      POSTGRES_PASSWORD: secret
`);

let services = compose["services"].keys();
for (svc in services) {
    print("Service: " + svc + " -> " + compose["services"][svc]["image"]);
}
```

### CI/CD Pipeline

```hemlock
import { parse } from "@stdlib/yaml";

let pipeline = parse(`
stages:
  - build
  - test
  - deploy
jobs:
  build:
    stage: build
    script:
      - make build
  test:
    stage: test
    script:
      - make test
`);

for (stage in pipeline["stages"]) {
    print("Stage: " + stage);
}
```

## Limitations

- Anchors (`&`) and aliases (`*`) are not supported
- Tags (`!!str`, `!!int`, etc.) are not supported
- Complex mapping keys (multi-line keys) are not supported
- Merge keys (`<<`) are not supported

## See Also

- [YAML Specification](https://yaml.org/spec/1.2.2/)
- [@stdlib/toml](./toml.md) - TOML parsing and serialization
- [@stdlib/json](./json.md) - JSON parsing and serialization
