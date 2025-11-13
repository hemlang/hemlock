#ifndef HEMLOCK_AST_H
#define HEMLOCK_AST_H

#include <stdint.h>  // For int64_t

// Forward declarations
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Type Type;

// ========== EXPRESSION TYPES ==========

typedef enum {
    EXPR_NUMBER,
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_RUNE,       // Rune literal 'x'
    EXPR_IDENT,
    EXPR_NULL,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_TERNARY,
    EXPR_CALL,
    EXPR_ASSIGN,
    EXPR_GET_PROPERTY,
    EXPR_SET_PROPERTY,
    EXPR_INDEX,
    EXPR_INDEX_ASSIGN,
    EXPR_FUNCTION,
    EXPR_ARRAY_LITERAL,
    EXPR_OBJECT_LITERAL,
    EXPR_PREFIX_INC,
    EXPR_PREFIX_DEC,
    EXPR_POSTFIX_INC,
    EXPR_POSTFIX_DEC,
    EXPR_AWAIT,
} ExprType;

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_AND,
    OP_OR,
} BinaryOp;

typedef enum {
    UNARY_NOT,
    UNARY_NEGATE,
} UnaryOp;

// Expression node
struct Expr {
    ExprType type;
    union {
        struct {           // ← Number can be int or float
            int64_t int_value;  // Changed to int64_t to support 64-bit literals
            double float_value;
            int is_float;  // flag: which one to use
        } number;
        int boolean;
        char *string;
        uint32_t rune;     // Unicode codepoint
        char *ident;
        struct {
            Expr *left;
            Expr *right;
            BinaryOp op;
        } binary;
        struct {
            Expr *operand;
            UnaryOp op;
        } unary;
        struct {
            Expr *condition;
            Expr *true_expr;
            Expr *false_expr;
        } ternary;
        struct {
            Expr *func;  // Changed from char *name to support method calls
            Expr **args;
            int num_args;
        } call;
        struct {
            char *name;
            Expr *value;
        } assign;
        struct {
            Expr *object;
            char *property;
        } get_property;
        struct {
            Expr *object;
            char *property;
            Expr *value;
        } set_property;
        struct {
            Expr *object;
            Expr *index;
        } index;
        struct {
            Expr *object;
            Expr *index;
            Expr *value;
        } index_assign;
        struct {
            int is_async;
            char **param_names;
            Type **param_types;
            int num_params;
            Type *return_type;
            Stmt *body;
        } function;
        struct {
            Expr **elements;
            int num_elements;
        } array_literal;
        struct {
            char **field_names;
            Expr **field_values;
            int num_fields;
        } object_literal;
        struct {
            Expr *operand;
        } prefix_inc;
        struct {
            Expr *operand;
        } prefix_dec;
        struct {
            Expr *operand;
        } postfix_inc;
        struct {
            Expr *operand;
        } postfix_dec;
        struct {
            Expr *awaited_expr;
        } await_expr;
    } as;
};

// Type representation
typedef enum {
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_I64,
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_U64,
    //TYPE_F16,
    TYPE_F32,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_RUNE,           // Unicode codepoint (U+0000 to U+10FFFF)
    TYPE_PTR,
    TYPE_BUFFER,
    TYPE_NULL,
    TYPE_INFER,          // No annotation, infer from value
    TYPE_CUSTOM_OBJECT,  // Custom object type (Person, User, etc.)
    TYPE_GENERIC_OBJECT, // Generic 'object' keyword
    TYPE_VOID,           // Void type (for FFI functions with no return)
} TypeKind;

struct Type {
    TypeKind kind;
    char *type_name;  // For TYPE_CUSTOM_OBJECT (e.g., "Person")
};

// ========== STATEMENT TYPES ==========

typedef enum {
    STMT_LET,
    STMT_CONST,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_FOR_IN,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_BLOCK,
    STMT_RETURN,
    STMT_DEFINE_OBJECT,
    STMT_TRY,
    STMT_THROW,
    STMT_SWITCH,
    STMT_IMPORT,
    STMT_EXPORT,
    STMT_IMPORT_FFI,
    STMT_EXTERN_FN,
} StmtType;

