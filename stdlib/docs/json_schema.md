# JSON Schema Module Documentation

Validates that values match expected shapes defined as JSON Schema objects. Built as a pure Hemlock module on top of `@stdlib/json`. Useful for validating LLM structured output, API responses, configuration files, and user input.

Supports a practical subset of JSON Schema Draft 7: type checking, number/string/array/object constraints, composition (`allOf`/`anyOf`/`oneOf`/`not`), conditional schemas (`if`/`then`/`else`), `enum`, and `const`.

## Import

```hemlock
import { validate, is_valid } from "@stdlib/json_schema";

// Builder helpers
import {
    string_type, number_type, integer_type, boolean_type, null_type,
    array_type, object_type, nullable, enum_type, const_type,
    any_of, one_of, all_of, not_schema
} from "@stdlib/json_schema";

// Utilities
import { validate_json, format_errors } from "@stdlib/json_schema";
```

## Quick Start

```hemlock
import { validate, is_valid, object_type, string_type, integer_type } from "@stdlib/json_schema";

// Define a schema
let user_schema = object_type(
    {
        name: string_type({ minLength: 1 }),
        age: integer_type({ minimum: 0, maximum: 150 })
    },
    ["name", "age"]  // required fields
);

// Validate data
let user = { name: "Alice", age: 30 };
assert(is_valid(user, user_schema));  // true

// Get detailed errors
let bad = { name: "", age: -1 };
let result = validate(bad, user_schema);
// result.valid == false
// result.errors contains details about each violation
```

## API Reference

### Core Functions

#### `validate(value, schema)`

Validate a value against a JSON Schema. Returns a result object with all errors.

```hemlock
let schema = { type: "string", minLength: 3 };
let result = validate("hi", schema);
print(result.valid);         // false
print(result.errors.length); // 1
print(result.errors[0].path);    // ""
print(result.errors[0].message); // "string length 2 < minLength 3"
```

**Parameters:**
- `value` - Any Hemlock value to validate
- `schema` - JSON Schema object (or boolean)

**Returns:** `{ valid: bool, errors: array }`

Each error object has:
- `path` (string) - Dot-notation path to the failing value (e.g., `"users[1].name"`)
- `message` (string) - Human-readable description of the error
- `schema_path` (string) - Path within the schema that triggered the error

---

#### `is_valid(value, schema): bool`

Check if a value matches a schema. Returns only a boolean, no error details.

```hemlock
if (is_valid(data, schema)) {
    process(data);
} else {
    print("Invalid data");
}
```

---

#### `validate_json(json_str: string, schema)`

Parse a JSON string and validate the result against a schema. Returns a parse error if the string is not valid JSON.

```hemlock
let result = validate_json("{\"name\":\"Alice\"}", schema);
if (!result.valid) {
    for (err in result.errors) {
        print(err.message);
    }
}

// Invalid JSON
let bad = validate_json("{bad json", schema);
// bad.errors[0].message contains "invalid JSON: ..."
```

---

#### `format_errors(errors: array): string`

Format an array of error objects into a human-readable multi-line string.

```hemlock
let result = validate(data, schema);
if (!result.valid) {
    print(format_errors(result.errors));
}
// Output:
// name: expected type "string", got integer
// age: value -1 < minimum 0
```

---

### Schema Builder Helpers

Builder functions create schema objects with less boilerplate. They return plain objects that can be further modified.

#### `string_type(opts?)`

```hemlock
string_type()                           // { type: "string" }
string_type({ minLength: 1 })           // { type: "string", minLength: 1 }
string_type({ maxLength: 100 })         // { type: "string", maxLength: 100 }
string_type({ minLength: 3, maxLength: 50 })
```

**Options:** `minLength`, `maxLength`, `enum`

---

#### `number_type(opts?)`

```hemlock
number_type()                           // { type: "number" }
number_type({ minimum: 0 })             // { type: "number", minimum: 0 }
number_type({ minimum: 0, maximum: 1 }) // range [0, 1]
```

**Options:** `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf`

---

#### `integer_type(opts?)`

Same as `number_type` but requires integer values (no fractional part).

```hemlock
integer_type()                    // { type: "integer" }
integer_type({ minimum: 1 })     // positive integers
```

---

#### `boolean_type()`

```hemlock
boolean_type()  // { type: "boolean" }
```

---

#### `null_type()`

```hemlock
null_type()  // { type: "null" }
```

---

#### `array_type(item_schema?, opts?)`

```hemlock
array_type()                                     // { type: "array" }
array_type(string_type())                        // array of strings
array_type(integer_type(), { minItems: 1 })      // non-empty array of integers
array_type(null, { maxItems: 10 })               // any array, max 10 items
array_type(string_type(), { uniqueItems: true }) // unique strings
```

**Options:** `minItems`, `maxItems`, `uniqueItems`

---

#### `object_type(properties?, required?, opts?)`

