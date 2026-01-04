# Objects

Hemlock implements JavaScript-style objects with heap allocation, dynamic fields, methods, and duck typing. Objects are flexible data structures that combine data and behavior.

## Overview

```hemlock
// Anonymous object
let person = { name: "Alice", age: 30, city: "NYC" };
print(person.name);  // "Alice"

// Object with methods
let counter = {
    count: 0,
    increment: fn() {
        self.count = self.count + 1;
    }
};

counter.increment();
print(counter.count);  // 1
```

## Object Literals

### Basic Syntax

```hemlock
let person = {
    name: "Alice",
    age: 30,
    city: "NYC"
};
```

**Syntax:**
- Curly braces `{}` enclose the object
- Key-value pairs separated by commas
- Keys are identifiers (no quotes needed)
- Values can be any type

### Empty Objects

```hemlock
let obj = {};  // Empty object

// Add fields later
obj.name = "Alice";
obj.age = 30;
```

### Nested Objects

```hemlock
let user = {
    info: {
        name: "Bob",
        age: 25
    },
    active: true,
    settings: {
        theme: "dark",
        notifications: true
    }
};

print(user.info.name);           // "Bob"
print(user.settings.theme);      // "dark"
```

### Mixed Value Types

```hemlock
let mixed = {
    number: 42,
    text: "hello",
    flag: true,
    data: null,
    items: [1, 2, 3],
    config: { x: 10, y: 20 }
};
```

### Shorthand Property Syntax

When a variable name matches the property name, use shorthand syntax:

```hemlock
let name = "Alice";
let age = 30;
let active = true;

// Shorthand: { name } is equivalent to { name: name }
let person = { name, age, active };

print(person.name);   // "Alice"
print(person.age);    // 30
print(person.active); // true
```

**Mix shorthand with regular properties:**
```hemlock
let city = "NYC";
let obj = { name, age, city, role: "admin" };
```

### Spread Operator

The spread operator (`...`) copies all fields from one object into another:

```hemlock
let base = { x: 1, y: 2 };
let extended = { ...base, z: 3 };

print(extended.x);  // 1
print(extended.y);  // 2
print(extended.z);  // 3
```

**Override values with spread:**
```hemlock
let defaults = { theme: "light", size: "medium", debug: false };
let custom = { ...defaults, theme: "dark" };

print(custom.theme);  // "dark" (overridden)
print(custom.size);   // "medium" (from defaults)
print(custom.debug);  // false (from defaults)
```

**Multiple spreads (later spreads override earlier):**
```hemlock
let a = { x: 1 };
let b = { y: 2 };
let merged = { ...a, ...b, z: 3 };

print(merged.x);  // 1
print(merged.y);  // 2
print(merged.z);  // 3

// Later spread overrides earlier
let first = { val: "first" };
let second = { val: "second" };
let combined = { ...first, ...second };
print(combined.val);  // "second"
```

**Combine shorthand and spread:**
```hemlock
let status = "active";
let data = { id: 1, name: "Item" };
let full = { ...data, status };

print(full.id);      // 1
print(full.name);    // "Item"
print(full.status);  // "active"
```

**Configuration override pattern:**
```hemlock
let defaultConfig = {
    debug: false,
    timeout: 30,
    retries: 3
};

let prodConfig = { ...defaultConfig, timeout: 60 };
let devConfig = { ...defaultConfig, debug: true };

print(prodConfig.timeout);  // 60
print(devConfig.debug);     // true
```

**Note:** Spread performs a shallow copy. Nested objects share references:
```hemlock
let nested = { inner: { val: 42 } };
let copied = { ...nested };
print(copied.inner.val);  // 42 (same reference as nested.inner)
```

## Field Access

### Dot Notation

```hemlock
let person = { name: "Alice", age: 30 };

// Read field
let name = person.name;      // "Alice"
let age = person.age;        // 30

// Modify field
person.age = 31;
print(person.age);           // 31
```

### Dynamic Field Addition

Add new fields at runtime:

