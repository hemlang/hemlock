# Hemlock Language Design Philosophy

> "A small, unsafe language for writing unsafe things safely."

This document captures the core design principles and philosophy of Hemlock. Read this first before making any changes or additions to the language.

---

## Core Identity

Hemlock is a **systems scripting language** that embraces manual memory management and explicit control. It's designed for programmers who want:
- The power of C
- The ergonomics of modern scripting languages
- Structured async concurrency built-in
- No hidden behavior or magic

**Hemlock is NOT:**
- Memory-safe (dangling pointers are your responsibility)
- A replacement for Rust, Go, or Lua
- A language that hides complexity from you

**Hemlock IS:**
- Explicit over implicit, always
- Educational and experimental
- A "C scripting layer" for systems work
- Honest about tradeoffs

---

## Design Principles

### 1. **Explicit Over Implicit**
- Semicolons are mandatory (no ASI)
- No garbage collection
- Manual memory management (alloc/free)
- Type annotations are optional but checked at runtime
- No automatic resource cleanup (no RAII, no defer yet)

**Bad (implicit):**
```hemlock
let x = 5  // Missing semicolon - should error
```

**Good (explicit):**
```hemlock
let x = 5;
free(ptr);  // You allocated it, you free it
```

### 2. **Dynamic by Default, Typed by Choice**
- Every value has a runtime type tag
- Literals infer types intelligently:
  - Small integers (fits in i32): `42` → `i32`
  - Large integers (> i32 range): `9223372036854775807` → `i64`
  - Floats: `3.14` → `f64`
- Optional type annotations enforce runtime checks
- Implicit type conversions follow clear promotion rules

```hemlock
let x = 42;              // i32 inferred (small value)
let y: u8 = 255;         // explicit u8
let z = x + y;           // promotes to i32
let big = 5000000000;    // i64 inferred (> i32 max)
```

