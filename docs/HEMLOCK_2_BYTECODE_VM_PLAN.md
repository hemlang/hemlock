# Hemlock 2.0 Bytecode VM Plan

> A proposal for adding a bytecode virtual machine to Hemlock, providing a third execution backend alongside the tree-walking interpreter and C compiler.

## Executive Summary

This document proposes a **stack-based bytecode VM** for Hemlock 2.0 that:
- Compiles AST to compact bytecode representation
- Executes via a dispatch loop (faster than tree-walking)
- Maintains full semantic parity with existing backends
- Enables future optimizations (JIT, AOT, optimization passes)
- Reduces memory footprint vs AST interpretation

**Target**: 2-5x speedup over tree-walking interpreter for compute-bound code.

---

## Table of Contents

1. [Motivation](#1-motivation)
2. [Architecture Overview](#2-architecture-overview)
3. [Bytecode Format](#3-bytecode-format)
4. [Instruction Set](#4-instruction-set)
5. [Value Representation](#5-value-representation)
6. [Memory Management](#6-memory-management)
7. [VM Execution Engine](#7-vm-execution-engine)
8. [Closures and Functions](#8-closures-and-functions)
9. [Control Flow](#9-control-flow)
10. [Async and Concurrency](#10-async-and-concurrency)
11. [FFI Integration](#11-ffi-integration)
12. [Standard Library](#12-standard-library)
13. [Debugging and Profiling](#13-debugging-and-profiling)
14. [Optimization Opportunities](#14-optimization-opportunities)
15. [Migration Strategy](#15-migration-strategy)
16. [Implementation Phases](#16-implementation-phases)
17. [Risks and Mitigations](#17-risks-and-mitigations)

---

## 1. Motivation

### Current State

Hemlock has two execution backends:

```
Source (.hml)
    ↓
┌─────────────────────────────┐
│  SHARED FRONTEND            │
│  - Lexer (src/frontend/)    │
│  - Parser (src/frontend/)   │
│  - AST (src/frontend/)      │
└─────────────────────────────┘
    ↓                    ↓
┌────────────┐    ┌────────────┐
│ INTERPRETER│    │  COMPILER  │
│ (hemlock)  │    │ (hemlockc) │
│            │    │            │
│ Tree-walk  │    │ AST → C    │
│ recursion  │    │ gcc link   │
└────────────┘    └────────────┘
```

### Problems with Tree-Walking Interpretation

1. **Overhead per node**: Each AST node requires:
   - Function call overhead (`eval_expr` recursion)
   - Switch dispatch on node type
   - Memory indirection to access node data

2. **Memory bloat**: AST nodes are large (~100+ bytes each):
   - Full `Expr` struct with union of all expression types
   - Line number, type annotations, resolved variable info
   - Child pointers requiring memory indirection

3. **Cache unfriendly**: AST scattered across heap, poor locality

4. **No optimization window**: Interpretation happens at AST level, limiting optimization

### Why Bytecode?

| Aspect | Tree-Walking | Bytecode VM |
|--------|--------------|-------------|
| Dispatch overhead | Function call per node | Single switch/computed goto |
| Memory per instruction | ~100+ bytes (AST node) | 1-9 bytes (opcode + operands) |
| Cache locality | Poor (scattered heap) | Excellent (linear bytecode) |
| Startup time | Instant | Small compilation overhead |
| Optimization potential | Limited | Constant folding, dead code, inlining |
| Debugging | Easy (full AST) | Requires source maps |

### Goals

1. **Performance**: 2-5x faster than tree-walking for compute-bound code
2. **Parity**: Identical semantics to interpreter and compiler
3. **Simplicity**: Clean, maintainable implementation
4. **Debuggability**: Source maps, stack traces, breakpoints
5. **Future-proof**: Foundation for JIT compilation

---

## 2. Architecture Overview

### Proposed Architecture

```
Source (.hml)
    ↓
┌─────────────────────────────┐
│  SHARED FRONTEND            │
│  - Lexer                    │
│  - Parser                   │
│  - AST                      │
│  - Resolver (variable idx)  │
└─────────────────────────────┘
    ↓              ↓              ↓
┌──────────┐ ┌──────────────┐ ┌──────────┐
│INTERPRETER│ │ BYTECODE VM  │ │ COMPILER │
│(hemlock)  │ │ (hemlockvm)  │ │(hemlockc)│
│           │ │              │ │          │
│Tree-walk  │ │ AST→Bytecode │ │ AST→C    │
│           │ │ VM Execute   │ │          │
└──────────┘ └──────────────┘ └──────────┘
```

### Components

```
src/backends/vm/
├── main.c                 # Entry point (hemlockvm binary)
├── compiler/
│   ├── bytecode.h         # Bytecode format definitions
│   ├── bytecode.c         # Bytecode chunk management
│   ├── compiler.c         # AST → Bytecode compilation
│   ├── compiler_expr.c    # Expression compilation
│   ├── compiler_stmt.c    # Statement compilation
│   └── optimizer.c        # Bytecode optimization passes
├── vm/
│   ├── vm.h               # VM state and API
│   ├── vm.c               # Main execution loop
│   ├── stack.c            # Value stack operations
│   ├── frames.c           # Call frame management
│   └── dispatch.c         # Instruction dispatch (computed goto)
├── runtime/
│   ├── values.c           # Value operations (reuse existing)
│   ├── builtins.c         # Builtin function dispatch
│   ├── async.c            # Task/channel operations
│   └── memory.c           # GC/refcounting integration
└── debug/
    ├── disasm.c           # Bytecode disassembler
    ├── source_map.c       # Source location mapping
    └── debugger.c         # Interactive debugger
```

---

## 3. Bytecode Format

### Chunk Structure

A **Chunk** is the compiled bytecode for a single function or module:

```c
typedef struct {
    // Bytecode
    uint8_t *code;           // Bytecode instructions
    int code_count;
    int code_capacity;

    // Constants pool
    Value *constants;        // Literal values, function objects
    int const_count;
    int const_capacity;

    // Source mapping (for debugging)
    int *lines;              // Line number for each instruction

    // Upvalue info (for closures)
    UpvalueInfo *upvalues;   // Captured variable descriptors
    int upvalue_count;

    // Function metadata
    char *name;              // Function name (NULL for top-level)
    int arity;               // Number of parameters
    int num_locals;          // Local variable slots needed
    int is_async;            // Async function flag
} Chunk;
```

### Bytecode Encoding

**Variable-length encoding** for compact representation:

```
┌─────────┬─────────────────────────────────┐
│ 1 byte  │ Opcode                          │
├─────────┼─────────────────────────────────┤
│ 0-8     │ Operands (instruction-specific) │
└─────────┴─────────────────────────────────┘
```

**Operand encoding**:
- 1-byte operands: Local indices (0-255), small constants
- 2-byte operands: Extended indices (0-65535)
- 4-byte operands: Jump offsets, large constant indices

**Example**:
```
OP_LOAD_LOCAL   0x01    ; Load local at slot 1
OP_CONST        0x00    ; Load constant at index 0
OP_ADD                  ; Pop two, push sum
OP_RETURN               ; Return top of stack
```

---

## 4. Instruction Set

### Instruction Categories

#### 4.1 Stack Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_CONST` | index(2) | Push constant from pool |
| `OP_CONST_LONG` | index(4) | Push constant (extended index) |
| `OP_NULL` | - | Push null |
| `OP_TRUE` | - | Push true |
| `OP_FALSE` | - | Push false |
| `OP_POP` | - | Discard top of stack |
| `OP_DUP` | - | Duplicate top of stack |
| `OP_SWAP` | - | Swap top two values |

#### 4.2 Local Variables

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_LOAD_LOCAL` | slot(1) | Push local variable |
| `OP_LOAD_LOCAL_LONG` | slot(2) | Push local (extended) |
| `OP_STORE_LOCAL` | slot(1) | Store to local variable |
| `OP_STORE_LOCAL_LONG` | slot(2) | Store local (extended) |

#### 4.3 Upvalues (Closures)

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_LOAD_UPVALUE` | index(1) | Push captured variable |
| `OP_STORE_UPVALUE` | index(1) | Store to captured variable |
| `OP_CLOSE_UPVALUE` | - | Close upvalue (move to heap) |

#### 4.4 Globals and Module Variables

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_LOAD_GLOBAL` | name_idx(2) | Push global by name |
| `OP_STORE_GLOBAL` | name_idx(2) | Store to global |
| `OP_LOAD_MODULE` | mod_idx(2), name_idx(2) | Push module export |

#### 4.5 Arithmetic Operations

| Opcode | Description |
|--------|-------------|
| `OP_ADD` | a + b (with type promotion) |
| `OP_SUB` | a - b |
| `OP_MUL` | a * b |
| `OP_DIV` | a / b (always f64) |
| `OP_MOD` | a % b |
| `OP_NEG` | -a (unary negation) |
| `OP_FLOOR_DIV` | a // b (integer division) |

#### 4.6 Bitwise Operations

| Opcode | Description |
|--------|-------------|
| `OP_BIT_AND` | a & b |
| `OP_BIT_OR` | a \| b |
| `OP_BIT_XOR` | a ^ b |
| `OP_BIT_NOT` | ~a |
| `OP_LSHIFT` | a << b |
| `OP_RSHIFT` | a >> b |

#### 4.7 Comparison Operations

| Opcode | Description |
|--------|-------------|
| `OP_EQ` | a == b |
| `OP_NE` | a != b |
| `OP_LT` | a < b |
| `OP_LE` | a <= b |
| `OP_GT` | a > b |
| `OP_GE` | a >= b |

#### 4.8 Logical Operations

| Opcode | Description |
|--------|-------------|
| `OP_NOT` | !a |
| `OP_AND` | Short-circuit AND (special handling) |
| `OP_OR` | Short-circuit OR (special handling) |

#### 4.9 Control Flow

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_JUMP` | offset(2) | Unconditional jump |
| `OP_JUMP_LONG` | offset(4) | Extended jump |
| `OP_JUMP_IF_FALSE` | offset(2) | Conditional jump |
| `OP_JUMP_IF_TRUE` | offset(2) | Conditional jump |
| `OP_LOOP` | offset(2) | Backward jump (for loops) |

#### 4.10 Function Calls

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_CALL` | argc(1) | Call function with argc args |
| `OP_CALL_BUILTIN` | builtin_id(2), argc(1) | Direct builtin call |
| `OP_RETURN` | - | Return from function |
| `OP_RETURN_NULL` | - | Return null (implicit) |

#### 4.11 Object and Array Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_ARRAY` | count(2) | Create array from stack values |
| `OP_OBJECT` | count(2) | Create object from key/value pairs |
| `OP_GET_INDEX` | - | array[index] or object[key] |
| `OP_SET_INDEX` | - | array[index] = val |
| `OP_GET_FIELD` | name_idx(2) | object.field |
| `OP_SET_FIELD` | name_idx(2) | object.field = val |
| `OP_GET_FIELD_OPT` | name_idx(2) | object?.field (optional chain) |

#### 4.12 Method Calls

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_INVOKE` | name_idx(2), argc(1) | Call method on object |
| `OP_INVOKE_STRING` | method_id(1), argc(1) | String method (fast path) |
| `OP_INVOKE_ARRAY` | method_id(1), argc(1) | Array method (fast path) |

#### 4.13 Closures

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_CLOSURE` | func_idx(2), upvalue_count(1) | Create closure |
| | + per upvalue: is_local(1), index(1) | Upvalue descriptors |

#### 4.14 Exception Handling

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_TRY_BEGIN` | catch_offset(2), finally_offset(2) | Enter try block |
| `OP_TRY_END` | - | Exit try block normally |
| `OP_THROW` | - | Throw exception (top of stack) |
| `OP_CATCH` | - | Begin catch block |
| `OP_FINALLY` | - | Begin finally block |
| `OP_RETHROW` | - | Re-throw current exception |

#### 4.15 Defer

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_DEFER` | func_idx(2) | Push deferred call |
| `OP_EXEC_DEFERS` | - | Execute all deferred calls (LIFO) |

#### 4.16 Async Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_SPAWN` | argc(1) | Spawn async task |
| `OP_AWAIT` | - | Await task result |
| `OP_DETACH` | - | Detach task |
| `OP_CHANNEL` | - | Create channel (capacity on stack) |
| `OP_CHAN_SEND` | - | Send value to channel |
| `OP_CHAN_RECV` | - | Receive from channel |
| `OP_CHAN_CLOSE` | - | Close channel |

#### 4.17 Type Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_TYPEOF` | - | Push type string |
| `OP_CONVERT` | type(1) | Convert to type |
| `OP_CHECK_TYPE` | type(1) | Runtime type check |

#### 4.18 Memory Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OP_ALLOC` | - | Allocate raw memory |
| `OP_FREE` | - | Free memory |
| `OP_BUFFER` | - | Create bounds-checked buffer |
| `OP_MEMSET` | - | memset(ptr, byte, size) |
| `OP_MEMCPY` | - | memcpy(dest, src, size) |

---

## 5. Value Representation

### Reuse Existing Tagged Union

The VM will use the **same Value representation** as the interpreter:

```c
typedef struct Value {
    ValueType type;      // 24 distinct types
    union {
        int8_t as_i8;
        int16_t as_i16;
        int32_t as_i32;
        int64_t as_i64;
        uint8_t as_u8;
        uint16_t as_u16;
        uint32_t as_u32;
        uint64_t as_u64;
        float as_f32;
        double as_f64;
        int as_bool;
        String *as_string;
        uint32_t as_rune;
        void *as_ptr;
        Buffer *as_buffer;
        Array *as_array;
        Object *as_object;
        Function *as_function;
        Task *as_task;
        Channel *as_channel;
        // ... etc
    } as;
} Value;
```

**Rationale**:
- Maintains semantic parity with interpreter
- Allows sharing runtime library code
- Consistent refcounting behavior

### NaN Boxing (Future Optimization)

For future performance, consider **NaN boxing** where all values fit in 64 bits:

```
┌────────────────────────────────────────────────────────────────┐
│ 64-bit NaN-boxed value                                         │
├────────────────────────────────────────────────────────────────┤
│ If bits 51-62 are all 1s (NaN), remaining bits encode:         │
│   - Type tag (3-4 bits)                                        │
│   - Payload (pointer or small value)                           │
│ Otherwise: IEEE 754 double                                     │
└────────────────────────────────────────────────────────────────┘
```

This reduces Value size from 16-32 bytes to 8 bytes, improving cache performance.

---

## 6. Memory Management

### Hybrid Model (Matching Interpreter)

1. **Manual memory** for `ptr` type:
   - `alloc(size)` → raw pointer
   - `free(ptr)` → user responsibility
   - No tracking, no safety

2. **Reference counting** for heap types:
   - String, Array, Buffer, Object, Function, Task, Channel
   - Atomic refcount operations (thread-safe)
   - Deterministic cleanup

3. **Stack allocation** for primitives:
   - i8-i64, u8-u64, f32, f64, bool, rune, null
   - Zero allocation overhead

### VM Stack Management

```c
typedef struct {
    Value *values;        // Stack array
    Value *top;           // Current stack top
    int capacity;         // Maximum stack size
} ValueStack;

// Stack operations
void stack_push(ValueStack *stack, Value val);
Value stack_pop(ValueStack *stack);
Value stack_peek(ValueStack *stack, int distance);
```

**Stack frame layout**:
```
┌─────────────────────────────────────────┐
│ Local 0 (first param or 'this')         │ ← frame->slots
├─────────────────────────────────────────┤
│ Local 1                                 │
├─────────────────────────────────────────┤
│ ...                                     │
├─────────────────────────────────────────┤
│ Local N-1                               │
├─────────────────────────────────────────┤
│ Temporary values                        │
├─────────────────────────────────────────┤
│ ... (expression evaluation)             │
└─────────────────────────────────────────┘ ← stack top
```

### Upvalue Heap Promotion

When a local variable is captured by a closure and goes out of scope:

```c
typedef struct Upvalue {
    Value *location;      // Points to stack slot OR closed value
    Value closed;         // Heap copy when stack slot dies
    struct Upvalue *next; // Linked list for tracking
} Upvalue;

void close_upvalue(Upvalue *upvalue) {
    upvalue->closed = *upvalue->location;  // Copy to heap
    upvalue->location = &upvalue->closed;  // Redirect
}
```

---

## 7. VM Execution Engine

### VM State

```c
typedef struct {
    // Execution state
    Chunk *chunk;              // Current bytecode chunk
    uint8_t *ip;               // Instruction pointer

    // Value stack
    Value stack[STACK_MAX];    // Fixed-size stack (configurable)
    Value *stack_top;          // Current top

    // Call frames
    CallFrame frames[FRAMES_MAX];
    int frame_count;

    // Upvalues
    Upvalue *open_upvalues;    // Linked list of open upvalues

    // Exception handling
    TryFrame *try_stack;       // Stack of try blocks
    int try_depth;
    Value exception;           // Current exception
    bool is_throwing;

    // Defer stack
    DeferEntry *defer_stack;
    int defer_count;

    // Globals
    HashTable globals;         // Global variables

    // Modules
    HashTable modules;         // Loaded modules

    // Async state
    Task *current_task;        // NULL if main thread
} VM;
```

### Call Frame

```c
typedef struct {
    Closure *closure;          // Function being executed
    uint8_t *ip;               // Return address
    Value *slots;              // First slot for this frame
    int defer_start;           // Defer stack index at frame entry
} CallFrame;
```

### Main Dispatch Loop

**Computed goto dispatch** (GCC/Clang) for maximum performance:

```c
InterpretResult vm_run(VM *vm) {
    // Dispatch table
    static void *dispatch_table[] = {
        [OP_CONST] = &&op_const,
        [OP_ADD] = &&op_add,
        [OP_CALL] = &&op_call,
        // ... all opcodes
    };

    #define DISPATCH() goto *dispatch_table[READ_BYTE()]
    #define READ_BYTE() (*vm->ip++)
    #define READ_SHORT() (vm->ip += 2, (vm->ip[-2] << 8) | vm->ip[-1])

    DISPATCH();

op_const: {
    uint16_t idx = READ_SHORT();
    push(vm, vm->chunk->constants[idx]);
    DISPATCH();
}

op_add: {
    Value b = pop(vm);
    Value a = pop(vm);
    push(vm, value_add(a, b));  // Handles type promotion
    DISPATCH();
}

op_call: {
    uint8_t argc = READ_BYTE();
    Value callee = peek(vm, argc);
    if (!call_value(vm, callee, argc)) {
        return INTERPRET_RUNTIME_ERROR;
    }
    DISPATCH();
}

    // ... etc
}
```

### Portable Dispatch (Fallback)

For non-GCC compilers:

```c
InterpretResult vm_run(VM *vm) {
    for (;;) {
        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONST: { /* ... */ break; }
            case OP_ADD: { /* ... */ break; }
            // ...
        }
    }
}
```

---

## 8. Closures and Functions

### Function Compilation

Each function compiles to its own `Chunk`:

```c
typedef struct {
    Chunk chunk;               // The function's bytecode
    char *name;                // Function name
    int arity;                 // Required parameters
    int num_defaults;          // Optional parameters
    Value *defaults;           // Default values
    bool has_rest_param;       // Variadic
    bool is_async;             // Async function
    int upvalue_count;         // Number of captured variables
} ObjFunction;
```

### Closure Creation

```c
typedef struct {
    ObjFunction *function;
    Upvalue **upvalues;        // Array of upvalue pointers
    int upvalue_count;
} ObjClosure;
```

**OP_CLOSURE instruction**:
```
OP_CLOSURE func_idx upvalue_count
  [is_local index] × upvalue_count
```

Example compilation of:
```hemlock
fn make_counter() {
    let count = 0;
    return fn() {
        count = count + 1;
        return count;
    };
}
```

Bytecode for inner function:
```
OP_LOAD_UPVALUE 0      ; Push 'count'
OP_CONST 1             ; Push 1
OP_ADD                 ; count + 1
OP_STORE_UPVALUE 0     ; Store back to count
OP_LOAD_UPVALUE 0      ; Push count for return
OP_RETURN
```

Closure creation in outer function:
```
OP_CONST 0             ; Push 0 (initial count)
OP_STORE_LOCAL 0       ; Store in local slot 0
OP_CLOSURE 1 1         ; Create closure, 1 upvalue
  0x01 0x00            ; is_local=true, index=0 (capture local 0)
OP_RETURN
```

---

## 9. Control Flow

### Conditionals

```hemlock
if (condition) {
    then_branch;
} else {
    else_branch;
}
```

Compiles to:
```
<condition>
OP_JUMP_IF_FALSE else_offset
<then_branch>
OP_JUMP end_offset
<else_branch>           ; ← else_offset points here
<end>                   ; ← end_offset points here
```

### Loops

**While loop**:
```hemlock
while (condition) {
    body;
}
```

Compiles to:
```
loop_start:             ; ← continue target
<condition>
OP_JUMP_IF_FALSE end
<body>
OP_LOOP loop_start      ; Backward jump
end:                    ; ← break target
```

**For-in loop**:
```hemlock
for (item in array) {
    body;
}
```

Compiles to:
```
<array>
OP_CONST 0             ; Iterator index
OP_STORE_LOCAL idx_slot
loop_start:
OP_LOAD_LOCAL idx_slot
<array>
OP_GET_LENGTH
OP_LT
OP_JUMP_IF_FALSE end
<array>
OP_LOAD_LOCAL idx_slot
OP_GET_INDEX
OP_STORE_LOCAL item_slot
<body>
OP_LOAD_LOCAL idx_slot
OP_CONST 1
OP_ADD
OP_STORE_LOCAL idx_slot
OP_LOOP loop_start
end:
```

### Switch Statement

```hemlock
switch (x) {
    case 1: handle_one(); break;
    case 2: handle_two(); break;
    default: handle_other();
}
```

Compiles to jump table or if-else chain:
```
<x>
OP_DUP
OP_CONST 1
OP_EQ
OP_JUMP_IF_TRUE case_1
OP_DUP
OP_CONST 2
OP_EQ
OP_JUMP_IF_TRUE case_2
OP_POP
OP_JUMP default

case_1:
OP_POP
<handle_one()>
OP_JUMP end

case_2:
OP_POP
<handle_two()>
OP_JUMP end

default:
<handle_other()>

end:
```

---

## 10. Async and Concurrency

### Thread Model

Maintain Hemlock's **1:1 OS thread model**:

```c
typedef struct Task {
    int id;                    // Unique task ID
    TaskState state;           // READY, RUNNING, COMPLETED
    Value result;              // Return value

    // Thread management
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    // VM state (each task has its own)
    VM *vm;                    // Task's VM instance
    ObjClosure *closure;       // Function to execute
    Value *args;               // Arguments (deep copied)
    int num_args;

    // Bookkeeping
    int ref_count;
    bool joined;
    bool detached;
} Task;
```

### Spawn Implementation

```c
Value op_spawn(VM *vm, int argc) {
    Value fn = pop(vm);

    // Create new task
    Task *task = task_new();
    task->closure = AS_CLOSURE(fn);
    task->num_args = argc;

    // Deep copy arguments (thread isolation)
    task->args = malloc(sizeof(Value) * argc);
    for (int i = 0; i < argc; i++) {
        task->args[i] = value_deep_copy(peek(vm, argc - 1 - i));
    }

    // Pop arguments
    vm->stack_top -= argc;

    // Create task VM with fresh stack
    task->vm = vm_new();
    task->vm->current_task = task;

    // Spawn thread
    pthread_create(&task->thread, NULL, task_thread_fn, task);

    return val_task(task);
}

static void *task_thread_fn(void *arg) {
    Task *task = (Task *)arg;

    // Block signals (main thread handles)
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // Execute function in task's VM
    task->state = TASK_RUNNING;
    call_closure(task->vm, task->closure, task->args, task->num_args);
    InterpretResult result = vm_run(task->vm);

    // Store result
    pthread_mutex_lock(&task->mutex);
    task->result = (result == INTERPRET_OK) ? pop(task->vm) : val_null();
    task->state = TASK_COMPLETED;
    pthread_cond_signal(&task->cond);
    pthread_mutex_unlock(&task->mutex);

    return NULL;
}
```

### Channel Operations

Channels use the existing ring buffer implementation:

```c
Value op_chan_send(VM *vm) {
    Value value = pop(vm);
    Value chan_val = pop(vm);
    Channel *chan = AS_CHANNEL(chan_val);

    pthread_mutex_lock(&chan->mutex);

    // Wait if buffer full
    while (chan->count == chan->capacity && !chan->closed) {
        pthread_cond_wait(&chan->not_full, &chan->mutex);
    }

    if (chan->closed) {
        pthread_mutex_unlock(&chan->mutex);
        runtime_error(vm, "send on closed channel");
        return val_null();
    }

    // Enqueue value
    chan->buffer[chan->tail] = value;
    value_retain(value);
    chan->tail = (chan->tail + 1) % chan->capacity;
    chan->count++;

    pthread_cond_signal(&chan->not_empty);
    pthread_mutex_unlock(&chan->mutex);

    return val_null();
}
```

---

## 11. FFI Integration

### FFI Operations

The VM supports the same FFI interface as the interpreter:

```c
Value op_ffi_call(VM *vm) {
    Value types_val = pop(vm);      // Array of FFI types
    Value fn_ptr_val = pop(vm);     // Function pointer
    int argc = /* from instruction */;

    // Build libffi call
    ffi_cif cif;
    ffi_type **arg_types = /* convert from types_val */;
    ffi_type *ret_type = /* from types_val */;

    ffi_prep_cif(&cif, FFI_DEFAULT_ABI, argc, ret_type, arg_types);

    // Marshal arguments
    void **args = /* convert stack values */;

    // Call
    void *fn = AS_PTR(fn_ptr_val);
    ffi_call(&cif, fn, &result, args);

    // Convert result
    return marshal_ffi_result(result, ret_type);
}
```

### Builtin Function Dispatch

For builtins, use direct function pointers instead of FFI:

```c
typedef Value (*BuiltinFn)(VM *vm, int argc, Value *args);

typedef struct {
    const char *name;
    BuiltinFn fn;
    int arity;          // -1 for variadic
} BuiltinInfo;

static BuiltinInfo builtins[] = {
    {"print", builtin_print, -1},
    {"typeof", builtin_typeof, 1},
    {"alloc", builtin_alloc, 1},
    {"free", builtin_free, 1},
    {"spawn", builtin_spawn, -1},
    {"join", builtin_join, 1},
    // ... 150+ more
};

Value op_call_builtin(VM *vm) {
    uint16_t builtin_id = READ_SHORT();
    uint8_t argc = READ_BYTE();

    BuiltinFn fn = builtins[builtin_id].fn;
    Value *args = vm->stack_top - argc;

    Value result = fn(vm, argc, args);

    vm->stack_top -= argc;
    push(vm, result);
}
```

---

## 12. Standard Library

### Strategy: Shared Implementation

The 39 stdlib modules can be **shared** between all backends:

```
stdlib/
├── math.hml          # Pure Hemlock (compiles to bytecode)
├── collections.hml   # Pure Hemlock
├── json.hml          # Pure Hemlock
└── native/
    ├── fs.c          # Native file operations
    ├── net.c         # Native networking
    ├── crypto.c      # Native crypto (OpenSSL)
    └── ...
```

**Pure Hemlock modules**: Compile to bytecode, execute in VM
**Native modules**: Wrap C functions via builtins

### Module Loading

```c
Value load_module(VM *vm, const char *path) {
    // Check cache
    if (has_module(vm, path)) {
        return get_module(vm, path);
    }

    // Determine if stdlib
    if (starts_with(path, "@stdlib/")) {
        return load_stdlib_module(vm, path);
    }

    // Load and compile
    char *source = read_file(path);
    Chunk *chunk = compile(source);

    // Execute module (populates exports)
    VM module_vm = vm_new();
    execute_chunk(&module_vm, chunk);

    // Cache and return exports
    Value exports = get_exports(&module_vm);
    cache_module(vm, path, exports);
    return exports;
}
```

---

## 13. Debugging and Profiling

### Source Maps

Map bytecode offsets to source locations:

```c
typedef struct {
    int bytecode_offset;
    int source_line;
    int source_column;
    const char *source_file;
} SourceLocation;

typedef struct {
    SourceLocation *locations;
    int count;
} SourceMap;

SourceLocation get_location(SourceMap *map, int offset) {
    // Binary search for offset
    // ...
}
```

### Disassembler

```c
void disassemble_chunk(Chunk *chunk, const char *name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < chunk->code_count; ) {
        offset = disassemble_instruction(chunk, offset);
    }
}

int disassemble_instruction(Chunk *chunk, int offset) {
    printf("%04d ", offset);

    // Line info
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONST:
            return constant_instruction("OP_CONST", chunk, offset);
        case OP_ADD:
            return simple_instruction("OP_ADD", offset);
        // ...
    }
}
```

**Example output**:
```
== main ==
0000    1 OP_CONST         0 '42'
0003    | OP_STORE_LOCAL   0
0005    2 OP_CONST         1 '10'
0008    | OP_LOAD_LOCAL    0
0010    | OP_ADD
0011    | OP_CALL_BUILTIN  0 (print) 1
0015    | OP_RETURN_NULL
```

### Interactive Debugger

```c
typedef struct {
    VM *vm;
    HashTable breakpoints;    // line -> enabled
    bool step_mode;
    bool step_over;
    int step_frame;
} Debugger;

void debugger_step(Debugger *dbg) {
    int line = get_current_line(dbg->vm);

    // Check breakpoint
    if (has_breakpoint(dbg, line) || dbg->step_mode) {
        print_context(dbg);
        debugger_repl(dbg);  // Interactive commands
    }
}
```

### Profiler

```c
typedef struct {
    uint64_t instruction_counts[256];  // Per-opcode counts
    uint64_t function_times[MAX_FUNCTIONS];
    uint64_t function_calls[MAX_FUNCTIONS];
} Profiler;

void profiler_report(Profiler *prof) {
    printf("=== Instruction Profile ===\n");
    for (int i = 0; i < 256; i++) {
        if (prof->instruction_counts[i] > 0) {
            printf("%-20s %llu\n", opcode_name(i), prof->instruction_counts[i]);
        }
    }

    printf("\n=== Function Profile ===\n");
    // Sort by time, print top 20
}
```

---

## 14. Optimization Opportunities

### Compile-Time Optimizations

#### 14.1 Constant Folding

```hemlock
let x = 2 + 3 * 4;  // Fold to: let x = 14;
```

Bytecode before:
```
OP_CONST 2
OP_CONST 3
OP_CONST 4
OP_MUL
OP_ADD
```

Bytecode after:
```
OP_CONST 14
```

#### 14.2 Dead Code Elimination

```hemlock
if (false) {
    // This entire block is removed
}
```

#### 14.3 Strength Reduction

```hemlock
x * 2       → x + x (or x << 1)
x * 8       → x << 3
x / 2       → x >> 1 (for integers)
```

#### 14.4 Common Subexpression Elimination

```hemlock
let a = x + y;
let b = x + y + z;  // Reuse (x + y)
```

### Runtime Optimizations

#### 14.5 Inline Caching

Cache method lookups for hot paths:

```c
typedef struct {
    ValueType cached_type;
    int cached_offset;        // Field offset or method ID
    uint64_t cache_hits;
} InlineCache;

Value get_field_cached(Value obj, const char *name, InlineCache *cache) {
    if (obj.type == cache->cached_type) {
        // Fast path: use cached offset
        return object_get_field_at(obj, cache->cached_offset);
    }

    // Slow path: lookup and cache
    int offset = object_lookup_field(obj, name);
    cache->cached_type = obj.type;
    cache->cached_offset = offset;
    return object_get_field_at(obj, offset);
}
```

#### 14.6 Superinstructions

Combine common instruction sequences:

| Sequence | Superinstruction |
|----------|------------------|
| `LOAD_LOCAL 0; LOAD_LOCAL 1; ADD` | `OP_ADD_LOCALS 0 1` |
| `CONST; ADD` | `OP_ADD_CONST idx` |
| `LOAD_LOCAL; CALL 0` | `OP_CALL_LOCAL slot` |

#### 14.7 Type Specialization

Generate specialized bytecode paths for hot types:

```c
// Generic add
op_add:
    if (IS_I32(a) && IS_I32(b)) goto op_add_i32;
    if (IS_F64(a) && IS_F64(b)) goto op_add_f64;
    // ... fallback

// Specialized (no type checks)
op_add_i32:
    push(val_i32(AS_I32(a) + AS_I32(b)));
    DISPATCH();
```

### Future: JIT Compilation

The bytecode format enables future JIT:

1. **Trace recording**: Monitor hot loops
2. **Type profiling**: Track value types at each instruction
3. **Native code generation**: Compile hot traces to machine code
4. **OSR (On-Stack Replacement)**: Switch from bytecode to native mid-execution

---

## 15. Migration Strategy

### Phase 1: Parallel Development

- VM exists alongside interpreter and compiler
- All three share the frontend
- Parity tests run against all three backends

```
tests/parity/
├── test.hml
├── test.expected
└── (run with hemlock, hemlockvm, and hemlockc)
```

### Phase 2: Feature Parity

Complete VM implementation of all language features:
- Core language (variables, functions, control flow)
- Objects, arrays, strings
- Closures and upvalues
- Exception handling
- Defer
- Async/await
- FFI
- All 39 stdlib modules

### Phase 3: Performance Validation

- Benchmark suite comparing all three backends
- Profile and optimize hot paths
- Ensure no semantic differences

### Phase 4: Transition (Optional)

If VM proves superior:
1. VM becomes default interpreter: `hemlock` → bytecode VM
2. Tree-walker becomes debug mode: `hemlock --ast-interpret`
3. Compiler continues as AOT option

---

## 16. Implementation Phases

### Phase 1: Core VM (4-6 weeks)

**Deliverables**:
- Bytecode format and chunk structure
- Basic instruction set (arithmetic, locals, jumps)
- VM execution loop with computed goto
- Simple function calls (no closures)
- 50% of parity tests passing

**Files**:
```
src/backends/vm/
├── bytecode.h/c
├── compiler.c
├── vm.c
└── main.c
```

### Phase 2: Functions and Closures (2-3 weeks)

**Deliverables**:
- Closure compilation
- Upvalue capture and heap promotion
- Variadic functions
- Default parameters
- 70% of parity tests passing

### Phase 3: Objects and Collections (2-3 weeks)

**Deliverables**:
- Object literals and field access
- Array literals and indexing
- String/array method invocation
- 80% of parity tests passing

### Phase 4: Control Flow and Exceptions (2 weeks)

**Deliverables**:
- Try/catch/finally
- Throw
- Defer stack
- Switch statement
- 90% of parity tests passing

### Phase 5: Async and Concurrency (2-3 weeks)

**Deliverables**:
- Task spawning
- Join/await
- Channels
- 95% of parity tests passing

### Phase 6: FFI and Stdlib (2-3 weeks)

**Deliverables**:
- FFI bindings
- All 39 stdlib modules
- 100% parity tests passing

### Phase 7: Debugging and Polish (2 weeks)

**Deliverables**:
- Disassembler
- Source maps
- Stack traces
- Interactive debugger
- Documentation

**Total estimated time**: 16-22 weeks

---

## 17. Risks and Mitigations

### Risk 1: Semantic Divergence

**Risk**: VM behaves differently than interpreter/compiler

**Mitigation**:
- Extensive parity test suite (83+ tests, aim for 200+)
- Run all tests against all three backends
- Automated CI verification

### Risk 2: Performance Regression

**Risk**: VM slower than tree-walking for some workloads

**Mitigation**:
- Profile before optimizing
- Benchmark suite with diverse workloads
- Keep tree-walker as fallback

### Risk 3: Complexity

**Risk**: VM adds maintenance burden

**Mitigation**:
- Clean, modular architecture
- Extensive documentation
- Shared code with interpreter where possible

### Risk 4: Async Complexity

**Risk**: Thread model harder to implement in VM

**Mitigation**:
- Reuse existing Task/Channel implementations
- Each task gets its own VM instance
- Deep copy for thread isolation (already proven)

### Risk 5: FFI Compatibility

**Risk**: FFI harder to support in bytecode

**Mitigation**:
- Use same libffi approach as interpreter
- Builtins use direct C calls (no FFI overhead)
- Test extensively with FFI-heavy stdlib modules

---

## Appendix A: Full Opcode Table

| Opcode | Value | Operands | Stack Effect |
|--------|-------|----------|--------------|
| OP_NOP | 0x00 | - | 0 |
| OP_CONST | 0x01 | idx(2) | +1 |
| OP_CONST_LONG | 0x02 | idx(4) | +1 |
| OP_NULL | 0x03 | - | +1 |
| OP_TRUE | 0x04 | - | +1 |
| OP_FALSE | 0x05 | - | +1 |
| OP_POP | 0x06 | - | -1 |
| OP_DUP | 0x07 | - | +1 |
| OP_SWAP | 0x08 | - | 0 |
| OP_LOAD_LOCAL | 0x10 | slot(1) | +1 |
| OP_LOAD_LOCAL_LONG | 0x11 | slot(2) | +1 |
| OP_STORE_LOCAL | 0x12 | slot(1) | 0 |
| OP_STORE_LOCAL_LONG | 0x13 | slot(2) | 0 |
| OP_LOAD_UPVALUE | 0x14 | idx(1) | +1 |
| OP_STORE_UPVALUE | 0x15 | idx(1) | 0 |
| OP_CLOSE_UPVALUE | 0x16 | - | -1 |
| OP_LOAD_GLOBAL | 0x17 | name(2) | +1 |
| OP_STORE_GLOBAL | 0x18 | name(2) | 0 |
| OP_ADD | 0x20 | - | -1 |
| OP_SUB | 0x21 | - | -1 |
| OP_MUL | 0x22 | - | -1 |
| OP_DIV | 0x23 | - | -1 |
| OP_MOD | 0x24 | - | -1 |
| OP_NEG | 0x25 | - | 0 |
| OP_BIT_AND | 0x26 | - | -1 |
| OP_BIT_OR | 0x27 | - | -1 |
| OP_BIT_XOR | 0x28 | - | -1 |
| OP_BIT_NOT | 0x29 | - | 0 |
| OP_LSHIFT | 0x2A | - | -1 |
| OP_RSHIFT | 0x2B | - | -1 |
| OP_EQ | 0x30 | - | -1 |
| OP_NE | 0x31 | - | -1 |
| OP_LT | 0x32 | - | -1 |
| OP_LE | 0x33 | - | -1 |
| OP_GT | 0x34 | - | -1 |
| OP_GE | 0x35 | - | -1 |
| OP_NOT | 0x36 | - | 0 |
| OP_JUMP | 0x40 | off(2) | 0 |
| OP_JUMP_LONG | 0x41 | off(4) | 0 |
| OP_JUMP_IF_FALSE | 0x42 | off(2) | -1 |
| OP_JUMP_IF_TRUE | 0x43 | off(2) | -1 |
| OP_LOOP | 0x44 | off(2) | 0 |
| OP_CALL | 0x50 | argc(1) | -(argc) |
| OP_CALL_BUILTIN | 0x51 | id(2),argc(1) | -(argc)+1 |
| OP_RETURN | 0x52 | - | special |
| OP_RETURN_NULL | 0x53 | - | special |
| OP_CLOSURE | 0x54 | idx(2),uv(1) | +1 |
| OP_ARRAY | 0x60 | count(2) | -(count)+1 |
| OP_OBJECT | 0x61 | count(2) | -(count*2)+1 |
| OP_GET_INDEX | 0x62 | - | -1 |
| OP_SET_INDEX | 0x63 | - | -2 |
| OP_GET_FIELD | 0x64 | name(2) | 0 |
| OP_SET_FIELD | 0x65 | name(2) | -1 |
| OP_INVOKE | 0x66 | name(2),argc(1) | -(argc) |
| OP_TRY_BEGIN | 0x70 | catch(2),fin(2) | 0 |
| OP_TRY_END | 0x71 | - | 0 |
| OP_THROW | 0x72 | - | -1 |
| OP_CATCH | 0x73 | - | +1 |
| OP_FINALLY | 0x74 | - | 0 |
| OP_DEFER | 0x75 | func(2) | -1 |
| OP_EXEC_DEFERS | 0x76 | - | 0 |
| OP_SPAWN | 0x80 | argc(1) | -(argc) |
| OP_AWAIT | 0x81 | - | 0 |
| OP_DETACH | 0x82 | - | -1 |
| OP_CHANNEL | 0x83 | - | 0 |
| OP_CHAN_SEND | 0x84 | - | -2 |
| OP_CHAN_RECV | 0x85 | - | 0 |
| OP_CHAN_CLOSE | 0x86 | - | -1 |
| OP_TYPEOF | 0x90 | - | 0 |
| OP_CONVERT | 0x91 | type(1) | 0 |
| OP_ALLOC | 0xA0 | - | 0 |
| OP_FREE | 0xA1 | - | -1 |
| OP_BUFFER | 0xA2 | - | 0 |

---

## Appendix B: Example Compilation

### Source

```hemlock
fn fibonacci(n: i32): i32 {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

print(fibonacci(10));
```

### Bytecode (fibonacci function)

```
== fibonacci ==
0000    2 OP_LOAD_LOCAL    0        ; n
0002    | OP_CONST         0        ; 1
0005    | OP_LE
0006    | OP_JUMP_IF_FALSE 0012     ; skip to else
0009    3 OP_LOAD_LOCAL    0        ; return n
0011    | OP_RETURN
0012    5 OP_LOAD_GLOBAL   0        ; fibonacci
0015    | OP_LOAD_LOCAL    0        ; n
0017    | OP_CONST         0        ; 1
0020    | OP_SUB
0021    | OP_CALL          1
0023    | OP_LOAD_GLOBAL   0        ; fibonacci
0026    | OP_LOAD_LOCAL    0        ; n
0028    | OP_CONST         1        ; 2
0031    | OP_SUB
0032    | OP_CALL          1
0034    | OP_ADD
0035    | OP_RETURN
```

### Bytecode (top-level)

```
== <script> ==
0000    1 OP_CLOSURE       0 0      ; fibonacci (no upvalues)
0004    | OP_STORE_GLOBAL  0        ; define 'fibonacci'
0007    8 OP_LOAD_GLOBAL   0        ; fibonacci
0010    | OP_CONST         2        ; 10
0013    | OP_CALL          1
0015    | OP_CALL_BUILTIN  0 1      ; print(result)
0019    | OP_POP
0020    | OP_RETURN_NULL
```

---

## Conclusion

The Hemlock 2.0 Bytecode VM provides a path to:

1. **Better performance** through compact bytecode and efficient dispatch
2. **Optimization opportunities** via bytecode-level transformations
3. **Future extensibility** (JIT compilation, AOT via bytecode)
4. **Maintained compatibility** with existing semantics

The implementation is substantial but well-scoped, with clear phases and risk mitigations. The shared frontend and value representation reduce duplication, while the modular design allows incremental development and testing.

**Recommended next step**: Begin Phase 1 (Core VM) with focus on the execution loop and basic arithmetic operations, validating the architecture with simple parity tests.
