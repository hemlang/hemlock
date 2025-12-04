#ifndef HEMLOCK_INTERPRETER_INTERNAL_H
#define HEMLOCK_INTERPRETER_INTERNAL_H

#include "interpreter.h"
#include "ast.h"
#include <stdint.h>

// ========== CONTROL FLOW STATE ==========

typedef struct {
    int is_returning;
    Value return_value;
} ReturnState;

typedef struct {
    int is_breaking;
    int is_continuing;
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
} CallFrame;

typedef struct {
    CallFrame *frames;
    int count;
    int capacity;
} CallStack;

// ========== EXECUTION CONTEXT ==========

// Execution context - holds all control flow state
// Each async task will have its own context (future async support)
// Note: ExecutionContext is forward-declared in interpreter.h
struct ExecutionContext {
    ReturnState return_state;
    LoopState loop_state;
    ExceptionState exception_state;
    CallStack call_stack;
    DeferStack defer_stack;
};

// ========== OBJECT TYPE REGISTRY ==========

typedef struct {
    char *name;
    char **field_names;
    Type **field_types;
    int *field_optional;
    Expr **field_defaults;
    int num_fields;
} ObjectType;

typedef struct {
    ObjectType **types;
    int count;
    int capacity;
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
} EnumTypeRegistry;

extern EnumTypeRegistry enum_types;

// ========== ENVIRONMENT (environment.c) ==========

Environment* env_new(Environment *parent);
void env_free(Environment *env);
void env_retain(Environment *env);
void env_release(Environment *env);
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx);
void env_set(Environment *env, const char *name, Value value, ExecutionContext *ctx);
Value env_get(Environment *env, const char *name, ExecutionContext *ctx);

// Tracking for manually freed objects/arrays (for compatibility with builtin_free)
void register_manually_freed_pointer(void *ptr);
int is_manually_freed_pointer(void *ptr);
void clear_manually_freed_pointers(void);

// ========== VALUES (values.c) ==========

// Value constructors
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
Value val_int(int value);
Value val_float(double value);
Value val_bool(int value);
Value val_rune(uint32_t codepoint);
Value val_ptr(void *ptr);
Value val_type(TypeKind kind);
Value val_function(Function *fn);
Value val_null(void);

// String operations
String* string_new(const char *cstr);
String* string_copy(String *str);
String* string_concat(String *a, String *b);
void string_free(String *str);
Value val_string(const char *str);
Value val_string_take(char *data, int length, int capacity);

// Buffer operations
Value val_buffer(int size);
void buffer_free(Buffer *buf);

// Array operations
Array* array_new(void);
void array_free(Array *arr);
void array_push(Array *arr, Value val);
Value array_pop(Array *arr);
Value array_get(Array *arr, int index, ExecutionContext *ctx);
void array_set(Array *arr, int index, Value val, ExecutionContext *ctx);
Value val_array(Array *arr);

// Object operations
Object* object_new(char *type_name, int initial_capacity);
void object_free(Object *obj);
Value val_object(Object *obj);

// Function operations
void function_free(Function *fn);
void function_retain(Function *fn);
void function_release(Function *fn);

// File operations
Value val_file(FileHandle *file);
void file_free(FileHandle *file);

// Socket operations
Value val_socket(SocketHandle *sock);
void socket_free(SocketHandle *sock);

// Value cleanup and reference counting
void value_free(Value val);
void value_retain(Value val);
void value_release(Value val);

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

// Type promotion and conversion
int type_rank(ValueType type);
ValueType promote_types(ValueType left, ValueType right);
Value promote_value(Value val, ValueType target_type);
Value convert_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx);

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

// ========== BUILTINS (builtins.c) ==========

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx);

// Concurrency builtins (needed for await implementation)
Value builtin_join(Value *args, int num_args, ExecutionContext *ctx);

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
Value call_array_method(Array *arr, const char *method, Value *args, int num_args, ExecutionContext *ctx);
Value call_string_method(String *str, const char *method, Value *args, int num_args, ExecutionContext *ctx);
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

// External function structure
typedef struct {
    char *name;              // Function name
    void *func_ptr;          // Function pointer from dlsym()
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
void call_stack_pop(CallStack *stack);
void call_stack_print(CallStack *stack);
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
void runtime_error(ExecutionContext *ctx, const char *format, ...);

#endif // HEMLOCK_INTERPRETER_INTERNAL_H
