# Functions

Functions in Hemlock are **first-class values** that can be assigned to variables, passed as arguments, and returned from other functions. This guide covers function syntax, closures, recursion, and advanced patterns.

## Overview

```hemlock
// Named function syntax
fn add(a: i32, b: i32): i32 {
    return a + b;
}

// Anonymous function
let multiply = fn(x, y) {
    return x * y;
};

// Closures
fn makeAdder(x) {
    return fn(y) {
        return x + y;
    };
}

let add5 = makeAdder(5);
print(add5(3));  // 8
```

## Function Declaration

### Named Functions

```hemlock
fn greet(name: string): string {
    return "Hello, " + name;
}

let msg = greet("Alice");  // "Hello, Alice"
```

**Components:**
- `fn` - Function keyword
- `greet` - Function name
- `(name: string)` - Parameters with optional types
- `: string` - Optional return type
- `{ ... }` - Function body

### Anonymous Functions

Functions without names, assigned to variables:

```hemlock
let square = fn(x) {
    return x * x;
};

print(square(5));  // 25
```

**Named vs. Anonymous:**
```hemlock
// These are equivalent:
fn add(a, b) { return a + b; }

let add = fn(a, b) { return a + b; };
```

**Note:** Named functions desugar to variable assignments with anonymous functions.

## Parameters

### Basic Parameters

```hemlock
fn example(a, b, c) {
    return a + b + c;
}

let result = example(1, 2, 3);  // 6
```

### Type Annotations

Optional type annotations on parameters:

```hemlock
fn add(a: i32, b: i32): i32 {
    return a + b;
}

add(5, 10);      // OK
add(5, 10.5);    // Runtime type check promotes to f64
```

**Type checking:**
- Parameter types are checked at call time if annotated
- Implicit type conversions follow standard promotion rules
- Type mismatches cause runtime errors

### Pass-by-Value

All arguments are **copied** (pass-by-value):

```hemlock
fn modify(x) {
    x = 100;  // Only modifies local copy
}

let a = 10;
modify(a);
print(a);  // Still 10 (unchanged)
```

**Note:** Objects and arrays are passed by reference (the reference is copied), so their contents can be modified:

```hemlock
fn modify_array(arr) {
    arr[0] = 99;  // Modifies original array
}

let a = [1, 2, 3];
modify_array(a);
print(a[0]);  // 99 (modified)
```

## Return Values

### Return Statement

```hemlock
fn get_max(a: i32, b: i32): i32 {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}
```

### Return Type Annotations

Optional type annotation for return value:

```hemlock
fn calculate(): f64 {
    return 3.14159;
}

fn get_name(): string {
    return "Alice";
}
```

**Type checking:**
- Return types are checked when function returns (if annotated)
- Type conversions follow standard promotion rules

### Implicit Return

Functions without return type annotation implicitly return `null`:

```hemlock
fn print_message(msg) {
    print(msg);
    // Implicitly returns null
}

let result = print_message("hello");  // result is null
```

### Early Return

```hemlock
fn find_first_negative(arr) {
    for (let i = 0; i < arr.length; i = i + 1) {
        if (arr[i] < 0) {
            return i;  // Early exit
        }
    }
    return -1;  // Not found
}
```

### Return Without Value

`return;` without a value returns `null`:

```hemlock
fn maybe_process(value) {
    if (value < 0) {
        return;  // Returns null
    }
    return value * 2;
}
```

## First-Class Functions

Functions can be assigned, passed, and returned like any other value.

### Functions as Variables

```hemlock
let operation = fn(x, y) { return x + y; };

print(operation(5, 3));  // 8

// Reassign
operation = fn(x, y) { return x * y; };
print(operation(5, 3));  // 15
```

### Functions as Arguments

```hemlock
fn apply(f, x) {
    return f(x);
}

fn double(n) {
    return n * 2;
}

let result = apply(double, 5);  // 10
```

### Functions as Return Values

```hemlock
fn get_operation(op: string) {
    if (op == "add") {
        return fn(a, b) { return a + b; };
    } else if (op == "multiply") {
        return fn(a, b) { return a * b; };
    } else {
        return fn(a, b) { return 0; };
    }
}

let add = get_operation("add");
print(add(5, 3));  // 8
```

## Closures

Functions capture their defining environment (lexical scoping).

### Basic Closures

```hemlock
fn makeCounter() {
    let count = 0;
    return fn() {
        count = count + 1;
        return count;
    };
}

let counter = makeCounter();
print(counter());  // 1
print(counter());  // 2
print(counter());  // 3
```

