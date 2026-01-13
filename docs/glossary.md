# Glossary

New to programming or systems concepts? This glossary explains terms used throughout Hemlock's documentation in plain language.

---

## A

### Allocate / Allocation
**What it means:** Asking the computer for a chunk of memory to use.

**Analogy:** Like checking out a book from a library - you're borrowing space that you need to return later.

**In Hemlock:**
```hemlock
let space = alloc(100);  // "I need 100 bytes of memory, please"
// ... use it ...
free(space);             // "I'm done, you can have it back"
```

### Array
**What it means:** A list of values stored together, accessed by position (index).

**Analogy:** Like a row of mailboxes numbered 0, 1, 2, 3... You can put something in mailbox #2 and later get it from mailbox #2.

**In Hemlock:**
```hemlock
let colors = ["red", "green", "blue"];
print(colors[0]);  // "red" - first item is at position 0
print(colors[2]);  // "blue" - third item is at position 2
```

### Async / Asynchronous
**What it means:** Code that can run "in the background" while other code continues. In Hemlock, async code actually runs on separate CPU cores simultaneously.

**Analogy:** Like cooking multiple dishes at once - you put the rice on, then while it cooks, you chop vegetables. Both happen at the same time.

**In Hemlock:**
```hemlock
async fn slow_task(): i32 {
    // This can run on another CPU core
    return 42;
}

let task = spawn(slow_task);  // Start it running
// ... do other stuff while it runs ...
let result = join(task);      // Wait for it to finish, get result
```

---

## B

### Boolean / Bool
**What it means:** A value that is either `true` or `false`. Nothing else.

**Named after:** George Boole, a mathematician who studied true/false logic.

**In Hemlock:**
```hemlock
let is_raining = true;
let has_umbrella = false;

if (is_raining && !has_umbrella) {
    print("You'll get wet!");
}
```

### Bounds Checking
**What it means:** Automatically verifying that you're not trying to access memory outside what was allocated. Prevents crashes and security bugs.

**Analogy:** Like a librarian who checks that the book you're requesting actually exists before trying to get it.

**In Hemlock:**
```hemlock
let buf = buffer(10);  // 10 slots, numbered 0-9
buf[5] = 42;           // OK - slot 5 exists
buf[100] = 42;         // ERROR! Hemlock stops you - slot 100 doesn't exist
```

### Buffer
**What it means:** A safe container for raw bytes with a known size. Hemlock checks that you don't read or write past its boundaries.

**Analogy:** Like a safe with a specific number of compartments. You can use any compartment, but you can't access compartment #50 if the safe only has 10.

**In Hemlock:**
```hemlock
let data = buffer(64);   // 64 bytes of safe storage
data[0] = 65;            // Put 65 in the first byte
print(data.length);      // 64 - you can check its size
free(data);              // Clean up when done
```

---

## C

### Closure
**What it means:** A function that "remembers" variables from where it was created, even after that code has finished.

**Analogy:** Like a note that says "add 5 to whatever number you give me" - the "5" is baked into the note.

**In Hemlock:**
```hemlock
fn make_adder(amount) {
    return fn(x) {
        return x + amount;  // 'amount' is remembered!
    };
}

let add_five = make_adder(5);
print(add_five(10));  // 15 - it remembered that amount=5
```

### Coercion (Type Coercion)
**What it means:** Automatically converting a value from one type to another when needed.

**Example:** When you add an integer and a decimal, the integer is automatically converted to a decimal first.

**In Hemlock:**
```hemlock
let whole: i32 = 5;
let decimal: f64 = 2.5;
let result = whole + decimal;  // 'whole' becomes 5.0, then adds to 2.5
print(result);  // 7.5
```

### Compile / Compiler
**What it means:** Translating your code into a program the computer can run directly. The compiler (`hemlockc`) reads your `.hml` file and creates an executable.

**Analogy:** Like translating a book from English to Spanish - the content is the same, but now Spanish speakers can read it.

**In Hemlock:**
```bash
hemlockc myprogram.hml -o myprogram   # Translate to executable
./myprogram                            # Run the executable
```

### Concurrency
**What it means:** Multiple things happening at overlapping times. In Hemlock, this means actual parallel execution on multiple CPU cores.

**Analogy:** Two chefs cooking different dishes simultaneously in the same kitchen.

---

## D

### Defer
**What it means:** Schedule something to happen later, when the current function finishes. Useful for cleanup.

**Analogy:** Like telling yourself "when I leave, turn off the lights" - you set the reminder now, it happens later.

**In Hemlock:**
```hemlock
fn process_file() {
    let f = open("data.txt", "r");
    defer f.close();  // "Close this file when I'm done here"

    // ... lots of code ...
    // Even if there's an error, f.close() will run
}
```

### Duck Typing
**What it means:** If it looks like a duck and quacks like a duck, treat it as a duck. In code: if an object has the fields/methods you need, use it - don't worry about its official "type."

**Named after:** The duck test - a form of reasoning.

**In Hemlock:**
```hemlock
define Printable {
    name: string
}

fn greet(thing: Printable) {
    print("Hello, " + thing.name);
}

// Any object with a 'name' field works!
greet({ name: "Alice" });
greet({ name: "Bob", age: 30 });  // Extra fields are OK
```

