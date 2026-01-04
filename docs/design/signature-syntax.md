# Signature Syntax Design

> Extending Hemlock's type system with function types, nullable modifiers, type aliases, const parameters, and method signatures.

**Status:** Implemented (v1.7.0)
**Version:** 1.0
**Author:** Claude

---

## Overview

This document proposes five interconnected type system extensions that build on Hemlock's existing infrastructure:

1. **Function Type Annotations** - First-class function types
2. **Nullable Type Modifiers** - Explicit null handling (extends existing `nullable` flag)
3. **Type Aliases** - Named type abbreviations
4. **Const Parameters** - Immutability contracts
5. **Method Signatures in Define** - Interface-like behavior

These features share the philosophy: **explicit over implicit, optional but enforced when used**.

---

## 1. Function Type Annotations

### Motivation

Currently, there's no way to express a function's signature as a type:

```hemlock
// Current: callback has no type information
fn map(arr: array, callback) { ... }

// Proposed: explicit function type
fn map(arr: array, callback: fn(any, i32): any): array { ... }
```

### Syntax

```hemlock
// Basic function type
fn(i32, i32): i32

// With parameter names (documentation only, not enforced)
fn(a: i32, b: i32): i32

// No return value (void)
fn(string): void
fn(string)              // Shorthand: omit `: void`

// Nullable return
fn(i32): string?

// Optional parameters
fn(name: string, age?: i32): void

// Rest parameters
fn(...args: array): i32

// No parameters
fn(): bool

// Higher-order: function returning function
fn(i32): fn(i32): i32

// Async function type
async fn(i32): i32
```

### Usage Examples

```hemlock
// Variable with function type
let add: fn(i32, i32): i32 = fn(a, b) { return a + b; };

// Function parameter
fn apply(f: fn(i32): i32, x: i32): i32 {
    return f(x);
}

// Return type is function
fn make_adder(n: i32): fn(i32): i32 {
    return fn(x) { return x + n; };
}

// Array of functions
let ops: array<fn(i32, i32): i32> = [add, subtract, multiply];

// Object field
define EventHandler {
    name: string;
    callback: fn(Event): void;
}
```

### AST Changes

```c
// In TypeKind enum (include/ast.h)
typedef enum {
    // ... existing types ...
    TYPE_FUNCTION,      // NEW: Function type
} TypeKind;

// In Type struct (include/ast.h)
struct Type {
    TypeKind kind;
    // ... existing fields ...

    // For TYPE_FUNCTION:
    struct Type **param_types;      // Parameter types
    char **param_names;             // Optional parameter names (docs)
    int *param_optional;            // Which params are optional
    int num_params;
    char *rest_param_name;          // Rest parameter name or NULL
    struct Type *rest_param_type;   // Rest parameter type
    struct Type *return_type;       // Return type (NULL = void)
    int is_async;                   // async fn type
};
```

### Parsing

Function types begin with `fn` (or `async fn`) followed by parameter list:

```
function_type := ["async"] "fn" "(" [param_type_list] ")" [":" type]
param_type_list := param_type ("," param_type)*
param_type := [identifier ":"] ["?"] type | "..." [identifier] [":" type]
```

**Disambiguation:** When parsing a type and `fn` is encountered:
- If followed by `(`, it's a function type
- Otherwise, syntax error (bare `fn` is not a valid type)

### Type Compatibility

```hemlock
// Exact match required for function types
let f: fn(i32): i32 = fn(x: i32): i32 { return x; };  // OK

// Parameter contravariance (accepting broader types is OK)
let g: fn(any): i32 = fn(x: i32): i32 { return x; };  // OK: i32 <: any

// Return covariance (returning narrower types is OK)
let h: fn(i32): any = fn(x: i32): i32 { return x; };  // OK: i32 <: any

// Arity must match
let bad: fn(i32): i32 = fn(a, b) { return a; };       // ERROR: arity mismatch

// Optional parameters compatible with required
let opt: fn(i32, i32?): i32 = fn(a, b?: 0) { return a + b; };  // OK
```

---

## 2. Nullable Type Modifiers

### Motivation

The `?` suffix makes null-acceptance explicit in signatures:

```hemlock
// Current: unclear if null is valid
fn find(arr: array, val: any): i32 { ... }

// Proposed: explicit nullable return
fn find(arr: array, val: any): i32? { ... }
```