```hemlock
// Simple object
object_type()

// Object with typed properties
object_type({
    name: string_type(),
    age: integer_type()
})

// With required fields
object_type(
    { name: string_type(), age: integer_type() },
    ["name"]
)

// Strict (no extra properties allowed)
object_type(
    { x: number_type(), y: number_type() },
    ["x", "y"],
    { additional: false }
)
```

**Options:** `additional` (or `additionalProperties`), `minProperties`, `maxProperties`

---

#### `nullable(schema)`

Wraps a schema to also accept `null`.

```hemlock
nullable(string_type())   // accepts string or null
nullable(integer_type())  // accepts integer or null
```

---

#### `enum_type(values: array)`

Value must be one of the listed values.

```hemlock
enum_type(["red", "green", "blue"])
enum_type([1, 2, 3])
enum_type(["active", "inactive", null])
```

---

#### `const_type(value)`

Value must exactly equal the given value.

```hemlock
const_type("fixed")
const_type(42)
const_type(true)
```

---

#### `any_of(schemas: array)`

Value must match at least one of the given schemas.

```hemlock
any_of([string_type(), integer_type()])
any_of([
    object_type({ kind: const_type("a") }, ["kind"]),
    object_type({ kind: const_type("b") }, ["kind"])
])
```

---

#### `one_of(schemas: array)`

Value must match exactly one of the given schemas.

```hemlock
one_of([
    { type: "integer", minimum: 0 },
    { type: "string", minLength: 1 }
])
```

---

#### `all_of(schemas: array)`

Value must match all of the given schemas.

```hemlock
all_of([
    { type: "object", required: ["name"] },
    { type: "object", required: ["email"] }
])
```

---

#### `not_schema(schema)`

Value must NOT match the given schema.

```hemlock
not_schema(null_type())     // anything except null
not_schema(string_type())   // anything except string
```

---

## Schema Language Reference

Schemas are plain Hemlock objects. You can use builder helpers or write them directly.

### Type Checking

```hemlock
{ type: "string" }
{ type: "number" }     // any numeric type
{ type: "integer" }    // integers only (no fractional part)
{ type: "boolean" }
{ type: "null" }
{ type: "array" }
{ type: "object" }

// Union types
{ type: ["string", "null"] }          // nullable string
{ type: ["string", "number"] }        // string or number
```

### Number Constraints

```hemlock
{ type: "number", minimum: 0 }                 // >= 0
{ type: "number", maximum: 100 }                // <= 100
{ type: "number", exclusiveMinimum: 0 }         // > 0
{ type: "number", exclusiveMaximum: 100 }       // < 100
{ type: "integer", multipleOf: 5 }              // 0, 5, 10, ...
{ type: "number", minimum: 0, maximum: 1 }      // range [0, 1]
```

### String Constraints

```hemlock
{ type: "string", minLength: 1 }               // non-empty
{ type: "string", maxLength: 255 }              // bounded
{ type: "string", minLength: 3, maxLength: 50 } // range
```

### Array Constraints

```hemlock
{ type: "array", items: { type: "string" } }              // array of strings
{ type: "array", minItems: 1 }                             // non-empty
{ type: "array", maxItems: 10 }                            // bounded
{ type: "array", uniqueItems: true }                       // no duplicates
{ type: "array", items: { type: "integer" }, minItems: 1 } // combined
```

### Object Constraints

```hemlock
// Required fields
{ type: "object", required: ["name", "email"] }

// Property schemas
{
    type: "object",
    properties: {
        name: { type: "string" },
        age: { type: "integer", minimum: 0 }
    }
}

// No extra properties allowed
{
    type: "object",
    properties: { x: { type: "number" } },
    additionalProperties: false
}

// Extra properties must match a schema
{
    type: "object",
    properties: { name: { type: "string" } },
    additionalProperties: { type: "integer" }
}

// Object size constraints
{ type: "object", minProperties: 1, maxProperties: 10 }
```

### Enum and Const

```hemlock
// Must be one of listed values
{ enum: ["draft", "published", "archived"] }

// Must be exactly this value
{ const: "v1" }
```

### Composition

```hemlock
// Must match at least one schema
{ anyOf: [{ type: "string" }, { type: "null" }] }

// Must match exactly one schema
{ oneOf: [{ type: "string" }, { type: "integer" }] }

// Must match all schemas
{ allOf: [
    { required: ["name"] },
    { required: ["email"] }
]}

// Must NOT match the schema
{ not: { type: "null" } }
```

### Conditional (if/then/else)

Note: `if`, `then`, `else` are reserved keywords in Hemlock, so schemas using these must be built via `parse()` from `@stdlib/json`.

```hemlock
import { parse } from "@stdlib/json";

let schema = parse("{\"if\":{\"properties\":{\"kind\":{\"const\":\"email\"}},\"required\":[\"kind\"]},\"then\":{\"required\":[\"address\"]},\"else\":{\"required\":[\"phone\"]}}");
```

