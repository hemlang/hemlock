# Hemlock Bytecode VM Implementation Plan

## Overview

A stack-based bytecode virtual machine as a third execution backend for Hemlock, providing faster execution than tree-walking interpretation while maintaining full parity with existing backends.

```
Source (.hml)
    ↓
┌─────────────────────────────┐
│  SHARED FRONTEND            │
│  - Lexer, Parser, AST       │
│  - Resolver (variable res)  │
└─────────────────────────────┘
    ↓           ↓           ↓
┌────────┐  ┌────────┐  ┌────────┐
│Interp  │  │  VM    │  │Compiler│
│(tree)  │  │(byte)  │  │(C gen) │
└────────┘  └────────┘  └────────┘
```

---

## Directory Structure

```
src/backends/vm/
├── main.c              # Entry point, CLI handling
├── vm.h                # Public VM API
├── vm.c                # Main execution loop (dispatch)
├── chunk.h             # Bytecode chunk format
├── chunk.c             # Chunk creation, constant pool
├── instruction.h       # Opcode definitions
├── compiler.h          # AST → bytecode compiler API
├── compiler.c          # Main compilation driver
├── compile_expr.c      # Expression compilation
├── compile_stmt.c      # Statement compilation
├── compile_call.c      # Function/method call compilation
├── frame.h             # Call frame management
├── frame.c             # Stack frame operations
├── debug.h             # Disassembler, debugging
├── debug.c             # Bytecode disassembly
└── Makefile            # Build rules
```

---

## Instruction Set Architecture

### Encoding Format

```
┌─────────────────────────────────────────────────────────────┐
│  8-bit opcode  │  operands (variable length, 0-3 bytes)    │
└─────────────────────────────────────────────────────────────┘

Operand types:
- None:     OP alone
- Byte:     OP + 1-byte immediate
- Short:    OP + 2-byte immediate (big-endian)
- Constant: OP + 2-byte constant pool index
- Jump:     OP + 2-byte signed offset
```

### Complete Instruction Set (82 opcodes)

#### Category 1: Constants & Literals (10 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_CONST` | `[op][idx:16]` | +1 | Push constant from pool |
| `OP_CONST_BYTE` | `[op][val:8]` | +1 | Push small integer (0-255) |
| `OP_NULL` | `[op]` | +1 | Push null |
| `OP_TRUE` | `[op]` | +1 | Push true |
| `OP_FALSE` | `[op]` | +1 | Push false |
| `OP_ARRAY` | `[op][count:16]` | -(n-1) | Pop n, push array |
| `OP_OBJECT` | `[op][count:16]` | -(2n-1) | Pop n key-value pairs, push object |
| `OP_STRING_INTERP` | `[op][parts:16]` | -(n-1) | Interpolate n parts into string |
| `OP_CLOSURE` | `[op][idx:16][upvals:8]` | +1 | Create closure with upvalues |
| `OP_ENUM_VALUE` | `[op][idx:16]` | +1 | Push enum variant value |

#### Category 2: Variables (12 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_GET_LOCAL` | `[op][slot:8]` | +1 | Load local variable |
| `OP_SET_LOCAL` | `[op][slot:8]` | 0 | Store to local (keep on stack) |
| `OP_GET_UPVALUE` | `[op][slot:8]` | +1 | Load captured variable |
| `OP_SET_UPVALUE` | `[op][slot:8]` | 0 | Store to captured variable |
| `OP_GET_GLOBAL` | `[op][idx:16]` | +1 | Load global by name |
| `OP_SET_GLOBAL` | `[op][idx:16]` | 0 | Store to global |
| `OP_DEFINE_GLOBAL` | `[op][idx:16]` | -1 | Define new global |
| `OP_GET_PROPERTY` | `[op][idx:16]` | 0 | Get object property |
| `OP_SET_PROPERTY` | `[op][idx:16]` | -1 | Set object property |
| `OP_GET_INDEX` | `[op]` | -1 | array[index] or object[key] |
| `OP_SET_INDEX` | `[op]` | -2 | array[index] = value |
| `OP_CLOSE_UPVALUE` | `[op]` | 0 | Close upvalue on stack top |

