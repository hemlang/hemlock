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

// ========== CALL STACK (for error reporting) ==========

typedef struct {
    char *function_name;
    int line;  // Future: add line number tracking
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

// ========== ENVIRONMENT (environment.c) ==========

Environment* env_new(Environment *parent);
void env_free(Environment *env);
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx);
void env_set(Environment *env, const char *name, Value value, ExecutionContext *ctx);
Value env_get(Environment *env, const char *name, ExecutionContext *ctx);

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
Value array_get(Array *arr, int index);
void array_set(Array *arr, int index, Value val);
Value val_array(Array *arr);

// Object operations
Object* object_new(char *type_name, int initial_capacity);
void object_free(Object *obj);
Value val_object(Object *obj);

// File operations
Value val_file(FileHandle *file);
void file_free(FileHandle *file);

// Value cleanup
void value_free(Value val);

// Printing
void print_value(Value val);

// ========== TYPES (types.c) ==========

// Type checking helpers
int is_integer(Value val);
int is_float(Value val);
int is_numeric(Value val);
int32_t value_to_int(Value val);
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
Value check_object_type(Value value, ObjectType *object_type, Environment *env, ExecutionContext *ctx);

// ========== BUILTINS (builtins.c) ==========

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx);

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

Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args);
Value call_array_method(Array *arr, const char *method, Value *args, int num_args);
Value call_string_method(String *str, const char *method, Value *args, int num_args);
Value call_channel_method(Channel *ch, const char *method, Value *args, int num_args);

// I/O builtin functions
Value builtin_read_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_write_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_append_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_read_bytes(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_write_bytes(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_file_exists(Value *args, int num_args, ExecutionContext *ctx);
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

void ffi_init(void);
void ffi_cleanup(void);
void execute_import_ffi(Stmt *stmt, ExecutionContext *ctx);
void execute_extern_fn(Stmt *stmt, Environment *env, ExecutionContext *ctx);
Value ffi_call_function(FFIFunction *func, Value *args, int num_args, ExecutionContext *ctx);
void ffi_free_function(FFIFunction *func);

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
void call_stack_pop(CallStack *stack);
void call_stack_print(CallStack *stack);
void call_stack_free(CallStack *stack);

#endif // HEMLOCK_INTERPRETER_INTERNAL_H