### Syntax

```hemlock
// Nullable types with ? suffix
string?           // string or null
i32?              // i32 or null
User?             // User or null
array<i32>?       // array or null
fn(i32): i32?     // function returning i32 or null

// Composing with function types
fn(string?): i32          // Accepts string or null
fn(string): i32?          // Returns i32 or null
fn(string?): i32?         // Both nullable

// In define
define Result {
    value: any?;
    error: string?;
}
```

### Implementation Notes

**Already exists:** The `Type.nullable` flag is already in the AST. This feature primarily needs:
1. Parser support for `?` suffix on any type (verify/extend)
2. Proper composition with function types
3. Runtime enforcement

### Type Compatibility

```hemlock
// Non-nullable assignable to nullable
let x: i32? = 42;           // OK
let y: i32? = null;         // OK

// Nullable NOT assignable to non-nullable
let z: i32 = x;             // ERROR: x might be null

// Null coalescing to unwrap
let z: i32 = x ?? 0;        // OK: ?? provides default

// Optional chaining returns nullable
let name: string? = user?.name;
```

---

## 3. Type Aliases

### Motivation

Complex types benefit from named abbreviations:

```hemlock
// Current: repetitive compound types
fn process(entity: HasName & HasId & HasTimestamp) { ... }
fn validate(entity: HasName & HasId & HasTimestamp) { ... }

// Proposed: named alias
type Entity = HasName & HasId & HasTimestamp;
fn process(entity: Entity) { ... }
fn validate(entity: Entity) { ... }
```

### Syntax

```hemlock
// Basic alias
type Integer = i32;
type Text = string;

// Compound type alias
type Entity = HasName & HasId;
type Auditable = HasCreatedAt & HasUpdatedAt & HasCreatedBy;

// Function type alias
type Callback = fn(Event): void;
type Predicate = fn(any): bool;
type Reducer = fn(acc: any, val: any): any;
type AsyncTask = async fn(): any;

// Nullable alias
type OptionalString = string?;

// Generic alias (if we support generic type aliases)
type Pair<T> = { first: T, second: T };
type Result<T, E> = { value: T?, error: E? };

// Array type alias
type IntArray = array<i32>;
type Matrix = array<array<f64>>;
```

### Scope and Visibility

```hemlock
// Module-scoped by default
type Callback = fn(Event): void;

// Exportable
export type Handler = fn(Request): Response;

// In another file
import { Handler } from "./handlers.hml";
fn register(h: Handler) { ... }
```

### AST Changes

```c
// New statement kind
typedef enum {
    // ... existing statements ...
    STMT_TYPE_ALIAS,    // NEW
} StmtKind;

// In Stmt union
struct {
    char *name;                 // Alias name
    char **type_params;         // Generic params: <T, U>
    int num_type_params;
    Type *aliased_type;         // The actual type
} type_alias;
```

### Parsing

```
type_alias := "type" identifier ["<" type_params ">"] "=" type ";"
```

**Note:** `type` is a new keyword. Check for conflicts with existing identifiers.

### Resolution

Type aliases are resolved at:
- **Parse time:** Alias recorded in type environment
- **Check time:** Alias expanded to underlying type
- **Runtime:** Alias is transparent (same as underlying type)

```hemlock
type MyInt = i32;
let x: MyInt = 42;
typeof(x);           // "i32" (not "MyInt")
```

---

## 4. Const Parameters

### Motivation

Signal immutability intent in function signatures:

```hemlock
// Current: unclear if array will be modified
fn print_all(items: array) { ... }

// Proposed: explicit immutability contract
fn print_all(const items: array) { ... }
```

### Syntax

```hemlock
// Const parameter
fn process(const data: buffer) {
    // data[0] = 0;        // ERROR: cannot mutate const
    let x = data[0];       // OK: reading allowed
    return x;
}

// Multiple const params
fn compare(const a: array, const b: array): bool { ... }

// Mixed const and mutable
fn update(const source: array, target: array) {
    for (item in source) {
        target.push(item);   // OK: target is mutable
    }
}

// Const with type inference
fn log(const msg) {
    print(msg);
}

// Const in function types
type Reader = fn(const buffer): i32;
```

### What Const Prevents