#### Category 3: Arithmetic (11 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_ADD` | `[op]` | -1 | a + b (type promotion, string concat) |
| `OP_SUB` | `[op]` | -1 | a - b |
| `OP_MUL` | `[op]` | -1 | a * b |
| `OP_DIV` | `[op]` | -1 | a / b (always f64) |
| `OP_MOD` | `[op]` | -1 | a % b |
| `OP_NEGATE` | `[op]` | 0 | -a |
| `OP_INC` | `[op]` | 0 | ++a (in-place) |
| `OP_DEC` | `[op]` | 0 | --a (in-place) |
| `OP_ADD_I32` | `[op]` | -1 | Fast path: i32 + i32 |
| `OP_SUB_I32` | `[op]` | -1 | Fast path: i32 - i32 |
| `OP_MUL_I32` | `[op]` | -1 | Fast path: i32 * i32 |

#### Category 4: Comparison (8 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_EQ` | `[op]` | -1 | a == b |
| `OP_NE` | `[op]` | -1 | a != b |
| `OP_LT` | `[op]` | -1 | a < b |
| `OP_LE` | `[op]` | -1 | a <= b |
| `OP_GT` | `[op]` | -1 | a > b |
| `OP_GE` | `[op]` | -1 | a >= b |
| `OP_EQ_I32` | `[op]` | -1 | Fast path: i32 == i32 |
| `OP_LT_I32` | `[op]` | -1 | Fast path: i32 < i32 |

#### Category 5: Logical & Bitwise (9 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_NOT` | `[op]` | 0 | !a |
| `OP_BIT_NOT` | `[op]` | 0 | ~a |
| `OP_BIT_AND` | `[op]` | -1 | a & b |
| `OP_BIT_OR` | `[op]` | -1 | a \| b |
| `OP_BIT_XOR` | `[op]` | -1 | a ^ b |
| `OP_LSHIFT` | `[op]` | -1 | a << b |
| `OP_RSHIFT` | `[op]` | -1 | a >> b |
| `OP_COALESCE` | `[op][offset:16]` | 0 or -1 | a ?? b (short-circuit) |
| `OP_OPTIONAL_CHAIN` | `[op][offset:16]` | 0 | a?.b (short-circuit if null) |

#### Category 6: Control Flow (12 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_JUMP` | `[op][offset:16]` | 0 | Unconditional jump |
| `OP_JUMP_IF_FALSE` | `[op][offset:16]` | -1 | Jump if top is falsy |
| `OP_JUMP_IF_TRUE` | `[op][offset:16]` | -1 | Jump if top is truthy |
| `OP_JUMP_IF_FALSE_POP` | `[op][offset:16]` | -1 | Jump if false, always pop |
| `OP_LOOP` | `[op][offset:16]` | 0 | Jump backward (loop) |
| `OP_BREAK` | `[op]` | 0 | Break from loop |
| `OP_CONTINUE` | `[op]` | 0 | Continue to next iteration |
| `OP_SWITCH` | `[op][cases:16]` | -1 | Jump table dispatch |
| `OP_CASE` | `[op][offset:16]` | 0 | Case label marker |
| `OP_FOR_IN_INIT` | `[op]` | +1 | Initialize for-in iterator |
| `OP_FOR_IN_NEXT` | `[op][offset:16]` | +1 or jump | Get next or jump to end |
| `OP_POP` | `[op]` | -1 | Discard top of stack |

