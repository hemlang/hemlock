# Hemlock Arena Module

A standard library module providing memory arenas for efficient bulk allocation and deallocation.

## Overview

Memory arenas (also called "bump allocators" or "linear allocators") are a memory management pattern where:

1. **Allocation is O(1)** - Just increment a pointer ("bump" it forward)
2. **No fragmentation** - Allocations are contiguous
3. **Bulk deallocation** - Free everything at once with `reset()` or `destroy()`
4. **No per-allocation tracking** - No headers, no free lists

This makes arenas ideal for scenarios where many allocations share a common lifetime:
- Per-frame game allocations
- Per-request server allocations
- Parser/compiler temporary data
- Any "allocate a bunch, then free all at once" pattern

## Usage

```hemlock
import { Arena, GrowingArena, ScratchScope } from "@stdlib/arena";
```

Or import all:

```hemlock
import * as arena from "@stdlib/arena";
let a = arena.Arena(4096);
```

---

## Arena

Fixed-size memory arena with bump allocation.

### API

```hemlock
let a = Arena(capacity);  // capacity in bytes
```

**Methods:**
- `a.alloc(size)` - Allocate `size` bytes (returns `ptr` or `null` if full)
- `a.alloc_aligned(size, alignment)` - Allocate with alignment (power of 2)
- `a.alloc_zeroed(size)` - Allocate and zero-initialize
- `a.alloc_with_size(size)` - Returns `{ptr, size}` for bounds tracking
- `a.save()` - Save current position (returns offset)
- `a.restore(offset)` - Restore to saved position (frees allocations after it)
- `a.reset()` - Reset arena (free all allocations, keep memory)
- `a.destroy()` - Destroy arena (free backing memory)
- `a.can_alloc(size)` - Check if allocation would succeed
- `a.is_destroyed()` - Check if arena has been destroyed
- `a.base_ptr()` - Get base memory pointer (advanced)
- `a.current_ptr()` - Get current allocation pointer (advanced)

**Properties:**
- `a.used` - Bytes currently allocated
- `a.capacity` - Total capacity in bytes
- `a.remaining` - Bytes available for allocation

### Example

```hemlock
import { Arena } from "@stdlib/arena";

// Create a 64KB arena
let arena = Arena(64 * 1024);

// Allocate some memory
let p1 = arena.alloc(128);
let p2 = arena.alloc(256);
let p3 = arena.alloc_aligned(64, 16);  // 16-byte aligned

print("Used:", arena.used);           // ~448 bytes
print("Remaining:", arena.remaining); // ~65088 bytes

// Reset frees everything instantly
arena.reset();
print("Used after reset:", arena.used);  // 0

// Destroy when done
arena.destroy();
```

### Save/Restore Pattern

Use `save()` and `restore()` for temporary allocations:

```hemlock
import { Arena } from "@stdlib/arena";

let arena = Arena(4096);

// Some persistent allocations
let persistent = arena.alloc(100);

// Save position before temporary work
let mark = arena.save();

// Temporary allocations
let temp1 = arena.alloc(500);
let temp2 = arena.alloc(500);
// ... use temp1 and temp2 ...

// Restore frees temp1 and temp2
arena.restore(mark);

// persistent is still valid!
print("Used:", arena.used);  // 100 (only persistent remains)
```

---

## GrowingArena

An arena that automatically grows when full by allocating new chunks.

### API

```hemlock
let a = GrowingArena(chunk_size?);  // Default: 64KB chunks
```

**Methods:**
- `a.alloc(size)` - Allocate `size` bytes (grows if needed)
- `a.alloc_aligned(size, alignment)` - Allocate with alignment
- `a.alloc_zeroed(size)` - Allocate and zero-initialize
- `a.reset()` - Reset all chunks (keeps memory)
- `a.destroy()` - Free all chunk memory
- `a.is_destroyed()` - Check if destroyed
- `a.can_alloc_without_grow(size)` - Check if fits in current chunk

**Properties:**
- `a.used` - Total bytes allocated
- `a.capacity` - Total capacity across all chunks
- `a.chunk_count` - Number of chunks allocated

### Example