### Boolean Schemas

```hemlock
true   // allows any value
false  // rejects all values
```

---

## Use Cases

### Validating LLM Structured Output

```hemlock
import { validate, format_errors, object_type, string_type, integer_type, array_type, enum_type } from "@stdlib/json_schema";
import { parse } from "@stdlib/json";

// Define expected response shape
let response_schema = object_type(
    {
        model: string_type(),
        choices: array_type(
            object_type(
                {
                    index: integer_type({ minimum: 0 }),
                    message: object_type(
                        {
                            role: enum_type(["assistant", "system", "user"]),
                            content: string_type({ minLength: 1 })
                        },
                        ["role", "content"]
                    ),
                    finish_reason: enum_type(["stop", "length", "content_filter"])
                },
                ["index", "message", "finish_reason"]
            ),
            { minItems: 1 }
        )
    },
    ["model", "choices"]
);

// Validate LLM response
let response_json = get_llm_response();
let result = validate(parse(response_json), response_schema);

if (!result.valid) {
    print("LLM response doesn't match expected shape:");
    print(format_errors(result.errors));
}
```

### Validating Tool Call Arguments

```hemlock
import { validate_json, object_type, string_type, number_type, nullable } from "@stdlib/json_schema";

let search_args_schema = object_type(
    {
        query: string_type({ minLength: 1, maxLength: 500 }),
        max_results: integer_type({ minimum: 1, maximum: 100 }),
        language: nullable(string_type())
    },
    ["query"]
);

fn handle_tool_call(name: string, args_json: string) {
    if (name == "search") {
        let result = validate_json(args_json, search_args_schema);
        if (!result.valid) {
            throw "Invalid search arguments: " + format_errors(result.errors);
        }
        let args = parse(args_json);
        // ... perform search
    }
}
```

### Validating Configuration

```hemlock
import { validate, format_errors, object_type, string_type, integer_type, boolean_type, enum_type } from "@stdlib/json_schema";
import { parse_file } from "@stdlib/json";

let config_schema = object_type(
    {
        server: object_type(
            {
                host: string_type(),
                port: integer_type({ minimum: 1, maximum: 65535 })
            },
            ["host", "port"]
        ),
        database: object_type(
            {
                url: string_type({ minLength: 1 }),
                pool_size: integer_type({ minimum: 1, maximum: 100 })
            },
            ["url"]
        ),
        log_level: enum_type(["debug", "info", "warn", "error"])
    },
    ["server"]
);

let config = parse_file("config.json");
let result = validate(config, config_schema);
if (!result.valid) {
    eprint("Configuration errors:");
    eprint(format_errors(result.errors));
}
```

---

## Error Path Reporting

Errors include dot-notation paths showing exactly where validation failed:

```hemlock
let schema = object_type({
    users: array_type(object_type(
        { name: string_type(), age: integer_type() },
        ["name"]
    ))
});

let data = { users: [{ name: "Alice" }, { age: 30 }, { name: 42 }] };
let result = validate(data, schema);
// Errors:
// users[1]: missing required property "name"
// users[2].name: expected type "string", got integer
```

---

## Supported Schema Keywords

| Keyword | Applies to | Description |
|---------|-----------|-------------|
| `type` | any | Type or array of types |
| `enum` | any | Value must be one of listed values |
| `const` | any | Value must exactly equal |
| `minimum` | number | `>=` bound |
| `maximum` | number | `<=` bound |
| `exclusiveMinimum` | number | `>` bound |
| `exclusiveMaximum` | number | `<` bound |
| `multipleOf` | number | Must be divisible by |
| `minLength` | string | Minimum character count |
| `maxLength` | string | Maximum character count |
| `items` | array | Schema for each element |
| `minItems` | array | Minimum element count |
| `maxItems` | array | Maximum element count |
| `uniqueItems` | array | No duplicate elements |
| `properties` | object | Schema per property |
| `required` | object | Required property names |
| `additionalProperties` | object | `false` or schema for extra properties |
| `minProperties` | object | Minimum property count |
| `maxProperties` | object | Maximum property count |
| `allOf` | any | Must match all schemas |
| `anyOf` | any | Must match at least one |
| `oneOf` | any | Must match exactly one |
| `not` | any | Must NOT match |
| `if`/`then`/`else` | any | Conditional validation |

## Not Supported

- `$ref` / `$id` / `$defs` (schema references)
- `pattern` (string regex - would require `@stdlib/regex`)
- `format` (semantic string formats like "email", "uri")
- `contains` / `prefixItems` (array keywords)
- `patternProperties` / `propertyNames` (object keywords)
- `dependentRequired` / `dependentSchemas`

## See Also

- `@stdlib/json` - JSON parsing, serialization, path access
- JSON Schema specification: https://json-schema.org/
