# Control Flow

Hemlock provides familiar C-style control flow with mandatory braces and explicit syntax. This guide covers conditionals, loops, switch statements, and operators.

## Overview

Available control flow features:

- `if`/`else`/`else if` - Conditional branches
- `while` loops - Condition-based iteration
- `for` loops - C-style and for-in iteration
- `loop` - Infinite loops (cleaner than `while (true)`)
- `switch` statements - Multi-way branching
- `break`/`continue` - Loop control
- Loop labels - Targeted break/continue for nested loops
- `defer` - Deferred execution (cleanup)
- Boolean operators: `&&`, `||`, `!`
- Comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Bitwise operators: `&`, `|`, `^`, `<<`, `>>`, `~`

## If Statements

### Basic If/Else

```hemlock
if (x > 10) {
    print("large");
} else {
    print("small");
}
```

**Rules:**
- Braces are **always required** for all branches
- Conditions must be enclosed in parentheses
- No optional braces (unlike C)

### If Without Else

```hemlock
if (x > 0) {
    print("positive");
}
// No else branch needed
```

### Else-If Chains

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

**Note:** `else if` is syntactic sugar for nested if statements. These are equivalent:

```hemlock
// else if (syntactic sugar)
if (a) {
    foo();
} else if (b) {
    bar();
}

// Equivalent nested if
if (a) {
    foo();
} else {
    if (b) {
        bar();
    }
}
```

### Nested If Statements

```hemlock
if (x > 0) {
    if (x < 10) {
        print("single digit positive");
    } else {
        print("multi-digit positive");
    }
} else {
    print("non-positive");
}
```

## While Loops

Condition-based iteration:

```hemlock
let i = 0;
while (i < 10) {
    print(i);
    i = i + 1;
}
```

**Infinite loops (old style):**
```hemlock
while (true) {
    // ... do work
    if (should_exit) {
        break;
    }
}
```

**Note:** For infinite loops, prefer the `loop` keyword (see below).

## Loop (Infinite Loop)

The `loop` keyword provides a cleaner syntax for infinite loops:

```hemlock
loop {
    // ... do work
    if (should_exit) {
        break;
    }
}
```

**Equivalent to `while (true)` but more explicit about intent.**

### Basic Loop with Break

```hemlock
let i = 0;
loop {
    if (i >= 5) {
        break;
    }
    print(i);
    i = i + 1;
}
// Prints: 0, 1, 2, 3, 4
```

### Loop with Continue

```hemlock
let i = 0;
loop {
    i = i + 1;
    if (i > 5) {
        break;
    }
    if (i == 3) {
        continue;  // Skip printing 3
    }
    print(i);
}
// Prints: 1, 2, 4, 5
```

### Nested Loops

```hemlock
let x = 0;
loop {
    if (x >= 2) { break; }
    let y = 0;
    loop {
        if (y >= 3) { break; }
        print(x * 10 + y);
        y = y + 1;
    }
    x = x + 1;
}
// Prints: 0, 1, 2, 10, 11, 12
```

### When to Use Loop

- **Use `loop`** for intentionally infinite loops that exit via `break`
- **Use `while`** when there's a natural termination condition
- **Use `for`** when iterating a known number of times or over a collection

## For Loops

### C-Style For

Classic three-part for loop:

```hemlock
for (let i = 0; i < 10; i = i + 1) {
    print(i);
}
```

**Components:**
- **Initializer**: `let i = 0` - Runs once before loop
- **Condition**: `i < 10` - Checked before each iteration
- **Update**: `i = i + 1` - Runs after each iteration

**Scope:**
```hemlock
for (let i = 0; i < 10; i = i + 1) {
    print(i);
}
// i not accessible here (loop-scoped)
```

### For-In Loops

Iterate over array elements:

```hemlock
let arr = [1, 2, 3, 4, 5];
for (let item in arr) {
    print(item);  // Prints each element
}
```

**With index and value:**
```hemlock
let arr = ["a", "b", "c"];
for (let i = 0; i < arr.length; i = i + 1) {
    print(`Index: ${i}, Value: ${arr[i]}`);
}
```

## Switch Statements

Multi-way branching based on value:

### Basic Switch

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

### Switch with Default

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

**Rules:**
- `default` matches when no other case matches
- `default` can appear anywhere in the switch body
- Only one default case allowed

### Fall-Through Behavior

Cases without `break` fall through to the next case (C-style behavior). This is **intentional** and can be used to group cases:

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