---

## E

### Expression
**What it means:** Code that produces a value. Can be used anywhere a value is expected.

**Examples:** `42`, `x + y`, `get_name()`, `true && false`

### Enum / Enumeration
**What it means:** A type with a fixed set of possible values, each with a name.

**Analogy:** Like a dropdown menu - you can only pick from the options listed.

**In Hemlock:**
```hemlock
enum Status {
    PENDING,
    APPROVED,
    REJECTED
}

let my_status = Status.APPROVED;

if (my_status == Status.REJECTED) {
    print("Sorry!");
}
```

---

## F

### Float / Floating-Point
**What it means:** A number with a decimal point. Called "floating" because the decimal point can be at different positions.

**In Hemlock:**
```hemlock
let pi = 3.14159;      // f64 - 64-bit float (default)
let half: f32 = 0.5;   // f32 - 32-bit float (smaller, less precise)
```

### Free
**What it means:** Return memory you're done using back to the system so it can be reused.

**Analogy:** Returning a library book so others can check it out.

**In Hemlock:**
```hemlock
let data = alloc(100);  // Borrow 100 bytes
// ... use data ...
free(data);             // Return it - REQUIRED!
```

### Function
**What it means:** A reusable block of code that takes inputs (parameters) and may produce an output (return value).

**Analogy:** Like a recipe - give it ingredients (inputs), follow the steps, get a dish (output).

**In Hemlock:**
```hemlock
fn add(a, b) {
    return a + b;
}

let result = add(3, 4);  // result is 7
```

---

## G

### Garbage Collection (GC)
**What it means:** Automatic memory cleanup. The runtime periodically finds unused memory and frees it for you.

**Why Hemlock doesn't have it:** GC can cause unpredictable pauses. Hemlock prefers explicit control - you decide when to free memory.

**Note:** Most Hemlock types (strings, arrays, objects) ARE automatically cleaned up when they go out of scope. Only raw `ptr` from `alloc()` needs manual `free()`.

---

## H

### Heap
**What it means:** A region of memory for data that needs to outlive the current function. You explicitly allocate and free heap memory.

**Contrast with:** Stack (automatic, temporary storage for local variables)

**In Hemlock:**
```hemlock
let ptr = alloc(100);  // This goes on the heap
// ... use it ...
free(ptr);             // You clean up the heap yourself
```

---

## I

### Index
**What it means:** The position of an item in an array or string. Starts at 0 in Hemlock.

**In Hemlock:**
```hemlock
let letters = ["a", "b", "c"];
//             [0]  [1]  [2]   <- indices

print(letters[0]);  // "a" - first item
print(letters[2]);  // "c" - third item
```

### Integer
**What it means:** A whole number without a decimal point. Can be positive, negative, or zero.

**In Hemlock:**
```hemlock
let small = 42;       // i32 - fits in 32 bits
let big = 5000000000; // i64 - needs 64 bits (auto-detected)
let tiny: i8 = 100;   // i8 - explicitly 8 bits
```

### Interpreter
**What it means:** A program that reads your code and runs it directly, line by line.

**Contrast with:** Compiler (translates code first, then runs the translation)

**In Hemlock:**
```bash
./hemlock script.hml   # Interpreter runs your code directly
```

---

## L

### Literal
**What it means:** A value written directly in your code, not computed.

**Examples:**
```hemlock
42              // integer literal
3.14            // float literal
"hello"         // string literal
true            // boolean literal
[1, 2, 3]       // array literal
{ x: 10 }       // object literal
```

---

## M

### Memory Leak
**What it means:** Forgetting to free allocated memory. The memory stays reserved but unused, wasting resources.

**Analogy:** Checking out library books and never returning them. Eventually, the library runs out of books.

**In Hemlock:**
```hemlock
fn leaky() {
    let ptr = alloc(1000);
    // Oops! Forgot to free(ptr)
    // Those 1000 bytes are lost until program exits
}
```

### Method
**What it means:** A function attached to an object or type.

**In Hemlock:**
```hemlock
let text = "hello";
let upper = text.to_upper();  // to_upper() is a method on strings
print(upper);  // "HELLO"
```

### Mutex
**What it means:** A lock that ensures only one thread accesses something at a time. Prevents data corruption when multiple threads touch shared data.

**Analogy:** Like a bathroom lock - only one person can use it at a time.

---

## N

### Null
**What it means:** A special value meaning "nothing" or "no value."

**In Hemlock:**
```hemlock
let maybe_name = null;

if (maybe_name == null) {
    print("No name provided");
}
```

---

## O

### Object
**What it means:** A collection of named values (fields/properties) grouped together.

**In Hemlock:**
```hemlock
let person = {
    name: "Alice",
    age: 30,
    city: "NYC"
};

print(person.name);  // "Alice"
print(person.age);   // 30
```

---

## P

### Parameter
**What it means:** A variable that a function expects to receive when called.

**Also called:** Argument (technically, parameter is in the definition, argument is in the call)

