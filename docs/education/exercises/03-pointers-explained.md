# Exercise 3: Pointers Explained

**Goal:** Demystify pointers - they're just numbers that happen to be addresses.

**Prerequisites:** [Exercise 2: Stack vs Heap](./02-stack-vs-heap.md)

---

## Concepts Introduced

- Pointers as memory addresses (numbers)
- Address-of operator (`&`)
- Pointer arithmetic
- Null pointers
- Multi-level pointers (pointers to pointers)

---

## The Big Reveal: Pointers Are Just Numbers

A pointer is simply an integer that represents a memory address. That's it.

```hemlock
let p = alloc(4);
print(p);           // ptr(0x7f8a2b3c4d50) - it's just a number!
print(typeof(p));   // "ptr"

// The number tells you WHERE in memory your data lives
// Address 0x7f8a2b3c4d50 is byte 140,234,567,890,000 in memory
```

**Mental model:** Memory is like a giant hotel. Each room has a number (address). A pointer is like a room key card - it just holds the room number.

---

## Part 1: The Address-Of Operator

`&variable` gives you the address where a variable is stored:

```hemlock
let x = 42;
print("x = " + x);           // 42
print("address of x: " + &x); // Some address like ptr(0x7ffc...)

// You can read through the address
let p = &x;
print("value at p: " + ptr_read_i32(p));  // 42
```

This works for stack variables and heap allocations alike.

---

## Part 2: Pointer Arithmetic

Since pointers are numbers, you can do math on them:

```hemlock
let p = alloc(20);  // 20 bytes = space for 5 integers

// Write values at different offsets
ptr_write_i32(p, 100);       // Address p + 0
ptr_write_i32(p + 4, 200);   // Address p + 4
ptr_write_i32(p + 8, 300);   // Address p + 8
ptr_write_i32(p + 12, 400);  // Address p + 12
ptr_write_i32(p + 16, 500);  // Address p + 16

// Read them back
for (let i = 0; i < 5; i++) {
    let offset = i * 4;  // 4 bytes per integer
    print("p[" + i + "] at offset " + offset + " = " + ptr_read_i32(p + offset));
}

free(p);
```

**Key insight:** `p + 4` means "the address 4 bytes after p". Hemlock does byte-level arithmetic (unlike C, which scales by type size).

---

## Part 3: Why Pointer Arithmetic is Byte-Based

Consider this memory layout:

```
Address:  1000   1001   1002   1003   1004   1005   1006   1007
          ├──────────────────┤├──────────────────┤
          │     i32 = 42     ││     i32 = 99     │
          └──────────────────┘└──────────────────┘
```

To move from one i32 to the next, you add 4 (bytes), not 1:

```hemlock
let p = alloc(8);
ptr_write_i32(p, 42);
ptr_write_i32(p + 4, 99);  // Next i32 is 4 bytes later

// WRONG: p + 1 would point into the MIDDLE of the first integer!
// ptr_write_i32(p + 1, 99);  // This corrupts data!

free(p);
```

---

## Part 4: Walking Through an Array

```hemlock
// Create an "array" of 10 doubles
let count = 10;
let elem_size = sizeof(f64);  // 8 bytes
let p = alloc(count * elem_size);

// Fill with squares
for (let i = 0; i < count; i++) {
    let addr = p + i * elem_size;
    ptr_write_f64(addr, (i + 1) * (i + 1));
}

// Sum them up
let sum = 0.0;
let current = p;
for (let i = 0; i < count; i++) {
    sum = sum + ptr_read_f64(current);
    current = current + elem_size;  // Move to next element
}

print("Sum of squares 1-10: " + sum);  // 385.0

free(p);
```

---

## Part 5: Null Pointers

A null pointer is address 0 - a "nowhere" address:

```hemlock
let p = ptr_null();
print(p);                  // ptr(0x0) or ptr(null)
print(p == ptr_null());    // true

// Dereferencing null is UNDEFINED BEHAVIOR
// ptr_read_i32(p);  // DON'T DO THIS - crash or worse!

// Always check before dereferencing
fn safe_read(p: ptr): i32 {
    if (p == ptr_null()) {
        print("Error: null pointer");
        return 0;
    }
    return ptr_read_i32(p);
}
```

**Convention:** Use `ptr_null()` to represent "no value" or "not initialized".

---

## Part 6: Pointers to Pointers

A pointer can hold the address of another pointer:

```hemlock
let x = 42;
let p1 = &x;        // p1 points to x
let p2 = &p1;       // p2 points to p1 (pointer to pointer)

print("x = " + x);
print("*p1 = " + ptr_read_i32(p1));           // 42
print("**p2 = " + ptr_read_i32(ptr_read_ptr(p2)));  // 42

// Modify x through p2
let inner_ptr = ptr_read_ptr(p2);  // Get p1's value (address of x)
ptr_write_i32(inner_ptr, 100);     // Write to x through p1
print("x is now: " + x);            // 100
```