**Explicit fallthrough example:**
```hemlock
let day = 3;

switch (day) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        print("Weekday");
        break;
    case 6:
    case 7:
        print("Weekend");
        break;
}
```

**Important:** Unlike some modern languages, Hemlock does NOT require an explicit `fallthrough` keyword. Cases automatically fall through unless terminated by `break`, `return`, or `throw`. Always use `break` to prevent unintended fallthrough.

### Switch with Return

In functions, `return` exits the switch immediately:

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

### Switch Value Types

Switch works with any value type:

```hemlock
// Integers
switch (count) {
    case 0: print("zero"); break;
    case 1: print("one"); break;
}

// Strings
switch (name) {
    case "Alice": print("A"); break;
    case "Bob": print("B"); break;
}

// Booleans
switch (flag) {
    case true: print("on"); break;
    case false: print("off"); break;
}
```

**Note:** Cases are compared using value equality.

## Break and Continue

### Break

Exit the innermost loop or switch:

```hemlock
// In loops
let i = 0;
while (true) {
    if (i >= 10) {
        break;  // Exit loop
    }
    print(i);
    i = i + 1;
}

// In switch
switch (x) {
    case 1:
        print("one");
        break;  // Exit switch
    case 2:
        print("two");
        break;
}
```

### Continue

Skip to next iteration of loop:

```hemlock
for (let i = 0; i < 10; i = i + 1) {
    if (i == 5) {
        continue;  // Skip iteration when i is 5
    }
    print(i);  // Prints 0,1,2,3,4,6,7,8,9
}
```

**Difference:**
- `break` - Exits loop entirely
- `continue` - Skips to next iteration

## Loop Labels

Loop labels allow `break` and `continue` to target specific outer loops instead of just the innermost loop. This is useful for nested loops where you need to control an outer loop from an inner one.

### Labeled Break

Exit an outer loop from an inner loop:

```hemlock
outer: while (i < 3) {
    let j = 0;
    while (j < 3) {
        if (i == 1 && j == 1) {
            break outer;  // Exit the outer while loop
        }
        print(i * 10 + j);
        j = j + 1;
    }
    i = i + 1;
}
// Prints: 0, 1, 2, 10 (stops at i=1, j=1)
```

### Labeled Continue

Skip to the next iteration of an outer loop:

```hemlock
let i = 0;
outer: while (i < 3) {
    i = i + 1;
    let j = 0;
    while (j < 3) {
        j = j + 1;
        if (i == 2 && j == 1) {
            continue outer;  // Skip rest of inner loop, continue outer
        }
        print(i * 10 + j);
    }
}
// When i=2, j=1: skips to next outer iteration
```

### Labels with For Loops

Labels work with all loop types:

```hemlock
outer: for (let x = 0; x < 3; x = x + 1) {
    for (let y = 0; y < 3; y = y + 1) {
        if (x == 1 && y == 1) {
            break outer;
        }
        print(x * 10 + y);
    }
}
```

### Labels with For-In Loops

```hemlock
let arr1 = [1, 2, 3];
let arr2 = [10, 20, 30];

outer: for (let a in arr1) {
    for (let b in arr2) {
        if (a == 2 && b == 20) {
            break outer;
        }
        print(a * 100 + b);
    }
}
```

### Labels with Loop Keyword

```hemlock
let x = 0;
outer: loop {
    let y = 0;
    loop {
        if (x == 1 && y == 1) {
            break outer;
        }
        print(x * 10 + y);
        y = y + 1;
        if (y >= 3) { break; }
    }
    x = x + 1;
    if (x >= 3) { break; }
}
```

### Multiple Labels

You can have labels at different nesting levels:

```hemlock
outer: for (let a = 0; a < 2; a = a + 1) {
    inner: for (let b = 0; b < 3; b = b + 1) {
        for (let c = 0; c < 3; c = c + 1) {
            if (c == 1) {
                continue inner;  // Skip to next iteration of middle loop
            }
            if (a == 1 && b == 1) {
                break outer;      // Exit outermost loop
            }
            print(a * 100 + b * 10 + c);
        }
    }
}
```

### Unlabeled Break/Continue with Labeled Loops

Unlabeled `break` and `continue` still work normally (affecting the innermost loop), even when outer loops have labels:

```hemlock
outer: for (let x = 0; x < 3; x = x + 1) {
    for (let y = 0; y < 5; y = y + 1) {
        if (y == 2) {
            break;  // Only breaks inner loop
        }
        print(x * 10 + y);
    }
}
// Prints: 0, 1, 10, 11, 20, 21
```

### Label Syntax

- Labels are identifiers followed by a colon
- Labels must immediately precede a loop statement (`while`, `for`, `loop`)
- Label names follow identifier rules (letters, digits, underscores)
- Common conventions: `outer`, `inner`, `row`, `col`, descriptive names

## Defer Statement

The `defer` statement schedules code to execute when the current function returns. This is useful for cleanup operations like closing files, freeing resources, or releasing locks.

### Basic Defer

```hemlock
fn example() {
    print("start");
    defer print("cleanup");  // Runs when function returns
    print("end");
}

example();
// Output:
// start
// end
// cleanup
```

**Key behavior:**
- Deferred statements execute **after** the function body completes
- Deferred statements execute **before** the function returns to its caller
- Deferred statements always execute, even if the function throws an exception

### Multiple Defers (LIFO Order)

When multiple `defer` statements are used, they execute in **reverse order** (Last-In-First-Out):

```hemlock
fn example() {
    defer print("first");   // Executes last
    defer print("second");  // Executes second
    defer print("third");   // Executes first
    print("body");
}

example();
// Output:
// body
// third
// second
// first
```

This LIFO order is intentional - it matches the natural order for nested resource cleanup (close inner resources before outer ones).

### Defer with Return

Deferred statements execute before `return` transfers control:

```hemlock
fn get_value(): i32 {
    defer print("cleanup");
    print("before return");
    return 42;
}

let result = get_value();
print("result:", result);
// Output:
// before return
// cleanup
// result: 42
```

### Defer with Exceptions

Deferred statements execute even when an exception is thrown:

```hemlock
fn risky() {
    defer print("cleanup 1");
    defer print("cleanup 2");
    print("before throw");
    throw "error!";
    print("after throw");  // Never reached
}

try {
    risky();
} catch (e) {
    print("Caught:", e);
}
// Output:
// before throw
// cleanup 2
// cleanup 1
// Caught: error!
```

### Resource Cleanup Pattern

The primary use case for `defer` is ensuring resources are cleaned up:

```hemlock
fn process_file(filename: string) {
    let file = open(filename, "r");
    defer file.close();  // Always closes, even on error

    let content = file.read();
    // ... process content ...

    // File automatically closed when function returns
}
```

**Without defer (error-prone):**
```hemlock
fn process_file_bad(filename: string) {
    let file = open(filename, "r");
    let content = file.read();
    // If this throws, file.close() is never called!
    process(content);
    file.close();
}
```

### Defer with Closures

Defer can use closures to capture state:

```hemlock
fn example() {
    let resource = acquire_resource();
    defer fn() {
        print("Releasing resource");
        release(resource);
    }();  // Note: immediately-invoked function expression

    use_resource(resource);
}
```

### When to Use Defer

**Use defer for:**
- Closing files and network connections
- Freeing allocated memory
- Releasing locks and mutexes
- Cleanup in any function that acquires resources

**Defer vs Finally:**
- `defer` is simpler for single-resource cleanup
- `try/finally` is better for complex error handling with recovery

### Best Practices

1. **Place defer immediately after acquiring a resource:**
   ```hemlock
   let file = open("data.txt", "r");
   defer file.close();
   // ... use file ...
   ```

2. **Use multiple defers for multiple resources:**
   ```hemlock
   let file1 = open("input.txt", "r");
   defer file1.close();

   let file2 = open("output.txt", "w");
   defer file2.close();

   // Both files will be closed in reverse order
   ```

3. **Remember LIFO order for dependent resources:**
   ```hemlock
   let outer = acquire_outer();
   defer release_outer(outer);

   let inner = acquire_inner(outer);
   defer release_inner(inner);

   // inner released before outer (correct dependency order)
   ```

## Boolean Operators

### Logical AND (`&&`)

Both conditions must be true:

```hemlock
if (x > 0 && x < 10) {
    print("single digit positive");
}
```

**Short-circuit evaluation:**
```hemlock
if (false && expensive_check()) {
    // expensive_check() never called
}
```

### Logical OR (`||`)

At least one condition must be true:

```hemlock
if (x < 0 || x > 100) {
    print("out of range");
}
```

**Short-circuit evaluation:**
```hemlock
if (true || expensive_check()) {
    // expensive_check() never called
}
```

