# Exercise 2: Stack vs Heap

**Goal:** Understand the difference between stack and heap memory, and when to use each.

**Prerequisites:** [Exercise 1: Your First Allocation](./01-first-allocation.md)

---

## Concepts Introduced

- Stack memory: automatic, fast, limited
- Heap memory: manual, flexible, unlimited
- Variable lifetimes
- Dangling pointers

---

## Part 1: Stack Variables

When you declare a variable in a function, it lives on the stack:

```hemlock
fn stack_demo() {
    let a = 10;      // 'a' is on the stack
    let b = 20;      // 'b' is on the stack, right after 'a'
    let c = 30;      // 'c' follows 'b'

    print("a = " + a);
    print("b = " + b);
    print("c = " + c);

    // When this function returns, a, b, c are automatically gone
}

stack_demo();
// a, b, c no longer exist - their memory is reclaimed
```

**Key properties of stack:**
- **Automatic:** Variables are created when declared, destroyed when scope ends
- **Fast:** Allocation is just moving a pointer
- **LIFO:** Last In, First Out - like a stack of plates

---

## Part 2: Heap Allocation

Heap memory persists until you explicitly free it:

```hemlock
fn heap_demo(): ptr {
    let p = alloc(4);        // Allocate on heap
    ptr_write_i32(p, 42);
    return p;                 // Pointer survives function return!
}

let ptr = heap_demo();
print("Value: " + ptr_read_i32(ptr));  // Still valid: 42
free(ptr);                              // Must free manually
```

**Key properties of heap:**
- **Manual:** You control when memory is allocated and freed
- **Persistent:** Survives function returns
- **Flexible:** Allocate any size at runtime
- **Slower:** Allocator must search for free space

---

## Part 3: The Dangling Pointer Problem

What happens if you return a pointer to a stack variable?

```hemlock
fn bad_idea(): ptr {
    let x = 42;       // x is on the stack
    return &x;        // Return address of x
    // x dies here!
}

// DON'T RUN THIS - it demonstrates undefined behavior
// let p = bad_idea();
// print(ptr_read_i32(p));  // Reading garbage!
```

The pointer `&x` becomes "dangling" - it points to memory that no longer belongs to `x`. Reading it gives unpredictable results.

**The fix: Use the heap when data must outlive the function:**

```hemlock
fn good_idea(): ptr {
    let p = alloc(4);     // Heap allocation
    ptr_write_i32(p, 42);
    return p;              // Caller owns this memory now
}

let p = good_idea();
print(ptr_read_i32(p));   // 42 - safe!
free(p);                   // Caller must free
```

---

## Part 4: Visualizing Memory

Let's trace what happens during execution:

```hemlock
fn outer() {
    let a = 1;                    // Stack: [a=1]
    let p = alloc(4);             // Stack: [a=1, p=0x1000], Heap: [4 bytes at 0x1000]
    ptr_write_i32(p, 100);        // Heap now contains 100

    inner(p);                      // Pass pointer to inner

    print(ptr_read_i32(p));       // 200 - inner modified it!
    free(p);
}

fn inner(p: ptr) {
    let b = 2;                    // Stack: [a=1, p=0x1000, b=2]
    ptr_write_i32(p, 200);        // Modify heap through pointer
    // b dies here
}

outer();
// a, p die here, heap is freed
```

**Memory timeline:**
```
Time 1 (enter outer):    Stack: [a=1]
Time 2 (after alloc):    Stack: [a=1, p→heap], Heap: [_]
Time 3 (after write):    Stack: [a=1, p→heap], Heap: [100]
Time 4 (enter inner):    Stack: [a=1, p→heap, b=2], Heap: [100]
Time 5 (inner writes):   Stack: [a=1, p→heap, b=2], Heap: [200]
Time 6 (exit inner):     Stack: [a=1, p→heap], Heap: [200]
Time 7 (exit outer):     Stack: [], Heap: [] (freed)
```

---

## Part 5: Stack Size Limits

The stack is limited (typically 8MB). Large allocations should use the heap:

```hemlock
// BAD: This might overflow the stack
fn stack_overflow() {
    let arr = [0; 10000000];  // 10 million elements on stack? Dangerous!
}

// GOOD: Large data on heap
fn safe_large_data() {
    let size = 10000000 * sizeof(i32);
    let p = alloc(size);
    // ... use p ...
    free(p);
}
```

---

## Part 6: Lifetime Patterns

### Pattern 1: Create and Destroy in Same Scope

```hemlock
fn process() {
    let p = alloc(100);
    // ... use p ...
    free(p);  // Clean up before returning
}
```

**Best for:** Temporary working memory

### Pattern 2: Factory Function (Caller Owns)

```hemlock
fn create_thing(): ptr {
    let p = alloc(64);
    // ... initialize p ...
    return p;  // Caller must free
}

let thing = create_thing();
// ... use thing ...
free(thing);
```

**Best for:** Creating objects with longer lifetimes

### Pattern 3: Pass Ownership

```hemlock
fn consume_thing(p: ptr) {
    // ... use p ...
    free(p);  // This function takes ownership and frees
}

let p = alloc(64);
consume_thing(p);
// p is now invalid - don't use it!
```

**Best for:** Transfer of responsibility

### Pattern 4: Borrow (No Ownership Transfer)

