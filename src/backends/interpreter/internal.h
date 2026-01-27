#ifndef HEMLOCK_INTERPRETER_INTERNAL_H
#define HEMLOCK_INTERPRETER_INTERNAL_H

#include "interpreter.h"
#include "runtime/memory.h"
#include "frontend/ast.h"
#include "hemlock_limits.h"
#include <stdint.h>

// Forward declaration for profiler
typedef struct ProfilerState ProfilerState;

// ========== CONTROL FLOW STATE ==========

typedef struct {
    int is_returning;
    Value return_value;
} ReturnState;

typedef struct {
    int is_breaking;
    int is_continuing;
    char *target_label;  // NULL for innermost loop, or label name to target
} LoopState;

typedef struct {
    int is_throwing;
    Value exception_value;
} ExceptionState;

// Defer stack - stores deferred expressions (function calls) to execute later
typedef struct {
    Expr **calls;       // Array of deferred expressions
    Environment **envs; // Array of environments (captured for each defer)
    int count;
    int capacity;
} DeferStack;

// ========== CALL STACK (for error reporting) ==========

typedef struct {
    char *function_name;
    char *source_file;  // Source file name (optional, can be NULL)
    int line;           // Line number of the call site
    int column;         // Column number of the call site (0 if unknown)
} CallFrame;

typedef struct {
    CallFrame *frames;
    int count;
    int capacity;
} CallStack;

// ========== EXECUTION CONTEXT ==========

// Default maximum call stack depth (configurable via set_stack_limit() or --stack-depth)
#define DEFAULT_MAX_STACK_DEPTH HML_DEFAULT_MAX_STACK_DEPTH

// Execution context - holds all control flow state
// Each async task will have its own context (future async support)
// Note: ExecutionContext is forward-declared in interpreter.h
struct ExecutionContext {
    ReturnState return_state;
    LoopState loop_state;
    ExceptionState exception_state;
    CallStack call_stack;
    DeferStack defer_stack;
    int max_stack_depth;  // Configurable stack limit (default: DEFAULT_MAX_STACK_DEPTH)
    // Sandbox configuration
    int sandbox_flags;    // Bitmask of HML_SANDBOX_RESTRICT_* flags (0 = unrestricted)
    char *sandbox_root;   // Root directory for file access (NULL = no restriction)
    // Profiler state (NULL when profiling disabled)
    ProfilerState *profiler;
    // Current source location (for profiler allocation tracking)
    const char *current_source_file;
    int current_line;
};

// ========== OBJECT TYPE REGISTRY ==========

typedef struct {
    char *name;
    char **type_params;       // Type parameters (e.g., ["T", "U"] for define Pair<T, U>)
    int num_type_params;      // Number of type parameters (0 for non-generic types)
    char **field_names;
    Type **field_types;
    int *field_optional;
    Expr **field_defaults;
    int num_fields;
    // Method signatures
    char **method_names;      // Method names
    Type **method_types;      // Function types (TYPE_FUNCTION)
    int *method_optional;     // 1 if optional method (fn method?())
    Expr **method_defaults;   // Default implementation (NULL if signature only)
    int num_methods;
} ObjectType;

typedef struct {
    ObjectType **types;
    int count;
    int capacity;
    // Hash table for O(1) lookup by name
    int *hash_table;          // Maps hash slots to type indices (-1 = empty)
    int hash_capacity;        // Size of hash table
} ObjectTypeRegistry;

extern ObjectTypeRegistry object_types;

// ========== ENUM TYPE REGISTRY ==========

typedef struct {
    char *name;
    char **variant_names;
    int32_t *variant_values;
    int num_variants;
} EnumType;

typedef struct {
    EnumType **types;
    int count;
    int capacity;
    // Hash table for O(1) lookup by name
    int *hash_table;
    int hash_capacity;
} EnumTypeRegistry;

extern EnumTypeRegistry enum_types;

// ========== TYPE ALIAS REGISTRY ==========

typedef struct {
    char *name;
    char **type_params;       // Type parameters (e.g., ["T", "U"] for type Pair<T, U> = ...)
    int num_type_params;      // Number of type parameters (0 for non-generic aliases)
    Type *aliased_type;       // The actual type
} TypeAlias;

typedef struct {
    TypeAlias **aliases;
    int count;
    int capacity;
    // Hash table for O(1) lookup by name
    int *hash_table;
    int hash_capacity;
} TypeAliasRegistry;

extern TypeAliasRegistry type_aliases;

// ========== VISITED SET (for cycle detection) ==========