**Why useful?** Pointers to pointers are used for:
- Arrays of strings (each string is a pointer)
- Linked data structures
- Output parameters (modify the caller's pointer)

---

## Part 7: The `memcpy` Function

Copy bytes from one location to another:

```hemlock
let src = alloc(20);
let dst = alloc(20);

// Initialize source
for (let i = 0; i < 5; i++) {
    ptr_write_i32(src + i * 4, (i + 1) * 10);
}

// Copy all 20 bytes
memcpy(dst, src, 20);

// Verify
print("Source:");
for (let i = 0; i < 5; i++) {
    print("  src[" + i + "] = " + ptr_read_i32(src + i * 4));
}

print("Destination (after copy):");
for (let i = 0; i < 5; i++) {
    print("  dst[" + i + "] = " + ptr_read_i32(dst + i * 4));
}

free(src);
free(dst);
```

---

## Challenge Exercises

### Challenge 1: Implement `memcpy` Yourself

Write a function that copies bytes from source to destination:

```hemlock
fn my_memcpy(dst: ptr, src: ptr, n: i32) {
    // Copy n bytes from src to dst
    // Hint: Use ptr_read_u8 and ptr_write_u8 for byte-by-byte copy
}

// Test it
let src = alloc(16);
let dst = alloc(16);
ptr_write_i32(src, 0x12345678);
ptr_write_i32(src + 4, 0xDEADBEEF);

my_memcpy(dst, src, 8);
print(ptr_read_i32(dst));      // Should match src
print(ptr_read_i32(dst + 4));  // Should match src

free(src);
free(dst);
```

<details>
<summary>Solution</summary>

```hemlock
fn my_memcpy(dst: ptr, src: ptr, n: i32) {
    for (let i = 0; i < n; i++) {
        let byte = ptr_read_u8(src + i);
        ptr_write_u8(dst + i, byte);
    }
}
```
</details>

### Challenge 2: Reverse an Array In-Place

Given a pointer to an array of integers, reverse it without allocating new memory:

```hemlock
fn reverse_array(p: ptr, count: i32) {
    // Swap elements from both ends, moving toward middle
}

let p = alloc(5 * sizeof(i32));
for (let i = 0; i < 5; i++) {
    ptr_write_i32(p + i * sizeof(i32), i + 1);
}

print("Before: ");
// Should print: 1 2 3 4 5

reverse_array(p, 5);

print("After: ");
// Should print: 5 4 3 2 1

free(p);
```

<details>
<summary>Solution</summary>

```hemlock
fn reverse_array(p: ptr, count: i32) {
    let elem_size = sizeof(i32);
    let left = 0;
    let right = count - 1;

    while (left < right) {
        let left_addr = p + left * elem_size;
        let right_addr = p + right * elem_size;

        // Swap
        let temp = ptr_read_i32(left_addr);
        ptr_write_i32(left_addr, ptr_read_i32(right_addr));
        ptr_write_i32(right_addr, temp);

        left = left + 1;
        right = right - 1;
    }
}
```
</details>

### Challenge 3: Build a Linked List Node

Create a simple linked list structure using raw pointers:

```hemlock
// Node layout:
// Bytes 0-3: value (i32)
// Bytes 4-11: next pointer (ptr)

fn create_node(value: i32, next: ptr): ptr {
    // Allocate and initialize a node
}

fn get_value(node: ptr): i32 {
    // Return the value from a node
}

fn get_next(node: ptr): ptr {
    // Return the next pointer from a node
}

// Create list: 1 -> 2 -> 3 -> null
let n3 = create_node(3, ptr_null());
let n2 = create_node(2, n3);
let n1 = create_node(1, n2);

// Walk the list
let current = n1;
while (current != ptr_null()) {
    print(get_value(current));
    current = get_next(current);
}

// Free all nodes
free(n1);
free(n2);
free(n3);
```

<details>
<summary>Solution</summary>

```hemlock
fn create_node(value: i32, next: ptr): ptr {
    let node = alloc(12);  // 4 bytes for i32 + 8 bytes for ptr
    ptr_write_i32(node, value);
    ptr_write_ptr(node + 4, next);
    return node;
}

fn get_value(node: ptr): i32 {
    return ptr_read_i32(node);
}

fn get_next(node: ptr): ptr {
    return ptr_read_ptr(node + 4);
}
```
</details>

---

## Pointer Arithmetic Cheat Sheet

| Operation | Meaning |
|-----------|---------|
| `p + n` | Address n bytes after p |
| `p - n` | Address n bytes before p |
| `p + i * sizeof(T)` | Address of i-th element of type T |
| `p == q` | Do p and q point to the same address? |
| `p == ptr_null()` | Is p a null pointer? |

---

## Key Takeaways

1. **Pointers are addresses** - Just numbers representing memory locations
2. **Arithmetic is byte-based** - Add `sizeof(T)` to move between elements
3. **Null is zero** - `ptr_null()` represents "no address"
4. **Pointers can point to pointers** - Multi-level indirection is possible
5. **Check before dereferencing** - Null dereference is undefined behavior

---

## Next Exercise

Continue to [Exercise 4: Numeric Types](./04-numeric-types.md) to understand how numbers are represented in memory.