```hemlock
let person = { name: "Alice" };

// Add new field
person.email = "alice@example.com";
person.phone = "555-1234";

print(person.email);  // "alice@example.com"
```

### Field Deletion

**Note:** Field deletion is not currently supported. Set to `null` instead:

```hemlock
let obj = { x: 10, y: 20 };

// Cannot delete fields (not supported)
// obj.x = undefined;  // No 'undefined' in Hemlock

// Workaround: Set to null
obj.x = null;
```

## Methods and `self`

### Defining Methods

Methods are functions stored in object fields:

```hemlock
let counter = {
    count: 0,
    increment: fn() {
        self.count = self.count + 1;
    },
    decrement: fn() {
        self.count = self.count - 1;
    },
    get: fn() {
        return self.count;
    }
};
```

### The `self` Keyword

When a function is called as a method, `self` is automatically bound to the object:

```hemlock
let counter = {
    count: 0,
    increment: fn() {
        self.count = self.count + 1;  // self refers to counter
    }
};

counter.increment();  // self is bound to counter
print(counter.count);  // 1
```

**How it works:**
- Method calls are detected by checking if function expression is property access
- `self` is automatically bound to the object at call time
- `self` is read-only (cannot reassign `self` itself)

### Method Call Detection

```hemlock
let obj = {
    value: 10,
    method: fn() {
        return self.value;
    }
};

// Called as method - self is bound
print(obj.method());  // 10

// Called as function - self is null (error)
let f = obj.method;
print(f());  // ERROR: self is not defined
```

### Methods with Parameters

```hemlock
let calculator = {
    result: 0,
    add: fn(x) {
        self.result = self.result + x;
    },
    multiply: fn(x) {
        self.result = self.result * x;
    },
    get: fn() {
        return self.result;
    }
};

calculator.add(5);
calculator.multiply(2);
print(calculator.get());  // 10
```

## Type Definitions with `define`

### Basic Type Definition

Define object shapes with `define`:

```hemlock
define Person {
    name: string,
    age: i32,
    active: bool,
}

// Create object and assign to typed variable
let p = { name: "Alice", age: 30, active: true };
let typed_p: Person = p;  // Duck typing validates structure

print(typeof(typed_p));  // "Person"
```

**What `define` does:**
- Declares a type with required fields
- Enables duck typing validation
- Sets the object's type name for `typeof()`

### Duck Typing

Objects are validated against `define` using **structural compatibility**:

```hemlock
define Person {
    name: string,
    age: i32,
}

// ✅ OK: Has all required fields
let p1: Person = { name: "Alice", age: 30 };

// ✅ OK: Extra fields are allowed
let p2: Person = {
    name: "Bob",
    age: 25,
    city: "NYC",
    active: true
};

// ❌ ERROR: Missing required field 'age'
let p3: Person = { name: "Carol" };

// ❌ ERROR: Wrong type for 'age'
let p4: Person = { name: "Dave", age: "thirty" };
```

**Duck typing rules:**
- All required fields must be present
- Field types must match
- Extra fields are allowed and preserved
- Validation happens at assignment time

### Optional Fields

Fields can be optional with default values:

```hemlock
define Person {
    name: string,
    age: i32,
    active?: true,       // Optional with default value
    nickname?: string,   // Optional, defaults to null
}

// Object with only required fields
let p = { name: "Alice", age: 30 };
let typed_p: Person = p;

print(typed_p.active);    // true (default applied)
print(typed_p.nickname);  // null (no default)

// Can override optional fields
let p2: Person = { name: "Bob", age: 25, active: false };
print(p2.active);  // false (overridden)
```

**Optional field syntax:**
- `field?: default_value` - Optional with default
- `field?: type` - Optional with type annotation, defaults to null
- Optional fields are added during duck typing if missing

### Type Checking

```hemlock
define Point {
    x: i32,
    y: i32,
}

let p = { x: 10, y: 20 };
let point: Point = p;  // Type checking happens here

print(typeof(point));  // "Point"
print(typeof(p));      // "object" (original is still anonymous)
```