// Internal structure for tracking visited objects/arrays during cycle detection
// Used by both environment.c (cycle breaking) and values.c (cycle-safe deallocation)
typedef struct {
    void **pointers;
    int count;
    int capacity;
} VisitedSet;

VisitedSet* visited_set_new(void);
void visited_set_free(VisitedSet *set);
int visited_set_contains(VisitedSet *set, void *ptr);
void visited_set_add(VisitedSet *set, void *ptr);

// ========== ENVIRONMENT (environment.c) ==========

// DJB2 hash function for variable names
uint32_t hash_string(const char *str);

Environment* env_new(Environment *parent);
void env_free(Environment *env);
void env_clear(Environment *env);  // Clear variables without deallocating (for loop reuse)
void env_retain(Environment *env);
void env_release(Environment *env);
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx);
// Fast variant that borrows the name string without strdup - caller must ensure name outlives env
void env_define_borrowed(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx);
// Fastest variant for function params - uses pre-computed hash, skips "already defined" check
void env_define_param(Environment *env, const char *name, uint32_t hash, Value value);
void env_set(Environment *env, const char *name, Value value, ExecutionContext *ctx);
Value env_get(Environment *env, const char *name, ExecutionContext *ctx);

// ========== VALUES (values.c) ==========

// String operations
String* string_new(const char *cstr);
int object_lookup_field(Object *obj, const char *name);  // O(1) field lookup using hash table
int object_lookup_field_with_hash(Object *obj, const char *name, uint32_t hash);  // With pre-computed hash
int object_validate_ic(Object *obj, int cached_idx, const char *name);  // Validate IC cache entry
int object_hash_insert(Object *obj, const char *name, int field_index);  // Insert field into hash table

// Value cleanup and reference counting
void value_free(Value val);

// Fast path macro: check if type needs refcounting before calling
// Types that need refcounting: STRING, BUFFER, ARRAY, OBJECT, FUNCTION, TASK, CHANNEL, REF
#define VALUE_NEEDS_REFCOUNT(type) \
    ((type) == VAL_STRING || (type) == VAL_BUFFER || (type) == VAL_ARRAY || \
     (type) == VAL_OBJECT || (type) == VAL_FUNCTION || (type) == VAL_TASK || \
     (type) == VAL_CHANNEL || (type) == VAL_REF)

// Fast path macros - skip function call for primitives
#define VALUE_RETAIN(val) do { if (VALUE_NEEDS_REFCOUNT((val).type)) value_retain(val); } while(0)
#define VALUE_RELEASE(val) do { if (VALUE_NEEDS_REFCOUNT((val).type)) value_release(val); } while(0)

// Printing
void print_value(Value val);
char* value_to_string(Value val);  // Caller must free result

// ========== TYPES (types.c) ==========

// Type checking helpers
int is_integer(Value val);
int is_float(Value val);
int is_numeric(Value val);
int32_t value_to_int(Value val);
int64_t value_to_int64(Value val);
double value_to_float(Value val);
int value_is_truthy(Value val);

// Get the type name of a value (for error messages)
const char* value_type_name(ValueType type);

// Fast path macro for boolean truthy check (most common case in loops)
#define VALUE_IS_TRUTHY_FAST(val) \
    ((val).type == VAL_BOOL ? (val).as.as_bool : \
     (val).type == VAL_I32 ? (val).as.as_i32 != 0 : \
     value_is_truthy(val))

// Type promotion and conversion
int type_rank(ValueType type);
ValueType promote_types(ValueType left, ValueType right);
Value promote_value(Value val, ValueType target_type);
Value convert_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx);
Value parse_string_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx);

// Object type registry
void init_object_types(void);
void register_object_type(ObjectType *type);
ObjectType* lookup_object_type(const char *name);
void cleanup_object_types(void);

// Enum type registry
void init_enum_types(void);
void register_enum_type(EnumType *type);
EnumType* lookup_enum_type(const char *name);
void cleanup_enum_types(void);
Value check_object_type(Value value, ObjectType *object_type, Environment *env, ExecutionContext *ctx);

// Type alias registry
void init_type_aliases(void);
void register_type_alias(TypeAlias *alias);
TypeAlias* lookup_type_alias(const char *name);
void cleanup_type_aliases(void);

// ========== BUILTINS (builtins.c) ==========

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx);

// Concurrency builtins (needed for await implementation)
Value builtin_join(Value *args, int num_args, ExecutionContext *ctx);

// Function call utilities
Value builtin_apply(Value *args, int num_args, ExecutionContext *ctx);