### Logical NOT (`!`)

Negates boolean value:

```hemlock
if (!is_valid) {
    print("invalid");
}

if (!(x > 10)) {
    // Same as: if (x <= 10)
}
```

## Comparison Operators

### Equality

```hemlock
if (x == 10) { }    // Equal
if (x != 10) { }    // Not equal
```

Works with all types:
```hemlock
"hello" == "hello"  // true
true == false       // false
null == null        // true
```

### Relational

```hemlock
if (x < 10) { }     // Less than
if (x > 10) { }     // Greater than
if (x <= 10) { }    // Less than or equal
if (x >= 10) { }    // Greater than or equal
```

**Type promotion applies:**
```hemlock
let a: i32 = 10;
let b: i64 = 10;
if (a == b) { }     // true (i32 promoted to i64)
```

## Bitwise Operators

Hemlock provides bitwise operators for integer manipulation. These work **only with integer types** (i8-i64, u8-u64).

### Binary Bitwise Operators

**Bitwise AND (`&`)**
```hemlock
let a = 12;  // 1100 in binary
let b = 10;  // 1010 in binary
print(a & b);   // 8 (1000)
```

**Bitwise OR (`|`)**
```hemlock
print(a | b);   // 14 (1110)
```

**Bitwise XOR (`^`)**
```hemlock
print(a ^ b);   // 6 (0110)
```

**Left Shift (`<<`)**
```hemlock
print(a << 2);  // 48 (110000) - shift left by 2
```

**Right Shift (`>>`)**
```hemlock
print(a >> 1);  // 6 (110) - shift right by 1
```

### Unary Bitwise Operator

**Bitwise NOT (`~`)**
```hemlock
let a = 12;
print(~a);      // -13 (two's complement)

let c: u8 = 15;   // 00001111 in binary
print(~c);        // 240 (11110000) in u8
```

### Bitwise Examples

**With unsigned types:**
```hemlock
let c: u8 = 15;   // 00001111 in binary
let d: u8 = 7;    // 00000111 in binary

print(c & d);     // 7  (00000111)
print(c | d);     // 15 (00001111)
print(c ^ d);     // 8  (00001000)
print(~c);        // 240 (11110000) - in u8
```

**Type preservation:**
```hemlock
// Bitwise operations preserve the type of operands
let x: u8 = 255;
let result = ~x;  // result is u8 with value 0

let y: i32 = 100;
let result2 = y << 2;  // result2 is i32 with value 400
```

**Common patterns:**
```hemlock
// Check if bit is set
if (flags & 0x04) {
    print("bit 2 is set");
}

// Set a bit
flags = flags | 0x08;

// Clear a bit
flags = flags & ~0x02;

// Toggle a bit
flags = flags ^ 0x01;
```

### Operator Precedence

Bitwise operators follow C-style precedence:

1. `~` (unary NOT) - highest, same level as `!` and `-`
2. `<<`, `>>` (shifts) - higher than comparisons, lower than `+`/`-`
3. `&` (bitwise AND) - higher than `^` and `|`
4. `^` (bitwise XOR) - between `&` and `|`
5. `|` (bitwise OR) - lower than `&` and `^`, higher than `&&`
6. `&&`, `||` (logical) - lowest precedence

**Examples:**
```hemlock
// & has higher precedence than |
let result1 = 12 | 10 & 8;  // (10 & 8) | 12 = 8 | 12 = 12

// Shift has higher precedence than bitwise operators
let result2 = 8 | 1 << 2;   // 8 | (1 << 2) = 8 | 4 = 12

// Use parentheses for clarity
let result3 = (5 & 3) | (2 << 1);  // 1 | 4 = 5
```

**Important notes:**
- Bitwise operators only work with integer types (not floats, strings, etc.)
- Type promotion follows standard rules (smaller types promote to larger)
- Right shift (`>>`) is arithmetic for signed types, logical for unsigned
- Shift amounts are not range-checked (behavior is platform-dependent for large shifts)

## Operator Precedence (Complete)

From highest to lowest precedence:

1. **Unary**: `!`, `-`, `~`
2. **Multiplicative**: `*`, `/`, `%`
3. **Additive**: `+`, `-`
4. **Shift**: `<<`, `>>`
5. **Relational**: `<`, `>`, `<=`, `>=`
6. **Equality**: `==`, `!=`
7. **Bitwise AND**: `&`
8. **Bitwise XOR**: `^`
9. **Bitwise OR**: `|`
10. **Logical AND**: `&&`
11. **Logical OR**: `||`

