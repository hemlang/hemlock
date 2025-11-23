# Hemlock Application Binary Interface (ABI) Specification

**Version:** 1.0
**Status:** Frozen (Do Not Modify)
**Date:** 2025-11-23
**Applies To:** Hemlock v0.2+ (compiled mode)

---

## Table of Contents

1. [Introduction](#introduction)
2. [ABI Version Policy](#abi-version-policy)
3. [Value Type Layout](#value-type-layout)
4. [Heap Type Layouts](#heap-type-layouts)
5. [Calling Conventions](#calling-conventions)
6. [Memory Management Contract](#memory-management-contract)
7. [Runtime Library API](#runtime-library-api)
8. [Type System Contract](#type-system-contract)
9. [Platform Requirements](#platform-requirements)
10. [Future Evolution](#future-evolution)

---

## Introduction

This document defines the **Application Binary Interface (ABI)** for Hemlock compiled code. The ABI is the contract between:
- Compiled Hemlock programs (generated C code)
- The Hemlock runtime library (`libhemlock_runtime.a/so`)
- User-provided native extensions

**Critical**: Once frozen, this ABI **cannot change** without incrementing the major version. Any compiled Hemlock program depends on these exact memory layouts and calling conventions.

**Purpose**: Enable separate compilation, binary distribution, and long-term compatibility.

---

## ABI Version Policy

### Version Numbering

ABI versions follow **semantic versioning**:
- **Major version (X.y.z)**: Breaking changes (incompatible)
- **Minor version (x.Y.z)**: Additions only (backward compatible)
- **Patch version (x.y.Z)**: Clarifications, documentation

**Current Version**: `1.0.0`

### Compatibility Guarantees

**Within the same major version:**
- ✅ Compiled code from v1.0 works with runtime v1.2
- ✅ Runtime v1.0 works with code compiled against v1.2
- ✅ New runtime functions can be added (minor bump)
- ✅ Existing functions can gain optional parameters (minor bump)

**Across major versions:**
- ❌ v1.x code may NOT work with v2.x runtime
- ❌ Must recompile code when runtime changes major version

### Version Checking

Every compiled Hemlock program MUST include:

```c
#define HEMLOCK_ABI_VERSION_MAJOR 1
#define HEMLOCK_ABI_VERSION_MINOR 0
#define HEMLOCK_ABI_VERSION_PATCH 0

int main(int argc, char **argv) {
    // First line of every compiled program
    hml_check_abi_version(
        HEMLOCK_ABI_VERSION_MAJOR,
        HEMLOCK_ABI_VERSION_MINOR,
        HEMLOCK_ABI_VERSION_PATCH
    );
    // ... rest of program
}
```

The runtime library exports:

```c
void hml_check_abi_version(int major, int minor, int patch);
```

This function:
- Compares against runtime's compiled ABI version
- Exits with error if major versions differ
- Warns if minor/patch versions differ significantly

---

## Value Type Layout

### Core Structure

The `Value` struct is the **most critical** part of the ABI. Its layout is **frozen forever** for ABI v1.0.

```c
typedef enum {
    VAL_I8 = 0,
    VAL_I16 = 1,
    VAL_I32 = 2,
    VAL_I64 = 3,
    VAL_U8 = 4,
    VAL_U16 = 5,
    VAL_U32 = 6,
    VAL_U64 = 7,
    VAL_F32 = 8,
    VAL_F64 = 9,
    VAL_BOOL = 10,
    VAL_STRING = 11,
    VAL_RUNE = 12,
    VAL_PTR = 13,
    VAL_BUFFER = 14,
    VAL_ARRAY = 15,
    VAL_OBJECT = 16,
    VAL_FILE = 17,
    VAL_SOCKET = 18,
    VAL_TYPE = 19,
    VAL_BUILTIN_FN = 20,
    VAL_FUNCTION = 21,
    VAL_FFI_FUNCTION = 22,
    VAL_TASK = 23,
    VAL_CHANNEL = 24,
    VAL_NULL = 25,
} ValueType;

typedef struct Value {
    ValueType type;  // 4 bytes (enum stored as int32_t)
    union {
        int8_t as_i8;           // 1 byte
        int16_t as_i16;         // 2 bytes
        int32_t as_i32;         // 4 bytes
        int64_t as_i64;         // 8 bytes
        uint8_t as_u8;          // 1 byte
        uint16_t as_u16;        // 2 bytes
        uint32_t as_u32;        // 4 bytes
        uint64_t as_u64;        // 8 bytes
        float as_f32;           // 4 bytes
        double as_f64;          // 8 bytes
        int as_bool;            // 4 bytes
        String *as_string;      // 8 bytes (pointer)
        uint32_t as_rune;       // 4 bytes
        void *as_ptr;           // 8 bytes (pointer)
        Buffer *as_buffer;      // 8 bytes (pointer)
        Array *as_array;        // 8 bytes (pointer)
        FileHandle *as_file;    // 8 bytes (pointer)
        SocketHandle *as_socket;// 8 bytes (pointer)
        Object *as_object;      // 8 bytes (pointer)
        TypeKind as_type;       // 4 bytes (enum)
        BuiltinFn as_builtin_fn;// 8 bytes (function pointer)
        Function *as_function;  // 8 bytes (pointer)
        void *as_ffi_function;  // 8 bytes (pointer)
        Task *as_task;          // 8 bytes (pointer)
        Channel *as_channel;    // 8 bytes (pointer)
    } as;                       // 8 bytes (largest member)
} Value;
```

### Memory Layout Specification

**Total Size**: 16 bytes (on 64-bit systems)

**Layout**:
```
Offset  | Size | Field
--------|------|-------
0       | 4    | type (ValueType)
4       | 4    | (padding for alignment)
8       | 8    | as (union)
```

**Alignment**: 8 bytes (must be aligned on 8-byte boundaries)

**Guarantees**:
1. `sizeof(Value)` = 16 bytes (on 64-bit systems)
2. `offsetof(Value, type)` = 0
3. `offsetof(Value, as)` = 8
4. Union is 8 bytes (size of largest member: pointers, i64, u64, f64)

**Endianness**: Host byte order (typically little-endian on x86-64)

**ABI Stability Rules**:
- ❌ NEVER change the order of fields
- ❌ NEVER add fields to this struct
- ❌ NEVER change the size of the union
- ❌ NEVER change enum values (ValueType)
- ✅ MAY add new enum values at the end (minor version bump)

### ValueType Enum Values

**Frozen Values** (v1.0):
```c
VAL_I8           = 0
VAL_I16          = 1
VAL_I32          = 2
VAL_I64          = 3
VAL_U8           = 4
VAL_U16          = 5
VAL_U32          = 6
VAL_U64          = 7
VAL_F32          = 8
VAL_F64          = 9
VAL_BOOL         = 10
VAL_STRING       = 11
VAL_RUNE         = 12
VAL_PTR          = 13
VAL_BUFFER       = 14
VAL_ARRAY        = 15
VAL_OBJECT       = 16
VAL_FILE         = 17
VAL_SOCKET       = 18
VAL_TYPE         = 19
VAL_BUILTIN_FN   = 20
VAL_FUNCTION     = 21
VAL_FFI_FUNCTION = 22
VAL_TASK         = 23
VAL_CHANNEL      = 24
VAL_NULL         = 25
```

**Reserved Range**: 0-99 (for ABI v1.x additions)

**New types** in future minor versions:
- Must use values 26-99
- Must not break existing code
- Must be documented in ABI changelog

---

## Heap Type Layouts

Heap types are pointed to by the `Value.as` union. These layouts are also **frozen** for ABI v1.0.

### String Layout

```c
typedef struct {
    char *data;          // UTF-8 encoded bytes (null-terminated)
    int length;          // Length in bytes
    int char_length;     // Length in Unicode codepoints (-1 if uncached)
    int capacity;        // Allocated capacity in bytes
    int ref_count;       // Reference count (0 = owned by one reference)
} String;
```

**Size**: 32 bytes (on 64-bit systems)

**Guarantees**:
- `data` is always null-terminated (includes extra byte in capacity)
- `char_length` is lazily computed (may be -1)
- `ref_count` starts at 0 (first owner doesn't increment)

### Buffer Layout

```c
typedef struct {
    void *data;          // Raw memory
    int length;          // Current length in bytes
    int capacity;        // Allocated capacity in bytes
    int ref_count;       // Reference count
} Buffer;
```

**Size**: 24 bytes (on 64-bit systems)

**Guarantees**:
- `data` may contain any bytes (not necessarily printable)
- Bounds checking is performed by runtime library

### Array Layout

```c
typedef struct {
    Value *elements;     // Array of Value structs
    int length;          // Current number of elements
    int capacity;        // Allocated capacity (in elements, not bytes)
    int ref_count;       // Reference count
    Type *element_type;  // Type constraint (NULL = untyped)
} Array;
```

**Size**: 32 bytes (on 64-bit systems)

**Guarantees**:
- `elements` is an array of `Value` structs (not pointers)
- `element_type` is optional (NULL for untyped arrays)

### Object Layout

```c
typedef struct {
    char *type_name;     // Type name (NULL for anonymous)
    char **field_names;  // Array of field name strings
    Value *field_values; // Array of field values
    int num_fields;      // Current number of fields
    int capacity;        // Allocated capacity (parallel arrays)
    int ref_count;       // Reference count
} Object;
```

**Size**: 40 bytes (on 64-bit systems)

**Guarantees**:
- `field_names` and `field_values` are parallel arrays
- Fields are stored in insertion order
- Dynamic field addition is supported

### Function Layout

```c
typedef struct {
    int is_async;                // 1 if async, 0 if sync
    char **param_names;          // Parameter names
    Type **param_types;          // Parameter types (NULL = no annotation)
    Expr **param_defaults;       // Default values (NULL = required)
    int num_params;              // Number of parameters
    Type *return_type;           // Return type (NULL = no annotation)
    Stmt *body;                  // AST of function body
    Environment *closure_env;    // Captured environment
    int ref_count;               // Reference count
} Function;
```

**Size**: 72 bytes (on 64-bit systems)

**Guarantees**:
- `closure_env` may be NULL (for non-closures)
- `body` is an AST node (not compiled in current implementation)

### Task Layout

```c
typedef struct Task {
    int id;                      // Unique task ID (atomic counter)
    TaskState state;             // READY, RUNNING, BLOCKED, COMPLETED
    Function *function;          // Async function to execute
    Value *args;                 // Argument array
    int num_args;                // Number of arguments
    Value *result;               // Return value (NULL until completed)
    int joined;                  // 1 if task has been joined
    Environment *env;            // Task's environment
    ExecutionContext *ctx;       // Task's execution context
    struct Task *waiting_on;     // Task being waited on (for join)
    void *thread;                // pthread_t (opaque)
    int detached;                // 1 if detached
    void *task_mutex;            // pthread_mutex_t (opaque)
    int ref_count;               // Reference count (atomic)
} Task;
```

**Size**: 112 bytes (on 64-bit systems)

**Guarantees**:
- Task IDs are unique (atomic counter)
- Thread and mutex pointers are opaque (platform-specific)

### Channel Layout

```c
typedef struct {
    Value *buffer;               // Ring buffer of Value structs
    int capacity;                // Buffer capacity (0 = unbuffered)
    int head;                    // Read position
    int tail;                    // Write position
    int count;                   // Number of messages in buffer
    int closed;                  // 1 if closed
    void *mutex;                 // pthread_mutex_t (opaque)
    void *not_empty;             // pthread_cond_t (opaque)
    void *not_full;              // pthread_cond_t (opaque)
    int ref_count;               // Reference count
} Channel;
```

**Size**: 72 bytes (on 64-bit systems)

**Guarantees**:
- Ring buffer wraps around (circular)
- Thread-safe with mutex and condition variables

---

## Calling Conventions

### Function Signatures

All compiled Hemlock functions use the **dynamic calling convention**:

```c
typedef Value (*HemlockFn)(Value *args, int num_args);
```

**Parameters**:
- `args`: Pointer to array of `Value` structs (NOT pointers to Value)
- `num_args`: Number of arguments in the array

**Return Value**:
- Always returns a `Value` struct (by value, not pointer)
- For `void` functions, returns `val_null()`

### Calling Convention Rules

1. **Arguments are passed by copying the Value struct** (16 bytes each)
2. **Return value is passed by copying the Value struct** (16 bytes)
3. **Caller is responsible for retaining arguments** (if needed beyond call)
4. **Callee is responsible for releasing arguments** (if it retains them)
5. **Return value is owned by caller** (ref_count includes this reference)

### Example Call Sequence

**Compiled Hemlock code**:
```hemlock
fn add(a: i32, b: i32): i32 {
    return a + b;
}

let result = add(10, 20);
```

**Generated C code**:
```c
// Function definition
Value hml_fn_add(Value *args, int num_args) {
    // Type checks
    hml_check_type(args[0], VAL_I32, "a");
    hml_check_type(args[1], VAL_I32, "b");

    // Extract values
    int32_t a = args[0].as.as_i32;
    int32_t b = args[1].as.as_i32;

    // Compute
    int32_t sum = a + b;

    // Return
    return hml_val_i32(sum);
}

// Call site
Value call_args[2];
call_args[0] = hml_val_i32(10);
call_args[1] = hml_val_i32(20);

Value result = hml_fn_add(call_args, 2);
// result now owns the return value
```

### Variadic Functions

Hemlock does NOT support true variadic functions. All functions have a fixed signature:

```c
Value hml_fn_foo(Value *args, int num_args);
```

The runtime library provides helpers for argument validation:

```c
void hml_check_argc(int num_args, int expected, const char *fn_name);
void hml_check_argc_range(int num_args, int min, int max, const char *fn_name);
```

---

## Memory Management Contract

### Reference Counting Rules

**Ownership Model**:
1. **Creation**: New values have `ref_count = 0` (creator owns the first reference)
2. **Retention**: `value_retain(val)` increments `ref_count`
3. **Release**: `value_release(val)` decrements `ref_count`, frees at 0
4. **Pass-by-value**: Primitives (i8-i64, u8-u64, f32, f64, bool, rune, null) are copied (no refcount)
5. **Pass-by-reference**: Heap types (string, array, object, etc.) are retained when stored

### Refcounting Contract

**For primitives** (i8-i64, u8-u64, f32, f64, bool, rune, null):
- NO reference counting (values are copied)
- `value_retain()` and `value_release()` are no-ops

**For heap types** (string, array, object, buffer, function, task, channel):
- Reference counting is MANDATORY
- `value_retain()` increments `ref_count` atomically (for thread-safe types)
- `value_release()` decrements `ref_count`, frees when reaching 0

### Ownership Transfer

**Rule 1: Return values transfer ownership**
```c
Value create_string() {
    String *s = string_new("hello");  // ref_count = 0
    return val_string(s);  // Caller owns this reference
}

// Caller:
Value s = create_string();  // Owns the string
value_release(s);  // Must release when done
```

**Rule 2: Storing in environments retains**
```c
Value s = create_string();  // ref_count = 0
env_define(env, "name", s);  // ref_count = 1 (env owns it)
value_release(s);  // Release our reference (ref_count = 0)
// String is now owned by environment only
```

**Rule 3: Assigning to arrays/objects retains**
```c
Value arr = hml_val_array();
Value s = create_string();  // ref_count = 0
hml_array_push(arr, s);  // ref_count = 1 (array owns it)
value_release(s);  // Release our reference
```

### Memory Leaks

**Circular references are NOT automatically detected**. User code must break cycles manually:

```c
// LEAK: Circular reference
Value obj1 = hml_val_object();
Value obj2 = hml_val_object();
hml_object_set_field(obj1, "other", obj2);
hml_object_set_field(obj2, "other", obj1);
// obj1 and obj2 will never be freed!

// FIX: Break cycle before release
hml_object_set_field(obj1, "other", hml_val_null());
value_release(obj1);
value_release(obj2);
```

**Environments provide cycle breaking**:
```c
env_break_cycles(env);  // Breaks cycles in closure functions
env_release(env);  // Now safe to release
```

---

## Runtime Library API

### Core Value API

**Constructors** (create values with ref_count = 0):
```c
Value hml_val_i8(int8_t val);
Value hml_val_i16(int16_t val);
Value hml_val_i32(int32_t val);
Value hml_val_i64(int64_t val);
Value hml_val_u8(uint8_t val);
Value hml_val_u16(uint16_t val);
Value hml_val_u32(uint32_t val);
Value hml_val_u64(uint64_t val);
Value hml_val_f32(float val);
Value hml_val_f64(double val);
Value hml_val_bool(int val);
Value hml_val_string(const char *str);  // Copies string
Value hml_val_rune(uint32_t codepoint);
Value hml_val_ptr(void *ptr);
Value hml_val_null(void);

Value hml_val_array(void);
Value hml_val_object(void);
Value hml_val_buffer(int size);
```

**Reference Counting**:
```c
void hml_retain(Value val);   // Increment ref_count (no-op for primitives)
void hml_release(Value val);  // Decrement ref_count, free at 0
```

**Type Checking**:
```c
int hml_is_i32(Value val);
int hml_is_string(Value val);
int hml_is_numeric(Value val);
ValueType hml_typeof(Value val);
const char *hml_typeof_str(Value val);
```

**Type Conversion**:
```c
Value hml_convert_to_type(Value val, Type *target_type);
Value hml_promote_types(Value left, Value right, ValueType *result_type);
int32_t hml_value_to_int(Value val);
double hml_value_to_double(Value val);
int hml_value_to_bool(Value val);
```

### Operators

**Binary operators**:
```c
Value hml_binary_op(int op, Value left, Value right);
// op: OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_LT, OP_GT, etc.
```

**Unary operators**:
```c
Value hml_unary_op(int op, Value operand);
// op: OP_NEGATE, OP_NOT, OP_BITWISE_NOT
```

### String Operations

```c
Value hml_string_concat(Value s1, Value s2);
Value hml_string_substr(Value str, int start, int length);
Value hml_string_slice(Value str, int start, int end);
int hml_string_find(Value str, Value needle);
int hml_string_contains(Value str, Value needle);
Value hml_string_split(Value str, Value delimiter);
Value hml_string_trim(Value str);
Value hml_string_to_upper(Value str);
Value hml_string_to_lower(Value str);
int hml_string_starts_with(Value str, Value prefix);
int hml_string_ends_with(Value str, Value suffix);
Value hml_string_replace(Value str, Value old, Value new);
Value hml_string_replace_all(Value str, Value old, Value new);
Value hml_string_repeat(Value str, int count);
Value hml_string_char_at(Value str, int index);
Value hml_string_byte_at(Value str, int index);
Value hml_string_chars(Value str);
Value hml_string_bytes(Value str);
```

### Array Operations

```c
void hml_array_push(Value arr, Value val);
Value hml_array_pop(Value arr);
Value hml_array_shift(Value arr);
void hml_array_unshift(Value arr, Value val);
void hml_array_insert(Value arr, int index, Value val);
Value hml_array_remove(Value arr, int index);
int hml_array_find(Value arr, Value val);
int hml_array_contains(Value arr, Value val);
Value hml_array_slice(Value arr, int start, int end);
Value hml_array_join(Value arr, Value delimiter);
Value hml_array_concat(Value arr1, Value arr2);
void hml_array_reverse(Value arr);
Value hml_array_first(Value arr);
Value hml_array_last(Value arr);
void hml_array_clear(Value arr);
Value hml_array_map(Value arr, Value callback);
Value hml_array_filter(Value arr, Value predicate);
Value hml_array_reduce(Value arr, Value reducer, Value initial);
```

### Object Operations

```c
void hml_object_set_field(Value obj, const char *name, Value val);
Value hml_object_get_field(Value obj, const char *name);
int hml_object_has_field(Value obj, const char *name);
Value hml_object_serialize(Value obj);  // JSON serialization
```

### Memory Management

```c
Value hml_alloc(int32_t size);
void hml_free(Value ptr_or_buffer);
Value hml_talloc(Type *type, int32_t count);
Value hml_realloc(Value ptr, int32_t new_size);
void hml_memset(Value ptr, uint8_t byte, int32_t size);
void hml_memcpy(Value dest, Value src, int32_t size);
int32_t hml_sizeof(Type *type);
Value hml_buffer(int32_t size);
```

### I/O Operations

```c
void hml_print(Value val);
void hml_eprint(Value val);
Value hml_read_line(void);
Value hml_open(Value path, Value mode);
Value hml_exec(Value command);
```

### Async Operations

```c
Value hml_spawn(Value async_fn, Value *args, int num_args);
Value hml_join(Value task);
void hml_detach(Value task);
Value hml_channel(int32_t capacity);
void hml_channel_send(Value channel, Value val);
Value hml_channel_recv(Value channel);
void hml_channel_close(Value channel);
```

### Exception Handling

```c
typedef struct {
    jmp_buf exception_buf;
    Value exception_value;
    int is_active;
} ExceptionContext;

ExceptionContext *hml_exception_push(void);
void hml_exception_pop(void);
void hml_throw(Value exception_value);
Value hml_exception_get_value(void);
```

### Defer Stack

```c
typedef void (*DeferFn)(void *arg);
void hml_defer_push(DeferFn fn, void *arg);
void hml_defer_pop_and_execute(void);
void hml_defer_execute_all(void);
```

---

## Type System Contract

### Type Annotations

Hemlock's type system is **dynamic with optional annotations**. Type checking happens at runtime.

**Type checking API**:
```c
void hml_check_type(Value val, ValueType expected, const char *name);
void hml_check_object_type(Value obj, const char *type_name);
```

**Type promotion**:
```c
// Promotion rules (smaller → larger):
// i8 → i16 → i32 → i64
// u8 → u16 → u32 → u64
// Any integer → f32 → f64
Value hml_promote_value(Value val, ValueType target);
```

### Duck Typing

Objects are validated structurally:

```c
// Define type
define Person {
    name: string,
    age: i32,
}

// Duck typing validation
let obj = { name: "Alice", age: 30 };
let p: Person = obj;  // Validates at assignment time
```

**Runtime API**:
```c
void hml_check_object_type(Value obj, const char *type_name);
// Validates:
// - Object has all required fields
// - Field types match
// - Sets obj->type_name for typeof()
```

---

## Platform Requirements

### Supported Platforms

**Operating Systems**:
- Linux (x86-64, ARM64)
- macOS (x86-64, ARM64)
- BSD variants (FreeBSD, OpenBSD)

**Architectures**:
- x86-64 (64-bit Intel/AMD)
- ARM64 (64-bit ARM)

**Compilers**:
- GCC 7.0+
- Clang 10.0+
- Compatible C11 compiler with POSIX support

### Platform Dependencies

**Required Standards**:
- C11 (`-std=c11`)
- POSIX.1-2008 (`-D_POSIX_C_SOURCE=200809L`)

**Required Libraries**:
- libm (math library)
- libpthread (POSIX threads)
- libffi (foreign function interface)
- libdl (dynamic linking)

**Endianness**:
- Little-endian (x86-64, ARM64)
- Big-endian NOT supported (would require serialization changes)

### Pointer Size Requirements

The ABI assumes **64-bit pointers**:
- `sizeof(void*) == 8`
- `sizeof(size_t) == 8`
- `sizeof(intptr_t) == 8`

**32-bit systems are NOT supported** in ABI v1.0.

### Alignment Requirements

**Value struct**: 8-byte alignment
**Heap types**: Natural alignment for each field
**Arrays**: 16-byte alignment (for Value elements)

---

## Future Evolution

### ABI v1.x (Minor Versions)

**Allowed changes**:
- ✅ Add new ValueType enum values (26-99)
- ✅ Add new runtime functions
- ✅ Add optional parameters to existing functions
- ✅ Add new heap types (with new ValueType)
- ✅ Optimize existing functions (if ABI unchanged)

**Forbidden changes**:
- ❌ Change Value struct layout
- ❌ Change existing ValueType enum values
- ❌ Remove or rename runtime functions
- ❌ Change function signatures (parameters, return type)
- ❌ Change heap type layouts

### ABI v2.0 (Breaking Changes)

If we need breaking changes in the future, examples might include:
- Changing Value struct size (e.g., adding metadata field)
- Changing calling convention (e.g., type-specialized functions)
- Changing reference counting model (e.g., GC integration)
- Changing pointer size (e.g., 32-bit support)

**Process for breaking changes**:
1. Document proposed changes
2. Implement in separate branch
3. Increment ABI major version
4. Require recompilation of all code
5. Maintain both v1.x and v2.x runtimes during transition

### Deprecation Policy

Functions can be deprecated but not removed within a major version:

```c
// Deprecated in v1.5, removed in v2.0
__attribute__((deprecated("Use hml_foo_v2 instead")))
Value hml_foo(Value arg);
```

**Deprecation process**:
1. Mark as deprecated in minor version (e.g., v1.5)
2. Document replacement in changelog
3. Maintain for at least 2 minor versions
4. Remove in next major version (e.g., v2.0)

---

## Changelog

### v1.0.0 (2025-11-23)

**Initial ABI specification**:
- Defined Value struct layout (16 bytes)
- Defined all 26 ValueType enum values
- Defined heap type layouts (String, Buffer, Array, Object, Function, Task, Channel)
- Defined dynamic calling convention
- Defined reference counting contract
- Defined runtime library API (100+ functions)
- Defined platform requirements (64-bit, C11, POSIX)

**Status**: Frozen for Hemlock v0.2+

---

## Appendix A: ABI Verification

### Compile-Time Checks

The runtime library includes compile-time assertions to verify ABI assumptions:

```c
// In hemlock_runtime.c
_Static_assert(sizeof(Value) == 16, "Value must be 16 bytes");
_Static_assert(sizeof(void*) == 8, "Must be 64-bit platform");
_Static_assert(offsetof(Value, type) == 0, "type must be at offset 0");
_Static_assert(offsetof(Value, as) == 8, "as must be at offset 8");
```

### Runtime Checks

```c
void hml_check_abi_version(int major, int minor, int patch) {
    if (major != HEMLOCK_ABI_VERSION_MAJOR) {
        fprintf(stderr,
            "Fatal: ABI version mismatch!\n"
            "Compiled with ABI v%d.%d.%d\n"
            "Runtime is ABI v%d.%d.%d\n"
            "Recompile your code.\n",
            major, minor, patch,
            HEMLOCK_ABI_VERSION_MAJOR,
            HEMLOCK_ABI_VERSION_MINOR,
            HEMLOCK_ABI_VERSION_PATCH
        );
        exit(1);
    }

    if (minor > HEMLOCK_ABI_VERSION_MINOR) {
        fprintf(stderr,
            "Warning: Code uses newer ABI features (v%d.%d.%d) "
            "than runtime (v%d.%d.%d)\n",
            major, minor, patch,
            HEMLOCK_ABI_VERSION_MAJOR,
            HEMLOCK_ABI_VERSION_MINOR,
            HEMLOCK_ABI_VERSION_PATCH
        );
    }
}
```

---

## Appendix B: Symbol Naming Convention

### Runtime Library Symbols

All runtime library symbols use the `hml_` prefix:

**Value constructors**: `hml_val_*`
```c
hml_val_i32()
hml_val_string()
hml_val_array()
```

**Operations**: `hml_<category>_<operation>`
```c
hml_string_concat()
hml_array_push()
hml_object_set_field()
```

**Memory management**: `hml_<operation>`
```c
hml_alloc()
hml_free()
hml_retain()
hml_release()
```

**Type system**: `hml_<type_operation>`
```c
hml_check_type()
hml_convert_to_type()
hml_typeof()
```

### User Function Symbols

Compiled Hemlock functions use the `hml_fn_` prefix:

**Simple functions**: `hml_fn_<name>`
```hemlock
fn calculate_total() -> hml_fn_calculate_total()
```

**Module-qualified**: `hml_fn_<module>_<name>`
```hemlock
// In module "utils"
fn helper() -> hml_fn_utils_helper()
```

**Nested modules**: `hml_fn_<path>_<name>`
```hemlock
// In module "app/services"
fn process() -> hml_fn_app_services_process()
```

**Anonymous functions**: `hml_fn_<parent>_anon_<index>`
```hemlock
fn foo() {
    let f = fn(x) { return x + 1; };
    // -> hml_fn_foo_anon_0()
}
```

### Name Mangling Rules

**Identifiers**:
- Lowercase ASCII: `a-z` → as-is
- Uppercase ASCII: `A-Z` → as-is
- Digits: `0-9` → as-is
- Underscore: `_` → as-is
- Other characters: `%XX` (hex encoding)

**Examples**:
```
calculate_total    -> hml_fn_calculate_total
get-value          -> hml_fn_get%2Dvalue
hello_世界         -> hml_fn_hello_%E4%B8%96%E7%95%8C
```

**Collision prevention**:
- User functions: `hml_fn_*`
- Runtime functions: `hml_*` (no `fn`)
- Internal helpers: `_hml_*` (private)

---

**End of ABI Specification v1.0.0**
