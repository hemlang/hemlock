# @stdlib/uuid - UUID Generation

The `uuid` module provides functions for generating and working with UUIDs (Universally Unique Identifiers).

## Quick Start

```hemlock
import { v4, v7, is_valid } from "@stdlib/uuid";

// Generate a random UUID (v4)
let id = v4();
print(id);  // e.g., "550e8400-e29b-41d4-a716-446655440000"

// Generate a time-ordered UUID (v7)
let time_id = v7();
print(time_id);

// Validate a UUID
print(is_valid(id));  // true
```

## API Reference

### v4(): string

Generate a UUID v4 (random).

```hemlock
import { v4 } from "@stdlib/uuid";

let id = v4();
print(id);  // "550e8400-e29b-41d4-a716-446655440000"
```

### v7(): string

Generate a UUID v7 (time-ordered, sortable). The first 48 bits encode the Unix timestamp in milliseconds.

```hemlock
import { v7 } from "@stdlib/uuid";

let id1 = v7();
sleep(0.001);  // Wait 1ms
let id2 = v7();

// id2 > id1 when compared lexicographically
print(id1);
print(id2);
```

### v1(): string

Generate a UUID v1 (time-based). Uses random bytes for the node ID since MAC address isn't available.

```hemlock
import { v1 } from "@stdlib/uuid";

let id = v1();
print(id);  // Time-based UUID
```

### parse(uuid): object

Parse a UUID string into its components.

**Returns:** `{ version, variant, bytes, timestamp }`

```hemlock
import { parse, v4, v7 } from "@stdlib/uuid";

let parsed = parse(v4());
print(parsed.version);  // 4
print(parsed.variant);  // "RFC4122"

let parsed7 = parse(v7());
print(parsed7.version);    // 7
print(parsed7.timestamp);  // Unix timestamp in ms
```

### is_valid(uuid): bool

Check if a string is a valid UUID format.

```hemlock
import { is_valid } from "@stdlib/uuid";

print(is_valid("550e8400-e29b-41d4-a716-446655440000"));  // true
print(is_valid("not-a-uuid"));                             // false
print(is_valid("550e8400e29b41d4a716446655440000"));      // false (no hyphens)
```

### compare(a, b): i32

Compare two UUIDs lexicographically (case-insensitive).

**Returns:** -1 if a < b, 0 if equal, 1 if a > b

```hemlock
import { compare } from "@stdlib/uuid";

let a = "550e8400-e29b-41d4-a716-446655440000";
let b = "550e8400-e29b-41d4-a716-446655440001";

print(compare(a, b));  // -1
print(compare(a, a));  // 0
print(compare(b, a));  // 1
```

### equals(a, b): bool

Check if two UUIDs are equal (case-insensitive).

```hemlock
import { equals } from "@stdlib/uuid";

let a = "550e8400-e29b-41d4-a716-446655440000";
let b = "550E8400-E29B-41D4-A716-446655440000";

print(equals(a, b));  // true (case-insensitive)
```

### NIL

The nil UUID (all zeros).

```hemlock
import { NIL, is_nil } from "@stdlib/uuid";

print(NIL);          // "00000000-0000-0000-0000-000000000000"
print(is_nil(NIL));  // true
```

### is_nil(uuid): bool

Check if a UUID is the nil UUID.

### to_upper(uuid): string

Convert UUID to uppercase.

### to_lower(uuid): string

Convert UUID to lowercase.

### short_id(): string

Generate a short 8-character ID (first segment of a v4 UUID).

```hemlock
import { short_id } from "@stdlib/uuid";

let id = short_id();
print(id);  // "550e8400"
```

### compact(): string

Generate a compact UUID (v4 without hyphens).

```hemlock
import { compact } from "@stdlib/uuid";

let id = compact();
print(id);  // "550e8400e29b41d4a716446655440000"
```

## UUID Versions

| Version | Description | Use Case |
|---------|-------------|----------|
| v1 | Time-based with node ID | Legacy systems |
| v4 | Random | Most common, general purpose |
| v7 | Time-ordered random | Databases, sortable IDs |

## Examples

### Generate unique database keys

```hemlock
import { v7 } from "@stdlib/uuid";

// v7 UUIDs are time-ordered, good for database primary keys
let user_id = v7();
let order_id = v7();
```

### Check UUID validity before use

```hemlock
import { is_valid, parse } from "@stdlib/uuid";

fn process_uuid(uuid) {
    if (!is_valid(uuid)) {
        throw "Invalid UUID: " + uuid;
    }

    let parsed = parse(uuid);
    print("UUID version: " + parsed.version);
}
```

### Generate session tokens

```hemlock
import { compact } from "@stdlib/uuid";

// Compact UUIDs work well as tokens
let session_token = compact();
print("Session: " + session_token);
```

## See Also

- [@stdlib/crypto](crypto.md) - Cryptographic functions (used internally)
- [@stdlib/time](time.md) - Time functions (used for v7 timestamps)