```hemlock
import { GrowingArena } from "@stdlib/arena";

// Create with 16KB chunks
let arena = GrowingArena(16 * 1024);

// Allocate freely - arena grows as needed
for (let i = 0; i < 1000; i++) {
    let p = arena.alloc(100);
    ptr_write_i32(p, i);
}

print("Chunks:", arena.chunk_count);  // Multiple chunks
print("Used:", arena.used);           // 100,000 bytes

arena.destroy();
```

---

## ScratchScope

A convenience wrapper for temporary allocations that auto-restores.

### API

```hemlock
let scratch = ScratchScope(arena);
```

**Methods:**
- `scratch.alloc(size)` - Allocate from underlying arena
- `scratch.alloc_aligned(size, alignment)` - Allocate with alignment
- `scratch.alloc_zeroed(size)` - Allocate zeroed
- `scratch.release()` - Release all allocations (restore arena)
- `scratch.is_released()` - Check if already released

**Properties:**
- `scratch.arena` - The underlying arena

### Example

```hemlock
import { Arena, ScratchScope } from "@stdlib/arena";

let arena = Arena(64 * 1024);

fn process_data(data) {
    // Create scratch scope for temporary allocations
    let scratch = ScratchScope(arena);
    defer scratch.release();  // Auto-release when function returns

    let temp_buffer = scratch.alloc(1024);
    let work_area = scratch.alloc(2048);

    // ... do work with temp_buffer and work_area ...

    return result;
    // scratch.release() called automatically by defer
}
```

---

## Typed Allocation Helpers

Convenience functions for common allocation patterns.

### Size Constants

```hemlock
SIZEOF_I8   // 1
SIZEOF_I16  // 2
SIZEOF_I32  // 4
SIZEOF_I64  // 8
SIZEOF_U8   // 1
SIZEOF_U16  // 2
SIZEOF_U32  // 4
SIZEOF_U64  // 8
SIZEOF_F32  // 4
SIZEOF_F64  // 8
SIZEOF_PTR  // 8 (64-bit)
```

### Helper Functions

```hemlock
arena_alloc_i32_array(arena, count)  // Allocate array of i32
arena_alloc_i64_array(arena, count)  // Allocate array of i64
arena_alloc_f64_array(arena, count)  // Allocate array of f64
arena_alloc_ptr_array(arena, count)  // Allocate array of pointers
arena_copy_string(arena, str)        // Copy string into arena (null-terminated)
```

### Example

```hemlock
import { Arena, arena_alloc_i32_array, arena_copy_string, SIZEOF_I32 } from "@stdlib/arena";

let arena = Arena(4096);

// Allocate array of 100 integers
let nums = arena_alloc_i32_array(arena, 100);
for (let i = 0; i < 100; i++) {
    ptr_write_i32(nums + i * SIZEOF_I32, i * i);
}

// Copy string into arena
let name = arena_copy_string(arena, "Hello, Arena!");

arena.destroy();
```

---

## Real-World Patterns

### Game Frame Allocator

```hemlock
import { Arena } from "@stdlib/arena";

// 1MB per-frame arena
let frame_arena = Arena(1024 * 1024);

fn game_loop() {
    loop {
        // Reset at start of each frame - instant "free" of last frame's data
        frame_arena.reset();

        // All frame allocations use the arena
        let entities = frame_arena.alloc(entity_count * 64);
        let particles = frame_arena.alloc(particle_count * 32);
        let collision_pairs = frame_arena.alloc(pair_count * 16);

        update_entities(entities);
        simulate_particles(particles);
        resolve_collisions(collision_pairs);
        render();

        // No individual frees needed!
    }
}
```

### Request Handler

```hemlock
import { Arena } from "@stdlib/arena";

async fn handle_request(req) {
    // Per-request arena
    let arena = Arena(32 * 1024);
    defer arena.destroy();

    // All request processing uses arena
    let headers = parse_headers(req.raw, arena);
    let body = parse_body(req.raw, arena);
    let result = process(headers, body, arena);

    // Only the response escapes the arena
    return serialize(result);
}  // arena.destroy() called by defer
```

### Parser/Compiler

