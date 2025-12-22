# JSON Module Documentation

Comprehensive JSON manipulation library for parsing, serialization, formatting, validation, and querying JSON data.

## Import

```hemlock
import { parse, stringify, pretty } from "@stdlib/json";
import * as json from "@stdlib/json";
```

## API Reference

### Core Functions

#### `parse(json_str: string)`

Parse JSON string to value. Wrapper around built-in `.deserialize()` with enhanced error messages.

```hemlock
import { parse } from "@stdlib/json";

let obj = parse('{"x":10,"y":20}');
print(obj.x);  // 10

let arr = parse('[1,2,3]');
print(arr.length);  // 3

// Error handling
try {
    let invalid = parse('{"unclosed"');
} catch (e) {
    print("Parse error: " + e);
}
```

**Parameters:**
- `json_str` (string) - JSON string to parse

**Returns:** Parsed value (object, array, string, number, bool, or null)

**Throws:** Parse error if JSON is malformed

---

#### `stringify(value)`

Serialize value to compact JSON string. Wrapper around built-in `.serialize()`.

```hemlock
import { stringify } from "@stdlib/json";

let json = stringify({ name: "Alice", age: 30 });
print(json);  // {"name":"Alice","age":30}

let arr_json = stringify([1, 2, 3]);
print(arr_json);  // [1,2,3]

// Circular reference detection
let obj = {};
obj.self = obj;
try {
    stringify(obj);
} catch (e) {
    print(e);  // "serialize() detected circular reference"
}
```

**Parameters:**
- `value` - Value to serialize (object, array, primitives)

**Returns:** JSON string (compact format)

**Throws:** Error on circular references

---

#### `parse_file(path: string)`

Read and parse JSON from file.

```hemlock
import { parse_file } from "@stdlib/json";

let config = parse_file("config.json");
print(config.database.host);
```

**Parameters:**
- `path` (string) - Path to JSON file

**Returns:** Parsed value

**Throws:** File I/O errors or parse errors

---

#### `stringify_file(path: string, value)`

Serialize value and write to file (compact format).

```hemlock
import { stringify_file } from "@stdlib/json";

let data = { users: [{ name: "Alice" }, { name: "Bob" }] };
stringify_file("output.json", data);
```

**Parameters:**
- `path` (string) - Output file path
- `value` - Value to serialize

**Returns:** null

**Throws:** File I/O errors or serialization errors

---

### Pretty Printing

#### `pretty(value, indent?)`

Format value as pretty-printed JSON with indentation.

```hemlock
import { pretty } from "@stdlib/json";

let data = { name: "Alice", items: [1, 2, 3] };

// Default 2-space indentation
let formatted = pretty(data);
print(formatted);
/*
{
  "name": "Alice",
  "items": [
    1,
    2,
    3
  ]
}
*/

// Custom 4-space indentation
let formatted4 = pretty(data, 4);

// Tab indentation
let tabbed = pretty(data, "\t");
```

**Parameters:**
- `value` - Value to format
- `indent` (optional) - Number of spaces (i32) or string (e.g., "\t")

**Returns:** Formatted JSON string

**Default:** 2 spaces

---

#### `pretty_file(path: string, value, indent?)`

Pretty-print value to file.

```hemlock
import { pretty_file } from "@stdlib/json";

let config = { server: { port: 8080, host: "localhost" } };
pretty_file("config.json", config, 2);
```

**Parameters:**
- `path` (string) - Output file path
- `value` - Value to format
- `indent` (optional) - Number of spaces or string

**Returns:** null

---

### Path Access

#### `get(obj, path: string, default_val?)`

Get value by path using dot notation.

```hemlock
import { get } from "@stdlib/json";

let doc = {
    user: {
        name: "Alice",
        address: { city: "NYC", zip: 10001 }
    },
    items: [1, 2, 3]
};

// Nested object access
let name = get(doc, "user.name");  // "Alice"
let city = get(doc, "user.address.city");  // "NYC"

// Array access (numeric indices)
let first = get(doc, "items.0");  // 1
let second = get(doc, "items.1");  // 2

// Default value for missing paths
let phone = get(doc, "user.phone", "(none)");  // "(none)"

// Empty path returns root
let root = get(doc, "");  // doc
```

**Parameters:**
- `obj` - Root object to query
- `path` (string) - Dot-separated path (e.g., "user.address.city")
- `default_val` (optional) - Value to return if path doesn't exist

**Returns:** Value at path, or `default_val`, or null

**Path syntax:**
- `"user.name"` - Object property access
- `"items.0"` - Array index access (numeric parts)
- `""` - Empty path returns root object

---

#### `set(obj, path: string, value)`

Set value by path. Modifies object in-place.