**Use parentheses for clarity:**
```hemlock
// Unclear
if (a || b && c) { }

// Clear
if (a || (b && c)) { }
if ((a || b) && c) { }
```

## Common Patterns

### Pattern: Input Validation

```hemlock
fn validate_age(age: i32): bool {
    if (age < 0 || age > 150) {
        return false;
    }
    return true;
}
```

### Pattern: Range Checking

```hemlock
fn in_range(value: i32, min: i32, max: i32): bool {
    return value >= min && value <= max;
}

if (in_range(score, 0, 100)) {
    print("valid score");
}
```

### Pattern: State Machine

```hemlock
let state = "start";

while (true) {
    switch (state) {
        case "start":
            print("Starting...");
            state = "running";
            break;

        case "running":
            if (should_pause) {
                state = "paused";
            } else if (should_stop) {
                state = "stopped";
            }
            break;

        case "paused":
            if (should_resume) {
                state = "running";
            }
            break;

        case "stopped":
            print("Stopped");
            break;
    }

    if (state == "stopped") {
        break;
    }
}
```

### Pattern: Iteration with Filtering

```hemlock
let arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

// Print only even numbers
for (let i = 0; i < arr.length; i = i + 1) {
    if (arr[i] % 2 != 0) {
        continue;  // Skip odd numbers
    }
    print(arr[i]);
}
```

### Pattern: Early Exit

```hemlock
fn find_first_negative(arr: array): i32 {
    for (let i = 0; i < arr.length; i = i + 1) {
        if (arr[i] < 0) {
            return i;  // Early exit
        }
    }
    return -1;  // Not found
}
```

## Best Practices

1. **Always use braces** - Even for single-statement blocks (enforced by syntax)
2. **Explicit conditions** - Use `x == 0` instead of `!x` for clarity
3. **Avoid deep nesting** - Extract nested conditions into functions
4. **Use early returns** - Reduce nesting with guard clauses
5. **Break complex conditions** - Split into named boolean variables
6. **Default in switch** - Always include a default case
7. **Comment fall-through** - Make intentional fall-through explicit

## Common Pitfalls

### Pitfall: Assignment in Condition

```hemlock
// This is NOT allowed (no assignment in conditions)
if (x = 10) { }  // ERROR: Syntax error

// Use comparison instead
if (x == 10) { }  // OK
```

### Pitfall: Missing Break in Switch

```hemlock
// Unintentional fall-through
switch (x) {
    case 1:
        print("one");
        // Missing break - falls through!
    case 2:
        print("two");  // Executes for both 1 and 2
        break;
}

// Fix: Add break
switch (x) {
    case 1:
        print("one");
        break;  // Now correct
    case 2:
        print("two");
        break;
}
```

### Pitfall: Loop Variable Scope

```hemlock
// i is scoped to the loop
for (let i = 0; i < 10; i = i + 1) {
    print(i);
}
print(i);  // ERROR: i not defined here
```

## Examples

### Example: FizzBuzz

```hemlock
for (let i = 1; i <= 100; i = i + 1) {
    if (i % 15 == 0) {
        print("FizzBuzz");
    } else if (i % 3 == 0) {
        print("Fizz");
    } else if (i % 5 == 0) {
        print("Buzz");
    } else {
        print(i);
    }
}
```

### Example: Prime Checker

```hemlock
fn is_prime(n: i32): bool {
    if (n < 2) {
        return false;
    }

    let i = 2;
    while (i * i <= n) {
        if (n % i == 0) {
            return false;
        }
        i = i + 1;
    }

    return true;
}
```

### Example: Menu System

```hemlock
fn menu() {
    while (true) {
        print("1. Start");
        print("2. Settings");
        print("3. Exit");

        let choice = get_input();

        switch (choice) {
            case 1:
                start_game();
                break;
            case 2:
                show_settings();
                break;
            case 3:
                print("Goodbye!");
                return;
            default:
                print("Invalid choice");
                break;
        }
    }
}
```

## Related Topics

- [Functions](functions.md) - Control flow with function calls and returns
- [Error Handling](error-handling.md) - Control flow with exceptions
- [Types](types.md) - Type conversions in conditions

## See Also

- **Syntax**: See [Syntax](syntax.md) for statement syntax details
- **Operators**: See [Types](types.md) for type promotion in operations