**When type checking happens:**
- At assignment time to typed variable
- Validates all required fields are present
- Validates field types match (with implicit conversions)
- Sets the object's type name

## Method Signatures in Define

Define blocks can specify method signatures, creating interface-like contracts:

### Required Methods

```hemlock
define Comparable {
    value: i32,
    fn compare(other: Self): i32;  // Required method signature
}

// Objects must provide the required method
let a: Comparable = {
    value: 10,
    compare: fn(other) { return self.value - other.value; }
};
```

### Optional Methods

```hemlock
define Serializable {
    fn serialize(): string;       // Required
    fn pretty?(): string;         // Optional method (may be absent)
}
```

### The `Self` Type

`Self` refers to the type being defined, enabling recursive type definitions:

```hemlock
define Cloneable {
    fn clone(): Self;  // Returns same type as the object
}

define Comparable {
    fn compare(other: Self): i32;  // Takes same type as parameter
    fn equals(other: Self): bool;
}

let item: Cloneable = {
    value: 42,
    clone: fn() {
        return { value: self.value, clone: self.clone };
    }
};
```

### Mixed Fields and Methods

```hemlock
define Entity {
    id: i32,
    name: string,
    fn validate(): bool;
    fn serialize(): string;
}

let user: Entity = {
    id: 1,
    name: "Alice",
    validate: fn() { return self.id > 0 && self.name != ""; },
    serialize: fn() { return '{"id":' + self.id + ',"name":"' + self.name + '"}'; }
};
```

## Compound Types (Intersection Types)

Compound types use `&` to require an object to satisfy multiple type definitions:

### Basic Compound Types

```hemlock
define HasName { name: string }
define HasAge { age: i32 }

// Compound type: object must satisfy ALL types
let person: HasName & HasAge = { name: "Alice", age: 30 };
```

### Function Parameters with Compound Types

```hemlock
fn greet(p: HasName & HasAge) {
    print(p.name + " is " + p.age);
}

greet({ name: "Bob", age: 25, city: "NYC" });  // Extra fields OK
```

### Three or More Types

```hemlock
define HasEmail { email: string }

fn describe(p: HasName & HasAge & HasEmail) {
    print(p.name + " <" + p.email + ">");
}
```

### Type Aliases for Compound Types

```hemlock
// Create a named alias for a compound type
type Person = HasName & HasAge;
type Employee = HasName & HasAge & HasEmail;

let emp: Employee = {
    name: "Charlie",
    age: 35,
    email: "charlie@example.com"
};
```

**Duck typing with compounds:** Extra fields are always allowed - the object just needs to have at least the fields required by all component types.

## JSON Serialization

### Serialize to JSON

Convert objects to JSON strings:

```hemlock
// obj.serialize() - Convert object to JSON string
let obj = { x: 10, y: 20, name: "test" };
let json = obj.serialize();
print(json);  // {"x":10,"y":20,"name":"test"}

// Nested objects
let nested = { inner: { a: 1, b: 2 }, outer: 3 };
print(nested.serialize());  // {"inner":{"a":1,"b":2},"outer":3}
```

### Deserialize from JSON

Parse JSON strings back to objects:

```hemlock
// json.deserialize() - Parse JSON string to object
let json_str = '{"x":10,"y":20,"name":"test"}';
let obj = json_str.deserialize();

print(obj.name);   // "test"
print(obj.x);      // 10
```

### Cycle Detection

Circular references are detected and cause errors:

```hemlock
let obj = { x: 10 };
obj.me = obj;  // Create circular reference

obj.serialize();  // ERROR: serialize() detected circular reference
```

### Supported Types

JSON serialization supports:

- **Numbers**: i8-i32, u8-u32, f32, f64
- **Booleans**: true, false
- **Strings**: With escape sequences
- **Null**: null value
- **Objects**: Nested objects
- **Arrays**: Nested arrays

**Not supported:**
- Functions (silently omitted)
- Pointers (error)
- Buffers (error)

### Error Handling

