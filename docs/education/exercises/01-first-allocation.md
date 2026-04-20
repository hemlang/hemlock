# Exercise 1: Your First Allocation

**Goal:** Understand the basics of manual memory allocation and deallocation.

**Prerequisites:** None - this is where you start!

---

## Concepts Introduced

- `alloc(size)` - Request bytes from the heap
- `free(ptr)` - Return memory to the heap
- `ptr_write_*` / `ptr_read_*` - Write/read values through pointers
- Memory as a sequence of bytes

---

## Part 1: Allocating a Single Integer

An `i32` (32-bit integer) requires 4 bytes of storage. Let's allocate space for one:

```hemlock
// Step 1: Request 4 bytes from the heap
let p = alloc(4);
print("Allocated memory at: " + p);

// Step 2: Write a value to that memory
ptr_write_i32(p, 42);
print("Wrote value 42");

// Step 3: Read the value back
let value = ptr_read_i32(p);
print("Read back: " + value);

// Step 4: Release the memory
free(p);
print("Memory freed");
```

**Run this and observe:**
- The pointer value (memory address)
- The value survives write → read
- No errors on free

---

## Part 2: Understanding Bytes

Let's see how an integer is actually stored:

```hemlock
let p = alloc(4);
ptr_write_i32(p, 0x12345678);  // Hex value for clarity

// Read individual bytes
print("Byte 0: " + ptr_read_u8(p));
print("Byte 1: " + ptr_read_u8(p + 1));
print("Byte 2: " + ptr_read_u8(p + 2));
print("Byte 3: " + ptr_read_u8(p + 3));

free(p);
```

**Expected output (on little-endian systems):**
```
Byte 0: 120   (0x78)
Byte 1: 86    (0x56)
Byte 2: 52    (0x34)
Byte 3: 18    (0x12)
```

**Key insight:** The bytes are stored in reverse order! This is called "little-endian" byte order, used by most modern CPUs (x86, ARM). The "least significant byte" comes first.

---

## Part 3: Multiple Values

Allocate space for multiple integers:

```hemlock
// 3 integers × 4 bytes each = 12 bytes
let p = alloc(12);

// Write three values
ptr_write_i32(p, 10);          // First integer at offset 0
ptr_write_i32(p + 4, 20);      // Second integer at offset 4
ptr_write_i32(p + 8, 30);      // Third integer at offset 8

// Read them back
print("Value 0: " + ptr_read_i32(p));
print("Value 1: " + ptr_read_i32(p + 4));
print("Value 2: " + ptr_read_i32(p + 8));

free(p);
```

**Key insight:** Pointer arithmetic! `p + 4` means "4 bytes after p". You must calculate offsets manually.

---

## Part 4: Using `sizeof`

Instead of hardcoding sizes, use `sizeof`:

```hemlock
let int_size = sizeof(i32);
print("Size of i32: " + int_size + " bytes");

let count = 5;
let p = alloc(count * int_size);

for (let i = 0; i < count; i++) {
    ptr_write_i32(p + i * int_size, i * 10);
}

for (let i = 0; i < count; i++) {
    print("arr[" + i + "] = " + ptr_read_i32(p + i * int_size));
}

free(p);
```

**Output:**
```
Size of i32: 4 bytes
arr[0] = 0
arr[1] = 10
arr[2] = 20
arr[3] = 30
arr[4] = 40
```

---

## Part 5: Using `memset`

Initialize memory to a specific byte value:

```hemlock
let p = alloc(16);

// Fill with zeros
memset(p, 0, 16);
print("After memset(0): " + ptr_read_i32(p));  // 0

// Fill with a pattern (careful: this sets BYTES, not integers)
memset(p, 0xFF, 16);
print("After memset(0xFF): " + ptr_read_i32(p));  // -1 (all bits set)

free(p);
```