#### Category 7: Functions & Calls (8 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_CALL` | `[op][argc:8]` | -(argc) | Call function |
| `OP_CALL_METHOD` | `[op][idx:16][argc:8]` | -(argc) | Call method on object |
| `OP_CALL_BUILTIN` | `[op][id:16][argc:8]` | -(argc) | Call builtin function |
| `OP_RETURN` | `[op]` | special | Return from function |
| `OP_APPLY` | `[op]` | -1 | apply(fn, args_array) |
| `OP_TAIL_CALL` | `[op][argc:8]` | special | Tail call optimization |
| `OP_SUPER` | `[op][idx:16]` | 0 | Access super method |
| `OP_INVOKE` | `[op][idx:16][argc:8]` | -(argc) | Optimized method call |

#### Category 8: Exception Handling (6 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_TRY` | `[op][catch:16][finally:16]` | 0 | Begin try block |
| `OP_CATCH` | `[op]` | +1 | Begin catch (push exception) |
| `OP_FINALLY` | `[op]` | 0 | Begin finally block |
| `OP_END_TRY` | `[op]` | 0 | End try-catch-finally |
| `OP_THROW` | `[op]` | -1 | Throw exception |
| `OP_DEFER` | `[op][idx:16]` | 0 | Register deferred call |

#### Category 9: Async & Concurrency (8 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_SPAWN` | `[op][argc:8]` | -(argc) | Spawn async task |
| `OP_AWAIT` | `[op]` | 0 | Await task result |
| `OP_JOIN` | `[op]` | 0 | Join task (explicit) |
| `OP_DETACH` | `[op]` | -1 | Detach task |
| `OP_CHANNEL` | `[op]` | 0 | Create channel (capacity on stack) |
| `OP_SEND` | `[op]` | -2 | Send on channel |
| `OP_RECV` | `[op]` | 0 | Receive from channel |
| `OP_SELECT` | `[op]` | 0 | Select on multiple channels |

#### Category 10: Type Operations (5 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_TYPEOF` | `[op]` | 0 | Get type string |
| `OP_CAST` | `[op][type:8]` | 0 | Explicit type cast |
| `OP_CHECK_TYPE` | `[op][type:8]` | 0 | Runtime type check |
| `OP_DEFINE_TYPE` | `[op][idx:16]` | 0 | Register type definition |
| `OP_DEFINE_ENUM` | `[op][idx:16]` | 0 | Register enum definition |

#### Category 11: Debug & Misc (3 opcodes)

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|--------------|-------------|
| `OP_NOP` | `[op]` | 0 | No operation |
| `OP_PRINT` | `[op][argc:8]` | -(argc) | Print values |
| `OP_ASSERT` | `[op]` | -1 or -2 | Assert with optional message |

**Total: 82 opcodes**

---

## Constant Pool Design

```c
typedef enum {
    CONST_I32,
    CONST_I64,
    CONST_F64,
    CONST_STRING,
    CONST_FUNCTION,
    CONST_IDENTIFIER,  // for globals/properties
} ConstantType;

typedef struct {
    ConstantType type;
    union {
        int32_t i32;
        int64_t i64;
        double f64;
        struct { char *data; int len; } string;
        struct Chunk *function;
    } as;
} Constant;

typedef struct Chunk {
    uint8_t *code;          // Bytecode array
    int code_count;
    int code_capacity;

    Constant *constants;    // Constant pool
    int const_count;
    int const_capacity;

    int *lines;             // Line numbers for debugging

    // Function metadata
    char *name;
    int arity;
    int upvalue_count;
    bool is_async;

    // Type annotations (optional)
    ValueType *param_types;
    ValueType return_type;
} Chunk;
```

---

## VM Execution State