// Statement node
struct Stmt {
    StmtType type;
    union {
        struct {
            char *name;
            Type *type_annotation;
            Expr *value;
        } let;
        struct {
            char *name;
            Type *type_annotation;
            Expr *value;
        } const_stmt;
        Expr *expr;
        struct {
            Expr *condition;
            Stmt *then_branch;
            Stmt *else_branch;  // can be NULL
        } if_stmt;
        struct {
            Expr *condition;
            Stmt *body;
        } while_stmt;
        struct {
            Stmt *initializer;  // let i = 0
            Expr *condition;    // i < 10
            Expr *increment;    // i = i + 1
            Stmt *body;
        } for_loop;
        struct {
            char *key_var;      // variable name (or NULL for value-only iteration)
            char *value_var;    // variable name
            Expr *iterable;     // array or object to iterate
            Stmt *body;
        } for_in;
        // break and continue have no fields
        struct {
            Stmt **statements;
            int count;
        } block;
        struct {
            Expr *value;  // can be NULL for bare `return;`
        } return_stmt;
        struct {
            char *name;
            char **field_names;
            Type **field_types;       // NULL for dynamic fields
            int *field_optional;      // 1 if optional, 0 if required
            Expr **field_defaults;    // NULL or default value expression
            int num_fields;
        } define_object;
        struct {
            Stmt *try_block;
            char *catch_param;        // NULL if no catch block
            Stmt *catch_block;        // NULL if no catch block
            Stmt *finally_block;      // NULL if no finally block
        } try_stmt;
        struct {
            Expr *value;
        } throw_stmt;
        struct {
            Expr *expr;              // Expression to switch on
            Expr **case_values;      // Array of case values (NULL for default)
            Stmt **case_bodies;      // Array of case body statements
            int num_cases;
        } switch_stmt;
        struct {
            int is_namespace;        // 1 for "import * as", 0 for named imports
            char *namespace_name;    // Name for namespace import (NULL if not namespace)
            char **import_names;     // Array of imported names (NULL if namespace)
            char **import_aliases;   // Array of aliases (NULL for no alias)
            int num_imports;         // Number of named imports
            char *module_path;       // Path to module file
        } import_stmt;
        struct {
            int is_declaration;      // 1 for "export fn/const/let", 0 for export list
            int is_reexport;         // 1 for "export {...} from", 0 otherwise
            Stmt *declaration;       // Declaration to export (NULL if list)
            char **export_names;     // Array of exported names (NULL if declaration)
            char **export_aliases;   // Array of aliases (NULL for no alias)
            int num_exports;         // Number of named exports
            char *module_path;       // Path for re-exports (NULL if not re-export)
        } export_stmt;
        struct {
            char *library_path;      // Library file path (e.g., "libc.so.6")
        } import_ffi;
        struct {
            char *function_name;     // C function name
            Type **param_types;      // Parameter types
            int num_params;          // Number of parameters
            Type *return_type;       // Return type (NULL for void)
        } extern_fn;
    } as;
};

// ========== CONSTRUCTORS ==========

// Expression constructors
Expr* expr_number(int value);
Expr* expr_number_int(int64_t value);  // Changed to int64_t to support 64-bit literals
Expr* expr_number_float(double value);
Expr* expr_bool(int value);
Expr* expr_string(const char *str);
Expr* expr_rune(uint32_t codepoint);
Expr* expr_ident(const char *name);
Expr* expr_null(void);
Expr* expr_binary(Expr *left, BinaryOp op, Expr *right);
Expr* expr_unary(UnaryOp op, Expr *operand);
Expr* expr_ternary(Expr *condition, Expr *true_expr, Expr *false_expr);
Expr* expr_call(Expr *func, Expr **args, int num_args);
Expr* expr_assign(const char *name, Expr *value);
Expr* expr_get_property(Expr *object, const char *property);
Expr* expr_set_property(Expr *object, const char *property, Expr *value);
Expr* expr_index(Expr *object, Expr *index);
Expr* expr_index_assign(Expr *object, Expr *index, Expr *value);
Expr* expr_function(int is_async, char **param_names, Type **param_types, int num_params, Type *return_type, Stmt *body);
Expr* expr_array_literal(Expr **elements, int num_elements);
Expr* expr_object_literal(char **field_names, Expr **field_values, int num_fields);
Expr* expr_prefix_inc(Expr *operand);
Expr* expr_prefix_dec(Expr *operand);
Expr* expr_postfix_inc(Expr *operand);
Expr* expr_postfix_dec(Expr *operand);
Expr* expr_await(Expr *awaited_expr);

// Statement constructors
Stmt* stmt_let(const char *name, Expr *value);
Stmt* stmt_let_typed(const char *name, Type *type_annotation, Expr *value);
Stmt* stmt_const(const char *name, Expr *value);
Stmt* stmt_const_typed(const char *name, Type *type_annotation, Expr *value);
Stmt* stmt_if(Expr *condition, Stmt *then_branch, Stmt *else_branch);
Stmt* stmt_while(Expr *condition, Stmt *body);
Stmt* stmt_for(Stmt *initializer, Expr *condition, Expr *increment, Stmt *body);
Stmt* stmt_for_in(char *key_var, char *value_var, Expr *iterable, Stmt *body);
Stmt* stmt_break(void);
Stmt* stmt_continue(void);
Stmt* stmt_block(Stmt **statements, int count);
Stmt* stmt_expr(Expr *expr);
Stmt* stmt_return(Expr *value);
Stmt* stmt_define_object(const char *name, char **field_names, Type **field_types,
                         int *field_optional, Expr **field_defaults, int num_fields);
Stmt* stmt_try(Stmt *try_block, char *catch_param, Stmt *catch_block, Stmt *finally_block);
Stmt* stmt_throw(Expr *value);
Stmt* stmt_switch(Expr *expr, Expr **case_values, Stmt **case_bodies, int num_cases);
Stmt* stmt_import_named(char **import_names, char **import_aliases, int num_imports, const char *module_path);
Stmt* stmt_import_namespace(const char *namespace_name, const char *module_path);
Stmt* stmt_export_declaration(Stmt *declaration);
Stmt* stmt_export_list(char **export_names, char **export_aliases, int num_exports);
Stmt* stmt_export_reexport(char **export_names, char **export_aliases, int num_exports, const char *module_path);
Stmt* stmt_import_ffi(const char *library_path);
Stmt* stmt_extern_fn(const char *function_name, Type **param_types, int num_params, Type *return_type);
Type* type_new(TypeKind kind);
void type_free(Type *type);

// Cloning
Expr* expr_clone(const Expr *expr);

// Cleanup
void expr_free(Expr *expr);
void stmt_free(Stmt *stmt);

#endif // HEMLOCK_AST_H