Serialization and deserialization can throw errors:

```hemlock
// Invalid JSON throws an error
try {
    let bad = "not valid json".deserialize();
} catch (e) {
    print("Parse error:", e);
}

// Pointers cannot be serialized
let obj = { ptr: alloc(10) };
try {
    obj.serialize();
} catch (e) {
    print("Serialize error:", e);
}
```

### Round-Trip Example

Complete example of serializing and deserializing:

```hemlock
define Config {
    host: string,
    port: i32,
    debug: bool
}

// Create and serialize
let config: Config = {
    host: "localhost",
    port: 8080,
    debug: true
};
let json = config.serialize();
print(json);  // {"host":"localhost","port":8080,"debug":true}

// Deserialize back
let restored = json.deserialize();
print(restored.host);  // "localhost"
print(restored.port);  // 8080
```

## Built-in Functions

### `typeof(value)`

Returns the type name as a string:

```hemlock
let obj = { x: 10 };
print(typeof(obj));  // "object"

define Person { name: string, age: i32 }
let p: Person = { name: "Alice", age: 30 };
print(typeof(p));    // "Person"
```

**Return values:**
- Anonymous objects: `"object"`
- Typed objects: Custom type name (e.g., `"Person"`)

## Implementation Details

### Memory Model

- **Heap-allocated** - All objects are allocated on the heap
- **Shallow copy** - Assignment copies the reference, not the object
- **Dynamic fields** - Stored as dynamic arrays of name/value pairs
- **Refcounted** - Objects are automatically freed when scope exits

### Reference Semantics

```hemlock
let obj1 = { x: 10 };
let obj2 = obj1;  // Shallow copy (same reference)

obj2.x = 20;
print(obj1.x);  // 20 (both refer to same object)
```

### Method Storage

Methods are just functions stored in fields:

```hemlock
let obj = {
    value: 10,
    method: fn() { return self.value; }
};

// method is a function stored in obj.method
print(typeof(obj.method));  // "function"
```

## Common Patterns

### Pattern: Constructor Function

```hemlock
fn createPerson(name: string, age: i32) {
    return {
        name: name,
        age: age,
        greet: fn() {
            return "Hi, I'm " + self.name;
        }
    };
}

let person = createPerson("Alice", 30);
print(person.greet());  // "Hi, I'm Alice"
```

### Pattern: Object Builder

```hemlock
fn PersonBuilder() {
    return {
        name: null,
        age: null,

        setName: fn(n) {
            self.name = n;
            return self;  // Enable chaining
        },

        setAge: fn(a) {
            self.age = a;
            return self;
        },

        build: fn() {
            return { name: self.name, age: self.age };
        }
    };
}

let person = PersonBuilder()
    .setName("Alice")
    .setAge(30)
    .build();
```

### Pattern: State Object

```hemlock
let state = {
    status: "idle",
    data: null,
    error: null,

    setState: fn(new_status) {
        self.status = new_status;
    },

    setData: fn(new_data) {
        self.data = new_data;
        self.status = "success";
    },

    setError: fn(err) {
        self.error = err;
        self.status = "error";
    }
};
```

### Pattern: Configuration Object

```hemlock
let config = {
    defaults: {
        timeout: 30,
        retries: 3,
        debug: false
    },

    get: fn(key) {
        if (self.defaults[key] != null) {
            return self.defaults[key];
        }
        return null;
    },

    set: fn(key, value) {
        self.defaults[key] = value;
    }
};
```

## Best Practices

1. **Use `define` for structure** - Document expected object shapes
2. **Prefer factory functions** - Create objects with constructors
3. **Keep objects simple** - Don't nest too deeply
4. **Document `self` usage** - Make method behavior clear
5. **Validate on assignment** - Use duck typing to catch errors early
6. **Avoid circular references** - Will cause serialization errors
7. **Use optional fields** - Provide sensible defaults

## Common Pitfalls

### Pitfall: Reference vs. Value

