#ifndef HEMLOCK_INTERPRETER_H
#define HEMLOCK_INTERPRETER_H

#include "ast.h"

// Value types that can exist at runtime
typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_STRING,
    VAL_NULL,
} ValueType;

// String struct
typedef struct {
    char *data;
    int length;
    int capacity;
} String;

// Runtime value
typedef struct {
    ValueType type;
    union {
        int as_int;
        double as_float;
        int as_bool;
        String *as_string;
    } as;
} Value;

// Environment (symbol table for variables)
typedef struct Environment {
    char **names;
    Value *values;
    int count;
    int capacity;
    struct Environment *parent;  // for nested scopes later
} Environment;

// Public interface
Environment* env_new(Environment *parent);
void env_free(Environment *env);
void env_set(Environment *env, const char *name, Value value);
Value env_get(Environment *env, const char *name);

Value eval_expr(Expr *expr, Environment *env);
void eval_stmt(Stmt *stmt, Environment *env);
void eval_program(Stmt **stmts, int count, Environment *env);

// Value constructors
Value val_int(int value);
Value val_float(double value);
Value val_bool(int value);
Value val_string(const char *str);
Value val_string_take(char *str, int length, int capacity);
Value val_null(void);

// Value operations
void print_value(Value val);

// String operations
void string_free(String *str);
String* string_concat(String *a, String *b);
String* string_copy(String *str);

#endif // HEMLOCK_INTERPRETER_H