```hemlock
fn bad(const arr: array) {
    arr.push(1);         // ERROR: mutating method
    arr.pop();           // ERROR: mutating method
    arr[0] = 5;          // ERROR: index assignment
    arr.clear();         // ERROR: mutating method
}

fn ok(const arr: array) {
    let x = arr[0];      // OK: reading
    let len = len(arr);  // OK: length check
    let copy = arr.slice(0, 10);  // OK: creates new array
    for (item in arr) {  // OK: iteration
        print(item);
    }
}
```

### Mutating vs Non-Mutating Methods

| Type | Mutating (blocked by const) | Non-Mutating (allowed) |
|------|----------------------------|------------------------|
| array | push, pop, shift, unshift, insert, remove, clear, reverse (in-place) | slice, concat, map, filter, find, contains, first, last, join |
| string | index assignment (`s[0] = 'x'`) | all methods (return new strings) |
| buffer | index assignment, memset, memcpy (to) | index read, slice |
| object | field assignment | field read |

### AST Changes

```c
// In function expression (include/ast.h)
struct {
    // ... existing fields ...
    int *param_is_const;    // NEW: 1 if const, 0 otherwise
} function;

// In Type struct for function types
struct Type {
    // ... existing fields ...
    int *param_is_const;    // For TYPE_FUNCTION
};
```

### Enforcement

**Interpreter:**
- Track const-ness in variable bindings
- Check before mutation operations
- Runtime error on const violation

**Compiler:**
- Emit const-qualified C variables where beneficial
- Static analysis for const violations
- Warning/error at compile time

---

## 5. Method Signatures in Define

### Motivation

Allow `define` blocks to specify expected methods, not just data fields:

```hemlock
// Current: only data fields
define User {
    name: string;
    age: i32;
}

// Proposed: method signatures
define Comparable {
    fn compare(other: Self): i32;
}

define Serializable {
    fn serialize(): string;
    fn deserialize(data: string): Self;  // Static method
}
```

### Syntax

```hemlock
// Method signature (no body)
define Hashable {
    fn hash(): i32;
}

// Multiple methods
define Collection {
    fn size(): i32;
    fn is_empty(): bool;
    fn contains(item: any): bool;
}

// Mixed fields and methods
define Entity {
    id: i32;
    name: string;
    fn validate(): bool;
    fn serialize(): string;
}

// Using Self type
define Cloneable {
    fn clone(): Self;
}

define Comparable {
    fn compare(other: Self): i32;
    fn equals(other: Self): bool;
}

// Optional methods
define Printable {
    fn to_string(): string;
    fn debug_string?(): string;  // Optional method (may be absent)
}

// Methods with default implementations
define Ordered {
    fn compare(other: Self): i32;  // Required

    // Default implementations (inherited if not overridden)
    fn less_than(other: Self): bool {
        return self.compare(other) < 0;
    }
    fn greater_than(other: Self): bool {
        return self.compare(other) > 0;
    }
    fn equals(other: Self): bool {
        return self.compare(other) == 0;
    }
}
```

### The `Self` Type

`Self` refers to the concrete type implementing the interface:

```hemlock
define Addable {
    fn add(other: Self): Self;
}

// When used:
let a: Addable = {
    value: 10,
    add: fn(other) {
        return { value: self.value + other.value, add: self.add };
    }
};
```

### Structural Typing (Duck Typing)

Method signatures use the same duck typing as fields:

```hemlock
define Stringifiable {
    fn to_string(): string;
}

// Any object with to_string() method satisfies Stringifiable
let x: Stringifiable = {
    name: "test",
    to_string: fn() { return self.name; }
};

// Compound types with methods
define Named { name: string; }
define Printable { fn to_string(): string; }

type NamedPrintable = Named & Printable;

let y: NamedPrintable = {
    name: "Alice",
    to_string: fn() { return "Name: " + self.name; }
};
```

### AST Changes

```c
// Extend define_object in Stmt union
struct {
    char *name;
    char **type_params;
    int num_type_params;

    // Fields (existing)
    char **field_names;
    Type **field_types;
    int *field_optional;
    Expr **field_defaults;
    int num_fields;

    // Methods (NEW)
    char **method_names;
    Type **method_types;        // TYPE_FUNCTION
    int *method_optional;       // Optional methods (fn name?(): type)
    Expr **method_defaults;     // Default implementations (NULL if signature only)
    int num_methods;
} define_object;
```

### Type Checking

When checking `value: InterfaceType`:
1. Check all required fields exist with compatible types
2. Check all required methods exist with compatible signatures
3. Optional fields/methods may be absent