```hemlock
let obj1 = { x: 10 };
let obj2 = obj1;  // Shallow copy

obj2.x = 20;
print(obj1.x);  // 20 (surprise! both changed)

// To avoid: Create new object
let obj3 = { x: obj1.x };  // Deep copy (manual)
```

### Pitfall: `self` in Non-Method Calls

```hemlock
let obj = {
    value: 10,
    method: fn() { return self.value; }
};

// Works: Called as method
print(obj.method());  // 10

// ERROR: Called as function
let f = obj.method;
print(f());  // ERROR: self is not defined
```

### Pitfall: Raw Pointers in Objects

```hemlock
// Objects are auto-freed, but raw pointers inside are NOT
fn create_objects() {
    let obj = { data: alloc(1000) };  // raw ptr needs manual free
    // obj is auto-freed when scope exits, but obj.data leaks!
}

// Solution: Free raw pointers before scope exits
fn safe_create() {
    let obj = { data: alloc(1000) };
    // ... use obj.data ...
    free(obj.data);  // Free the raw pointer explicitly
}  // obj itself is auto-freed
```

### Pitfall: Type Confusion

```hemlock
let obj = { x: 10 };

define Point { x: i32, y: i32 }

// ERROR: Missing required field 'y'
let p: Point = obj;
```

## Examples

### Example: Vector Math

```hemlock
fn createVector(x, y) {
    return {
        x: x,
        y: y,

        add: fn(other) {
            return createVector(
                self.x + other.x,
                self.y + other.y
            );
        },

        length: fn() {
            return sqrt(self.x * self.x + self.y * self.y);
        },

        toString: fn() {
            return "(" + typeof(self.x) + ", " + typeof(self.y) + ")";
        }
    };
}

let v1 = createVector(3, 4);
let v2 = createVector(1, 2);
let v3 = v1.add(v2);

print(v3.toString());  // "(4, 6)"
```

### Example: Simple Database

```hemlock
fn createDatabase() {
    let records = [];
    let next_id = 1;

    return {
        insert: fn(data) {
            let record = { id: next_id, data: data };
            records.push(record);
            next_id = next_id + 1;
            return record.id;
        },

        find: fn(id) {
            let i = 0;
            while (i < records.length) {
                if (records[i].id == id) {
                    return records[i];
                }
                i = i + 1;
            }
            return null;
        },

        count: fn() {
            return records.length;
        }
    };
}

let db = createDatabase();
let id = db.insert({ name: "Alice", age: 30 });
let record = db.find(id);
print(record.data.name);  // "Alice"
```

### Example: Event Emitter

```hemlock
fn createEventEmitter() {
    let listeners = {};

    return {
        on: fn(event, handler) {
            if (listeners[event] == null) {
                listeners[event] = [];
            }
            listeners[event].push(handler);
        },

        emit: fn(event, data) {
            if (listeners[event] != null) {
                let i = 0;
                while (i < listeners[event].length) {
                    listeners[event][i](data);
                    i = i + 1;
                }
            }
        }
    };
}

let emitter = createEventEmitter();

emitter.on("message", fn(data) {
    print("Received: " + data);
});

emitter.emit("message", "Hello!");
```

## Limitations

Current limitations:

- **No deep copy** - Must manually copy nested objects (spread is shallow)
- **No pass-by-value** - Objects always passed by reference
- **No computed properties** - No `{[key]: value}` syntax
- **`self` is read-only** - Cannot reassign `self` in methods
- **No property deletion** - Cannot remove fields once added

**Note:** Objects are refcounted and automatically freed when scope exits. See [Memory Management](memory.md#internal-reference-counting) for details.

## Related Topics

- [Functions](functions.md) - Methods are functions stored in objects
- [Arrays](arrays.md) - Arrays are also object-like
- [Types](types.md) - Duck typing and type definitions
- [Error Handling](error-handling.md) - Throwing error objects

## See Also

- **Duck Typing**: See CLAUDE.md section "Objects" for duck typing details
- **JSON**: See CLAUDE.md for JSON serialization details
- **Memory**: See [Memory](memory.md) for object allocation
