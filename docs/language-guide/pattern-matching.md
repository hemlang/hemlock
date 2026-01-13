# Pattern Matching

Hemlock provides powerful pattern matching through `match` expressions, offering a concise way to destructure values, check types, and handle multiple cases.

## Basic Syntax

```hemlock
let result = match (value) {
    pattern1 => expression1,
    pattern2 => expression2,
    _ => default_expression
};
```

Match expressions evaluate `value` against each pattern in order, returning the result of the first matching arm's expression.

## Pattern Types

### Literal Patterns

Match against exact values:

```hemlock
let x = 42;
let msg = match (x) {
    0 => "zero",
    1 => "one",
    42 => "the answer",
    _ => "other"
};
print(msg);  // "the answer"
```

Supported literals:
- **Integers**: `0`, `42`, `-5`
- **Floats**: `3.14`, `-0.5`
- **Strings**: `"hello"`, `"world"`
- **Booleans**: `true`, `false`
- **Null**: `null`

### Wildcard Pattern (`_`)

Matches any value without binding:

```hemlock
let x = "anything";
let result = match (x) {
    "specific" => "found it",
    _ => "wildcard matched"
};
```

### Variable Binding Patterns

Bind the matched value to a variable:

```hemlock
let x = 100;
let result = match (x) {
    0 => "zero",
    n => "value is " + n  // n binds to 100
};
print(result);  // "value is 100"
```

### OR Patterns (`|`)

Match multiple alternatives:

```hemlock
let x = 2;
let size = match (x) {
    1 | 2 | 3 => "small",
    4 | 5 | 6 => "medium",
    _ => "large"
};

// Works with strings too
let cmd = "quit";
let action = match (cmd) {
    "exit" | "quit" | "q" => "exiting",
    "help" | "h" | "?" => "showing help",
    _ => "unknown"
};
```

### Guard Expressions (`if`)

Add conditions to patterns:

```hemlock
let x = 15;
let category = match (x) {
    n if n < 0 => "negative",
    n if n == 0 => "zero",
    n if n < 10 => "small",
    n if n < 100 => "medium",
    n => "large: " + n
};
print(category);  // "medium"

// Complex guards
let y = 12;
let result = match (y) {
    n if n % 2 == 0 && n > 10 => "even and greater than 10",
    n if n % 2 == 0 => "even",
    n => "odd"
};
```

### Type Patterns

Check and bind based on type:

```hemlock
let val = 42;
let desc = match (val) {
    num: i32 => "integer: " + num,
    str: string => "string: " + str,
    flag: bool => "boolean: " + flag,
    _ => "other type"
};
print(desc);  // "integer: 42"
```

Supported types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`, `string`, `array`, `object`

## Destructuring Patterns

### Object Destructuring

Extract fields from objects:

```hemlock
let point = { x: 10, y: 20 };
let result = match (point) {
    { x, y } => "point at " + x + "," + y
};
print(result);  // "point at 10,20"

// With literal field values
let origin = { x: 0, y: 0 };
let name = match (origin) {
    { x: 0, y: 0 } => "origin",
    { x: 0, y } => "on y-axis at " + y,
    { x, y: 0 } => "on x-axis at " + x,
    { x, y } => "point at " + x + "," + y
};
print(name);  // "origin"
```

### Array Destructuring

Match array structure and elements:

```hemlock
let arr = [1, 2, 3];
let desc = match (arr) {
    [] => "empty",
    [x] => "single: " + x,
    [x, y] => "pair: " + x + "," + y,
    [x, y, z] => "triple: " + x + "," + y + "," + z,
    _ => "many elements"
};
print(desc);  // "triple: 1,2,3"

// With literal values
let pair = [1, 2];
let result = match (pair) {
    [0, 0] => "both zero",
    [1, x] => "starts with 1, second is " + x,
    [x, 1] => "ends with 1",
    _ => "other"
};
print(result);  // "starts with 1, second is 2"
```

### Array Rest Patterns (`...`)

Capture remaining elements:

```hemlock
let nums = [1, 2, 3, 4, 5];

// Head and tail
let result = match (nums) {
    [first, ...rest] => "first: " + first,
    [] => "empty"
};
print(result);  // "first: 1"

// First two elements
let result2 = match (nums) {
    [a, b, ...rest] => "first two: " + a + "," + b,
    _ => "too short"
};
print(result2);  // "first two: 1,2"
```

### Nested Destructuring

Combine patterns for complex data:

```hemlock
let user = {
    name: "Alice",
    address: { city: "NYC", zip: 10001 }
};

let result = match (user) {
    { name, address: { city, zip } } => name + " lives in " + city,
    _ => "unknown"
};
print(result);  // "Alice lives in NYC"

// Object containing array
let data = { items: [1, 2, 3], count: 3 };
let result2 = match (data) {
    { items: [first, ...rest], count } => "first: " + first + ", total: " + count,
    _ => "no items"
};
print(result2);  // "first: 1, total: 3"
```

## Match as Expression

Match is an expression that returns a value:

```hemlock
// Direct assignment
let grade = 85;
let letter = match (grade) {
    n if n >= 90 => "A",
    n if n >= 80 => "B",
    n if n >= 70 => "C",
    n if n >= 60 => "D",
    _ => "F"
};

// In string concatenation
let msg = "Grade: " + match (grade) {
    n if n >= 70 => "passing",
    _ => "failing"
};

// In function return
fn classify(n: i32): string {
    return match (n) {
        0 => "zero",
        n if n > 0 => "positive",
        _ => "negative"
    };
}
```

## Pattern Matching Best Practices

1. **Order matters**: Patterns are checked top-to-bottom; put specific patterns before general ones
2. **Use wildcards for exhaustiveness**: Always include a `_` fallback unless you're certain all cases are covered
3. **Prefer guards over nested conditions**: Guards make intent clearer
4. **Use destructuring over manual field access**: More concise and safer

```hemlock
// Good: Guards for range checking
match (score) {
    n if n >= 90 => "A",
    n if n >= 80 => "B",
    _ => "below B"
}

// Good: Destructure instead of accessing fields
match (point) {
    { x: 0, y: 0 } => "origin",
    { x, y } => "at " + x + "," + y
}

// Avoid: Overly complex nested patterns
// Instead, consider breaking into multiple matches or using guards
```

## Comparison with Other Languages

| Feature | Hemlock | Rust | JavaScript |
|---------|---------|------|------------|
| Basic matching | `match (x) { ... }` | `match x { ... }` | `switch (x) { ... }` |
| Destructuring | Yes | Yes | Partial (switch doesn't destructure) |
| Guards | `n if n > 0 =>` | `n if n > 0 =>` | N/A |
| OR patterns | `1 \| 2 \| 3 =>` | `1 \| 2 \| 3 =>` | `case 1: case 2: case 3:` |
| Rest patterns | `[a, ...rest]` | `[a, rest @ ..]` | N/A |
| Type patterns | `n: i32` | Type via `match` arm | N/A |
| Returns value | Yes | Yes | No (statement) |

## Implementation Notes

Pattern matching is implemented in both the interpreter and compiler backends with full parity - both produce identical results for the same input. The feature is available in Hemlock v1.8.0+.
