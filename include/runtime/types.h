#ifndef HEMLOCK_RUNTIME_TYPES_H
#define HEMLOCK_RUNTIME_TYPES_H

#include "frontend/ast.h"
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>

// Forward declarations
typedef struct Value Value;
typedef struct ExecutionContext ExecutionContext;
typedef struct Environment Environment;

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
    VAL_SOCKET,         // Socket handle
    VAL_WEBSOCKET,      // WebSocket handle (client or server connection)
    VAL_TYPE,           // Represents a type (for sizeof, talloc, etc.)
    VAL_BUILTIN_FN,
    VAL_FUNCTION,       // User-defined function
    VAL_FFI_FUNCTION,   // FFI function
    VAL_TASK,           // Async task handle
    VAL_CHANNEL,        // Communication channel
    VAL_REF,            // Reference (for pass-by-reference parameters)
    VAL_NULL,
} ValueType;

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
    _Atomic int freed;   // Atomic flag: 1 if freed via free(), 0 otherwise
} Buffer;

// Array struct (dynamic array)
typedef struct {
    Value *elements;
    int length;
    int capacity;
    int ref_count;       // Reference count for memory management
    Type *element_type;  // Optional: type constraint for array elements (NULL = untyped)
    _Atomic int freed;   // Atomic flag: 1 if freed via free(), 0 otherwise
} Array;

// File handle struct
typedef struct {
    FILE *fp;           // C file pointer
    char *path;         // File path (for error messages)
    char *mode;         // Mode string ("r", "w", etc.)
    int closed;         // 1 if closed, 0 if open
} FileHandle;

// Socket handle struct
typedef struct {
    int fd;              // File descriptor
    char *address;       // Bound/connected address (nullable)
    int port;            // Port number
    int domain;          // AF_INET, AF_INET6
    int type;            // SOCK_STREAM, SOCK_DGRAM
    int closed;          // Whether socket is closed
    int listening;       // Whether listening (server socket)
    int nonblocking;     // Whether socket is in non-blocking mode
} SocketHandle;

// WebSocket handle struct (wraps libwebsockets connection/server)
typedef struct {
    void *handle;        // Opaque pointer to ws_connection_t or ws_server_t
    char *url;           // URL for clients, NULL for server connections
    char *host;          // Host address for servers
    int port;            // Port for servers
    int closed;          // Whether connection is closed
    int is_server;       // 1 if this is a server, 0 if client
    int ref_count;       // Reference count for memory management
} WebSocketHandle;

// Forward declaration for FieldEntry (full definition after Value)
typedef struct FieldEntry FieldEntry;

// Object struct (JavaScript-style object)
typedef struct {
    char *type_name;     // NULL for anonymous
    FieldEntry *fields;  // Unified array of field entries (reduces fragmentation)
    int num_fields;
    int capacity;
    int ref_count;       // Reference count for memory management
    _Atomic int freed;   // Atomic flag: 1 if freed via free(), 0 otherwise
    // Hash table for O(1) field lookup (linear probing)
    int *hash_table;     // Array of field indices, -1 = empty slot
    int hash_capacity;   // Size of hash table (usually 2x num_fields)
} Object;

// Function struct (user-defined function)
typedef struct {
    int is_async;
    char **param_names;
    Type **param_types;
    Expr **param_defaults;  // Default value expressions (NULL for required params)
    int *param_is_ref;      // 1 if parameter is pass-by-reference (ref keyword)
    uint32_t *param_hashes; // Pre-computed hashes of param names (optimization)
    int num_params;
    char *rest_param;       // Name of rest parameter (...args), NULL if none
    Type *rest_param_type;  // Type of rest parameter, NULL if none
    Type *return_type;
    Stmt *body;
    Environment *closure_env;  // CAPTURED ENVIRONMENT
    int ref_count;             // Reference count for memory management
    int is_bound;              // If true, this is a bound method (don't free param arrays)
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
    char *name;                 // Optional debug name (from spawn_with)
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
    int ref_count;              // Reference count for memory management
    // Unbuffered channel support (rendezvous)
    Value *unbuffered_value;    // Pointer to value being transferred in rendezvous
    int sender_waiting;         // Flag: sender is blocked waiting for receiver
    int receiver_waiting;       // Flag: receiver is blocked waiting for sender
    void *rendezvous;           // pthread_cond_t for rendezvous completion
} Channel;

// Reference types for pass-by-reference
typedef enum {
    REF_VARIABLE,       // Reference to a variable in an environment
    REF_ARRAY_INDEX,    // Reference to an array element
    REF_OBJECT_PROPERTY // Reference to an object property
} RefType;

// Reference struct (for pass-by-reference parameters)
typedef struct {
    RefType ref_type;
    union {
        struct {
            Environment *env;   // Environment where variable lives
            char *name;         // Variable name
        } variable;
        struct {
            Array *array;       // Array to index into
            int index;          // Element index
        } array_index;
        struct {
            Object *object;     // Object to access
            char *property;     // Property name
        } object_property;
    } as;
    int ref_count;              // Reference count for memory management
} Reference;

// Runtime value (TypeKind is from ast.h included at top)
struct Value {
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
        SocketHandle *as_socket;
        WebSocketHandle *as_websocket;
        Object *as_object;
        TypeKind as_type;
        BuiltinFn as_builtin_fn;
        Function *as_function;
        void *as_ffi_function;  // FFIFunction* (opaque)
        Task *as_task;
        Channel *as_channel;
        Reference *as_ref;      // Reference for pass-by-reference
    } as;
};

// Field entry struct - unified storage for name and value (reduces fragmentation)
// Defined after Value since it contains a Value member
struct FieldEntry {
    char *name;          // Field name (owned)
    Value value;         // Field value
};

#endif // HEMLOCK_RUNTIME_TYPES_H