**Key insight:** `memset` sets individual bytes, not values. `memset(p, 1, 4)` doesn't set an integer to 1 - it sets each byte to 1, resulting in `0x01010101` (16843009 in decimal).

---

## Challenge Exercises

### Challenge 1: Build an Array

Write a program that:
1. Allocates space for 10 integers
2. Fills them with values 1-10
3. Calculates and prints the sum
4. Frees the memory

<details>
<summary>Solution</summary>

```hemlock
let count = 10;
let p = alloc(count * sizeof(i32));

// Fill with 1-10
for (let i = 0; i < count; i++) {
    ptr_write_i32(p + i * sizeof(i32), i + 1);
}

// Calculate sum
let sum = 0;
for (let i = 0; i < count; i++) {
    sum = sum + ptr_read_i32(p + i * sizeof(i32));
}

print("Sum: " + sum);  // Should be 55

free(p);
```
</details>

### Challenge 2: Swap Two Values

Write a function that swaps two integers in memory using a temporary variable:

```hemlock
fn swap(p: ptr, i: i32, j: i32) {
    // Your code here
    // Swap values at offsets i*4 and j*4
}

let p = alloc(12);
ptr_write_i32(p, 10);
ptr_write_i32(p + 4, 20);
ptr_write_i32(p + 8, 30);

swap(p, 0, 2);  // Swap first and third

print(ptr_read_i32(p));      // Should be 30
print(ptr_read_i32(p + 8));  // Should be 10

free(p);
```

<details>
<summary>Solution</summary>

```hemlock
fn swap(p: ptr, i: i32, j: i32) {
    let offset_i = i * sizeof(i32);
    let offset_j = j * sizeof(i32);

    let temp = ptr_read_i32(p + offset_i);
    ptr_write_i32(p + offset_i, ptr_read_i32(p + offset_j));
    ptr_write_i32(p + offset_j, temp);
}
```
</details>

### Challenge 3: Different Types

Allocate memory for a "struct" containing:
- An i32 (4 bytes)
- An f64 (8 bytes)
- An i8 (1 byte)

Write values and read them back.

<details>
<summary>Solution</summary>

```hemlock
// Layout: i32 at 0, f64 at 4, i8 at 12
// Total size: 13 bytes (but we'd typically align to 16)
let p = alloc(16);

ptr_write_i32(p, 42);           // i32 at offset 0
ptr_write_f64(p + 4, 3.14159);  // f64 at offset 4
ptr_write_i8(p + 12, 7);        // i8 at offset 12

print("i32: " + ptr_read_i32(p));
print("f64: " + ptr_read_f64(p + 4));
print("i8: " + ptr_read_i8(p + 12));

free(p);
```

**Note:** Real C compilers add padding for alignment. Hemlock's interpreter is more forgiving, but understanding alignment matters for FFI.
</details>

---

## Common Mistakes

### Mistake 1: Wrong Size

```hemlock
let p = alloc(4);        // Only 4 bytes
ptr_write_i64(p, 123);   // i64 needs 8 bytes - overflow!
free(p);
```

### Mistake 2: Off-by-One in Offset

```hemlock
let p = alloc(12);
ptr_write_i32(p, 1);
ptr_write_i32(p + 3, 2);  // WRONG! Should be p + 4
                          // This overlaps with first integer
free(p);
```

### Mistake 3: Forgetting to Free

```hemlock
fn leaky() {
    let p = alloc(100);
    // ... use p ...
    // Forgot free(p)!
}
```

---

## Key Takeaways

1. **`alloc(n)`** requests `n` bytes from the heap and returns a pointer
2. **Pointer arithmetic** is in bytes, not elements
3. **`sizeof(type)`** gives the size of a type in bytes
4. **Always `free()`** what you `alloc()`
5. **Memory is just bytes** - interpretation depends on how you read it

---

## Next Exercise

Continue to [Exercise 2: Stack vs Heap](./02-stack-vs-heap.md) to understand where your variables actually live.