// ========== UTF-8 UTILITIES (utf8.c) ==========

int utf8_count_codepoints(const char *data, int byte_length);
int utf8_byte_offset(const char *data, int byte_length, int char_index);
uint32_t utf8_decode_at(const char *data, int byte_pos);
uint32_t utf8_decode_next(const char **data_ptr);
int utf8_encode(uint32_t codepoint, char *buffer);
int utf8_char_byte_length(unsigned char first_byte);
int utf8_validate(const char *data, int byte_length);
int utf8_is_ascii(const char *data, int byte_length);

// ========== I/O (io.c) ==========

// Value comparison
int values_equal(Value a, Value b);

Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args, ExecutionContext *ctx);
Value call_socket_method(SocketHandle *sock, const char *method, Value *args, int num_args, ExecutionContext *ctx);
Value call_array_method(Array *arr, const char *method, Value *args, int num_args, int line, ExecutionContext *ctx);
Value call_string_method(String *str, const char *method, Value *args, int num_args, int line, ExecutionContext *ctx);
Value call_channel_method(Channel *ch, const char *method, Value *args, int num_args, ExecutionContext *ctx);
Value call_object_method(Object *obj, const char *method, Value *args, int num_args, ExecutionContext *ctx);

// Property accessors
Value get_socket_property(SocketHandle *sock, const char *property, ExecutionContext *ctx);