### 3. **Unsafe is a Feature, Not a Bug**
- Pointer arithmetic can overflow (user's responsibility)
- No bounds checking on raw `ptr` (use `buffer` if you want safety)
- Double-free crashes are allowed (manual memory management)
- Type system prevents accidents but allows footguns when needed

```hemlock
let p = alloc(10);
let q = p + 100;  // Way past allocation - allowed but dangerous
```

### 4. **Structured Concurrency First-Class**
- `async`/`await` built into the language
- Channels for communication
- `spawn`/`join`/`detach` for task management
- No raw threads, no locks - structured only

### 5. **C-like Syntax, Low Ceremony**
- Familiar to systems programmers
- `{}` blocks always, no optional braces
- Operators match C: `+`, `-`, `*`, `/`, `&&`, `||`, `!`
- Type syntax matches Rust/TypeScript: `let x: type = value;`

---

## Type System

### Numeric Types
- **Signed integers:** `i8`, `i16`, `i32`, `i64`
- **Unsigned integers:** `u8`, `u16`, `u32`, `u64`
- **Floats:** `f32`, `f64`
- **Aliases:** `integer` (i32), `number` (f64), `char` (u8)

### Other Primitives
- **bool:** `true`, `false`
- **string:** UTF-8, mutable, heap-allocated with `.length` property (see [Strings](#strings) section for 15 methods)
- **array:** Dynamic arrays with mixed types, `.length` property (see [Arrays](#arrays) section for 15 methods)
- **ptr:** Raw pointer (8 bytes, no bounds checking)
- **buffer:** Safe wrapper (ptr + length + capacity, bounds checked)
- **null:** The null value

### Type Promotion Rules
When mixing types in operations, promote to the "higher" type:
```
f64 (highest precision)
 ↑
f32
 ↑
u64
 ↑
i64
 ↑
u32
 ↑
i32
 ↑
u16
 ↑
i16
 ↑
u8
 ↑
i8 (lowest)
```

**Examples:**
- `u8 + i32` → `i32` (larger size wins)
- `i32 + i64` → `i64` (larger size wins)
- `u32 + u64` → `u64` (larger size wins)
- `i32 + f32` → `f32` (float always wins)
- `i64 + f64` → `f64` (float always wins)
- `i8 + f64` → `f64` (float + largest wins)

### Range Checking
Type annotations enforce range checks at assignment:
```hemlock
let x: u8 = 255;   // OK
let y: u8 = 256;   // ERROR: out of range
let z: i8 = 128;   // ERROR: max is 127

// 64-bit types
let a: i64 = 2147483647;   // OK
let b: u64 = 4294967295;   // OK
let c: u64 = -1;           // ERROR: u64 cannot be negative
```

### Integer Literal Inference
Integer literals are automatically typed based on their value range:
- Values in i32 range (-2147483648 to 2147483647): infer as `i32`
- Values outside i32 range but within i64 range: infer as `i64`
- Use explicit type annotations for other types (i8, i16, u8, u16, u32, u64)

```hemlock
let small = 42;                    // i32 (fits in i32)
let large = 5000000000;            // i64 (> i32 max)
let max_i64 = 9223372036854775807; // i64 (INT64_MAX)
let explicit: u32 = 100;           // u32 (type annotation overrides)
```

---

## Memory Management

### The Two Pointer Types

**`ptr` - Raw Pointer (Dangerous)**
- Just an address, no tracking
- No bounds checking
- User manages lifetime
- For experts and FFI

```hemlock
let p: ptr = alloc(64);
memset(p, 0, 64);
free(p);  // You must remember to free
```

**`buffer` - Safe Wrapper (Recommended)**
- Pointer + length + capacity
- Bounds checked on access
- Still manual free required
- Better default for most code

```hemlock
let b: buffer = buffer(64);
b[0] = 65;              // bounds checked
print(b.length);        // 64
free(b);                // still manual
```

### Memory API

**Core Allocation:**
- `alloc(bytes)` - allocate raw memory, returns `ptr`
- `free(ptr)` - free memory (works on both `ptr` and `buffer`)
- `buffer(size)` - allocate safe buffer

**Operations:**
- `memset(ptr, byte, size)` - fill memory
- `memcpy(dest, src, size)` - copy memory
- `realloc(ptr, size)` - resize allocation

**Typed Allocation (TODO):**
- `talloc(type, count)` - allocate array of typed values
- `sizeof(type)` - get size of type

---

## Strings

Strings are **first-class mutable sequences** with a rich set of methods for text processing:

```hemlock
let s = "hello";
s[0] = 72;              // mutate to "Hello"
print(s.length);        // 5
let c = s[0];           // returns u8 (byte value)
let msg = s + " world"; // concatenation
```

**Properties:**
- Mutable (unlike Python/JS/Java)
- Indexing returns `u8` (byte value, not substring)
- UTF-8 encoded
- `.length` is a property, not a method
- Heap-allocated with internal capacity tracking

### String Methods

**Substring & Slicing:**
```hemlock
let s = "hello world";
let sub = s.substr(6, 5);       // "world" (start, length)
let slice = s.slice(0, 5);      // "hello" (start, end exclusive)
```

**Search & Find:**
```hemlock
let pos = s.find("world");      // 6 (index of first occurrence)
let pos2 = s.find("foo");       // -1 (not found)
let has = s.contains("world");  // true
```

**Split & Trim:**
```hemlock
let parts = "a,b,c".split(",");  // ["a", "b", "c"]
let clean = "  hello  ".trim();  // "hello"
```

**Case Conversion:**
```hemlock
let upper = s.to_upper();       // "HELLO WORLD"
let lower = s.to_lower();       // "hello world"
```

**Prefix/Suffix:**
```hemlock
let starts = s.starts_with("hello");  // true
let ends = s.ends_with("world");      // true
```

**Replacement:**
```hemlock
let s2 = s.replace("world", "there");      // "hello there"
let s3 = "foo foo".replace_all("foo", "bar"); // "bar bar"
```

**Repetition:**
```hemlock
let repeated = "ha".repeat(3);  // "hahaha"
```

**Character & Byte Access:**
```hemlock
let byte = s.char_at(0);        // 104 (u8)
let buf = s.to_bytes();         // Convert to buffer
```

**Method Chaining:**
```hemlock
let result = "  Hello World  "
    .trim()
    .to_lower()
    .replace("world", "hemlock");  // "hello hemlock"
```

**Available String Methods:**
- `substr(start, length)` - Extract substring by position and length
- `slice(start, end)` - Extract substring by range (end exclusive)
- `find(needle)` - Find first occurrence, returns index or -1
- `contains(needle)` - Check if string contains substring
- `split(delimiter)` - Split into array of strings
- `trim()` - Remove leading/trailing whitespace
- `to_upper()` - Convert to uppercase
- `to_lower()` - Convert to lowercase
- `starts_with(prefix)` - Check if starts with prefix
- `ends_with(suffix)` - Check if ends with suffix
- `replace(old, new)` - Replace first occurrence
- `replace_all(old, new)` - Replace all occurrences
- `repeat(count)` - Repeat string n times
- `char_at(index)` - Get byte value at index (returns u8)
- `to_bytes()` - Convert to buffer for low-level access

---

## Control Flow

### Current Features
- `if`/`else`/`else if` with mandatory braces
- `while` loops
- `for` loops (C-style and for-in iteration)
- `switch` statements with case/default
- `break`/`continue`
- Boolean operators: `&&`, `||`, `!`
- Comparisons: `==`, `!=`, `<`, `>`, `<=`, `>=`

### If Statements

**Basic if/else:**
```hemlock
if (x > 10) {
    print("large");
} else {
    print("small");
}
```

**Else-if chains:**
```hemlock
if (x > 100) {
    print("very large");
} else if (x > 50) {
    print("large");
} else if (x > 10) {
    print("medium");
} else {
    print("small");
}
```

**Notes:**
- Braces are always required for all branches
- Conditions must be enclosed in parentheses
- `else if` is supported for cleaner chaining (syntactic sugar for nested if statements)

### Switch Statements

**Basic switch:**
```hemlock
let x = 2;

switch (x) {
    case 1:
        print("one");
        break;
    case 2:
        print("two");
        break;
    case 3:
        print("three");
        break;
}
```

**Switch with default:**
```hemlock
let color = "blue";

switch (color) {
    case "red":
        print("stop");
        break;
    case "yellow":
        print("slow");
        break;
    case "green":
        print("go");
        break;
    default:
        print("unknown color");
        break;
}
```

**Fall-through behavior:**
```hemlock
let grade = 85;

switch (grade) {
    case 100:
    case 95:
    case 90:
        print("A");
        break;
    case 85:
    case 80:
        print("B");
        break;
    default:
        print("C or below");
        break;
}
```

**Switch with return (in functions):**
```hemlock
fn get_day_name(day: i32): string {
    switch (day) {
        case 1:
            return "Monday";
        case 2:
            return "Tuesday";
        case 3:
            return "Wednesday";
        default:
            return "Unknown";
    }
}
```

**Notes:**
- Supports any value type (integers, strings, booleans, etc.)
- Cases are compared using value equality
- Fall-through is supported (C-style) - cases without `break` continue to next case
- `break` exits the switch statement
- `default` case matches when no other case matches
- `default` can appear anywhere in the switch body
- Expressions are allowed in both switch value and case values

---

## Error Handling

Hemlock supports exception-based error handling with `try`, `catch`, `finally`, and `throw` statements.

### Syntax

**Basic try/catch:**
```hemlock
try {
    // risky code
} catch (e) {
    // handle error, e contains the thrown value
}
```

**Try/finally:**
```hemlock
try {
    // risky code
} finally {
    // always executes, even if exception thrown
}
```

**Try/catch/finally:**
```hemlock
try {
    // risky code
} catch (e) {
    // handle error
} finally {
    // cleanup code
}
```

**Throw statement:**
```hemlock
throw expression;
```

### Semantics

**1. Throw Statement**
- `throw <expr>;` evaluates `<expr>` and throws it as an exception
- Any value can be thrown: strings, numbers, objects, null, etc.
- Execution immediately jumps to the nearest enclosing `catch` block
- If no `catch` exists, propagates up the call stack

```hemlock
throw "file not found";
throw 404;
throw { code: 500, message: "Internal error" };
throw null;
```

**2. Try Block**
- Executes statements sequentially
- If exception thrown, immediately jumps to `catch` (if present) or `finally` (if no catch)
- If no exception, executes `finally` (if present) then continues

**3. Catch Block**
- Receives thrown value in the parameter variable (e.g., `e`)
- Can use the caught value however needed
- Can re-throw: `throw e;`
- Can throw new exception: `throw "different error";`
- Catch parameter is scoped to the catch block only

```hemlock
try {
    throw "oops";
} catch (error) {
    print("Caught: " + error);
    // error only accessible here
}
// error not accessible here
```

**4. Finally Block**
- **Always executes**, regardless of whether exception was thrown
- Runs after try block (if no exception) or after catch block (if exception caught)
- Runs even if try/catch contains `return`, `break`, or `continue`
- If finally block throws, that exception replaces any previous exception

**Execution order example:**
```hemlock
try {
    print("1: try");
    throw "error";
} catch (e) {
    print("2: catch");
} finally {
    print("3: finally");
}
print("4: after");

// Output: 1: try, 2: catch, 3: finally, 4: after
```

**5. Uncaught Exceptions**
- If exception propagates to top of call stack without being caught: **crash**
- Print error message (stack trace to be added)
- Exit with non-zero status code

```hemlock
fn foo() {
    throw "uncaught!";
}

foo();  // Crashes with: Runtime error: uncaught!
```

### Control Flow Interactions

**Return inside try/catch/finally:**
```hemlock
fn example() {
    try {
        return 1;  // ✅ Returns 1 after finally runs
    } finally {
        print("cleanup");
    }
}

fn example2() {
    try {
        return 1;
    } finally {
        return 2;  // ⚠️ Finally return overrides try return - returns 2
    }
}
```

**Rule:** Finally block return values override try/catch return values.

**Break/continue inside try/catch/finally:**
```hemlock
for (let i = 0; i < 10; i = i + 1) {
    try {
        if (i == 5) { break; }  // ✅ Breaks after finally runs
    } finally {
        print("cleanup " + typeof(i));
    }
}
```

**Rule:** Break/continue execute after finally block.

**Nested try/catch:**
```hemlock
try {
    try {
        throw "inner";
    } catch (e) {
        print("Caught: " + e);  // Prints: Caught: inner
        throw "outer";  // Re-throw different error
    }
} catch (e) {
    print("Caught: " + e);  // Prints: Caught: outer
}
```

**Rule:** Nested try/catch blocks work as expected, inner catches happen first.

### Examples

**Basic error handling:**
```hemlock
fn divide(a, b) {
    if (b == 0) {
        throw "division by zero";
    }
    return a / b;
}

try {
    print(divide(10, 0));
} catch (e) {
    print("Error: " + e);  // Prints: Error: division by zero
}
```

**Resource cleanup:**
```hemlock
fn process_file(filename) {
    let file = null;
    try {
        file = open(filename);
        let content = read(file);
        process(content);
    } catch (e) {
        print("Error processing file: " + e);
    } finally {
        if (file != null) {
            close(file);  // Always closes, even on error
        }
    }
}
```

**Re-throwing:**
```hemlock
fn wrapper() {
    try {
        risky_operation();
    } catch (e) {
        print("Logging error: " + e);
        throw e;  // Re-throw to caller
    }
}

try {
    wrapper();
} catch (e) {
    print("Caught in main: " + e);
}
```

### Current Limitations

- No stack trace on uncaught exceptions (planned)
- Some built-in functions still `exit()` instead of throwing (to be updated)
- No custom exception types yet (any value can be thrown)

---

## Functions

Functions are **first-class values** that can be assigned, passed, and returned:

```hemlock
// Named function syntax
fn add(a: i32, b: i32): i32 {
    return a + b;
}

// Anonymous function
let multiply = fn(x, y) {
    return x * y;
};

// Recursion
fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

// Closures
fn makeAdder(x) {
    return fn(y) {
        return x + y;
    };
}

let add5 = makeAdder(5);
print(add5(3));  // 8
```

**Features:**
- **First-class:** Functions can be assigned to variables, passed as arguments, and returned
- **Lexical scoping:** Functions can read (not write) outer scope variables
- **Closures:** Functions capture their defining environment
- **Recursion:** Fully supported (no tail call optimization yet)
- **Type annotations:** Optional for parameters and return type
- **Pass-by-value:** All arguments are copied

**Return semantics:**
- Functions with return type annotation MUST return a value
- Functions without return type annotation implicitly return `null` if no return statement
- `return;` without a value returns `null`

**Type checking:**
- Parameter types are checked at call time if annotated
- Return types are checked when function returns if annotated
- Implicit type conversions follow standard promotion rules

**Named vs Anonymous:**
- `fn name(...) {}` desugars to `let name = fn(...) {};`
- Both forms are equivalent

**Known limitations (v0.1):**
- Closure environments are never freed (memory leak, to be fixed with refcounting in v0.2)
- No pass-by-reference yet (`ref` keyword parsed but not implemented)
- No variadic functions
- No default arguments
- No function overloading
- No tail call optimization

---

## Objects

Hemlock implements JavaScript-style objects with heap allocation, dynamic fields, methods, and duck typing.

### Object Literals
```hemlock
// Anonymous object
let person = { name: "Alice", age: 30, city: "NYC" };
print(person.name);  // "Alice"

// Empty object
let obj = {};

// Nested objects
let user = {
    info: { name: "Bob", age: 25 },
    active: true
};
print(user.info.name);  // "Bob"
```

### Field Access and Assignment
```hemlock
// Access
let x = person.name;

// Modify existing field
person.age = 31;

// Add new field dynamically
person.email = "alice@example.com";
```

### Methods and `self` Keyword
```hemlock
let counter = {
    count: 0,
    increment: fn() {
        self.count = self.count + 1;
    },
    get: fn() {
        return self.count;
    }
};

counter.increment();
counter.increment();
print(counter.get());  // 2
```

**How `self` works:**
- When a function is called as a method (e.g., `obj.method()`), `self` is automatically bound to the object
- `self` is read-only in the current implementation
- Method calls are detected at runtime by checking if the function expression is a property access

### Type Definitions with `define`
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

### Duck Typing
Objects are validated against `define` statements using **structural compatibility** (duck typing):

```hemlock
define Person {
    name: string,
    age: i32,
}

// OK: Has all required fields
let p1: Person = { name: "Alice", age: 30 };

// OK: Extra fields are allowed
let p2: Person = { name: "Bob", age: 25, city: "NYC", active: true };

// ERROR: Missing required field 'age'
let p3: Person = { name: "Carol" };

// ERROR: Wrong type for 'age'
let p4: Person = { name: "Dave", age: "thirty" };
```

**Type checking happens:**
- At assignment time (when assigning to a typed variable)
- Validates all required fields are present
- Validates field types match
- Extra fields are allowed and preserved
- Sets the object's type name for `typeof()`

### Optional Fields
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
- `field?: default_value` - Optional field with default
- `field?: type` - Optional field with type annotation, defaults to null
- Optional fields are added to objects during duck typing if missing

### JSON Serialization
```hemlock
// serialize() - Convert to JSON string
let obj = { x: 10, y: 20, name: "test" };
let json = serialize(obj);
print(json);  // {"x":10,"y":20,"name":"test"}

// Nested objects
let nested = { inner: { a: 1, b: 2 }, outer: 3 };
print(serialize(nested));  // {"inner":{"a":1,"b":2},"outer":3}

// deserialize() - Parse JSON string
let json_str = serialize(obj);
let restored = deserialize(json_str);
print(restored.name);  // "test"
```

**Cycle Detection:**
```hemlock
let obj = { x: 10 };
obj.me = obj;  // Create circular reference
serialize(obj);  // ERROR: serialize() detected circular reference
```

**Supported types in JSON:**
- Numbers (i8-i32, u8-u32, f32, f64)
- Booleans
- Strings (with escape sequences)
- Null
- Objects (nested)
- Not supported: functions, pointers, buffers

### Built-in Functions
- `typeof(value)` - Returns type name string
  - Anonymous objects: `"object"`
  - Typed objects: custom type name (e.g., `"Person"`)
- `serialize(object)` - Convert object to JSON string (with cycle detection)
- `deserialize(json_string)` - Parse JSON string to object

### Implementation Details
- Objects are heap-allocated
- Shallow copy semantics (assignment copies the reference)
- Fields stored as dynamic arrays (name/value pairs)
- Methods are just functions stored in object fields
- Duck typing validates at assignment time
- Type names are stored in objects for `typeof()`

**Current Limitations:**
- No deep copy built-in
- No reference counting (objects are never freed automatically)
- No pass-by-value for objects
- No object spread syntax
- No computed property names
- `self` is read-only (cannot reassign in methods)

---

## Arrays

Hemlock provides **dynamic arrays** with comprehensive methods for data manipulation and processing:

```hemlock
// Array literals
let arr = [1, 2, 3, 4, 5];
print(arr[0]);         // 1
print(arr.length);     // 5

// Mixed types allowed
let mixed = [1, "hello", true, null];
```

**Properties:**
- Dynamic sizing (grow automatically)
- Zero-indexed
- Mixed types allowed
- `.length` property
- Heap-allocated

### Array Methods

**Stack & Queue Operations:**
```hemlock
let arr = [1, 2, 3];
arr.push(4);           // Add to end: [1, 2, 3, 4]
let last = arr.pop();  // Remove from end: 4

let first = arr.shift();   // Remove from start: 1
arr.unshift(0);            // Add to start: [0, 2, 3]
```

**Array Manipulation:**
```hemlock
let arr = [1, 2, 4, 5];
arr.insert(2, 3);      // Insert at index: [1, 2, 3, 4, 5]
let removed = arr.remove(0);  // Remove at index: 1

arr.reverse();         // Reverse in-place: [5, 4, 3, 2]
arr.clear();           // Remove all elements: []
```

**Slicing & Extraction:**
```hemlock
let arr = [1, 2, 3, 4, 5];
let sub = arr.slice(1, 4);   // [2, 3, 4] (end exclusive)
let f = arr.first();         // 1 (without removing)
let l = arr.last();          // 5 (without removing)
```

**Search & Find:**
```hemlock
let arr = [10, 20, 30, 40];
let idx = arr.find(30);      // 2 (index of first occurrence)
let idx2 = arr.find(99);     // -1 (not found)
let has = arr.contains(20);  // true
```

**Array Combination:**
```hemlock
let a = [1, 2, 3];
let b = [4, 5, 6];
let combined = a.concat(b);  // [1, 2, 3, 4, 5, 6] (new array)
```

**String Conversion:**
```hemlock
let words = ["hello", "world", "foo"];
let joined = words.join(" ");  // "hello world foo"

let numbers = [1, 2, 3];
let csv = numbers.join(",");   // "1,2,3"

// Works with mixed types
let mixed = [1, "hello", true, null];
print(mixed.join(" | "));  // "1 | hello | true | null"
```

**Method Chaining:**
```hemlock
let result = [1, 2, 3]
    .concat([4, 5, 6])
    .slice(2, 5);  // [3, 4, 5]

let text = ["apple", "banana", "cherry"]
    .slice(0, 2)
    .join(" and ");  // "apple and banana"
```

**Available Array Methods:**
- `push(value)` - Add element to end
- `pop()` - Remove and return last element
- `shift()` - Remove and return first element
- `unshift(value)` - Add element to beginning
- `insert(index, value)` - Insert element at index
- `remove(index)` - Remove and return element at index
- `find(value)` - Find first occurrence, returns index or -1
- `contains(value)` - Check if array contains value
- `slice(start, end)` - Extract subarray (end exclusive, returns new array)
- `join(delimiter)` - Join elements into string
- `concat(other)` - Concatenate with another array (returns new array)
- `reverse()` - Reverse array in-place
- `first()` - Get first element (without removing)
- `last()` - Get last element (without removing)
- `clear()` - Remove all elements

### Implementation Details
- Arrays are heap-allocated with dynamic capacity
- Automatic growth when capacity exceeded (doubles capacity)
- Value comparison for `find()` and `contains()` works correctly for primitives and strings
- `join()` converts all elements to strings automatically
- Methods like `slice()` and `concat()` return new arrays (no mutation)
- Methods like `reverse()`, `push()`, `insert()` mutate in-place

**Current Limitations:**
- No reference counting (arrays are never freed automatically)
- No bounds checking on direct index access (use methods for safety)
- Comparing objects/arrays in `find()` uses reference equality

---

## Command-Line Arguments

Hemlock programs can access command-line arguments via a built-in **`args` array** that is automatically populated at program startup.

### The `args` Array

```hemlock
// args[0] is always the script filename
// args[1] through args[n-1] are the actual arguments
print(args[0]);        // "script.hml"
print(args.length);    // Total number of arguments (including script name)
```

**Example usage:**
```bash
./hemlock script.hml hello world "test 123"
```

```hemlock
// In script.hml
print("Script name: " + args[0]);     // "script.hml"
print("Total args: " + typeof(args.length));  // "4"
print("First arg: " + args[1]);       // "hello"
print("Second arg: " + args[2]);      // "world"
print("Third arg: " + args[3]);       // "test 123"
```

### Iteration Pattern

```hemlock
// Skip args[0] (script name) and process actual arguments
let i = 1;
while (i < args.length) {
    print("Argument " + typeof(i) + ": " + args[i]);
    i = i + 1;
}
```

### Properties
- **Always present:** `args` is a global array available in all Hemlock programs
- **Script name included:** `args[0]` always contains the script path/name
- **Type:** `args` is an array of strings
- **Minimum length:** Always at least 1 (the script name)
- **REPL behavior:** In the REPL, `args.length` is 0 (empty array)

### Implementation Details
- `args` is registered as a built-in global variable during environment initialization
- Arguments are passed from `main()` through to `register_builtins()`
- Each argument is stored as a string value in the array
- Arguments with spaces are preserved if quoted in the shell

### Common Use Cases

**Simple argument processing:**
```hemlock
if (args.length < 2) {
    print("Usage: " + args[0] + " <filename>");
} else {
    let filename = args[1];
    // ... process file
}
```

**Named arguments (simple pattern):**
```hemlock
let i = 1;
while (i < args.length) {
    if (args[i] == "--verbose") {
        let verbose = true;
    }
    if (args[i] == "--output") {
        i = i + 1;
        let output_file = args[i];
    }
    i = i + 1;
}
```

---

## Async/Concurrency

Hemlock provides **structured concurrency** with async/await syntax, task spawning, and channels for communication. The implementation uses POSIX threads (pthreads) for **TRUE multi-threaded parallelism**.

**What this means:**
- ✅ **Real OS threads** - Each spawned task runs on a separate pthread (POSIX thread)
- ✅ **True parallelism** - Tasks execute simultaneously on multiple CPU cores
- ✅ **Kernel-scheduled** - The OS scheduler distributes tasks across available cores
- ✅ **Thread-safe channels** - Uses pthread mutexes and condition variables for synchronization

**What this is NOT:**
- ❌ **NOT green threads** - Not user-space cooperative multitasking
- ❌ **NOT async/await coroutines** - Not single-threaded event loop like JavaScript/Python asyncio
- ❌ **NOT emulated concurrency** - Not simulated parallelism

This is the **same threading model as C, C++, and Rust** when using OS threads. You get actual parallel execution across multiple cores.

### Async Functions

Functions can be declared as `async` to indicate they're designed for concurrent execution:

```hemlock
async fn compute(n: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
```

**Key points:**
- `async fn` declares an asynchronous function
- Async functions can be spawned as concurrent tasks using `spawn()`
- Async functions can also be called directly (runs synchronously in current thread)
- When spawned, each task runs on its **own OS thread** (not a coroutine!)
- `await` keyword is reserved for future use

### Task Spawning

Use `spawn()` to run async functions **in parallel on separate OS threads**:

```hemlock
async fn factorial(n: i32): i32 {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

// Spawn multiple tasks - these run in PARALLEL on different CPU cores!
let t1 = spawn(factorial, 5);  // Thread 1
let t2 = spawn(factorial, 6);  // Thread 2
let t3 = spawn(factorial, 7);  // Thread 3

// All three are computing simultaneously right now!

// Wait for results
let f5 = join(t1);  // 120
let f6 = join(t2);  // 720
let f7 = join(t3);  // 5040
```

**Built-in functions:**
- `spawn(async_fn, arg1, arg2, ...)` - Create a new task on a new pthread, returns task handle
- `join(task)` - Wait for task completion (blocks until thread finishes), returns result
- `detach(task)` - Fire-and-forget execution (thread runs independently, no join allowed)

### Channels

Channels provide thread-safe communication between tasks:

```hemlock
async fn producer(ch, count: i32) {
    let i = 0;
    while (i < count) {
        ch.send(i * 10);
        i = i + 1;
    }
    ch.close();
    return null;
}

async fn consumer(ch, count: i32): i32 {
    let sum = 0;
    let i = 0;
    while (i < count) {
        let val = ch.recv();
        sum = sum + val;
        i = i + 1;
    }
    return sum;
}

// Create channel with buffer size
let ch = channel(10);

// Spawn producer and consumer
let p = spawn(producer, ch, 5);
let c = spawn(consumer, ch, 5);

// Wait for completion
join(p);
let total = join(c);  // 100 (0+10+20+30+40)
```

**Channel methods:**
- `channel(capacity)` - Create buffered channel
- `ch.send(value)` - Send value (blocks if full)
- `ch.recv()` - Receive value (blocks if empty)
- `ch.close()` - Close channel (recv on closed channel returns null)

### Exception Propagation

Exceptions thrown in spawned tasks are propagated when joined:

```hemlock
async fn risky_operation(should_fail: i32): i32 {
    if (should_fail == 1) {
        throw "Task failed!";
    }
    return 42;
}

let t = spawn(risky_operation, 1);
try {
    let result = join(t);
} catch (e) {
    print("Caught: " + e);  // "Caught: Task failed!"
}
```

### Implementation Details

**Threading Model:**
- **1:1 threading** - Each spawned task creates a dedicated OS thread via `pthread_create()`
- **Kernel-scheduled** - The OS kernel schedules threads across available CPU cores
- **Pre-emptive multitasking** - The OS can interrupt and switch between threads
- **No GIL** - Unlike Python, there's no Global Interpreter Lock limiting parallelism

**Synchronization:**
- **Mutexes** - Channels use `pthread_mutex_t` for thread-safe access
- **Condition variables** - Blocking send/recv use `pthread_cond_t` for efficient waiting
- **Lock-free operations** - Task state transitions are atomic

**Performance Characteristics:**
- **True parallelism** - N spawned tasks can utilize N CPU cores simultaneously
- **Proven speedup** - Stress tests show 8-9x CPU time vs wall time (multiple cores working)
- **Thread overhead** - Each task has ~8KB stack + pthread overhead
- **Blocking I/O safe** - Blocking operations in one task don't block others

**Memory & Cleanup:**
- Joined tasks are automatically cleaned up after `join()` returns
- Detached tasks: pthread is cleaned up by OS, but Task struct (~64-96 bytes) currently leaks
- Channels are reference-counted and freed when no longer used

**Current limitations:**
- No `select()` for multiplexing multiple channels (planned)
- No work-stealing scheduler (uses 1 thread per task, can be inefficient for many short tasks)
- No async I/O integration yet (file/network operations still block)
- Channel capacity is fixed at creation time (no dynamic resizing)

---

## Implementation Details

### Project Structure
```
hemlock/
├── include/              # Public headers
│   ├── ast.h
│   ├── lexer.h
│   ├── parser.h
│   └── interpreter.h
├── src/                  # Implementation
│   ├── ast.c             # AST node constructors and cleanup
│   ├── lexer.c           # Tokenization
│   ├── parser.c          # Parsing (tokens → AST)
│   ├── main.c            # CLI entry point, REPL
│   └── interpreter/      # Interpreter subsystem (modular)
│       ├── internal.h        # Internal API shared between modules
│       ├── environment.c     # Variable scoping (121 lines)
│       ├── values.c          # Value constructors, data structures (394 lines)
│       ├── types.c           # Type system, conversions, duck typing (440 lines)
│       ├── builtins.c        # Builtin functions, registration (955 lines)
│       ├── io.c              # File I/O, serialization (449 lines)
│       └── runtime.c         # eval_expr, eval_stmt, control flow (865 lines)
├── tests/                # Test suite, ran by tests/run_tests.sh
└── examples/             # Example programs
```

**Modular Design Benefits:**
- **Separation of concerns** - Each module has a single, clear responsibility
- **Faster incremental builds** - Only modified modules recompile
- **Easier navigation** - Find features quickly by module name
- **Testable** - Modules can be tested in isolation
- **Scalable** - New features can be added to specific modules without growing monolithic files

### Compilation Pipeline
1. **Lexer** → tokens
2. **Parser** → AST
3. **Interpreter** → tree-walking execution (current)
4. **Compiler** → C code generation (future)

### Current Runtime
- Tree-walking interpreter
- Tagged union for values (`Value` struct)
- Environment-based variable storage
- No optimization yet

### Future Runtime
- Compile to C code
- Keep runtime library for dynamic features
- Optional `--no-tags` flag for fully static builds

---

## Testing Philosophy

- **Test-driven development** for new features
- Comprehensive test suite in `tests/`
- Test both success and error cases
- Run `make test` before committing

Example test structure:
```bash
tests/
├── primitives/       # Type system tests
├── conversions/      # Implicit conversion tests
├── memory/           # Pointer/buffer tests
├── strings/          # String operation tests
├── control/          # Control flow tests
├── functions/        # Function and closure tests
├── objects/          # Object, method, and serialization tests
├── arrays/           # Array operations tests
├── loops/            # For, while, break, continue tests
├── exceptions/       # Try/catch/finally/throw tests
├── io/               # File I/O tests
├── args/             # Command-line argument tests
└── run_tests.sh      # Test runner
```

---

## Common Patterns

### Error Handling
```hemlock
// Use try/catch for recoverable errors
fn safe_divide(a, b) {
    try {
        if (b == 0) {
            throw "division by zero";
        }
        return a / b;
    } catch (e) {
        print("Error: " + e);
        return null;
    }
}

// Use finally for cleanup
fn process_data(filename) {
    let file = null;
    try {
        file = open(filename);
        // ... process file
    } finally {
        if (file != null) {
            close(file);
        }
    }
}
```

### Memory Patterns
```hemlock
// Pattern: Allocate, use, free
let data = alloc(1024);
// ... use data
free(data);

// Pattern: Safe buffer usage
let buf = buffer(256);
let i = 0;
while (i < buf.length) {
    buf[i] = i;
    i = i + 1;
}
free(buf);
```

### Type Patterns
```hemlock
// Pattern: Let types infer when obvious
let count = 0;
let sum = 0.0;

// Pattern: Annotate when precision matters
let byte: u8 = 255;
let precise: f64 = 3.14159265359;
```

---

## What NOT to Do

### ❌ Don't Add Implicit Behavior
```hemlock
// BAD: Automatic semicolon insertion
let x = 5
let y = 10

// BAD: Automatic memory management
let s = "hello"  // String auto-freed at end of scope? NO!

// BAD: Implicit type conversions that lose precision
let x: i32 = 3.14  // Should truncate or error?
```

### ❌ Don't Hide Complexity
```hemlock
// BAD: Magic behind-the-scenes optimization
let arr = [1, 2, 3]  // Is this stack or heap? User should know!

// BAD: Automatic reference counting
let p = create_thing()  // Does this increment a refcount? NO!
```

### ❌ Don't Break Existing Semantics
- Semicolons are mandatory - don't make them optional
- Manual memory management - don't add GC
- Mutable strings - don't make them immutable
- Runtime type checking - don't remove it

---

## Future Considerations

### Maybe Add (Under Discussion)
- `defer` for cleanup (explicit, not automatic)
- `break`/`continue` for loops
- Array/list types (not just buffers)
- Pattern matching
- Error types (`Result<T, E>`)

### Probably Never Add
- Garbage collection
- Automatic memory management
- Implicit type conversions that lose data
- Operator overloading (maybe for user types)
- Macros (too complex)

---

## Philosophy on Safety

**Hemlock's take on safety:**

"We give you the tools to be safe (`buffer`, type annotations, bounds checking) but we don't force you to use them (`ptr`, manual memory, unsafe operations).

The default should guide toward safety, but the escape hatch should always be available."

**Examples:**
- Default to `buffer` in docs, but `ptr` exists for FFI
- Encourage type annotations, but don't require them
- Provide bounds checking, but allow raw pointer math
- Include `sizeof()` and typed allocation to reduce errors

---

## Contributing Guidelines (Future)

When adding features to Hemlock:

1. **Read this document first**
2. **Write tests before implementation**
3. **Keep it explicit** - no magic
4. **Document the unsafe parts** - be honest
5. **Follow existing patterns** - consistency matters
6. **Update CLAUDE.md** - keep this doc current

---

## Version History

- **v0.1** - Primitives, memory management, strings, control flow, functions, closures, recursion, objects, arrays, error handling, file I/O, command-line arguments, async/await, structured concurrency, FFI (current)
  - Type system: i8-i64, u8-u64, f32/f64, bool, string, null, ptr, buffer, array, object, file, task, channel, void
  - **64-bit integer support:** i64 and u64 types with full type promotion, conversion, and FFI support
  - Memory: alloc, free, memset, memcpy, realloc, talloc, sizeof
  - Objects: literals, methods, duck typing, optional fields, serialize/deserialize
  - **Strings:** 15 methods including substr, slice, find, contains, split, trim, to_upper, to_lower, starts_with, ends_with, replace, replace_all, repeat, char_at, to_bytes
  - **Arrays:** 15 methods including push, pop, shift, unshift, insert, remove, find, contains, slice, join, concat, reverse, first, last, clear
  - Control flow: if/else, while, for, for-in, break, continue, switch
  - Error handling: try/catch/finally/throw
  - File I/O: open, read, write, close, seek, tell, file_exists
  - Command-line arguments: built-in `args` array
  - **Async/Concurrency:** async/await syntax, spawn/join/detach, channels with send/recv/close, pthread-based true parallelism, exception propagation
  - **FFI (Foreign Function Interface):** Call C functions from shared libraries using libffi, support for all primitive types, automatic type conversion
  - **Architecture:** Modular interpreter (environment, values, types, builtins, io, runtime, ffi)
  - 221 tests (all tests passing including 10 async/concurrency tests, 3 FFI tests, and 5 i64/u64 tests)
- **v0.2** - Compiler backend, optimization (planned)
- **v0.3** - Advanced features (planned)

---

## Final Thoughts

Hemlock is about **trust and responsibility**. We trust the programmer to:
- Manage memory correctly
- Use types appropriately
- Handle errors properly
- Understand the tradeoffs

In return, Hemlock provides:
- No hidden costs
- No surprise behavior
- Full control when needed
- Safety tools when wanted

**If you're not sure whether a feature fits Hemlock, ask: "Does this give the programmer more explicit control, or does it hide something?"**

If it hides, it probably doesn't belong in Hemlock.