**In Hemlock:**
```hemlock
fn greet(name, times) {   // 'name' and 'times' are parameters
    // ...
}

greet("Alice", 3);        // "Alice" and 3 are arguments
```

### Pointer
**What it means:** A value that holds a memory address - it "points to" where data is stored.

**Analogy:** Like a street address. The address isn't the house - it tells you where to find the house.

**In Hemlock:**
```hemlock
let ptr = alloc(100);  // ptr holds the address of 100 bytes
// ptr doesn't contain the data - it points to where the data lives
free(ptr);
```

### Primitive
**What it means:** A basic, built-in type that isn't made of other types.

**In Hemlock:** `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`, `rune`, `null`

---

## R

### Reference Counting (Refcounting)
**What it means:** Tracking how many things are using a piece of data. When nothing uses it anymore, automatically clean it up.

**In Hemlock:** Strings, arrays, objects, and buffers use refcounting internally. You don't see it, but it prevents memory leaks for most common types.

### Return Value
**What it means:** The value a function sends back when it finishes.

**In Hemlock:**
```hemlock
fn double(x) {
    return x * 2;  // This is the return value
}

let result = double(5);  // result gets the return value: 10
```

### Rune
**What it means:** A single Unicode character (codepoint). Can represent any character including emoji.

**Why "rune"?** The term comes from Go. It emphasizes that this is a full character, not just a byte.

**In Hemlock:**
```hemlock
let letter = 'A';
let emoji = '🚀';
let code: i32 = letter;  // 65 - the Unicode codepoint
```

### Runtime
**What it means:** The time when your program is actually running (as opposed to "compile time" when it's being translated).

**Also:** The supporting code that runs alongside your program (e.g., the memory allocator).

---

## S

### Scope
**What it means:** The region of code where a variable exists and can be used.

**In Hemlock:**
```hemlock
let outer = 1;              // Lives in outer scope

if (true) {
    let inner = 2;          // Lives only inside this block
    print(outer);           // OK - can see outer scope
    print(inner);           // OK - we're inside its scope
}

print(outer);               // OK
// print(inner);            // ERROR - inner doesn't exist here
```

### Stack
**What it means:** Memory for temporary, short-lived data. Automatically managed - when a function returns, its stack space is reclaimed.

**Contrast with:** Heap (longer-lived, manually managed)

### Statement
**What it means:** A single instruction or command. Statements DO things; expressions PRODUCE values.

**Examples:** `let x = 5;`, `print("hi");`, `if (x > 0) { ... }`

### String
**What it means:** A sequence of text characters.

**In Hemlock:**
```hemlock
let greeting = "Hello, World!";
print(greeting.length);    // 13 characters
print(greeting[0]);        // "H" - first character
```

### Structural Typing
**What it means:** Type compatibility based on structure (what fields/methods exist), not name. Same as "duck typing."

---

## T

### Thread
**What it means:** A separate path of execution. Multiple threads can run simultaneously on different CPU cores.

**In Hemlock:** `spawn()` creates a new thread.

### Type
**What it means:** The kind of data a value represents. Determines what operations are valid.

**In Hemlock:**
```hemlock
let x = 42;              // type: i32
let name = "Alice";      // type: string
let nums = [1, 2, 3];    // type: array

print(typeof(x));        // "i32"
print(typeof(name));     // "string"
```

### Type Annotation
**What it means:** Explicitly declaring what type a variable should have.

**In Hemlock:**
```hemlock
let x: i32 = 42;         // x must be an i32
let name: string = "hi"; // name must be a string

fn add(a: i32, b: i32): i32 {  // parameters and return type annotated
    return a + b;
}
```

---

## U

### UTF-8
**What it means:** A way to encode text that supports all world languages and emoji. Each character can be 1-4 bytes.

**In Hemlock:** All strings are UTF-8.

```hemlock
let text = "Hello, 世界! 🌍";  // Mix of ASCII, Chinese, emoji - all work
```

---

## V

### Variable
**What it means:** A named storage location that holds a value.

**In Hemlock:**
```hemlock
let count = 0;    // Create variable 'count', store 0
count = count + 1; // Update it to 1
print(count);     // Read its value: 1
```

---

## Quick Reference: What Type Should I Use?

| Situation | Use This | Why |
|-----------|----------|-----|
| Just need a number | `let x = 42;` | Hemlock picks the right type |
| Counting things | `i32` | Big enough for most counts |
| Huge numbers | `i64` | When i32 isn't enough |
| Bytes (0-255) | `u8` | Files, network data |
| Decimals | `f64` | Precise decimal math |
| Yes/No values | `bool` | Only `true` or `false` |
| Text | `string` | Any text content |
| Single character | `rune` | One letter/emoji |
| List of things | `array` | Ordered collection |
| Named fields | `object` | Group related data |
| Raw memory | `buffer` | Safe byte storage |
| FFI/systems work | `ptr` | Advanced, manual memory |

---

## See Also

- [Quick Start](getting-started/quick-start.md) - Your first Hemlock program
- [Type System](language-guide/types.md) - Full type documentation
- [Memory Management](language-guide/memory.md) - Understanding memory