```hemlock
fn inspect_thing(p: ptr) {
    // Read from p, but don't free
    print(ptr_read_i32(p));
}

let p = alloc(64);
ptr_write_i32(p, 42);
inspect_thing(p);  // Borrows p
inspect_thing(p);  // Can borrow again
free(p);           // Original owner frees
```

**Best for:** Read-only or temporary access

---

## Challenge Exercises

### Challenge 1: Identify the Bug

What's wrong with this code?

```hemlock
fn make_array(): ptr {
    let arr = [1, 2, 3, 4, 5];
    return &arr;  // ???
}

let p = make_array();
// What happens here?
```

<details>
<summary>Answer</summary>

`arr` is a managed array on the stack. Returning `&arr` creates a dangling pointer because `arr` is destroyed when the function returns. The correct approach is to either return the array directly (Hemlock arrays are reference-counted) or allocate on the heap:

```hemlock
// Option 1: Return managed array (correct)
fn make_array(): array {
    return [1, 2, 3, 4, 5];
}

// Option 2: Manual heap allocation (correct)
fn make_array_manual(): ptr {
    let p = alloc(5 * sizeof(i32));
    for (let i = 0; i < 5; i++) {
        ptr_write_i32(p + i * sizeof(i32), i + 1);
    }
    return p;  // Caller frees
}
```
</details>

### Challenge 2: Memory Tracker

Write a program that tracks allocations and frees to detect leaks:

```hemlock
let allocated = 0;
let freed = 0;

fn tracked_alloc(size: i32): ptr {
    // Your code: increment allocated, call alloc
}

fn tracked_free(p: ptr) {
    // Your code: increment freed, call free
}

// Test it
let p1 = tracked_alloc(100);
let p2 = tracked_alloc(200);
tracked_free(p1);
// Forgot to free p2!

print("Allocated: " + allocated);
print("Freed: " + freed);
print("Leaked: " + (allocated - freed));
```

<details>
<summary>Solution</summary>

```hemlock
let allocated = 0;
let freed = 0;

fn tracked_alloc(size: i32): ptr {
    allocated = allocated + 1;
    return alloc(size);
}

fn tracked_free(p: ptr) {
    freed = freed + 1;
    free(p);
}

let p1 = tracked_alloc(100);
let p2 = tracked_alloc(200);
tracked_free(p1);

print("Allocated: " + allocated);  // 2
print("Freed: " + freed);          // 1
print("Leaked: " + (allocated - freed));  // 1
```
</details>

### Challenge 3: Implement a Resizable Buffer

Create a buffer that can grow:

```hemlock
// Start with 10 elements capacity
let capacity = 10;
let size = 0;
let data = alloc(capacity * sizeof(i32));

fn push(value: i32) {
    // Your code:
    // 1. If size == capacity, grow the buffer
    // 2. Write value at position 'size'
    // 3. Increment size
}

fn get(index: i32): i32 {
    // Your code: return value at index
}

// Test
for (let i = 0; i < 25; i++) {
    push(i * 10);
}

for (let i = 0; i < 25; i++) {
    print("data[" + i + "] = " + get(i));
}

free(data);
```

<details>
<summary>Solution</summary>

```hemlock
let capacity = 10;
let size = 0;
let data = alloc(capacity * sizeof(i32));

fn push(value: i32) {
    if (size == capacity) {
        // Double the capacity
        let new_capacity = capacity * 2;
        let new_data = alloc(new_capacity * sizeof(i32));

        // Copy old data
        memcpy(new_data, data, size * sizeof(i32));

        // Free old buffer
        free(data);

        // Update globals
        data = new_data;
        capacity = new_capacity;
        print("Grew to capacity: " + capacity);
    }

    ptr_write_i32(data + size * sizeof(i32), value);
    size = size + 1;
}

fn get(index: i32): i32 {
    if (index < 0 || index >= size) {
        panic("Index out of bounds");
    }
    return ptr_read_i32(data + index * sizeof(i32));
}
```
</details>

---

## Key Takeaways

1. **Stack:** Fast, automatic, limited size, LIFO, dies with scope
2. **Heap:** Manual, flexible, unlimited (by RAM), persists until freed
3. **Never return pointers to stack variables** - they become dangling
4. **Document ownership** - who allocates, who frees?
5. **Large data belongs on the heap** - don't overflow the stack

---

## Memory Layout Diagram

```
┌──────────────────────────────────────────────┐
│                    STACK                      │
│  (grows downward)                             │
│  ┌────────────────────────────────────────┐  │
│  │ main() locals                          │  │
│  │   a: i32 = 10                          │  │
│  │   p: ptr = 0x7fa0                      │←─┘
│  ├────────────────────────────────────────┤
│  │ inner() locals                         │
│  │   b: i32 = 20                          │
│  └────────────────────────────────────────┘
│                    ↓                          │
│             (empty space)                     │
│                    ↑                          │
│  ┌────────────────────────────────────────┐  │
│  │ 0x7fa0: [100 bytes allocated]          │←─┐
│  │ 0x8000: [64 bytes allocated]           │  │
│  └────────────────────────────────────────┘  │
│                    HEAP                       │
│  (grows upward)                              │
└──────────────────────────────────────────────┘
```

---

## Next Exercise

Continue to [Exercise 3: Pointers Explained](./03-pointers-explained.md) to demystify pointer arithmetic.