```c
typedef struct {
    Chunk *chunk;           // Current bytecode chunk
    uint8_t *ip;            // Instruction pointer
    Value *slots;           // Local variable slots
    int slot_count;
    struct CallFrame *prev; // For call stack
} CallFrame;

typedef struct {
    // Value stack
    Value *stack;
    Value *stack_top;
    int stack_capacity;

    // Call frames
    CallFrame *frames;
    int frame_count;
    int frame_capacity;

    // Upvalues (for closures)
    ObjUpvalue *open_upvalues;

    // Global variables
    Table globals;

    // Control flow state
    bool is_returning;
    Value return_value;
    bool is_throwing;
    Value exception;
    bool is_breaking;
    bool is_continuing;

    // Defer stack
    DeferEntry *defers;
    int defer_count;

    // GC/memory
    Object *objects;        // All allocated objects (for cleanup)
    size_t bytes_allocated;
} VM;
```

---

## Builtin Function Table

Map builtin IDs to function pointers:

```c
typedef Value (*BuiltinFn)(VM *vm, int argc, Value *args);

typedef struct {
    const char *name;
    BuiltinFn fn;
    int min_arity;
    int max_arity;  // -1 for variadic
} BuiltinEntry;

// 67 builtins organized by category
static BuiltinEntry builtins[] = {
    // Memory (0-10)
    {"alloc", builtin_alloc, 1, 1},
    {"talloc", builtin_talloc, 2, 2},
    {"realloc", builtin_realloc, 2, 2},
    {"free", builtin_free, 1, 1},
    {"memset", builtin_memset, 3, 3},
    {"memcpy", builtin_memcpy, 3, 3},
    {"sizeof", builtin_sizeof, 1, 1},
    {"buffer", builtin_buffer, 1, 1},
    {"ptr_to_buffer", builtin_ptr_to_buffer, 2, 2},
    {"buffer_ptr", builtin_buffer_ptr, 1, 1},
    {"ptr_null", builtin_ptr_null, 0, 0},

    // I/O (11-14)
    {"print", builtin_print, 0, -1},
    {"eprint", builtin_eprint, 0, -1},
    {"read_line", builtin_read_line, 0, 0},
    {"open", builtin_open, 2, 2},

    // Type (15-17)
    {"typeof", builtin_typeof, 1, 1},
    {"assert", builtin_assert, 1, 2},
    {"panic", builtin_panic, 1, 1},

    // Async (18-24)
    {"spawn", builtin_spawn, 1, -1},
    {"join", builtin_join, 1, 1},
    {"detach", builtin_detach, 1, 1},
    {"channel", builtin_channel, 1, 1},
    {"select", builtin_select, 1, 2},
    {"task_debug_info", builtin_task_debug_info, 1, 1},
    {"apply", builtin_apply, 2, 2},

    // Pointer ops (25-50)
    {"ptr_read_i32", builtin_ptr_read_i32, 1, 1},
    {"ptr_write_i32", builtin_ptr_write_i32, 2, 2},
    // ... (all ptr_read/write variants)

    // Atomics (51-67)
    {"atomic_load_i32", builtin_atomic_load_i32, 1, 1},
    {"atomic_store_i32", builtin_atomic_store_i32, 2, 2},
    {"atomic_add_i32", builtin_atomic_add_i32, 2, 2},
    // ... (all atomic variants)
    {"atomic_fence", builtin_atomic_fence, 0, 0},

    // ... more builtins
};
```

---

## Test Suite Strategy

### Phase 1: Core Language (46 tests)

Run in order of dependency:

```
Phase 1a: Primitives & Variables (8 tests)
├── primitives.hml           ✓ literals, basic types
├── booleans.hml             ✓ boolean ops
├── constants.hml            ✓ const declarations
├── scientific_notation.hml  ✓ number parsing
├── variables.hml            ✓ let, assignment
├── scope_shadowing.hml      ✓ nested scopes
├── runtime_prefix_names.hml ✓ edge case names
└── string_interpolation.hml ✓ template strings

Phase 1b: Operations (9 tests)
├── arithmetic.hml           ✓ +, -, *, /, %
├── compound_assignment.hml  ✓ +=, -=, etc.
├── increment_decrement.hml  ✓ ++, --
├── comparisons.hml          ✓ ==, <, >, etc.
├── truthiness.hml           ✓ truthy/falsy
├── logical.hml              ✓ &&, ||, !
├── null_coalesce.hml        ✓ ??
├── bitwise.hml              ✓ &, |, ^, <<, >>
└── ternary.hml              ✓ ? :

Phase 1c: Control Flow (7 tests)
├── if_else.hml              ✓ if/else if/else
├── loops.hml                ✓ while, for
├── for_in.hml               ✓ for-in iteration
├── break_continue.hml       ✓ loop control
├── switch.hml               ✓ switch/case
├── switch_fallthrough.hml   ✓ fallthrough
└── defer.hml                ✓ defer statement

Phase 1d: Functions (8 tests)
├── functions.hml            ✓ definition, calls
├── recursion.hml            ✓ recursive calls
├── higher_order.hml         ✓ first-class functions
├── closures.hml             ✓ closure capture
├── nested_closures.hml      ✓ multi-level capture
├── optional_params.hml      ✓ default parameters
├── rest_params.hml          ✓ ...args
└── many_params.hml          ✓ large param counts

Phase 1e: Data Structures (7 tests)
├── arrays.hml               ✓ array ops
├── typed_arrays.hml         ✓ array<T>
├── objects.hml              ✓ object ops
├── object_edge_cases.hml    ✓ edge cases
├── type_definitions.hml     ✓ define
├── enums.hml                ✓ enum
└── string_utf8_iteration.hml ✓ UTF-8

Phase 1f: Error Handling & Types (7 tests)
├── exceptions.hml           ✓ try/catch/throw
├── conversions.hml          ✓ type conversion
├── type_coercion.hml        ✓ coercion rules
├── type_promotion.hml       ✓ i32 → i64 → f64
├── type_checking.hml        ✓ runtime checks
├── c_keyword_names.hml      ✓ edge case
└── string_null_concat.hml   ✓ null handling
```

### Phase 2: Builtins (36 tests)

```
Phase 2a: Memory & Pointers (5 tests)
├── memory.hml               ✓ alloc/free/buffer
├── pointer_ops.hml          ✓ pointer arithmetic
├── read_ptr.hml             ✓ ptr_read/write
├── sizeof.hml               ✓ sizeof
└── buffers.hml              ✓ bounded buffers

Phase 2b: I/O & System (8 tests)
├── print_typeof.hml         ✓ print, typeof
├── file_io.hml              ✓ open/read/write/close
├── env.hml                  ✓ getenv/setenv
├── exec.hml                 ✓ exec commands
├── exec_argv.hml            ✓ exec with args
├── signals.hml              ✓ signal handling
├── signal_tty_constants.hml ✓ signal constants
└── stack_limit.hml          ✓ stack limit

Phase 2c: Async & Concurrency (5 tests)
├── async.hml                ✓ spawn/join/await
├── async_many_args.hml      ✓ many async args
├── channels.hml             ✓ channel ops
├── unbuffered_channels.hml  ✓ unbuffered semantics
└── channel_timeout.hml      ✓ timeout

Phase 2d: FFI (5 tests)
├── ffi_callback.hml         ✓ callbacks
├── ffi_float.hml            ✓ float args
├── ffi_ptr_helpers.hml      ✓ ptr helpers
├── ffi_struct.hml           ✓ struct support
└── ffi_struct_concurrent.hml ✓ concurrent FFI

Phase 2e: Math & Misc (8 tests)
├── math.hml                 ✓ math functions
├── assert.hml               ✓ assertions
├── serialization.hml        ✓ serialize/deserialize
├── apply.hml                ✓ dynamic calls
├── atomic_operations.hml    ✓ atomics
├── poll_constants.hml       ✓ poll constants
├── socket_constants.hml     ✓ socket constants
└── socket_nonblocking.hml   ✓ non-blocking IO

Phase 2f: Network & HTTP (2 tests)
├── http_request_fn.hml      ✓ HTTP requests
└── ws_send_binary_fn.hml    ✓ WebSocket
```