// I/O builtin functions
Value builtin_read_line(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_eprint(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_open(Value *args, int num_args, ExecutionContext *ctx);

// ========== FFI (ffi.c) ==========

// Forward declaration for FFI library
typedef struct FFILibrary FFILibrary;

// Forward declaration for FFI struct type
typedef struct FFIStructType FFIStructType;

// External function structure
typedef struct {
    char *name;              // Function name
    void *func_ptr;          // Function pointer from dlsym() (NULL until first call for lazy resolution)
    void *lib_handle;        // Library handle for lazy symbol resolution
    char *lib_path;          // Library path for error messages
    void *cif;               // ffi_cif (opaque)
    void **arg_types;        // libffi argument types (opaque)
    void *return_type;       // libffi return type (opaque)
    Type **hemlock_params;   // Hemlock parameter types
    Type *hemlock_return;    // Hemlock return type
    int num_params;
} FFIFunction;

// FFI Callback structure - wraps a Hemlock function as a C function pointer
typedef struct {
    void *closure;           // ffi_closure (opaque)
    void *code_ptr;          // C-callable function pointer
    void *cif;               // ffi_cif (opaque)
    void **arg_types;        // libffi argument types
    void *return_type;       // libffi return type
    Function *hemlock_fn;    // The Hemlock function to invoke
    Type **hemlock_params;   // Hemlock parameter types (for marshalling)
    Type *hemlock_return;    // Hemlock return type (for marshalling)
    int num_params;          // Number of parameters
    int id;                  // Unique callback ID (for debugging)
} FFICallback;

void ffi_init(void);
void ffi_cleanup(void);
void execute_import_ffi(Stmt *stmt, ExecutionContext *ctx);
void execute_extern_fn(Stmt *stmt, Environment *env, ExecutionContext *ctx);
Value ffi_call_function(FFIFunction *func, Value *args, int num_args, ExecutionContext *ctx);
void ffi_free_function(FFIFunction *func);

// FFI Callbacks - create C-callable function pointers from Hemlock functions
FFICallback* ffi_create_callback(Function *fn, Type **param_types, int num_params, Type *return_type, ExecutionContext *ctx);
void ffi_free_callback(FFICallback *cb);
void* ffi_callback_get_ptr(FFICallback *cb);  // Get the C-callable function pointer
int ffi_free_callback_by_ptr(void *code_ptr);  // Free callback by its code pointer

// Type helpers for callback API
Type* type_from_string(const char *name);  // Create a Type from a string like "i32", "ptr", etc.

// FFI Struct support - register struct types for FFI use
FFIStructType* ffi_register_struct(const char *name, char **field_names, Type **field_types, int num_fields);
FFIStructType* ffi_lookup_struct(const char *name);
void ffi_struct_cleanup(void);

// ========== RUNTIME (runtime.c) ==========

Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
void eval_stmt(Stmt *stmt, Environment *env, ExecutionContext *ctx);
void eval_program(Stmt **stmts, int count, Environment *env, ExecutionContext *ctx);

// Execution context helpers
ExecutionContext* exec_context_new(void);
void exec_context_free(ExecutionContext *ctx);

// Call stack helpers
void call_stack_init(CallStack *stack);
void call_stack_push(CallStack *stack, const char *function_name);
void call_stack_push_line(CallStack *stack, const char *function_name, int line);
void call_stack_push_full(CallStack *stack, const char *function_name, const char *source_file, int line);
void call_stack_push_full_loc(CallStack *stack, const char *function_name, const char *source_file, int line, int column);
void call_stack_pop(CallStack *stack);
void call_stack_print(CallStack *stack);
void call_stack_print_with_source(CallStack *stack, const char *source);  // Enhanced stack trace with source snippets
void call_stack_free(CallStack *stack);

// Current source file tracking (for stack traces)
void set_current_source_file(const char *file);
const char* get_current_source_file(void);

// Defer stack helpers
void defer_stack_init(DeferStack *stack);
void defer_stack_push(DeferStack *stack, Expr *call, Environment *env);
void defer_stack_execute(DeferStack *stack, ExecutionContext *ctx);
void defer_stack_free(DeferStack *stack);

// Runtime error with exception support (printf-style)
// Now throws catchable exceptions when ctx is provided
__attribute__((format(printf, 2, 3)))
void runtime_error(ExecutionContext *ctx, const char *format, ...);

// Runtime error with line number (for better error reporting)
__attribute__((format(printf, 3, 4)))
void runtime_error_at(ExecutionContext *ctx, int line, const char *format, ...);

// Runtime error with full location (file, line, column) and source context
__attribute__((format(printf, 5, 6)))
void runtime_error_with_context(ExecutionContext *ctx, const char *file, int line, int column, const char *format, ...);

// ========== SOURCE CODE TRACKING (for error context) ==========

// Set the current source code content (for showing error snippets)
void set_current_source_code(const char *source);
const char* get_current_source_code(void);

// Get a specific line from the source code (returns pointer into source, not a copy)
// Returns NULL if line is out of bounds. Sets *line_length to the length of the line.
const char* get_source_line(const char *source, int line_num, int *line_length);

// Print source context around an error (shows the line with a caret pointing to the column)
void print_source_context(FILE *out, const char *source, int line, int column);

// Format an error message with full location and source context into a string
// Caller must free the returned string
char* format_error_with_context(const char *file, int line, int column, const char *message);

// ========== SANDBOX HELPERS ==========

// Check if a specific sandbox restriction is active
int sandbox_is_restricted(ExecutionContext *ctx, int restriction_flag);

// Check if a file path is allowed under sandbox rules
// Returns 1 if allowed, 0 if blocked
int sandbox_path_allowed(ExecutionContext *ctx, const char *path, int is_write);

// Throw a sandbox violation error
void sandbox_error(ExecutionContext *ctx, const char *operation);

// ========== PROFILER ==========

// Profiler instrumentation hooks (inline checks for NULL profiler)
void profiler_enter_function(ProfilerState *state, const char *name,
                             const char *source_file, int line);
void profiler_exit_function(ProfilerState *state);
void profiler_record_alloc(ProfilerState *state, const char *source_file,
                           int line, uint64_t bytes);
void profiler_record_free(ProfilerState *state, const char *source_file,
                          int line, uint64_t bytes);
void profiler_track_ptr(ProfilerState *state, void *ptr, uint64_t size,
                        const char *source_file, int line);
uint64_t profiler_untrack_ptr(ProfilerState *state, void *ptr);

// Convenience macros that check for NULL profiler
#define PROFILER_ENTER(ctx, name, file, line) \
    do { if ((ctx)->profiler) profiler_enter_function((ctx)->profiler, name, file, line); } while(0)
#define PROFILER_EXIT(ctx) \
    do { if ((ctx)->profiler) profiler_exit_function((ctx)->profiler); } while(0)
#define PROFILER_ALLOC(ctx, file, line, bytes) \
    do { if ((ctx)->profiler) profiler_record_alloc((ctx)->profiler, file, line, bytes); } while(0)
#define PROFILER_FREE(ctx, file, line, bytes) \
    do { if ((ctx)->profiler) profiler_record_free((ctx)->profiler, file, line, bytes); } while(0)
#define PROFILER_TRACK_PTR(ctx, ptr, size, file, line) \
    do { if ((ctx)->profiler) profiler_track_ptr((ctx)->profiler, ptr, size, file, line); } while(0)
#define PROFILER_UNTRACK_PTR(ctx, ptr) \
    ((ctx)->profiler ? profiler_untrack_ptr((ctx)->profiler, ptr) : 0)

#endif // HEMLOCK_INTERPRETER_INTERNAL_H
