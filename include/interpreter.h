#ifndef HEMLOCK_INTERPRETER_H
#define HEMLOCK_INTERPRETER_H

#include "ast.h"
#include <stdint.h>
#include <stdio.h>

// Value types that can exist at runtime
typedef enum {
    VAL_I8,
    VAL_I16,
    VAL_I32,
    VAL_I64,
    VAL_U8,
    VAL_U16,
    VAL_U32,
    VAL_U64,
    //VAL_F16,
    VAL_F32,
    VAL_F64,
    VAL_BOOL,
    VAL_STRING,
    VAL_RUNE,           // Unicode codepoint (U+0000 to U+10FFFF)
    VAL_PTR,
    VAL_BUFFER,
    VAL_ARRAY,          // Dynamic array
    VAL_OBJECT,         // JavaScript-style object
    VAL_FILE,           // File handle
    VAL_TYPE,           // Represents a type (for sizeof, talloc, etc.)
    VAL_BUILTIN_FN,
    VAL_FUNCTION,       // User-defined function
    VAL_FFI_FUNCTION,   // FFI function
    VAL_TASK,           // Async task handle
    VAL_CHANNEL,        // Communication channel
    VAL_NULL,
} ValueType;

typedef struct Value Value;
typedef struct ExecutionContext ExecutionContext;
typedef struct Environment Environment;
typedef Value (*BuiltinFn)(Value *args, int num_args, ExecutionContext *ctx);

// String struct
typedef struct {
    char *data;          // UTF-8 encoded bytes
    int length;          // Length in bytes (for backward compatibility, renamed to byte_length conceptually)
    int char_length;     // Length in Unicode codepoints (cached, -1 if unknown)
    int capacity;        // Allocated capacity in bytes
    int ref_count;       // Reference count for memory management
} String;

// Buffer struct (safe pointer wrapper)
typedef struct {
    void *data;
    int length;
    int capacity;
    int ref_count;       // Reference count for memory management
} Buffer;

// Array struct (dynamic array)
typedef struct {
    Value *elements;
    int length;
    int capacity;
    int ref_count;       // Reference count for memory management
} Array;

// File handle struct
typedef struct {
    FILE *fp;           // C file pointer
    char *path;         // File path (for error messages)
    char *mode;         // Mode string ("r", "w", etc.)
    int closed;         // 1 if closed, 0 if open
} FileHandle;

// Object struct (JavaScript-style object)
typedef struct {
    char *type_name;  // NULL for anonymous
    char **field_names;
    Value *field_values;
    int num_fields;
    int capacity;
    int ref_count;       // Reference count for memory management
} Object;

// Function struct (user-defined function)
typedef struct {
    int is_async;
    char **param_names;
    Type **param_types;
    int num_params;
    Type *return_type;
    Stmt *body;
    Environment *closure_env;  // CAPTURED ENVIRONMENT
} Function;

// Task states
typedef enum {
    TASK_READY,      // Ready to run
    TASK_RUNNING,    // Currently executing
    TASK_BLOCKED,    // Waiting on channel or join
    TASK_COMPLETED,  // Finished execution
} TaskState;

// Task struct (async task handle)
typedef struct Task {
    int id;                     // Unique task ID
    TaskState state;            // Current state
    Function *function;         // Async function to execute
    Value *args;                // Arguments to pass
    int num_args;               // Number of arguments
    Value *result;              // Return value when completed (NULL if not completed)
    int joined;                 // Flag: task has been joined
    Environment *env;           // Task's environment
    ExecutionContext *ctx;      // Task's execution context
    struct Task *waiting_on;    // Task we're blocked on (for join)
    void *thread;               // pthread_t (opaque pointer)
    int detached;               // Flag: task is detached (fire-and-forget)
    void *task_mutex;           // pthread_mutex_t for thread-safe state access
    int ref_count;              // Reference count for memory management (atomic)
} Task;