**How it works:**
- Inner function captures `count` from outer scope
- `count` persists across calls to the returned function
- Each call to `makeCounter()` creates a new closure with its own `count`

### Closure with Parameters

```hemlock
fn makeAdder(x) {
    return fn(y) {
        return x + y;
    };
}

let add5 = makeAdder(5);
let add10 = makeAdder(10);

print(add5(3));   // 8
print(add10(3));  // 13
```

### Multiple Closures

```hemlock
fn makeOperations(x) {
    let add = fn(y) { return x + y; };
    let multiply = fn(y) { return x * y; };

    return { add: add, multiply: multiply };
}

let ops = makeOperations(5);
print(ops.add(3));       // 8
print(ops.multiply(3));  // 15
```

### Lexical Scoping

Functions can access outer scope variables through lexical scoping:

```hemlock
let global = 10;

fn outer() {
    let outer_var = 20;

    fn inner() {
        // Can read global and outer_var
        print(global);      // 10
        print(outer_var);   // 20
    }

    inner();
}

outer();
```

Closures capture variables by reference, allowing both reading and mutation of outer scope variables (as shown in the `makeCounter` example above).

## Recursion

Functions can call themselves.

### Basic Recursion

```hemlock
fn factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

print(factorial(5));  // 120
```

### Mutual Recursion

Functions can call each other:

```hemlock
fn is_even(n: i32): bool {
    if (n == 0) {
        return true;
    }
    return is_odd(n - 1);
}

fn is_odd(n: i32): bool {
    if (n == 0) {
        return false;
    }
    return is_even(n - 1);
}

print(is_even(4));  // true
print(is_odd(4));   // false
```

### Recursive Data Processing

```hemlock
fn sum_array(arr: array, index: i32): i32 {
    if (index >= arr.length) {
        return 0;
    }
    return arr[index] + sum_array(arr, index + 1);
}

let numbers = [1, 2, 3, 4, 5];
print(sum_array(numbers, 0));  // 15
```

**Note:** No tail call optimization yet - deep recursion may cause stack overflow.

## Higher-Order Functions

Functions that take or return other functions.

### Map Pattern

```hemlock
fn map(arr, f) {
    let result = [];
    let i = 0;
    while (i < arr.length) {
        result.push(f(arr[i]));
        i = i + 1;
    }
    return result;
}

fn double(x) { return x * 2; }

let numbers = [1, 2, 3, 4, 5];
let doubled = map(numbers, double);  // [2, 4, 6, 8, 10]
```

### Filter Pattern

```hemlock
fn filter(arr, predicate) {
    let result = [];
    let i = 0;
    while (i < arr.length) {
        if (predicate(arr[i])) {
            result.push(arr[i]);
        }
        i = i + 1;
    }
    return result;
}

fn is_even(x) { return x % 2 == 0; }

let numbers = [1, 2, 3, 4, 5, 6];
let evens = filter(numbers, is_even);  // [2, 4, 6]
```

### Reduce Pattern

```hemlock
fn reduce(arr, f, initial) {
    let accumulator = initial;
    let i = 0;
    while (i < arr.length) {
        accumulator = f(accumulator, arr[i]);
        i = i + 1;
    }
    return accumulator;
}

fn add(a, b) { return a + b; }

let numbers = [1, 2, 3, 4, 5];
let sum = reduce(numbers, add, 0);  // 15
```

### Function Composition

```hemlock
fn compose(f, g) {
    return fn(x) {
        return f(g(x));
    };
}

fn double(x) { return x * 2; }
fn increment(x) { return x + 1; }

let double_then_increment = compose(increment, double);
print(double_then_increment(5));  // 11 (5*2 + 1)
```

## Common Patterns

### Pattern: Factory Functions

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

### Pattern: Callback Functions

```hemlock
fn process_async(data, callback) {
    // ... do processing
    callback(data);
}

process_async("test", fn(result) {
    print("Processing complete: " + result);
});
```

### Pattern: Partial Application

```hemlock
fn partial(f, x) {
    return fn(y) {
        return f(x, y);
    };
}

fn multiply(a, b) {
    return a * b;
}

let double = partial(multiply, 2);
let triple = partial(multiply, 3);

print(double(5));  // 10
print(triple(5));  // 15
```

### Pattern: Memoization

