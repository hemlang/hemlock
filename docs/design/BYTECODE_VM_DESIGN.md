# Hemlock 2.0 Bytecode VM Design

> A stack-based bytecode virtual machine for efficient Hemlock execution

## Table of Contents

1. [Motivation & Goals](#1-motivation--goals)
2. [Architecture Overview](#2-architecture-overview)
3. [Bytecode Format](#3-bytecode-format)
4. [Instruction Set](#4-instruction-set)
5. [Value Representation](#5-value-representation)
6. [Memory Model](#6-memory-model)
7. [Execution Model](#7-execution-model)
8. [Function Calls & Closures](#8-function-calls--closures)
9. [Async & Concurrency](#9-async--concurrency)
10. [Compilation Pipeline](#10-compilation-pipeline)
11. [Optimization Opportunities](#11-optimization-opportunities)
12. [Implementation Plan](#12-implementation-plan)

---

## 1. Motivation & Goals

### Why a Bytecode VM?

The current Hemlock interpreter is a tree-walking evaluator that traverses the AST for each execution. While clear and maintainable, this approach has inherent overhead:

- **Pointer chasing**: Each AST node requires memory dereference
- **Type dispatch**: Every evaluation requires switch on node type
- **No optimization**: The AST structure limits optimization opportunities
- **Memory overhead**: AST nodes are larger than bytecode instructions

A bytecode VM provides:

| Benefit | Description |
|---------|-------------|
| **Faster dispatch** | Computed goto or switch on compact opcodes |
| **Cache efficiency** | Linear bytecode is cache-friendly |
| **Optimization potential** | Bytecode enables peephole optimization, inlining |
| **Smaller footprint** | Bytecode is more compact than AST |
| **JIT foundation** | Bytecode is the first step toward JIT compilation |

### Design Goals

1. **Performance**: 3-10x faster than tree-walking interpreter
2. **Parity**: Identical semantics to interpreter and compiler
3. **Simplicity**: Clear, maintainable implementation
4. **Debuggability**: Source maps, stack traces, breakpoints
5. **Incrementality**: Can be developed alongside existing backends
6. **Future-proof**: Architecture supports future JIT compilation

### Non-Goals

- Complex optimizing compiler (keep it simple first)
- Register-based VM (stack-based is simpler for closures)
- Ahead-of-time bytecode serialization (not in v1)

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        HEMLOCK SOURCE                           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    SHARED FRONTEND                              │
│  ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────────────┐  │
│  │  Lexer  │ → │ Parser  │ → │   AST   │ → │ Variable Resolve│  │
│  └─────────┘   └─────────┘   └─────────┘   └─────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
    ┌─────────────────┐ ┌───────────┐ ┌─────────────────┐
    │   INTERPRETER   │ │ COMPILER  │ │  BYTECODE VM    │
    │  (tree-walking) │ │ (→ C)     │ │  (new in 2.0)   │
    └─────────────────┘ └───────────┘ └─────────────────┘
                                              │
                              ┌───────────────┴───────────────┐
                              ▼                               ▼
                    ┌─────────────────┐             ┌─────────────────┐
                    │ BYTECODE COMPILER│             │   VM RUNTIME    │
                    │                 │             │                 │
                    │ - AST → Bytecode│             │ - Value Stack   │
                    │ - Constant Pool │             │ - Call Frames   │
                    │ - Source Maps   │             │ - Heap          │
                    └─────────────────┘             │ - Task Scheduler│
                                                    └─────────────────┘
```

### Core Components

| Component | Description |
|-----------|-------------|
| **Bytecode Compiler** | Transforms AST to bytecode |
| **Chunk** | Bytecode container with constants |
| **VM** | Executes bytecode with value stack |
| **CallFrame** | Per-function execution state |
| **Heap** | Reference-counted object storage |
| **Task Scheduler** | Async task management |

---

## 3. Bytecode Format

### Chunk Structure

A **Chunk** is the unit of compiled code (one per function/script):

```c
typedef struct Chunk {
    // Bytecode
    uint8_t *code;           // Bytecode instructions
    size_t code_length;      // Number of bytes
    size_t code_capacity;    // Allocated capacity

    // Constant pool
    Value *constants;        // Constant values
    size_t const_count;      // Number of constants
    size_t const_capacity;   // Allocated capacity

    // Debug info (optional, stripped in release)
    uint32_t *lines;         // Source line for each instruction
    char *source_file;       // Source filename
    SourceMap *source_map;   // Detailed source mapping
} Chunk;
```

### Instruction Encoding

Variable-length encoding with 1-byte opcodes:

```
┌──────────┬──────────────────────────────────────────────────────────┐
│  Format  │  Layout                                                  │
├──────────┼──────────────────────────────────────────────────────────┤
│  Simple  │  [opcode:8]                                              │
│  1-byte  │  [opcode:8] [operand:8]                                  │
│  2-byte  │  [opcode:8] [operand:16]                                 │
│  3-byte  │  [opcode:8] [operand:24]                                 │
│  Wide    │  [OP_WIDE:8] [opcode:8] [operand:16/24/32]               │
└──────────┴──────────────────────────────────────────────────────────┘
```

**Operand types:**
- `u8`: Immediate byte value (0-255)
- `u16`: 16-bit unsigned (big-endian)
- `u24`: 24-bit unsigned (for large constant pools)
- `i16`: 16-bit signed (for jump offsets)

### Bytecode File Format (Future)

```
┌────────────────────────────────────────────────────────────────────┐
│  Magic: "HMLK"  │  Version: u16  │  Flags: u16  │  Checksum: u32   │
├────────────────────────────────────────────────────────────────────┤
│  Constant Pool Section                                             │
│  ├─ count: u32                                                     │
│  └─ entries: [type:u8, length:u32, data:...]                       │
├────────────────────────────────────────────────────────────────────┤
│  Code Section                                                       │
│  ├─ length: u32                                                     │
│  └─ bytecode: [u8...]                                              │
├────────────────────────────────────────────────────────────────────┤
│  Debug Section (optional)                                          │
│  ├─ line_table: [offset:u32, line:u32]...                          │
│  └─ source_file: [length:u16, chars:u8...]                         │
└────────────────────────────────────────────────────────────────────┘
```

---

## 4. Instruction Set

### Opcode Categories

The instruction set is organized into categories for clarity:

### 4.1 Stack Operations

```
┌──────────────┬──────┬─────────┬────────────────────────────────────┐
│    Opcode    │ Args │ Stack   │ Description                        │
├──────────────┼──────┼─────────┼────────────────────────────────────┤
│ OP_NOP       │  -   │ → -     │ No operation                       │
│ OP_POP       │  -   │ a → -   │ Discard top of stack               │
│ OP_POPN      │ u8   │ a...→ - │ Discard N values from stack        │
│ OP_DUP       │  -   │ a → a a │ Duplicate top of stack             │
│ OP_SWAP      │  -   │ a b → b a │ Swap top two values              │
│ OP_ROT3      │  -   │ a b c → c a b │ Rotate top 3 values          │
└──────────────┴──────┴─────────┴────────────────────────────────────┘
```

### 4.2 Constants & Literals

```
┌──────────────────┬───────┬─────────┬────────────────────────────────┐
│      Opcode      │ Args  │ Stack   │ Description                    │
├──────────────────┼───────┼─────────┼────────────────────────────────┤
│ OP_CONST         │ u8    │ → value │ Push constant[idx]             │
│ OP_CONST_LONG    │ u24   │ → value │ Push constant (large pool)     │
│ OP_NULL          │  -    │ → null  │ Push null                      │
│ OP_TRUE          │  -    │ → true  │ Push true                      │
│ OP_FALSE         │  -    │ → false │ Push false                     │
│ OP_ZERO          │  -    │ → 0     │ Push i32(0)                    │
│ OP_ONE           │  -    │ → 1     │ Push i32(1)                    │
│ OP_SMALL_INT     │ i8    │ → n     │ Push small integer (-128..127) │
│ OP_EMPTY_ARRAY   │  -    │ → []    │ Push empty array               │
│ OP_EMPTY_OBJECT  │  -    │ → {}    │ Push empty object              │
└──────────────────┴───────┴─────────┴────────────────────────────────┘
```

### 4.3 Arithmetic Operations

```
┌──────────────┬──────┬───────────────┬──────────────────────────────┐
│    Opcode    │ Args │ Stack         │ Description                  │
├──────────────┼──────┼───────────────┼──────────────────────────────┤
│ OP_ADD       │  -   │ a b → a+b     │ Add (with type promotion)    │
│ OP_SUB       │  -   │ a b → a-b     │ Subtract                     │
│ OP_MUL       │  -   │ a b → a*b     │ Multiply                     │
│ OP_DIV       │  -   │ a b → a/b     │ Divide (always float)        │
│ OP_MOD       │  -   │ a b → a%b     │ Modulo                       │
│ OP_NEG       │  -   │ a → -a        │ Negate                       │
│ OP_ADD_I32   │  -   │ a b → a+b     │ Add (i32 fast path)          │
│ OP_SUB_I32   │  -   │ a b → a-b     │ Subtract (i32 fast path)     │
│ OP_MUL_I32   │  -   │ a b → a*b     │ Multiply (i32 fast path)     │
│ OP_INC       │  -   │ a → a+1       │ Increment                    │
│ OP_DEC       │  -   │ a → a-1       │ Decrement                    │
└──────────────┴──────┴───────────────┴──────────────────────────────┘
```

### 4.4 Bitwise Operations

```
┌──────────────┬──────┬───────────────┬──────────────────────────────┐
│    Opcode    │ Args │ Stack         │ Description                  │
├──────────────┼──────┼───────────────┼──────────────────────────────┤
│ OP_BIT_AND   │  -   │ a b → a&b     │ Bitwise AND                  │
│ OP_BIT_OR    │  -   │ a b → a|b     │ Bitwise OR                   │
│ OP_BIT_XOR   │  -   │ a b → a^b     │ Bitwise XOR                  │
│ OP_BIT_NOT   │  -   │ a → ~a        │ Bitwise NOT                  │
│ OP_SHL       │  -   │ a b → a<<b    │ Shift left                   │
│ OP_SHR       │  -   │ a b → a>>b    │ Shift right (arithmetic)     │
└──────────────┴──────┴───────────────┴──────────────────────────────┘
```

### 4.5 Comparison Operations

```
┌──────────────┬──────┬───────────────┬──────────────────────────────┐
│    Opcode    │ Args │ Stack         │ Description                  │
├──────────────┼──────┼───────────────┼──────────────────────────────┤
│ OP_EQ        │  -   │ a b → a==b    │ Equal                        │
│ OP_NE        │  -   │ a b → a!=b    │ Not equal                    │
│ OP_LT        │  -   │ a b → a<b     │ Less than                    │
│ OP_LE        │  -   │ a b → a<=b    │ Less or equal                │
│ OP_GT        │  -   │ a b → a>b     │ Greater than                 │
│ OP_GE        │  -   │ a b → a>=b    │ Greater or equal             │
│ OP_EQ_I32    │  -   │ a b → a==b    │ Equal (i32 fast path)        │
│ OP_LT_I32    │  -   │ a b → a<b     │ Less than (i32 fast path)    │
└──────────────┴──────┴───────────────┴──────────────────────────────┘
```

### 4.6 Logical Operations

```
┌──────────────┬──────┬───────────────┬──────────────────────────────┐
│    Opcode    │ Args │ Stack         │ Description                  │
├──────────────┼──────┼───────────────┼──────────────────────────────┤
│ OP_NOT       │  -   │ a → !a        │ Logical NOT                  │
│ OP_TRUTHY    │  -   │ a → bool(a)   │ Convert to boolean           │
└──────────────┴──────┴───────────────┴──────────────────────────────┘
```

Note: `&&` and `||` use jump instructions for short-circuit evaluation.

### 4.7 Variable Operations

```
┌───────────────────┬───────────┬─────────┬──────────────────────────┐
│      Opcode       │   Args    │ Stack   │ Description              │
├───────────────────┼───────────┼─────────┼──────────────────────────┤
│ OP_GET_LOCAL      │ u8        │ → value │ Push local[slot]         │
│ OP_SET_LOCAL      │ u8        │ a → a   │ Store to local[slot]     │
│ OP_GET_UPVALUE    │ u8        │ → value │ Push captured upvalue    │
│ OP_SET_UPVALUE    │ u8        │ a → a   │ Store to upvalue         │
│ OP_GET_GLOBAL     │ u16       │ → value │ Push global (by name idx)│
│ OP_SET_GLOBAL     │ u16       │ a → a   │ Store to global          │
│ OP_DEFINE_GLOBAL  │ u16       │ a → -   │ Define new global        │
│ OP_CLOSE_UPVALUE  │  -        │ → -     │ Close upvalue on stack   │
└───────────────────┴───────────┴─────────┴──────────────────────────┘
```

### 4.8 Property & Index Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_GET_PROPERTY   │ u16   │ obj → value   │ Get obj.name             │
│ OP_SET_PROPERTY   │ u16   │ obj val → val │ Set obj.name = val       │
│ OP_GET_INDEX      │  -    │ obj idx → val │ Get obj[idx]             │
│ OP_SET_INDEX      │  -    │ obj idx v → v │ Set obj[idx] = v         │
│ OP_GET_PROP_CHAIN │ u16   │ obj → val/null│ obj?.name (optional)     │
│ OP_GET_IDX_CHAIN  │  -    │ obj idx → v/n │ obj?.[idx] (optional)    │
│ OP_NULL_COALESCE  │ i16   │ a → a/skip    │ a ?? b (jump if non-null)│
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.9 Control Flow

```
┌───────────────────┬───────┬─────────────┬────────────────────────────┐
│      Opcode       │ Args  │ Stack       │ Description                │
├───────────────────┼───────┼─────────────┼────────────────────────────┤
│ OP_JUMP           │ i16   │ → -         │ Unconditional jump         │
│ OP_JUMP_LONG      │ i24   │ → -         │ Long unconditional jump    │
│ OP_JUMP_IF_FALSE  │ i16   │ cond → -    │ Jump if false (pop)        │
│ OP_JUMP_IF_TRUE   │ i16   │ cond → -    │ Jump if true (pop)         │
│ OP_JUMP_IF_NULL   │ i16   │ val → -     │ Jump if null (pop)         │
│ OP_POP_JUMP_FALSE │ i16   │ cond → cond │ Jump if false (keep)       │
│ OP_POP_JUMP_TRUE  │ i16   │ cond → cond │ Jump if true (keep)        │
│ OP_LOOP           │ i16   │ → -         │ Jump backward (loop)       │
└───────────────────┴───────┴─────────────┴────────────────────────────┘
```

### 4.10 Function Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_CALL           │ u8    │ fn args → ret │ Call function (N args)   │
│ OP_CALL_METHOD    │ u8,u16│ obj args → ret│ Call obj.method(N args)  │
│ OP_RETURN         │  -    │ val → -       │ Return from function     │
│ OP_RETURN_NULL    │  -    │ → -           │ Return null              │
│ OP_CLOSURE        │ u16   │ → closure     │ Create closure           │
│ OP_TAIL_CALL      │ u8    │ fn args → ret │ Tail call optimization   │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.11 Array & Object Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_ARRAY          │ u8    │ elems → arr   │ Create array (N elems)   │
│ OP_ARRAY_LONG     │ u16   │ elems → arr   │ Create large array       │
│ OP_OBJECT         │ u8    │ k v... → obj  │ Create object (N pairs)  │
│ OP_OBJECT_LONG    │ u16   │ k v... → obj  │ Create large object      │
│ OP_TYPED_ARRAY    │ u8,u8 │ elems → arr   │ Create typed array       │
│ OP_TYPED_OBJECT   │ u16   │ elems → obj   │ Create typed object      │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.12 Type Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_TYPEOF         │  -    │ val → str     │ Get type as string       │
│ OP_INSTANCEOF     │ u16   │ val → bool    │ Check type               │
│ OP_CAST           │ u8    │ val → val'    │ Cast to type             │
│ OP_CHECK_TYPE     │ u8    │ val → val     │ Assert type (or error)   │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.13 Memory Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_ALLOC          │  -    │ size → ptr    │ Allocate raw memory      │
│ OP_BUFFER         │  -    │ size → buf    │ Allocate safe buffer     │
│ OP_FREE           │  -    │ ptr → -       │ Free memory              │
│ OP_MEMCPY         │  -    │ d s n → -     │ Copy memory              │
│ OP_MEMSET         │  -    │ p v n → -     │ Set memory               │
│ OP_SIZEOF         │  -    │ val → size    │ Get size of value        │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.14 Exception Handling

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_TRY_BEGIN      │ i16   │ → -           │ Push exception handler   │
│ OP_TRY_END        │  -    │ → -           │ Pop exception handler    │
│ OP_CATCH          │ u8    │ ex → -        │ Bind exception to local  │
│ OP_THROW          │  -    │ val → ⊥       │ Throw exception          │
│ OP_FINALLY_BEGIN  │  -    │ → -           │ Enter finally block      │
│ OP_FINALLY_END    │  -    │ → -           │ Exit finally, re-throw?  │
│ OP_PANIC          │  -    │ msg → ⊥       │ Unrecoverable error      │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.15 Defer Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_DEFER          │ u16   │ → -           │ Register deferred closure│
│ OP_RUN_DEFERS     │  -    │ → -           │ Execute deferred actions │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.16 Async Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_SPAWN          │ u8    │ fn args → task│ Spawn async task         │
│ OP_AWAIT          │  -    │ task → result │ Wait for task completion │
│ OP_DETACH         │  -    │ task → -      │ Detach task              │
│ OP_CHANNEL        │  -    │ cap → ch      │ Create channel           │
│ OP_SEND           │  -    │ ch val → -    │ Send to channel          │
│ OP_RECV           │  -    │ ch → val      │ Receive from channel     │
│ OP_CLOSE_CHANNEL  │  -    │ ch → -        │ Close channel            │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.17 Built-in Method Calls

For common operations, specialized opcodes avoid the overhead of general method dispatch:

```
┌─────────────────────┬───────┬───────────────┬────────────────────────┐
│       Opcode        │ Args  │ Stack         │ Description            │
├─────────────────────┼───────┼───────────────┼────────────────────────┤
│ OP_STRING_LENGTH    │  -    │ str → len     │ str.length             │
│ OP_STRING_SUBSTR    │  -    │ s a b → s'    │ str.substr(a, b)       │
│ OP_STRING_CONCAT    │  -    │ a b → a+b     │ String concatenation   │
│ OP_ARRAY_LENGTH     │  -    │ arr → len     │ arr.length             │
│ OP_ARRAY_PUSH       │  -    │ arr v → -     │ arr.push(v)            │
│ OP_ARRAY_POP        │  -    │ arr → v       │ arr.pop()              │
│ OP_PRINT            │  -    │ val → -       │ print(val)             │
│ OP_PRINTLN          │  -    │ val → -       │ println(val)           │
└─────────────────────┴───────┴───────────────┴────────────────────────┘
```

### 4.18 Module Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_IMPORT          │ u16   │ → module      │ Import module by name    │
│ OP_IMPORT_FROM     │ u16,u8│ → values...   │ Import N symbols         │
│ OP_EXPORT          │ u16   │ val → -       │ Export value             │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

### 4.19 FFI Operations

```
┌───────────────────┬───────┬───────────────┬──────────────────────────┐
│      Opcode       │ Args  │ Stack         │ Description              │
├───────────────────┼───────┼───────────────┼──────────────────────────┤
│ OP_FFI_OPEN       │  -    │ path → lib    │ Open shared library      │
│ OP_FFI_BIND       │  -    │ lib s t r → fn│ Bind function            │
│ OP_FFI_CALL       │ u8    │ fn args → ret │ Call FFI function        │
│ OP_FFI_CLOSE      │  -    │ lib → -       │ Close library            │
└───────────────────┴───────┴───────────────┴──────────────────────────┘
```

---

## 5. Value Representation

### Tagged Value Union

Values use the same representation as the interpreter for compatibility:

```c
typedef enum {
    VAL_NULL = 0,
    VAL_BOOL,
    VAL_I8, VAL_I16, VAL_I32, VAL_I64,
    VAL_U8, VAL_U16, VAL_U32, VAL_U64,
    VAL_F32, VAL_F64,
    VAL_RUNE,
    VAL_STRING,
    VAL_ARRAY,
    VAL_OBJECT,
    VAL_FUNCTION,
    VAL_CLOSURE,
    VAL_NATIVE_FN,
    VAL_TASK,
    VAL_CHANNEL,
    VAL_FILE,
    VAL_PTR,
    VAL_BUFFER,
    VAL_FFI_FN,
    VAL_TYPE,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        int8_t i8; int16_t i16; int32_t i32; int64_t i64;
        uint8_t u8; uint16_t u16; uint32_t u32; uint64_t u64;
        float f32; double f64;
        uint32_t rune;
        ObjString *string;
        ObjArray *array;
        ObjObject *object;
        ObjFunction *function;
        ObjClosure *closure;
        NativeFn native;
        ObjTask *task;
        ObjChannel *channel;
        ObjFile *file;
        void *ptr;
        ObjBuffer *buffer;
        ObjFFIFn *ffi;
    } as;
} Value;
```

### NaN-Boxing (Future Optimization)

For 64-bit systems, NaN-boxing can pack values into 64 bits:

```
┌───────────────────────────────────────────────────────────────────┐
│ 64-bit NaN-boxed Value                                            │
├───────────────────────────────────────────────────────────────────┤
│ Double:     [sign:1] [exp:11] [mantissa:52] (normal IEEE 754)     │
│ Quiet NaN:  [1] [11111111111] [1xxx...] (signaling bit set)       │
│ Tagged:     [1111111111111] [tag:3] [payload:48]                  │
│                                                                   │
│ Tags (3 bits):                                                    │
│   000 = Pointer (48-bit address)                                  │
│   001 = Integer (48-bit signed)                                   │
│   010 = Boolean/Null                                              │
│   011 = Small types (i8, u8, rune, etc.)                          │
│   1xx = Reserved                                                  │
└───────────────────────────────────────────────────────────────────┘
```

This optimization is deferred to a later phase.

### Heap Objects

All reference types inherit from a common header:

```c
typedef struct Obj {
    ObjType type;
    atomic_int ref_count;
    struct Obj *next;  // For GC tracking (optional)
} Obj;

typedef struct {
    Obj obj;
    char *chars;
    size_t length;
    size_t char_length;  // UTF-8 codepoint count
    uint32_t hash;       // Cached hash
} ObjString;

typedef struct {
    Obj obj;
    Value *elements;
    size_t length;
    size_t capacity;
    ValueType element_type;  // For typed arrays
} ObjArray;

typedef struct {
    Obj obj;
    Chunk *chunk;
    ObjString *name;
    int arity;
    int upvalue_count;
    bool is_async;
    ValueType *param_types;
    ValueType return_type;
} ObjFunction;

typedef struct {
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalue_count;
} ObjClosure;
```

### Upvalue Representation

Upvalues capture variables from enclosing scopes:

```c
typedef struct ObjUpvalue {
    Obj obj;
    Value *location;     // Points to stack slot or closed value
    Value closed;        // Value after variable goes out of scope
    struct ObjUpvalue *next;  // Linked list for closing
} ObjUpvalue;
```

```
Open Upvalue:                    Closed Upvalue:
┌──────────────┐                 ┌──────────────┐
│ location ────┼───► stack[i]    │ location ────┼──┐
│ closed: ?    │                 │ closed: val  │◄─┘
└──────────────┘                 └──────────────┘
```

---

## 6. Memory Model

### Stack Layout

```
┌─────────────────────────────────────────────────────────────────┐
│                     VALUE STACK (grows up)                      │
├─────────────────────────────────────────────────────────────────┤
│  Frame 0 (main script)                                          │
│  ├─ local[0]: <script>                                          │
│  ├─ local[1]: x = 10                                            │
│  ├─ local[2]: y = 20                                            │
│  └─ [temp values for expressions]                               │
├─────────────────────────────────────────────────────────────────┤
│  Frame 1 (function call)                                        │
│  ├─ local[0]: <function>                                        │
│  ├─ local[1]: arg1                                              │
│  ├─ local[2]: arg2                                              │
│  ├─ local[3]: local_var                                         │
│  └─ [temp values]                                               │
├─────────────────────────────────────────────────────────────────┤
│  Frame 2 (nested call)                                          │
│  └─ ...                                                         │
├─────────────────────────────────────────────────────────────────┤
│  [stack_top pointer]                                            │
└─────────────────────────────────────────────────────────────────┘
```

### Call Frame Structure

```c
typedef struct {
    ObjClosure *closure;     // Currently executing closure
    uint8_t *ip;             // Instruction pointer
    Value *slots;            // Pointer to first local slot

    // Exception handling
    ExceptionHandler *handler;  // Current try/catch handler

    // Defer stack
    DeferEntry *defers;      // Stack of deferred closures
    int defer_count;

    // Debug info
    int line;                // Current source line
} CallFrame;

typedef struct {
    uint8_t *catch_ip;       // Where to jump on exception
    uint8_t *finally_ip;     // Finally block (may be NULL)
    Value *stack_base;       // Stack to restore on exception
} ExceptionHandler;

typedef struct {
    ObjClosure *closure;     // Deferred closure to execute
    Value *env_slots;        // Captured environment
} DeferEntry;
```

### VM State

```c
typedef struct {
    // Execution
    CallFrame frames[FRAMES_MAX];
    int frame_count;

    // Value stack
    Value stack[STACK_MAX];
    Value *stack_top;

    // Upvalues
    ObjUpvalue *open_upvalues;  // Linked list of open upvalues

    // Globals
    Table globals;             // Hash table of global variables

    // Modules
    Table modules;             // Loaded modules cache

    // Strings
    Table strings;             // String interning table

    // Memory
    Obj *objects;              // All allocated objects (for GC)
    size_t bytes_allocated;
    size_t next_gc;

    // Async
    TaskScheduler *scheduler;  // Task management

    // Error state
    bool had_error;
    char error_message[256];
} VM;
```

### Memory Management

The VM uses reference counting with optional cycle detection:

```c
// Increment reference count
static inline void vm_retain(Value value) {
    if (IS_OBJ(value)) {
        atomic_fetch_add(&AS_OBJ(value)->ref_count, 1);
    }
}

// Decrement and potentially free
static inline void vm_release(Value value) {
    if (IS_OBJ(value)) {
        Obj *obj = AS_OBJ(value);
        if (atomic_fetch_sub(&obj->ref_count, 1) == 1) {
            free_object(obj);
        }
    }
}

// Cycle detection for arrays/objects (periodic)
void vm_collect_cycles(VM *vm);
```

---

## 7. Execution Model

### Main Execution Loop

The VM uses a dispatch loop with computed goto (when available):

```c
InterpretResult vm_run(VM *vm) {
    CallFrame *frame = &vm->frames[vm->frame_count - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk->constants[READ_BYTE()])
#define BINARY_OP(op) \
    do { \
        Value b = pop(); \
        Value a = pop(); \
        push(value_##op(a, b)); \
    } while (false)

#ifdef COMPUTED_GOTO
    static void *dispatch_table[] = {
        &&op_nop, &&op_pop, &&op_const, /* ... */
    };
    #define DISPATCH() goto *dispatch_table[READ_BYTE()]
    #define CASE(op) op_##op:
#else
    #define DISPATCH() goto dispatch
    #define CASE(op) case OP_##op:
#endif

    DISPATCH();

#ifndef COMPUTED_GOTO
dispatch:
    switch (READ_BYTE()) {
#endif

    CASE(NOP)
        DISPATCH();

    CASE(POP)
        vm_release(pop());
        DISPATCH();

    CASE(CONST) {
        Value constant = READ_CONSTANT();
        vm_retain(constant);
        push(constant);
        DISPATCH();
    }

    CASE(ADD) {
        Value b = pop();
        Value a = pop();
        // Fast path for i32
        if (IS_I32(a) && IS_I32(b)) {
            push(I32_VAL(AS_I32(a) + AS_I32(b)));
        } else {
            push(value_add(a, b));
            vm_release(a);
            vm_release(b);
        }
        DISPATCH();
    }

    CASE(CALL) {
        uint8_t arg_count = READ_BYTE();
        if (!call_value(vm, peek(arg_count), arg_count)) {
            return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm->frames[vm->frame_count - 1];
        DISPATCH();
    }

    CASE(RETURN) {
        Value result = pop();

        // Run deferred actions
        run_defers(vm, frame);

        // Close upvalues
        close_upvalues(vm, frame->slots);

        vm->frame_count--;
        if (vm->frame_count == 0) {
            pop();  // Pop main script
            return INTERPRET_OK;
        }

        // Restore caller's frame
        vm->stack_top = frame->slots;
        push(result);
        frame = &vm->frames[vm->frame_count - 1];
        DISPATCH();
    }

    // ... more opcodes ...

#ifndef COMPUTED_GOTO
    }
#endif

    return INTERPRET_OK;
}
```

### Function Calls

```c
static bool call_closure(VM *vm, ObjClosure *closure, int arg_count) {
    ObjFunction *function = closure->function;

    // Arity check
    if (arg_count != function->arity) {
        runtime_error(vm, "Expected %d arguments but got %d.",
                      function->arity, arg_count);
        return false;
    }

    // Stack overflow check
    if (vm->frame_count == FRAMES_MAX) {
        runtime_error(vm, "Stack overflow.");
        return false;
    }

    // Set up new frame
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = function->chunk->code;
    frame->slots = vm->stack_top - arg_count - 1;
    frame->handler = NULL;
    frame->defers = NULL;
    frame->defer_count = 0;

    return true;
}
```

### Exception Handling

```c
CASE(TRY_BEGIN) {
    int16_t catch_offset = READ_SHORT();

    ExceptionHandler *handler = ALLOCATE(ExceptionHandler, 1);
    handler->catch_ip = frame->ip + catch_offset;
    handler->stack_base = vm->stack_top;
    handler->next = frame->handler;
    frame->handler = handler;
    DISPATCH();
}

CASE(THROW) {
    Value exception = pop();

    // Unwind stack looking for handler
    while (vm->frame_count > 0) {
        CallFrame *f = &vm->frames[vm->frame_count - 1];

        // Run defers first
        run_defers(vm, f);

        if (f->handler != NULL) {
            // Found handler
            ExceptionHandler *h = f->handler;
            f->handler = h->next;
            vm->stack_top = h->stack_base;
            push(exception);
            frame->ip = h->catch_ip;
            FREE(ExceptionHandler, h);
            DISPATCH();
        }

        close_upvalues(vm, f->slots);
        vm->frame_count--;
    }

    // Uncaught exception
    runtime_error(vm, "Uncaught exception: %s", value_to_string(exception));
    return INTERPRET_RUNTIME_ERROR;
}
```

### Defer Execution

```c
CASE(DEFER) {
    uint16_t closure_idx = READ_SHORT();
    ObjClosure *deferred = AS_CLOSURE(READ_CONSTANT_AT(closure_idx));

    // Add to frame's defer stack
    DeferEntry entry = { .closure = deferred, .env_slots = vm->stack_top };
    vm_retain(OBJ_VAL(deferred));

    if (frame->defer_count >= frame->defer_capacity) {
        frame->defer_capacity = GROW_CAPACITY(frame->defer_capacity);
        frame->defers = GROW_ARRAY(DeferEntry, frame->defers, frame->defer_capacity);
    }
    frame->defers[frame->defer_count++] = entry;
    DISPATCH();
}

static void run_defers(VM *vm, CallFrame *frame) {
    // Execute in LIFO order
    for (int i = frame->defer_count - 1; i >= 0; i--) {
        DeferEntry *entry = &frame->defers[i];

        // Call the deferred closure
        push(OBJ_VAL(entry->closure));
        call_closure(vm, entry->closure, 0);
        vm_run_frame(vm);  // Execute deferred code

        vm_release(OBJ_VAL(entry->closure));
    }
    frame->defer_count = 0;
}
```

---

## 8. Function Calls & Closures

### Closure Creation

When a function is defined, the compiler emits `OP_CLOSURE`:

```c
CASE(CLOSURE) {
    ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
    ObjClosure *closure = new_closure(function);
    push(OBJ_VAL(closure));

    // Capture upvalues
    for (int i = 0; i < function->upvalue_count; i++) {
        uint8_t is_local = READ_BYTE();
        uint8_t index = READ_BYTE();

        if (is_local) {
            // Capture from current frame's stack
            closure->upvalues[i] = capture_upvalue(vm, frame->slots + index);
        } else {
            // Capture from enclosing closure
            closure->upvalues[i] = frame->closure->upvalues[index];
        }
    }
    DISPATCH();
}
```

### Upvalue Capture

```c
static ObjUpvalue *capture_upvalue(VM *vm, Value *local) {
    ObjUpvalue *prev = NULL;
    ObjUpvalue *upvalue = vm->open_upvalues;

    // Look for existing upvalue for this slot
    while (upvalue != NULL && upvalue->location > local) {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;  // Reuse existing
    }

    // Create new upvalue
    ObjUpvalue *created = new_upvalue(local);
    created->next = upvalue;

    if (prev == NULL) {
        vm->open_upvalues = created;
    } else {
        prev->next = created;
    }

    return created;
}

static void close_upvalues(VM *vm, Value *last) {
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
        ObjUpvalue *upvalue = vm->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;  // Now points to closed value
        vm->open_upvalues = upvalue->next;
    }
}
```

### Method Calls

Method calls are optimized with `OP_CALL_METHOD`:

```c
CASE(CALL_METHOD) {
    uint8_t arg_count = READ_BYTE();
    uint16_t name_idx = READ_SHORT();
    ObjString *name = AS_STRING(frame->closure->function->chunk->constants[name_idx]);

    Value receiver = peek(arg_count);

    // Dispatch based on receiver type
    if (IS_STRING(receiver)) {
        return string_method(vm, name, arg_count);
    } else if (IS_ARRAY(receiver)) {
        return array_method(vm, name, arg_count);
    } else if (IS_OBJECT(receiver)) {
        ObjObject *object = AS_OBJECT(receiver);
        Value method;
        if (table_get(&object->fields, name, &method)) {
            return call_value(vm, method, arg_count);
        }
        // Fall through to built-in object methods
        return object_method(vm, name, arg_count);
    }
    // ... other types ...

    runtime_error(vm, "Cannot call method '%s' on %s", name->chars,
                  value_type_name(receiver));
    return false;
}
```

---

## 9. Async & Concurrency

### Task Representation

```c
typedef struct {
    Obj obj;
    atomic_int id;
    atomic_int state;  // TASK_READY, RUNNING, BLOCKED, COMPLETED

    ObjClosure *closure;
    Value *args;
    int arg_count;

    Value result;
    Value exception;
    bool has_exception;

    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    atomic_bool joined;
    atomic_bool detached;
} ObjTask;
```

### Spawn Implementation

```c
CASE(SPAWN) {
    uint8_t arg_count = READ_BYTE();
    Value fn_val = peek(arg_count);

    if (!IS_CLOSURE(fn_val)) {
        runtime_error(vm, "Can only spawn closures");
        return INTERPRET_RUNTIME_ERROR;
    }

    ObjClosure *closure = AS_CLOSURE(fn_val);
    if (!closure->function->is_async) {
        runtime_error(vm, "Can only spawn async functions");
        return INTERPRET_RUNTIME_ERROR;
    }

    // Create task
    ObjTask *task = new_task(closure);

    // Deep copy arguments for thread isolation
    for (int i = 0; i < arg_count; i++) {
        task->args[i] = value_deep_copy(peek(arg_count - 1 - i));
    }
    task->arg_count = arg_count;

    // Pop arguments and function
    for (int i = 0; i <= arg_count; i++) {
        vm_release(pop());
    }

    // Start thread
    pthread_create(&task->thread, NULL, task_thread_entry, task);

    push(OBJ_VAL(task));
    DISPATCH();
}
```

### Task Thread Entry

```c
static void *task_thread_entry(void *arg) {
    ObjTask *task = (ObjTask *)arg;

    // Block signals (only main thread handles them)
    sigset_t mask;
    sigfillset(&mask);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    // Create new VM state for this thread
    VM *vm = vm_new();

    // Set up execution
    push_closure(vm, task->closure);
    for (int i = 0; i < task->arg_count; i++) {
        push(vm, task->args[i]);
    }

    // Execute
    atomic_store(&task->state, TASK_RUNNING);
    InterpretResult result = vm_call_closure(vm, task->closure, task->arg_count);

    // Store result
    pthread_mutex_lock(&task->mutex);
    if (result == INTERPRET_OK) {
        task->result = pop(vm);
        task->has_exception = false;
    } else {
        task->exception = vm->exception;
        task->has_exception = true;
    }
    atomic_store(&task->state, TASK_COMPLETED);
    pthread_cond_broadcast(&task->cond);
    pthread_mutex_unlock(&task->mutex);

    // Cleanup
    if (atomic_load(&task->detached)) {
        vm_release(OBJ_VAL(task));
    }

    vm_free(vm);
    return NULL;
}
```

### Await/Join

```c
CASE(AWAIT) {
    Value task_val = pop();

    if (!IS_TASK(task_val)) {
        runtime_error(vm, "Can only await tasks");
        return INTERPRET_RUNTIME_ERROR;
    }

    ObjTask *task = AS_TASK(task_val);

    // Wait for completion
    pthread_mutex_lock(&task->mutex);
    while (atomic_load(&task->state) != TASK_COMPLETED) {
        pthread_cond_wait(&task->cond, &task->mutex);
    }
    pthread_mutex_unlock(&task->mutex);

    // Join thread
    if (!atomic_exchange(&task->joined, true)) {
        pthread_join(task->thread, NULL);
    }

    // Check for exception
    if (task->has_exception) {
        push(task->exception);
        vm_release(task_val);
        goto throw_exception;
    }

    push(task->result);
    vm_release(task_val);
    DISPATCH();
}
```

### Channel Operations

```c
CASE(SEND) {
    Value value = pop();
    Value ch_val = pop();

    if (!IS_CHANNEL(ch_val)) {
        runtime_error(vm, "Can only send on channels");
        return INTERPRET_RUNTIME_ERROR;
    }

    ObjChannel *channel = AS_CHANNEL(ch_val);

    pthread_mutex_lock(&channel->mutex);

    if (channel->closed) {
        pthread_mutex_unlock(&channel->mutex);
        runtime_error(vm, "Cannot send on closed channel");
        return INTERPRET_RUNTIME_ERROR;
    }

    if (channel->capacity == 0) {
        // Unbuffered: rendezvous
        channel->unbuffered_value = value;
        channel->sender_waiting = true;
        pthread_cond_signal(&channel->not_empty);

        while (channel->sender_waiting && !channel->closed) {
            pthread_cond_wait(&channel->rendezvous, &channel->mutex);
        }
    } else {
        // Buffered: wait if full
        while (channel->count == channel->capacity && !channel->closed) {
            pthread_cond_wait(&channel->not_full, &channel->mutex);
        }

        if (!channel->closed) {
            channel->buffer[channel->tail] = value;
            channel->tail = (channel->tail + 1) % channel->capacity;
            channel->count++;
            pthread_cond_signal(&channel->not_empty);
        }
    }

    pthread_mutex_unlock(&channel->mutex);
    vm_release(ch_val);
    DISPATCH();
}
```

---

## 10. Compilation Pipeline

### Compiler Architecture

```
┌────────────────────────────────────────────────────────────────────┐
│                      AST (from frontend)                           │
└────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────────────┐
│                    BYTECODE COMPILER                               │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────────┐    │
│  │ Scope Resolver │→ │ Code Generator │→ │ Constant Pool Mgr  │    │
│  └────────────────┘  └────────────────┘  └────────────────────┘    │
│          │                   │                     │               │
│          ▼                   ▼                     ▼               │
│  ┌────────────────────────────────────────────────────────────────┐│
│  │                      Chunk                                     ││
│  │  ┌──────────┐  ┌──────────────┐  ┌───────────────┐             ││
│  │  │ Bytecode │  │ Constants    │  │ Debug Info    │             ││
│  │  └──────────┘  └──────────────┘  └───────────────┘             ││
│  └────────────────────────────────────────────────────────────────┘│
└────────────────────────────────────────────────────────────────────┘
```

### Compiler State

```c
typedef struct {
    // Current function being compiled
    ObjFunction *function;
    FunctionType type;  // SCRIPT, FUNCTION, METHOD, ASYNC

    // Locals in current scope
    Local locals[UINT8_COUNT];
    int local_count;
    int scope_depth;

    // Upvalues captured by current function
    Upvalue upvalues[UINT8_COUNT];

    // Enclosing compiler (for nested functions)
    struct Compiler *enclosing;

    // Loop state for break/continue
    int loop_start;
    int loop_scope_depth;
    int *break_jumps;
    int break_count;

    // Current source location
    const char *source_file;
    int current_line;
} Compiler;

typedef struct {
    Token name;
    int depth;
    bool is_captured;
    bool is_const;
} Local;

typedef struct {
    uint8_t index;
    bool is_local;
} Upvalue;
```

### Expression Compilation

```c
static void compile_expr(Compiler *compiler, Expr *expr) {
    switch (expr->type) {
        case EXPR_NUMBER:
            emit_constant(compiler, expr->number_value);
            break;

        case EXPR_IDENT:
            compile_variable(compiler, expr->identifier);
            break;

        case EXPR_BINARY:
            compile_binary(compiler, expr);
            break;

        case EXPR_CALL:
            compile_call(compiler, expr);
            break;

        case EXPR_FUNCTION:
            compile_function(compiler, expr, TYPE_FUNCTION);
            break;

        // ... other expression types
    }
}

static void compile_binary(Compiler *compiler, Expr *expr) {
    BinaryOp op = expr->binary.op;

    // Short-circuit operators need special handling
    if (op == OP_AND) {
        compile_expr(compiler, expr->binary.left);
        int end_jump = emit_jump(compiler, OP_POP_JUMP_FALSE);
        emit_byte(compiler, OP_POP);
        compile_expr(compiler, expr->binary.right);
        patch_jump(compiler, end_jump);
        return;
    }

    if (op == OP_OR) {
        compile_expr(compiler, expr->binary.left);
        int end_jump = emit_jump(compiler, OP_POP_JUMP_TRUE);
        emit_byte(compiler, OP_POP);
        compile_expr(compiler, expr->binary.right);
        patch_jump(compiler, end_jump);
        return;
    }

    // Standard binary operators: compile both operands then operator
    compile_expr(compiler, expr->binary.left);
    compile_expr(compiler, expr->binary.right);

    switch (op) {
        case OP_ADD: emit_byte(compiler, OP_ADD); break;
        case OP_SUB: emit_byte(compiler, OP_SUB); break;
        case OP_MUL: emit_byte(compiler, OP_MUL); break;
        case OP_DIV: emit_byte(compiler, OP_DIV); break;
        case OP_EQ:  emit_byte(compiler, OP_EQ);  break;
        case OP_LT:  emit_byte(compiler, OP_LT);  break;
        // ... other operators
    }
}
```

### Statement Compilation

```c
static void compile_stmt(Compiler *compiler, Stmt *stmt) {
    switch (stmt->type) {
        case STMT_LET:
            compile_let(compiler, stmt);
            break;

        case STMT_IF:
            compile_if(compiler, stmt);
            break;

        case STMT_WHILE:
            compile_while(compiler, stmt);
            break;

        case STMT_FOR:
            compile_for(compiler, stmt);
            break;

        case STMT_RETURN:
            compile_return(compiler, stmt);
            break;

        case STMT_TRY:
            compile_try(compiler, stmt);
            break;

        case STMT_DEFER:
            compile_defer(compiler, stmt);
            break;

        // ... other statement types
    }
}

static void compile_if(Compiler *compiler, Stmt *stmt) {
    compile_expr(compiler, stmt->if_stmt.condition);

    int then_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
    emit_byte(compiler, OP_POP);  // Pop condition

    compile_stmt(compiler, stmt->if_stmt.then_branch);

    int else_jump = emit_jump(compiler, OP_JUMP);
    patch_jump(compiler, then_jump);
    emit_byte(compiler, OP_POP);  // Pop condition

    if (stmt->if_stmt.else_branch != NULL) {
        compile_stmt(compiler, stmt->if_stmt.else_branch);
    }

    patch_jump(compiler, else_jump);
}

static void compile_while(Compiler *compiler, Stmt *stmt) {
    int loop_start = current_offset(compiler);

    // Save loop state for break/continue
    int enclosing_loop_start = compiler->loop_start;
    int enclosing_scope = compiler->loop_scope_depth;
    int *enclosing_breaks = compiler->break_jumps;
    int enclosing_break_count = compiler->break_count;

    compiler->loop_start = loop_start;
    compiler->loop_scope_depth = compiler->scope_depth;
    compiler->break_jumps = NULL;
    compiler->break_count = 0;

    compile_expr(compiler, stmt->while_stmt.condition);

    int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
    emit_byte(compiler, OP_POP);

    compile_stmt(compiler, stmt->while_stmt.body);

    emit_loop(compiler, loop_start);

    patch_jump(compiler, exit_jump);
    emit_byte(compiler, OP_POP);

    // Patch break jumps
    for (int i = 0; i < compiler->break_count; i++) {
        patch_jump(compiler, compiler->break_jumps[i]);
    }

    // Restore loop state
    compiler->loop_start = enclosing_loop_start;
    compiler->loop_scope_depth = enclosing_scope;
    compiler->break_jumps = enclosing_breaks;
    compiler->break_count = enclosing_break_count;
}
```

### Closure Compilation

```c
static void compile_function(Compiler *compiler, Expr *expr, FunctionType type) {
    Compiler fn_compiler;
    init_compiler(&fn_compiler, type);
    fn_compiler.enclosing = compiler;

    begin_scope(&fn_compiler);

    // Parameters
    for (int i = 0; i < expr->function.param_count; i++) {
        declare_variable(&fn_compiler, expr->function.params[i]);
        define_variable(&fn_compiler, 0);  // Locals don't need global index
    }

    // Body
    compile_stmt(&fn_compiler, expr->function.body);

    // Implicit return null if no explicit return
    emit_byte(&fn_compiler, OP_NULL);
    emit_byte(&fn_compiler, OP_RETURN);

    ObjFunction *function = end_compiler(&fn_compiler);

    // Emit closure creation in enclosing compiler
    emit_bytes(compiler, OP_CLOSURE, make_constant(compiler, OBJ_VAL(function)));

    // Emit upvalue capture info
    for (int i = 0; i < function->upvalue_count; i++) {
        emit_byte(compiler, fn_compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler, fn_compiler.upvalues[i].index);
    }
}

static int resolve_upvalue(Compiler *compiler, Token *name) {
    if (compiler->enclosing == NULL) return -1;

    // Look in immediately enclosing function's locals
    int local = resolve_local(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    // Look in enclosing function's upvalues (recursive)
    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}
```

---

## 11. Optimization Opportunities

### Phase 1: Basic Optimizations (Included in Initial Release)

#### Constant Folding

Evaluate constant expressions at compile time:

```c
// Before: let x = 2 + 3;
OP_CONST 2
OP_CONST 3
OP_ADD
OP_SET_LOCAL 0

// After: let x = 5;
OP_CONST 5
OP_SET_LOCAL 0
```

#### Specialized Instructions

Use type-specific fast paths:

```c
// Emit specialized instruction when types are known
if (left_type == VAL_I32 && right_type == VAL_I32) {
    emit_byte(compiler, OP_ADD_I32);  // No type checking
} else {
    emit_byte(compiler, OP_ADD);       // General case
}
```

#### Dead Code Elimination

Remove unreachable code:

```c
// if (false) { ... } → removed entirely
// After return → subsequent statements removed
```

### Phase 2: Peephole Optimizations

Pattern-based bytecode rewriting:

```c
static void peephole_optimize(Chunk *chunk) {
    for (int i = 0; i < chunk->code_length - 1; i++) {
        // Pattern: PUSH_NULL, POP → remove both
        if (chunk->code[i] == OP_NULL && chunk->code[i+1] == OP_POP) {
            nop_instruction(chunk, i);
            nop_instruction(chunk, i+1);
        }

        // Pattern: JUMP +0 → remove
        if (chunk->code[i] == OP_JUMP) {
            int16_t offset = read_short(chunk, i+1);
            if (offset == 0) {
                nop_instructions(chunk, i, 3);
            }
        }

        // Pattern: DUP, SET_LOCAL, POP → SET_LOCAL
        // ...
    }
}
```

### Phase 3: Inline Caching (Future)

Cache method lookups for repeated calls:

```c
typedef struct {
    ObjString *cached_name;
    ValueType cached_type;
    void *cached_method;
} InlineCache;

CASE(CALL_METHOD_CACHED) {
    InlineCache *cache = &frame->inline_caches[READ_BYTE()];
    Value receiver = peek(arg_count);

    if (receiver.type == cache->cached_type) {
        // Fast path: use cached method
        call_cached(vm, cache->cached_method, arg_count);
    } else {
        // Slow path: lookup and cache
        void *method = lookup_method(receiver.type, cache->cached_name);
        cache->cached_type = receiver.type;
        cache->cached_method = method;
        call_cached(vm, method, arg_count);
    }
    DISPATCH();
}
```

### Phase 4: JIT Compilation (Future)

Profile-guided compilation of hot paths:

```
┌──────────────────────────────────────────────────────────────────┐
│                       JIT PIPELINE                                │
├──────────────────────────────────────────────────────────────────┤
│  1. Profile: Track call counts, loop iterations                  │
│  2. Detect: Identify hot functions (> threshold calls)           │
│  3. Compile: Bytecode → IR → Native code                         │
│  4. Install: Replace bytecode entry with native trampoline       │
│  5. Optimize: Apply type specialization based on profiles        │
└──────────────────────────────────────────────────────────────────┘
```

---

## 12. Implementation Plan

### Phase 1: Core VM (Weeks 1-4)

**Goal:** Basic execution of simple programs

| Task | Description |
|------|-------------|
| Value representation | Implement `Value` type with all Hemlock types |
| Chunk structure | Bytecode container with constants |
| Basic opcodes | Stack ops, constants, arithmetic, comparison |
| Execution loop | Main dispatch with computed goto |
| Global variables | Define, get, set globals |
| Local variables | Stack-based locals with scope |
| Control flow | if/else, while, for loops |
| Functions | Basic function calls (no closures) |

**Deliverable:** Run `tests/parity/language/` subset (control flow, operators)

### Phase 2: Closures & Objects (Weeks 5-8)

**Goal:** Full closure support and object system

| Task | Description |
|------|-------------|
| Upvalue capture | Open and closed upvalues |
| Closure creation | `OP_CLOSURE` with upvalue binding |
| Objects | Create, get/set properties |
| Arrays | Create, index, push/pop |
| Strings | Interning, concatenation |
| Methods | String/array method dispatch |
| Type checking | `typeof`, type assertions |

**Deliverable:** Run string and array method tests

### Phase 3: Memory & Error Handling (Weeks 9-12)

**Goal:** Complete memory management and exceptions

| Task | Description |
|------|-------------|
| Reference counting | Retain/release for all heap types |
| Manual memory | `alloc`, `free`, `buffer` |
| Exception handling | try/catch/finally, throw |
| Defer | Register and execute deferred actions |
| Panic | Unrecoverable errors |
| Stack traces | Source locations in errors |

**Deliverable:** Pass all memory and error handling parity tests

### Phase 4: Async & Concurrency (Weeks 13-16)

**Goal:** Full async/await support

| Task | Description |
|------|-------------|
| Task creation | `spawn` with thread isolation |
| Task joining | `await`/`join` with result transfer |
| Task detach | Fire-and-forget tasks |
| Channels | Buffered and unbuffered |
| Thread safety | Atomic refcounts, proper locking |

**Deliverable:** Pass async parity tests

### Phase 5: Modules & FFI (Weeks 17-20)

**Goal:** Complete language support

| Task | Description |
|------|-------------|
| Import/export | Module loading and caching |
| Standard library | Port stdlib modules |
| FFI | Library loading, function binding |
| Signals | Signal handling integration |
| File I/O | File operations |

**Deliverable:** Full parity test suite passing

### Phase 6: Optimization & Polish (Weeks 21-24)

**Goal:** Performance and production readiness

| Task | Description |
|------|-------------|
| Constant folding | Compile-time evaluation |
| Peephole optimization | Bytecode pattern rewriting |
| Specialized opcodes | Type-specific fast paths |
| Benchmarking | Compare to interpreter |
| Documentation | API docs, internals guide |
| Integration | CLI flag to select VM backend |

**Deliverable:** 3-10x speedup over interpreter, full documentation

---

## Appendix A: Opcode Encoding Table

```
┌────────┬─────────────────────┬───────────┬─────────────────────────────┐
│ Opcode │ Name                │ Operands  │ Stack Effect                │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x00   │ OP_NOP              │ -         │ → -                         │
│ 0x01   │ OP_POP              │ -         │ a → -                       │
│ 0x02   │ OP_POPN             │ u8        │ a... → -                    │
│ 0x03   │ OP_DUP              │ -         │ a → a a                     │
│ 0x04   │ OP_SWAP             │ -         │ a b → b a                   │
│ 0x05   │ OP_ROT3             │ -         │ a b c → c a b               │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x10   │ OP_CONST            │ u8        │ → value                     │
│ 0x11   │ OP_CONST_LONG       │ u24       │ → value                     │
│ 0x12   │ OP_NULL             │ -         │ → null                      │
│ 0x13   │ OP_TRUE             │ -         │ → true                      │
│ 0x14   │ OP_FALSE            │ -         │ → false                     │
│ 0x15   │ OP_ZERO             │ -         │ → 0                         │
│ 0x16   │ OP_ONE              │ -         │ → 1                         │
│ 0x17   │ OP_SMALL_INT        │ i8        │ → n                         │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x20   │ OP_ADD              │ -         │ a b → a+b                   │
│ 0x21   │ OP_SUB              │ -         │ a b → a-b                   │
│ 0x22   │ OP_MUL              │ -         │ a b → a*b                   │
│ 0x23   │ OP_DIV              │ -         │ a b → a/b                   │
│ 0x24   │ OP_MOD              │ -         │ a b → a%b                   │
│ 0x25   │ OP_NEG              │ -         │ a → -a                      │
│ 0x26   │ OP_ADD_I32          │ -         │ a b → a+b (fast)            │
│ 0x27   │ OP_SUB_I32          │ -         │ a b → a-b (fast)            │
│ 0x28   │ OP_MUL_I32          │ -         │ a b → a*b (fast)            │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x30   │ OP_BIT_AND          │ -         │ a b → a&b                   │
│ 0x31   │ OP_BIT_OR           │ -         │ a b → a|b                   │
│ 0x32   │ OP_BIT_XOR          │ -         │ a b → a^b                   │
│ 0x33   │ OP_BIT_NOT          │ -         │ a → ~a                      │
│ 0x34   │ OP_SHL              │ -         │ a b → a<<b                  │
│ 0x35   │ OP_SHR              │ -         │ a b → a>>b                  │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x40   │ OP_EQ               │ -         │ a b → a==b                  │
│ 0x41   │ OP_NE               │ -         │ a b → a!=b                  │
│ 0x42   │ OP_LT               │ -         │ a b → a<b                   │
│ 0x43   │ OP_LE               │ -         │ a b → a<=b                  │
│ 0x44   │ OP_GT               │ -         │ a b → a>b                   │
│ 0x45   │ OP_GE               │ -         │ a b → a>=b                  │
│ 0x46   │ OP_NOT              │ -         │ a → !a                      │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x50   │ OP_GET_LOCAL        │ u8        │ → value                     │
│ 0x51   │ OP_SET_LOCAL        │ u8        │ a → a                       │
│ 0x52   │ OP_GET_UPVALUE      │ u8        │ → value                     │
│ 0x53   │ OP_SET_UPVALUE      │ u8        │ a → a                       │
│ 0x54   │ OP_GET_GLOBAL       │ u16       │ → value                     │
│ 0x55   │ OP_SET_GLOBAL       │ u16       │ a → a                       │
│ 0x56   │ OP_DEFINE_GLOBAL    │ u16       │ a → -                       │
│ 0x57   │ OP_CLOSE_UPVALUE    │ -         │ → -                         │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x60   │ OP_GET_PROPERTY     │ u16       │ obj → value                 │
│ 0x61   │ OP_SET_PROPERTY     │ u16       │ obj val → val               │
│ 0x62   │ OP_GET_INDEX        │ -         │ obj idx → val               │
│ 0x63   │ OP_SET_INDEX        │ -         │ obj idx v → v               │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x70   │ OP_JUMP             │ i16       │ → -                         │
│ 0x71   │ OP_JUMP_IF_FALSE    │ i16       │ cond → -                    │
│ 0x72   │ OP_JUMP_IF_TRUE     │ i16       │ cond → -                    │
│ 0x73   │ OP_LOOP             │ i16       │ → -                         │
│ 0x74   │ OP_POP_JUMP_FALSE   │ i16       │ cond → cond                 │
│ 0x75   │ OP_POP_JUMP_TRUE    │ i16       │ cond → cond                 │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x80   │ OP_CALL             │ u8        │ fn args → ret               │
│ 0x81   │ OP_CALL_METHOD      │ u8,u16    │ obj args → ret              │
│ 0x82   │ OP_RETURN           │ -         │ val → -                     │
│ 0x83   │ OP_RETURN_NULL      │ -         │ → -                         │
│ 0x84   │ OP_CLOSURE          │ u16       │ → closure                   │
│ 0x85   │ OP_TAIL_CALL        │ u8        │ fn args → ret               │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0x90   │ OP_ARRAY            │ u8        │ elems → arr                 │
│ 0x91   │ OP_OBJECT           │ u8        │ k v... → obj                │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0xA0   │ OP_TRY_BEGIN        │ i16       │ → -                         │
│ 0xA1   │ OP_TRY_END          │ -         │ → -                         │
│ 0xA2   │ OP_CATCH            │ u8        │ ex → -                      │
│ 0xA3   │ OP_THROW            │ -         │ val → ⊥                     │
│ 0xA4   │ OP_DEFER            │ u16       │ → -                         │
│ 0xA5   │ OP_RUN_DEFERS       │ -         │ → -                         │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0xB0   │ OP_SPAWN            │ u8        │ fn args → task              │
│ 0xB1   │ OP_AWAIT            │ -         │ task → result               │
│ 0xB2   │ OP_DETACH           │ -         │ task → -                    │
│ 0xB3   │ OP_CHANNEL          │ -         │ cap → ch                    │
│ 0xB4   │ OP_SEND             │ -         │ ch val → -                  │
│ 0xB5   │ OP_RECV             │ -         │ ch → val                    │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0xC0   │ OP_TYPEOF           │ -         │ val → str                   │
│ 0xC1   │ OP_CAST             │ u8        │ val → val'                  │
│ 0xC2   │ OP_ALLOC            │ -         │ size → ptr                  │
│ 0xC3   │ OP_FREE             │ -         │ ptr → -                     │
├────────┼─────────────────────┼───────────┼─────────────────────────────┤
│ 0xF0   │ OP_PRINT            │ -         │ val → -                     │
│ 0xFE   │ OP_WIDE             │ opcode    │ (extends next opcode)       │
│ 0xFF   │ OP_HALT             │ -         │ → -                         │
└────────┴─────────────────────┴───────────┴─────────────────────────────┘
```

---

## Appendix B: Example Bytecode

### Simple Arithmetic

```hemlock
let x = 2 + 3 * 4;
print(x);
```

```
0000  OP_CONST        0    ; 2
0002  OP_CONST        1    ; 3
0004  OP_CONST        2    ; 4
0006  OP_MUL               ; 3 * 4 = 12
0007  OP_ADD               ; 2 + 12 = 14
0008  OP_DEFINE_GLOBAL 0   ; x
0011  OP_GET_GLOBAL   0    ; x
0014  OP_PRINT             ; print(x)
0015  OP_HALT
```

### Function with Closure

```hemlock
fn counter(start) {
    let n = start;
    return fn() {
        n = n + 1;
        return n;
    };
}
let c = counter(0);
print(c());  // 1
print(c());  // 2
```

```
; Main script
0000  OP_CLOSURE      0    ; counter function
0003  OP_DEFINE_GLOBAL 0   ; counter
0006  OP_GET_GLOBAL   0    ; counter
0009  OP_ZERO              ; 0
0010  OP_CALL         1    ; counter(0)
0012  OP_DEFINE_GLOBAL 1   ; c
0015  OP_GET_GLOBAL   1    ; c
0018  OP_CALL         0    ; c()
0020  OP_PRINT             ; print result
0021  OP_GET_GLOBAL   1    ; c
0024  OP_CALL         0    ; c()
0026  OP_PRINT             ; print result
0027  OP_HALT

; counter function (chunk 1)
0000  OP_GET_LOCAL    1    ; start
0002  OP_CLOSURE      0    ; inner function
      01 01              ; capture local[1] as upvalue[0]
0007  OP_RETURN

; inner function (chunk 2)
0000  OP_GET_UPVALUE  0    ; n
0002  OP_ONE               ; 1
0003  OP_ADD               ; n + 1
0004  OP_SET_UPVALUE  0    ; n = n + 1
0006  OP_GET_UPVALUE  0    ; n
0008  OP_RETURN
```

### Async Task

```hemlock
async fn compute(n) {
    return n * n;
}

let task = spawn(compute, 5);
let result = await task;
print(result);  // 25
```

```
0000  OP_CLOSURE      0    ; compute (async)
0003  OP_DEFINE_GLOBAL 0   ; compute
0006  OP_GET_GLOBAL   0    ; compute
0009  OP_CONST        1    ; 5
0011  OP_SPAWN        1    ; spawn(compute, 5)
0013  OP_DEFINE_GLOBAL 1   ; task
0016  OP_GET_GLOBAL   1    ; task
0019  OP_AWAIT             ; await task
0020  OP_DEFINE_GLOBAL 2   ; result
0023  OP_GET_GLOBAL   2    ; result
0026  OP_PRINT             ; print(result)
0027  OP_HALT
```

---

## Appendix C: Comparison with Existing VMs

| Feature | Hemlock VM | Lua 5.x | CPython | V8 |
|---------|------------|---------|---------|-----|
| Architecture | Stack | Register | Stack | Stack+JIT |
| Value size | 64-bit tagged | 64-bit NaN-boxed | PyObject* | V8::Value |
| GC | Refcount | Mark-sweep | Refcount+GC | Generational |
| Closures | Upvalues | Upvalues | Cells | Hidden classes |
| Async | Native threads | Coroutines | asyncio | Event loop |
| JIT | Future | LuaJIT | PyPy | TurboFan |

### Design Influences

- **Lua**: Upvalue design, compact bytecode format
- **CPython**: Stack-based execution, exception handling model
- **Crafting Interpreters**: Overall VM structure and approach
- **JavaScriptCore**: Inline caching concepts (for future optimization)

---

## Appendix D: Glossary

| Term | Definition |
|------|------------|
| **Chunk** | Container for bytecode, constants, and debug info |
| **Opcode** | Single-byte instruction identifier |
| **Operand** | Additional bytes following opcode |
| **Stack slot** | Position on value stack |
| **Local** | Variable in current function's stack frame |
| **Upvalue** | Variable captured from enclosing scope |
| **Closure** | Function bundled with its upvalues |
| **CallFrame** | Per-function execution state |
| **IP** | Instruction pointer (current bytecode position) |
| **Dispatch** | Jump to handler for current opcode |
| **NaN-boxing** | Technique to pack values in 64-bit IEEE 754 NaN |

---

## Summary

This design provides a solid foundation for the Hemlock 2.0 bytecode VM:

1. **Stack-based architecture** - Simpler than register-based, good for closures
2. **Compact bytecode** - Variable-length encoding with common case optimization
3. **Efficient dispatch** - Computed goto with i32 fast paths
4. **Full closure support** - Upvalue capture with open/closed semantics
5. **Native async** - Real pthread-based parallelism with channels
6. **Reference counting** - Consistent with interpreter, thread-safe
7. **Incremental implementation** - Phased approach with parity testing

The VM maintains Hemlock's philosophy of **explicit over implicit** while providing significant performance improvements over tree-walking interpretation.