```hemlock
import { set } from "@stdlib/json";

let doc = { user: { name: "Alice" }, items: [1, 2, 3] };

// Update nested property
set(doc, "user.name", "Bob");
print(doc.user.name);  // "Bob"

// Update array element
set(doc, "items.1", 99);
print(doc.items[1]);  // 99

// Add new property
set(doc, "user.email", "bob@example.com");
```

**Parameters:**
- `obj` - Root object to modify
- `path` (string) - Dot-separated path
- `value` - New value to set

**Returns:** null

**Throws:** Error if path does not exist (intermediate nodes must exist)

---

#### `has(obj, path: string): bool`

Check if path exists in object.

```hemlock
import { has } from "@stdlib/json";

let doc = { user: { name: "Alice", age: 30 } };

print(has(doc, "user.name"));  // true
print(has(doc, "user.email"));  // false
print(has(doc, "items"));  // false
```

**Parameters:**
- `obj` - Root object
- `path` (string) - Dot-separated path

**Returns:** true if path exists, false otherwise

---

#### `delete(obj, path: string): bool`

Delete a property by path. Supports both object properties and array elements.

```hemlock
import { delete, has } from "@stdlib/json";

let doc = { user: { name: "Alice", email: "alice@example.com" } };

print(delete(doc, "user.email"));  // true
print(has(doc, "user.email"));     // false

// Also works with array indices
let arr = { items: [1, 2, 3] };
delete(arr, "items.1");  // Removes element at index 1
// arr.items is now [1, 3]
```

**Parameters:**
- `obj` - Root object
- `path` (string) - Path to delete

**Returns:** `true` if the property was deleted, `false` if not found

Supports deleting both object properties and array elements (by index).

---

### Validation

#### `is_valid(json_str: string): bool`

Check if string is valid JSON (lightweight check).

```hemlock
import { is_valid } from "@stdlib/json";

print(is_valid('{"x":10}'));  // true
print(is_valid('{"invalid}'));  // false
print(is_valid('[1,2,3]'));  // true
```

**Parameters:**
- `json_str` (string) - JSON string to validate

**Returns:** true if valid, false otherwise

**Note:** Parses the string to validate (not a regex check)

---

#### `validate(json_str: string)`

Validate JSON and return detailed result object.

```hemlock
import { validate } from "@stdlib/json";

let result = validate('{"x":10}');
print(result.valid);  // true

let result2 = validate('{"unclosed"');
print(result2.valid);  // false
print(result2.message);  // Error message
```

**Parameters:**
- `json_str` (string) - JSON string to validate

**Returns:** Object with fields:
- `valid` (bool) - true if valid
- `message` (string) - Error message or "Valid JSON"
- `line` (i32) - Line number of error (-1 if not available)

---

### Type Checking

#### `is_object(value): bool`

Check if value is an object.

```hemlock
import { is_object } from "@stdlib/json";

print(is_object({}));  // true
print(is_object({ x: 10 }));  // true
print(is_object([]));  // false
print(is_object("text"));  // false
```

---

#### `is_array(value): bool`

Check if value is an array.

```hemlock
import { is_array } from "@stdlib/json";

print(is_array([]));  // true
print(is_array([1, 2, 3]));  // true
print(is_array({}));  // false
```

---

#### `is_string(value): bool`, `is_number(value): bool`, `is_bool(value): bool`, `is_null(value): bool`

Check value types.

```hemlock
import { is_string, is_number, is_bool, is_null } from "@stdlib/json";

print(is_string("hello"));  // true
print(is_number(42));  // true
print(is_number(3.14));  // true
print(is_bool(true));  // true
print(is_null(null));  // true
```

---

#### `type_of(value): string`

Get JSON type as string.

```hemlock
import { type_of } from "@stdlib/json";

print(type_of({}));  // "object"
print(type_of([]));  // "array"
print(type_of("text"));  // "string"
print(type_of(42));  // "number"
print(type_of(true));  // "bool"
print(type_of(null));  // "null"
```

**Returns:** Type name: "object", "array", "string", "number", "bool", "null"

---

### Clone & Merge

#### `clone(value)`

Create deep copy of value (independent copy).

```hemlock
import { clone } from "@stdlib/json";

let original = { x: 10, items: [1, 2, 3] };
let copy = clone(original);

copy.x = 20;
copy.items.push(4);

print(original.x);  // 10 (unchanged)
print(original.items.length);  // 3 (unchanged)
```

**Parameters:**
- `value` - Value to clone

**Returns:** Deep copy of value

**Implementation:** Uses serialize/deserialize for reliable deep copy

---

#### `merge(base, update)`

Deep merge objects (combines nested objects).

**Status:** Not yet implemented (requires object iteration builtin)

```hemlock
// Planned API:
let base = { x: 10, nested: { a: 1 } };
let update = { y: 20, nested: { b: 2 } };
let result = merge(base, update);
// { x: 10, y: 20, nested: { a: 1, b: 2 } }
```