### Phase 3: Methods (4 tests)

```
├── string_methods.hml       ✓ 19 string methods
├── string_to_bytes.hml      ✓ UTF-8 bytes
├── array_methods.hml        ✓ 18 array methods
└── map_filter_reduce.hml    ✓ functional methods
```

### Phase 4: Modules (17 tests)

```
Phase 4a: Import/Export (6 tests)
├── named_import.hml         ✓ { x } from
├── star_import.hml          ✓ * from
├── namespace_import.hml     ✓ import module
├── chained_import.hml       ✓ re-exports
├── export_define.hml        ✓ export types
└── export_extern.hml        ✓ export FFI

Phase 4b: Standard Library (11 tests)
├── stdlib_math.hml          ✓ @stdlib/math
├── stdlib_json.hml          ✓ @stdlib/json
├── stdlib_collections.hml   ✓ @stdlib/collections
├── stdlib_encoding.hml      ✓ @stdlib/encoding
├── stdlib_fs.hml            ✓ @stdlib/fs
├── stdlib_hash.hml          ✓ @stdlib/hash
├── stdlib_os.hml            ✓ @stdlib/os
├── stdlib_strings.hml       ✓ @stdlib/strings
├── stdlib_time.hml          ✓ @stdlib/time
├── stdlib_path.hml          ✓ @stdlib/path
└── stdlib_url.hml           ✓ @stdlib/url
```

---

## Implementation Phases

### Phase 1: Foundation (Week 1-2)
- [ ] Instruction encoding (`instruction.h`)
- [ ] Chunk structure (`chunk.h`, `chunk.c`)
- [ ] Constant pool management
- [ ] Basic VM loop with stack operations
- [ ] Disassembler for debugging (`debug.c`)

**Milestone:** Can execute `OP_CONST`, `OP_ADD`, `OP_PRINT`

### Phase 2: Expressions (Week 2-3)
- [ ] Arithmetic operations (all 11 opcodes)
- [ ] Comparison operations (8 opcodes)
- [ ] Logical & bitwise (9 opcodes)
- [ ] Variable load/store (12 opcodes)
- [ ] Array/object literals
- [ ] Property access
- [ ] Index operations

**Milestone:** Pass `primitives.hml`, `arithmetic.hml`, `comparisons.hml`

### Phase 3: Control Flow (Week 3-4)
- [ ] Jump instructions
- [ ] If/else compilation
- [ ] While/for loops
- [ ] For-in iteration
- [ ] Break/continue
- [ ] Switch statement
- [ ] Ternary operator

**Milestone:** Pass all control flow tests (7 tests)

### Phase 4: Functions (Week 4-5)
- [ ] Function compilation to chunks
- [ ] Call frame management
- [ ] Parameter binding
- [ ] Return handling
- [ ] Closure capture (upvalues)
- [ ] Rest parameters
- [ ] Optional parameters

**Milestone:** Pass all function tests (8 tests)

### Phase 5: Error Handling (Week 5-6)
- [ ] Try/catch/finally
- [ ] Exception propagation
- [ ] Defer stack
- [ ] Type checking/casting
- [ ] Runtime type errors

**Milestone:** Pass `exceptions.hml`, `defer.hml`

### Phase 6: Builtins (Week 6-7)
- [ ] Builtin dispatch table
- [ ] Memory builtins (alloc, free, etc.)
- [ ] I/O builtins (print, open, etc.)
- [ ] Math builtins
- [ ] Type builtins

**Milestone:** Pass all builtin tests (36 tests)

### Phase 7: Methods (Week 7-8)
- [ ] String method dispatch (19 methods)
- [ ] Array method dispatch (18 methods)
- [ ] Method call optimization

**Milestone:** Pass all method tests (4 tests)