```hemlock
define Sortable {
    fn compare(other: Self): i32;
}

// Valid: has compare method
let valid: Sortable = {
    value: 10,
    compare: fn(other) { return self.value - other.value; }
};

// Invalid: missing compare
let invalid: Sortable = { value: 10 };  // ERROR: missing method 'compare'

// Invalid: wrong signature
let wrong: Sortable = {
    compare: fn() { return 0; }  // ERROR: expected (Self): i32
};
```

---

## Interaction Examples

### Combining All Features

```hemlock
// Type alias for complex function type
type EventCallback = fn(event: Event, context: Context?): bool;

// Type alias for compound interface
type Entity = HasId & HasName & Serializable;

// Define with method signatures
define Repository<T> {
    fn find(id: i32): T?;
    fn save(const entity: T): bool;
    fn delete(id: i32): bool;
    fn find_all(predicate: fn(T): bool): array<T>;
}

// Using it all together
fn create_user_repo(): Repository<User> {
    let users: array<User> = [];

    return {
        find: fn(id) {
            for (u in users) {
                if (u.id == id) { return u; }
            }
            return null;
        },
        save: fn(const entity) {
            users.push(entity);
            return true;
        },
        delete: fn(id) {
            // ...
            return true;
        },
        find_all: fn(predicate) {
            return users.filter(predicate);
        }
    };
}
```

### Callbacks with Explicit Types

```hemlock
type ClickHandler = fn(event: MouseEvent): void;
type KeyHandler = fn(event: KeyEvent, modifiers: i32): bool;

define Widget {
    x: i32;
    y: i32;
    on_click: ClickHandler?;
    on_key: KeyHandler?;
}

fn create_button(label: string, handler: ClickHandler): Widget {
    return {
        x: 0, y: 0,
        on_click: handler,
        on_key: null
    };
}
```

### Nullable Function Types

```hemlock
// Optional callback
fn fetch(url: string, on_complete: fn(Response): void?): void {
    let response = http_get(url);
    if (on_complete != null) {
        on_complete(response);
    }
}

// Nullable return from function type
type Parser = fn(input: string): AST?;

fn try_parse(parsers: array<Parser>, input: string): AST? {
    for (p in parsers) {
        let result = p(input);
        if (result != null) {
            return result;
        }
    }
    return null;
}
```

---

## Implementation Roadmap

### Phase 1: Core Infrastructure
1. Add `TYPE_FUNCTION` to TypeKind enum
2. Extend Type struct with function type fields
3. Add `CHECKED_FUNCTION` to compiler type checker
4. Add `Self` type support (TYPE_SELF)

### Phase 2: Parsing
1. Implement `parse_function_type()` in parser
2. Handle `fn(...)` in type position
3. Add `type` keyword and `STMT_TYPE_ALIAS` parsing
4. Add `const` parameter modifier parsing
5. Extend define parsing for method signatures

### Phase 3: Type Checking
1. Function type compatibility rules
2. Type alias resolution and expansion
3. Const parameter mutation checking
4. Method signature validation in define types
5. Self type resolution

### Phase 4: Runtime
1. Function type validation at call sites
2. Const violation detection
3. Type alias transparency

### Phase 5: Parity Tests
1. Function type annotation tests
2. Nullable composition tests
3. Type alias tests
4. Const parameter tests
5. Method signature tests

---

## Design Decisions

### 1. Generic Type Aliases: **YES**

Type aliases support generic parameters:

```hemlock
// Generic type aliases
type Pair<T> = { first: T, second: T };
type Result<T, E> = { value: T?, error: E? };
type Mapper<T, U> = fn(T): U;
type AsyncResult<T> = async fn(): T?;

// Usage
let coords: Pair<f64> = { first: 3.14, second: 2.71 };
let result: Result<User, string> = { value: user, error: null };
let transform: Mapper<i32, string> = fn(n) { return n.to_string(); };
```

### 2. Const Propagation: **DEEP**

Const parameters are fully immutable - no mutation through any path:

```hemlock
fn process(const arr: array<object>) {
    arr.push({});        // ERROR: cannot mutate const array
    arr[0] = {};         // ERROR: cannot mutate const array
    arr[0].x = 5;        // ERROR: cannot mutate through const (DEEP)

    let x = arr[0].x;    // OK: reading is fine
    let copy = arr[0];   // OK: creates a copy
    copy.x = 5;          // OK: copy is not const
}

fn nested(const obj: object) {
    obj.user.name = "x"; // ERROR: deep const prevents nested mutation
    obj.items[0] = 1;    // ERROR: deep const prevents nested mutation
}
```

**Rationale:** Deep const provides stronger guarantees and is more useful for
ensuring data integrity. If you need to mutate nested data, make a copy first.

### 3. Self in Standalone Type Aliases: **NO**

`Self` is only valid inside `define` blocks where it has clear meaning:

```hemlock
// Valid: Self refers to the defined type
define Comparable {
    fn compare(other: Self): i32;
}

// Invalid: Self has no meaning here
type Cloner = fn(Self): Self;  // ERROR: Self outside define context

// Instead, use generics:
type Cloner<T> = fn(T): T;
```

### 4. Method Default Implementations: **YES (Simple Only)**

Allow default implementations for simple/utility methods:

```hemlock
define Comparable {
    // Required: must be implemented
    fn compare(other: Self): i32;

    // Default implementations (simple convenience methods)
    fn equals(other: Self): bool {
        return self.compare(other) == 0;
    }
    fn less_than(other: Self): bool {
        return self.compare(other) < 0;
    }
    fn greater_than(other: Self): bool {
        return self.compare(other) > 0;
    }
}

define Printable {
    fn to_string(): string;

    // Default: delegates to required method
    fn print() {
        print(self.to_string());
    }
    fn println() {
        print(self.to_string() + "\n");
    }
}

// Object only needs to implement required methods
let item: Comparable = {
    value: 42,
    compare: fn(other) { return self.value - other.value; }
    // equals, less_than, greater_than are inherited from defaults
};

item.less_than({ value: 50, compare: item.compare });  // true
```

**Guidelines for defaults:**
- Keep them simple (1-3 lines)
- Should delegate to required methods
- No complex logic or side effects
- Primitives and straightforward compositions only

### 5. Variance: **INFERRED (No Explicit Annotations)**

Variance is inferred from how type parameters are used:

```hemlock
// Variance is automatic based on position
type Producer<T> = fn(): T;           // T in return = covariant
type Consumer<T> = fn(T): void;       // T in param = contravariant
type Transformer<T> = fn(T): T;       // T in both = invariant

// Example: Dog <: Animal (Dog is subtype of Animal)
let dog_producer: Producer<Dog> = fn() { return new_dog(); };
let animal_producer: Producer<Animal> = dog_producer;  // OK: covariant

let animal_consumer: Consumer<Animal> = fn(a) { print(a); };
let dog_consumer: Consumer<Dog> = animal_consumer;     // OK: contravariant
```

**Why infer?**
- Less boilerplate (`<out T>` / `<in T>` adds noise)
- Follows "explicit over implicit" - the position IS explicit
- Matches how most languages handle function type variance
- Errors are clear when variance rules are violated

---

## Appendix: Grammar Changes

```ebnf
(* Types *)
type := simple_type | compound_type | function_type
simple_type := base_type ["?"] | identifier ["<" type_args ">"] ["?"]
compound_type := simple_type ("&" simple_type)+
function_type := ["async"] "fn" "(" [param_types] ")" [":" type]

base_type := "i8" | "i16" | "i32" | "i64"
           | "u8" | "u16" | "u32" | "u64"
           | "f32" | "f64" | "bool" | "string" | "rune"
           | "ptr" | "buffer" | "void" | "null"
           | "array" ["<" type ">"]
           | "object"
           | "Self"

param_types := param_type ("," param_type)*
param_type := ["const"] [identifier ":"] ["?"] type
            | "..." [identifier] [":" type]

type_args := type ("," type)*

(* Statements *)
type_alias := "type" identifier ["<" type_params ">"] "=" type ";"

define_stmt := "define" identifier ["<" type_params ">"] "{" define_members "}"
define_members := (field_def | method_def)*
field_def := identifier (":" type ["=" expr] | "?:" (type | expr)) ";"?
method_def := "fn" identifier ["?"] "(" [param_types] ")" [":" type] (block | ";")
            (* "?" marks optional method, block provides default implementation *)

(* Parameters *)
param := ["const"] ["ref"] identifier [":" type] ["?:" expr]
       | "..." identifier [":" type]
```