// Channel struct (communication channel)
typedef struct {
    Value *buffer;              // Ring buffer for messages
    int capacity;               // Buffer capacity (0 for unbuffered)
    int head;                   // Read position
    int tail;                   // Write position
    int count;                  // Number of messages in buffer
    int closed;                 // Flag: channel is closed
    void *mutex;                // pthread_mutex_t (opaque pointer)
    void *not_empty;            // pthread_cond_t (opaque pointer)
    void *not_full;             // pthread_cond_t (opaque pointer)
} Channel;

// Forward declare TypeKind from ast.h
#include "ast.h"

// Runtime value
typedef struct Value {
    ValueType type;
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
        uint32_t as_rune;   // Unicode codepoint (0x0 to 0x10FFFF)
        void *as_ptr;
        Buffer *as_buffer;
        Array *as_array;
        FileHandle *as_file;
        Object *as_object;
        TypeKind as_type;
        BuiltinFn as_builtin_fn;
        Function *as_function;
        void *as_ffi_function;  // FFIFunction* (opaque)
        Task *as_task;
        Channel *as_channel;
    } as;
} Value;

// Environment (symbol table for variables)
typedef struct Environment {
    char **names;
    Value *values;
    int *is_const;  // 1 if const, 0 if mutable (let)
    int count;
    int capacity;
    int ref_count;  // Reference count for memory management
    struct Environment *parent;  // for nested scopes later
} Environment;

// Public interface
Environment* env_new(Environment *parent);
void env_free(Environment *env);
void env_retain(Environment *env);
void env_release(Environment *env);
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx);
void env_set(Environment *env, const char *name, Value value, ExecutionContext *ctx);
Value env_get(Environment *env, const char *name, ExecutionContext *ctx);

// Execution context management (opaque pointer pattern)
ExecutionContext* exec_context_new(void);
void exec_context_free(ExecutionContext *ctx);

Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
void eval_stmt(Stmt *stmt, Environment *env, ExecutionContext *ctx);
void eval_program(Stmt **stmts, int count, Environment *env, ExecutionContext *ctx);

// Value constructors
Value val_int(int value);    // creates i32
Value val_float(double value); // creates f64
Value val_i8(int8_t value);
Value val_i16(int16_t value);
Value val_i32(int32_t value);
Value val_i64(int64_t value);
Value val_u8(uint8_t value);
Value val_u16(uint16_t value);
Value val_u32(uint32_t value);
Value val_u64(uint64_t value);
Value val_f32(float value);
Value val_f64(double value);
Value val_bool(int value);
Value val_string(const char *str);
Value val_string_take(char *str, int length, int capacity);
Value val_rune(uint32_t codepoint);
Value val_ptr(void *ptr);
Value val_buffer(int size);
Value val_array(Array *arr);
Value val_file(FileHandle *file);
Value val_type(TypeKind kind);
Value val_builtin_fn(BuiltinFn fn);
Value val_function(Function *fn);
Value val_object(Object *obj);
Value val_task(Task *task);
Value val_channel(Channel *channel);
Value val_null(void);

// Value operations
void print_value(Value val);

// String operations
void string_free(String *str);
String* string_concat(String *a, String *b);
String* string_copy(String *str);

// Buffer operations
void buffer_free(Buffer *buf);

// Array operations
void array_free(Array *arr);
Array* array_new(void);
void array_push(Array *arr, Value val);
Value array_pop(Array *arr);
Value array_get(Array *arr, int index);
void array_set(Array *arr, int index, Value val);

// File operations
void file_free(FileHandle *file);

// Object operations
void object_free(Object *obj);
Object* object_new(char *type_name, int initial_capacity);

// Task operations
void task_free(Task *task);
Task* task_new(int id, Function *function, Value *args, int num_args, Environment *env);
void task_retain(Task *task);
void task_release(Task *task);

// Channel operations
void channel_free(Channel *channel);
Channel* channel_new(int capacity);

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx);

#endif // HEMLOCK_INTERPRETER_H