---

#### `patch(base, update)`

Shallow merge (replaces nested objects).

**Status:** Not yet implemented (requires object iteration builtin)

---

### Comparison

#### `equals(a, b): bool`

Deep equality check for values.

```hemlock
import { equals } from "@stdlib/json";

let obj1 = { x: 10, items: [1, 2, 3] };
let obj2 = { x: 10, items: [1, 2, 3] };

print(equals(obj1, obj2));  // true

obj2.x = 20;
print(equals(obj1, obj2));  // false

// Array comparison
print(equals([1, 2, 3], [1, 2, 3]));  // true
print(equals([1, 2, 3], [1, 2]));  // false
```

**Parameters:**
- `a`, `b` - Values to compare

**Returns:** true if deeply equal, false otherwise

**Note:** Object comparison not yet implemented (requires object iteration builtin)

---

## Usage Examples

### Configuration Management

```hemlock
import { parse_file, pretty_file, get, set } from "@stdlib/json";

// Load configuration
let config = parse_file("config.json");

// Read settings with defaults
let port = get(config, "server.port", 8080);
let host = get(config, "server.host", "localhost");
let db_pool = get(config, "database.pool_size", 10);

print("Server: " + host + ":" + typeof(port));

// Update settings
set(config, "server.port", 9000);
set(config, "database.pool_size", 20);

// Save with formatting
pretty_file("config.json", config, 2);
```

### API Response Processing

```hemlock
import { parse, get, is_array } from "@stdlib/json";

let response = parse(http_response_body);

// Extract data safely
let users = get(response, "data.users", []);

if (is_array(users)) {
    print("Found " + typeof(users.length) + " users");

    let i = 0;
    while (i < users.length) {
        let name = get(users[i], "name", "Unknown");
        let email = get(users[i], "email", "N/A");
        print(name + " <" + email + ">");
        i = i + 1;
    }
}
```

### Data Transformation

```hemlock
import { parse, stringify, clone } from "@stdlib/json";

let input = parse(input_json);

// Clone to avoid mutating original
let output = clone(input);

// Transform data
set(output, "metadata.processed", true);
set(output, "metadata.timestamp", time());

// Output as JSON
let result_json = stringify(output);
```

### Validation Workflow

```hemlock
import { is_valid, validate, parse } from "@stdlib/json";

fn process_json_input(user_input: string) {
    // Quick validation
    if (!is_valid(user_input)) {
        let result = validate(user_input);
        print("Invalid JSON: " + result.message);
        return null;
    }

    // Parse and process
    let data = parse(user_input);

    // Validate structure
    if (!has(data, "version")) {
        print("Missing required field: version");
        return null;
    }

    return data;
}
```

## Performance Notes

### Fast Operations
- `parse()` / `stringify()` - Built-in C implementation
- `get()` / `set()` - Direct property/array access
- `is_valid()` - Single parse attempt
- Type checks - Simple typeof() calls

### Slower Operations
- `pretty()` - Recursive formatting with string concatenation
- `clone()` - Serialize + deserialize round-trip
- `equals()` - Deep recursive comparison

### Memory Usage
- `clone()` - Creates full independent copy (2x memory)
- `pretty()` - Builds large string in memory
- `parse()` - Allocates objects/arrays on heap

## Error Handling

All JSON operations use Hemlock's exception system:

```hemlock
import { parse, stringify, get, set } from "@stdlib/json";

// Parse errors
try {
    let obj = parse('{"malformed}');
} catch (e) {
    print("Parse error: " + e);
}

// Serialization errors (circular references)
try {
    let obj = {};
    obj.self = obj;
    stringify(obj);
} catch (e) {
    print("Stringify error: " + e);
}

// Path errors
try {
    let obj = { user: { name: "Alice" } };
    set(obj, "nonexistent.path", "value");
} catch (e) {
    print("Path error: " + e);
}
```

## Current Limitations

1. **No object iteration builtin** - `merge()`, `patch()`, and object `equals()` not yet implemented
2. **No line numbers in parse errors** - Validation doesn't report exact error location
3. **No JSON Schema** - Schema validation not yet supported
4. **No streaming** - Large files must fit in memory

## Future Enhancements

Planned additions:
- Object iteration support (enables merge/patch/equals)
- JSON Schema validation
- JSON Patch (RFC 6902)
- JSON Pointer (RFC 6901)
- Streaming parser for large files
- JSON5 support (comments, trailing commas)

## See Also

- Built-in `serialize()` method (CLAUDE.md - Objects section)
- Built-in `deserialize()` method (CLAUDE.md - Strings section)
- `@stdlib/fs` - File operations
- `@stdlib/http` - HTTP requests with JSON