```hemlock
fn memoize(f) {
    let cache = {};

    return fn(x) {
        if (cache.has(x)) {
            return cache[x];
        }

        let result = f(x);
        cache[x] = result;
        return result;
    };
}

fn expensive_fibonacci(n) {
    if (n <= 1) { return n; }
    return expensive_fibonacci(n - 1) + expensive_fibonacci(n - 2);
}

let fast_fib = memoize(expensive_fibonacci);
print(fast_fib(10));  // Much faster with caching
```

## Function Semantics

### Return Type Requirements

Functions with return type annotation **must** return a value:

```hemlock
fn get_value(): i32 {
    // ERROR: Missing return statement
}

fn get_value(): i32 {
    return 42;  // OK
}
```

### Type Checking

```hemlock
fn add(a: i32, b: i32): i32 {
    return a + b;
}

add(5, 10);        // OK
add(5.5, 10.5);    // Promotes to f64, returns f64
add("a", "b");     // Runtime error: type mismatch
```

### Scope Rules

```hemlock
let global = "global";

fn outer() {
    let outer_var = "outer";

    fn inner() {
        let inner_var = "inner";
        // Can access: inner_var, outer_var, global
    }

    // Can access: outer_var, global
    // Cannot access: inner_var
}

// Can access: global
// Cannot access: outer_var, inner_var
```

## Best Practices

1. **Use type annotations** - Helps catch errors and documents intent
2. **Keep functions small** - Each function should do one thing
3. **Prefer pure functions** - Avoid side effects when possible
4. **Name functions clearly** - Use descriptive verb names
5. **Return early** - Use guard clauses to reduce nesting
6. **Document complex closures** - Make captured variables explicit
7. **Avoid deep recursion** - No tail call optimization yet

## Common Pitfalls

### Pitfall: Recursion Depth

```hemlock
// Deep recursion may cause stack overflow
fn count_down(n) {
    if (n == 0) { return; }
    count_down(n - 1);
}

count_down(100000);  // May crash with stack overflow
```

### Pitfall: Modifying Captured Variables

```hemlock
fn make_counter() {
    let count = 0;
    return fn() {
        count = count + 1;  // Can read and modify captured variables
        return count;
    };
}
```

**Note:** This works, but be aware that all closures share the same captured environment.

## Examples

### Example: Function Pipeline

```hemlock
fn pipeline(value, ...functions) {
    let result = value;
    for (f in functions) {
        result = f(result);
    }
    return result;
}

fn double(x) { return x * 2; }
fn increment(x) { return x + 1; }
fn square(x) { return x * x; }

let result = pipeline(3, double, increment, square);
print(result);  // 49 ((3*2+1)^2)
```

### Example: Event Handler

```hemlock
let handlers = [];

fn on_event(name: string, handler) {
    handlers.push({ name: name, handler: handler });
}

fn trigger_event(name: string, data) {
    let i = 0;
    while (i < handlers.length) {
        if (handlers[i].name == name) {
            handlers[i].handler(data);
        }
        i = i + 1;
    }
}

on_event("click", fn(data) {
    print("Clicked: " + data);
});

trigger_event("click", "button1");
```

### Example: Sorting with Custom Comparator

```hemlock
fn sort(arr, compare) {
    // Bubble sort with custom comparator
    let n = arr.length;
    let i = 0;
    while (i < n) {
        let j = 0;
        while (j < n - i - 1) {
            if (compare(arr[j], arr[j + 1]) > 0) {
                let temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
            j = j + 1;
        }
        i = i + 1;
    }
}

fn ascending(a, b) {
    if (a < b) { return -1; }
    if (a > b) { return 1; }
    return 0;
}

let numbers = [5, 2, 8, 1, 9];
sort(numbers, ascending);
print(numbers);  // [1, 2, 5, 8, 9]
```

## Optional Parameters (Default Arguments)

Functions can have optional parameters with default values using the `?:` syntax:

```hemlock
fn greet(name, greeting?: "Hello") {
    return greeting + " " + name;
}

print(greet("Alice"));           // "Hello Alice"
print(greet("Bob", "Hi"));       // "Hi Bob"

fn add(a, b?: 10, c?: 100) {
    return a + b + c;
}

print(add(1));          // 111 (1 + 10 + 100)
print(add(1, 2));       // 103 (1 + 2 + 100)
print(add(1, 2, 3));    // 6   (1 + 2 + 3)
```

**Rules:**
- Optional parameters must come after required parameters
- Default values can be any expression
- Omitted arguments use the default value

