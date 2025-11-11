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
    VAL_U8,
    VAL_U16,
    VAL_U32,
    //VAL_F16,
    VAL_F32,
    VAL_F64,
    VAL_BOOL,
    VAL_STRING,
    VAL_PTR,
    VAL_BUFFER,
    VAL_ARRAY,          // Dynamic array
    VAL_OBJECT,         // JavaScript-style object
    VAL_FILE,           // File handle
    VAL_TYPE,           // Represents a type (for sizeof, talloc, etc.)
    VAL_BUILTIN_FN,
    VAL_FUNCTION,       // User-defined function
    VAL_NULL,
} ValueType;

typedef struct Value Value;
typedef Value (*BuiltinFn)(Value *args, int num_args);

// Forward declare Environment for Function struct
typedef struct Environment Environment;

// String struct
typedef struct {
    char *data;
    int length;
    int capacity;
} String;

// Buffer struct (safe pointer wrapper)
typedef struct {
    void *data;
    int length;
    int capacity;
} Buffer;

// Array struct (dynamic array)
typedef struct {
    Value *elements;
    int length;
    int capacity;
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
} Object;

// Function struct (user-defined function)
typedef struct {
    char **param_names;
    Type **param_types;
    int num_params;
    Type *return_type;
    Stmt *body;
    Environment *closure_env;  // CAPTURED ENVIRONMENT
} Function;

// Forward declare TypeKind from ast.h
#include "ast.h"

// Runtime value
typedef struct Value {
    ValueType type;
    union {
        int8_t as_i8;
        int16_t as_i16;
        int32_t as_i32;
        uint8_t as_u8;
        uint16_t as_u16;
        uint32_t as_u32;
        float as_f32;
        double as_f64;
        int as_bool;
        String *as_string;
        void *as_ptr;
        Buffer *as_buffer;
        Array *as_array;
        FileHandle *as_file;
        Object *as_object;
        TypeKind as_type;
        BuiltinFn as_builtin_fn;
        Function *as_function;
    } as;
} Value;

// Environment (symbol table for variables)
typedef struct Environment {
    char **names;
    Value *values;
    int *is_const;  // 1 if const, 0 if mutable (let)
    int count;
    int capacity;
    struct Environment *parent;  // for nested scopes later
} Environment;

// Public interface
Environment* env_new(Environment *parent);
void env_free(Environment *env);
void env_define(Environment *env, const char *name, Value value, int is_const);
void env_set(Environment *env, const char *name, Value value);
Value env_get(Environment *env, const char *name);

Value eval_expr(Expr *expr, Environment *env);
void eval_stmt(Stmt *stmt, Environment *env);
void eval_program(Stmt **stmts, int count, Environment *env);

// Value constructors
Value val_int(int value);    // creates i32
Value val_float(double value); // creates f64
Value val_i8(int8_t value);
Value val_i16(int16_t value);
Value val_i32(int32_t value);
Value val_u8(uint8_t value);
Value val_u16(uint16_t value);
Value val_u32(uint32_t value);
Value val_f32(float value);
Value val_f64(double value);
Value val_bool(int value);
Value val_string(const char *str);
Value val_string_take(char *str, int length, int capacity);
Value val_ptr(void *ptr);
Value val_buffer(int size);
Value val_array(Array *arr);
Value val_file(FileHandle *file);
Value val_type(TypeKind kind);
Value val_builtin_fn(BuiltinFn fn);
Value val_function(Function *fn);
Value val_object(Object *obj);
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

void register_builtins(Environment *env, int argc, char **argv);

#endif // HEMLOCK_INTERPRETER_H