```hemlock
import { Arena, GrowingArena } from "@stdlib/arena";

fn compile(source) {
    // Separate arenas for different lifetimes
    let token_arena = Arena(64 * 1024);    // Tokens freed after parsing
    let ast_arena = GrowingArena();         // AST might be large
    let string_arena = Arena(32 * 1024);    // Interned strings

    // Lexing phase
    let tokens = lex(source, token_arena, string_arena);

    // Parsing phase - tokens no longer needed after this
    let ast = parse(tokens, ast_arena);
    token_arena.destroy();  // Free token memory early

    // Code generation
    let code = codegen(ast);

    // Cleanup
    ast_arena.destroy();
    string_arena.destroy();

    return code;
}
```

### Temporary Work Buffer

```hemlock
import { Arena, ScratchScope } from "@stdlib/arena";

// Shared scratch arena for the module
let scratch = Arena(256 * 1024);

fn complex_calculation(input) {
    let scope = ScratchScope(scratch);
    defer scope.release();

    // Temporary matrices
    let m1 = scope.alloc(1024);
    let m2 = scope.alloc(1024);
    let temp = scope.alloc(1024);

    // ... lots of temporary work ...

    // Copy result out before scope releases
    let result = alloc(64);
    memcpy(result, temp, 64);
    return result;
}  // scope.release() frees m1, m2, temp
```

---

## Performance Characteristics

### Arena (Fixed)
- `alloc()`: O(1) - just increment offset
- `alloc_aligned()`: O(1) - align then increment
- `reset()`: O(1) - just set offset to 0
- `destroy()`: O(1) - single free call
- `save()`/`restore()`: O(1)

### GrowingArena
- `alloc()`: O(1) amortized - may allocate new chunk
- `reset()`: O(n) where n = number of chunks
- `destroy()`: O(n) where n = number of chunks

### Memory Overhead
- **Arena**: Single allocation, ~0 bytes overhead per allocation
- **GrowingArena**: One allocation per chunk, ~24 bytes metadata per chunk

---

## Comparison with Regular Allocation

| Operation | `alloc()`/`free()` | Arena |
|-----------|-------------------|-------|
| Single allocation | O(1) avg, fragmentation | O(1), no fragmentation |
| N allocations | O(N), N headers | O(N), 0 headers |
| Free all | O(N) free calls | O(1) reset |
| Memory overhead | ~16-32 bytes/alloc | 0 bytes/alloc |
| Fragmentation | Yes | No (within arena) |

---

## Design Philosophy

Arenas embody Hemlock's "explicit over implicit" philosophy:

1. **You control the lifetime** - Arena doesn't guess when to free
2. **You see the memory** - Direct pointer access, no magic
3. **You choose the tradeoff** - Fixed vs. growing, aligned vs. unaligned
4. **Fast path is obvious** - Bump allocation is visibly O(1)

Arenas are NOT garbage collection. They're a tool for when you know allocations share a lifetime and want maximum performance with minimal bookkeeping.

---

## Common Mistakes

### Using arena after destroy
```hemlock
let arena = Arena(4096);
arena.destroy();
let p = arena.alloc(64);  // PANIC: Cannot allocate from destroyed arena
```

### Forgetting to destroy
```hemlock
fn process() {
    let arena = Arena(4096);
    let p = arena.alloc(64);
    return result;
    // LEAK: arena never destroyed!
}

// Fix: use defer
fn process() {
    let arena = Arena(4096);
    defer arena.destroy();
    let p = arena.alloc(64);
    return result;  // arena.destroy() called automatically
}
```

### Pointer escaping arena lifetime
```hemlock
fn bad() {
    let arena = Arena(4096);
    let p = arena.alloc(64);
    arena.destroy();
    return p;  // BUG: p points to freed memory!
}

// Fix: copy data out before destroy
fn good() {
    let arena = Arena(4096);
    let p = arena.alloc(64);
    // ... fill p with data ...
    let result = alloc(64);
    memcpy(result, p, 64);
    arena.destroy();
    return result;  // OK: result is independent copy
}
```

---

## Testing

```bash
# Run arena tests
./hemlock tests/stdlib/test_arena.hml
```

---

## License

Part of the Hemlock standard library.