### Phase 8: Async (Week 8-9)
- [ ] Task creation (spawn)
- [ ] Task join/await
- [ ] Channel implementation
- [ ] Select operation
- [ ] Thread-safe value copying

**Milestone:** Pass all async tests (5 tests)

### Phase 9: Modules (Week 9-10)
- [ ] Module loading
- [ ] Import/export handling
- [ ] Stdlib integration

**Milestone:** Pass all module tests (17 tests)

### Phase 10: Polish (Week 10-11)
- [ ] Performance optimization
- [ ] Fast paths for i32 operations
- [ ] Memory optimization
- [ ] Full parity verification
- [ ] Documentation

**Milestone:** 103/103 parity tests passing

---

## Parity Testing Strategy

### Test Runner

```bash
#!/bin/bash
# scripts/test_vm_parity.sh

PASS=0
FAIL=0
INTERP_ONLY=0
VM_ONLY=0

for test in tests/parity/**/*.hml; do
    expected="${test%.hml}.expected"

    # Run interpreter
    interp_out=$(timeout 10 ./hemlock "$test" 2>&1)
    interp_rc=$?

    # Run VM
    vm_out=$(timeout 10 ./hemlockvm "$test" 2>&1)
    vm_rc=$?

    # Compare
    if [ "$interp_out" = "$vm_out" ] && [ "$interp_rc" = "$vm_rc" ]; then
        if [ "$interp_out" = "$(cat $expected)" ]; then
            echo "✓ PASS: $test"
            ((PASS++))
        else
            echo "✗ BOTH_WRONG: $test"
            ((FAIL++))
        fi
    elif [ "$interp_out" = "$(cat $expected)" ]; then
        echo "◐ INTERP_ONLY: $test"
        ((INTERP_ONLY++))
    elif [ "$vm_out" = "$(cat $expected)" ]; then
        echo "◑ VM_ONLY: $test"
        ((VM_ONLY++))
    else
        echo "✗ BOTH_FAIL: $test"
        ((FAIL++))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed, $INTERP_ONLY interp-only, $VM_ONLY vm-only"
echo "Parity: $PASS / $((PASS + INTERP_ONLY + VM_ONLY + FAIL)) tests"
```

### Incremental Parity Tracking

```
tests/parity/
├── vm_status.json    # Track VM implementation status
└── ...

{
  "language/primitives.hml": "pass",
  "language/closures.hml": "interp_only",
  "builtins/async.hml": "not_started",
  ...
}
```

### CI Integration

```makefile
# Makefile additions

vm: src/backends/vm/*.c
	$(CC) $(CFLAGS) -o hemlockvm $^

test-vm: vm
	./scripts/test_vm_parity.sh

parity-vm: hemlock hemlockvm
	./scripts/test_vm_parity.sh --strict  # Fail if any test doesn't pass both
```

---

## Performance Targets

| Metric | Tree-Walker | Bytecode VM | C Compiler |
|--------|-------------|-------------|------------|
| Startup | ~1ms | ~2ms | ~500ms (compile) |
| Fib(30) | ~800ms | ~200ms | ~50ms |
| Loop 1M | ~500ms | ~100ms | ~10ms |
| Memory | Low | Medium | Low (at runtime) |

Target: **4-8x faster than tree-walker** for compute-heavy code.

---

## Open Questions

1. **Upvalue representation**: Open upvalues on stack vs. heap-allocated?
2. **NaN boxing**: Pack values into 64-bit with NaN tagging for faster ops?
3. **JIT preparation**: Structure bytecode to enable future JIT compilation?
4. **Debug info**: Full source mapping vs. line numbers only?

---

## References

- Crafting Interpreters (Bob Nystrom) - Bytecode VM design
- Lua 5.x source - Register-based inspiration
- Python/CPython - Stack-based bytecode
- Existing Hemlock interpreter - Semantic reference
