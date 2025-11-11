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

// Global control flow state (shared across modules)
extern ReturnState return_state;
extern LoopState loop_state;
extern ExceptionState exception_state;

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
void env_define(Environment *env, const char *name, Value value, int is_const);
void env_set(Environment *env, const char *name, Value value);
Value env_get(Environment *env, const char *name);

// ========== VALUES (values.c) ==========

// Value constructors
Value val_i8(int8_t value);
Value val_i16(int16_t value);
Value val_i32(int32_t value);
Value val_u8(uint8_t value);
Value val_u16(uint16_t value);
Value val_u32(uint32_t value);
Value val_f32(float value);
Value val_f64(double value);
Value val_int(int value);
Value val_float(double value);
Value val_bool(int value);
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
Value convert_to_type(Value value, Type *target_type, Environment *env);

// Object type registry
void init_object_types(void);
void register_object_type(ObjectType *type);
ObjectType* lookup_object_type(const char *name);
Value check_object_type(Value value, ObjectType *object_type, Environment *env);

// ========== BUILTINS (builtins.c) ==========

void register_builtins(Environment *env, int argc, char **argv);

// ========== I/O (io.c) ==========

Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args);
Value call_array_method(Array *arr, const char *method, Value *args, int num_args);

// I/O builtin functions
Value builtin_read_file(Value *args, int num_args);
Value builtin_write_file(Value *args, int num_args);
Value builtin_append_file(Value *args, int num_args);
Value builtin_read_bytes(Value *args, int num_args);
Value builtin_write_bytes(Value *args, int num_args);
Value builtin_file_exists(Value *args, int num_args);
Value builtin_read_line(Value *args, int num_args);
Value builtin_eprint(Value *args, int num_args);
Value builtin_open(Value *args, int num_args);

// ========== RUNTIME (runtime.c) ==========

Value eval_expr(Expr *expr, Environment *env);
void eval_stmt(Stmt *stmt, Environment *env);
void eval_program(Stmt **stmts, int count, Environment *env);

#endif // HEMLOCK_INTERPRETER_INTERNAL_H