## Variadic Functions (Rest Parameters)

Functions can accept a variable number of arguments using rest parameters (`...`):

```hemlock
fn sum(...args) {
    let total = 0;
    for (arg in args) {
        total = total + arg;
    }
    return total;
}

print(sum(1, 2, 3));        // 6
print(sum(1, 2, 3, 4, 5));  // 15
print(sum());               // 0

fn log(prefix, ...messages) {
    for (msg in messages) {
        print(prefix + ": " + msg);
    }
}

log("INFO", "Starting", "Running", "Done");
// INFO: Starting
// INFO: Running
// INFO: Done
```

**Rules:**
- Rest parameter must be the last parameter
- Rest parameter collects all remaining arguments into an array
- Can be combined with regular and optional parameters

## Function Type Annotations

Function types allow you to specify the exact signature expected for function parameters and return values:

### Basic Function Types

```hemlock
// Function type syntax: fn(param_types): return_type
fn apply(f: fn(i32): i32, x: i32): i32 {
    return f(x);
}

let double = fn(n) { return n * 2; };
let result = apply(double, 5);  // 10
```

### Higher-Order Function Types

```hemlock
// Function returning a function
fn make_adder(n: i32): fn(i32): i32 {
    return fn(x) { return x + n; };
}

let add5 = make_adder(5);
print(add5(10));  // 15
```

### Async Function Types

```hemlock
// Async function type
fn run_task(handler: async fn(): void) {
    spawn(handler);
}

run_task(async fn() {
    print("Running async!");
});
```

### Function Type Aliases

```hemlock
// Create named function types for clarity
type Callback = fn(i32): void;
type Predicate = fn(any): bool;
type BinaryOp = fn(i32, i32): i32;

fn filter_with(arr: array, pred: Predicate): array {
    return arr.filter(pred);
}
```

## Const Parameters

The `const` modifier prevents a parameter from being mutated within the function:

### Basic Const Parameters

```hemlock
fn print_all(const items: array) {
    // items.push(4);  // ERROR: cannot mutate const parameter
    for (item in items) {
        print(item);   // OK: reading is allowed
    }
}

let nums = [1, 2, 3];
print_all(nums);
```

### Deep Immutability

Const parameters enforce deep immutability - no mutation through any path:

```hemlock
fn describe(const person: object) {
    print(person.name);       // OK: reading is allowed
    // person.name = "Bob";   // ERROR: cannot mutate
    // person.address.city = "NYC";  // ERROR: deep const
}
```

### What Const Prevents

| Type | Blocked by Const | Allowed |
|------|-----------------|---------|
| array | push, pop, shift, unshift, insert, remove, clear, reverse | slice, concat, map, filter, find, contains |
| object | field assignment | field read |
| buffer | index assignment | index read |
| string | index assignment | all methods (return new strings) |

## Named Arguments

Functions can be called with named arguments for clarity and flexibility:

### Basic Named Arguments

```hemlock
fn create_user(name: string, age?: 18, active?: true) {
    print(name + " is " + age + " years old");
}

// Positional arguments (traditional)
create_user("Alice", 25, false);

// Named arguments - can be in any order
create_user(name: "Bob", age: 30);
create_user(age: 25, name: "Charlie", active: false);
```

### Mixing Positional and Named

```hemlock
// Skip optional parameters by naming what you need
create_user("David", active: false);  // Uses default age=18

// Named arguments must come after positional
create_user("Eve", age: 21);          // OK
// create_user(name: "Bad", 25);      // ERROR: positional after named
```

### Rules for Named Arguments

- Use `name: value` syntax for named arguments
- Named arguments can appear in any order after positional arguments
- Positional arguments cannot follow named arguments
- Works with default/optional parameters
- Unknown parameter names cause runtime errors

## Limitations

Current limitations to be aware of:

- **No pass-by-reference** - `ref` keyword parsed but not implemented
- **No function overloading** - One function per name
- **No tail call optimization** - Deep recursion limited by stack size

## Related Topics

- [Control Flow](control-flow.md) - Using functions with control structures
- [Objects](objects.md) - Methods are functions stored in objects
- [Error Handling](error-handling.md) - Functions and exception handling
- [Types](types.md) - Type annotations and conversions

## See Also

- **Closures**: See CLAUDE.md section "Functions" for closure semantics
- **First-Class Values**: Functions are values like any other
- **Lexical Scoping**: Functions capture their defining environment
