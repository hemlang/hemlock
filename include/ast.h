#ifndef HEMLOCK_AST_H
#define HEMLOCK_AST_H

// Forward declarations
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Type Type;

// ========== EXPRESSION TYPES ==========

typedef enum {
    EXPR_NUMBER,
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_IDENT,
    EXPR_NULL,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL,
    EXPR_ASSIGN,
    EXPR_GET_PROPERTY,
    EXPR_SET_PROPERTY,
    EXPR_INDEX,
    EXPR_INDEX_ASSIGN,
    EXPR_FUNCTION,
    EXPR_OBJECT_LITERAL,
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
            int int_value;
            double float_value;
            int is_float;  // flag: which one to use
        } number;
        int boolean;
        char *string;
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
            char **param_names;
            Type **param_types;
            int num_params;
            Type *return_type;
            Stmt *body;
        } function;
        struct {
            char **field_names;
            Expr **field_values;
            int num_fields;
        } object_literal;
    } as;
};

// Type representation
typedef enum {
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    //TYPE_F16,
    TYPE_F32,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_PTR,
    TYPE_BUFFER,
    TYPE_NULL,
    TYPE_INFER,          // No annotation, infer from value
    TYPE_CUSTOM_OBJECT,  // Custom object type (Person, User, etc.)
    TYPE_GENERIC_OBJECT, // Generic 'object' keyword
} TypeKind;

struct Type {
    TypeKind kind;
    char *type_name;  // For TYPE_CUSTOM_OBJECT (e.g., "Person")
};

// ========== STATEMENT TYPES ==========

typedef enum {
    STMT_LET,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_BLOCK,
    STMT_RETURN,
    STMT_DEFINE_OBJECT,
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
    } as;
};

// ========== CONSTRUCTORS ==========

// Expression constructors
Expr* expr_number(int value);
Expr* expr_number_int(int value);
Expr* expr_number_float(double value);
Expr* expr_bool(int value);
Expr* expr_string(const char *str);
Expr* expr_ident(const char *name);
Expr* expr_null(void);
Expr* expr_binary(Expr *left, BinaryOp op, Expr *right);
Expr* expr_unary(UnaryOp op, Expr *operand);
Expr* expr_call(Expr *func, Expr **args, int num_args);
Expr* expr_assign(const char *name, Expr *value);
Expr* expr_get_property(Expr *object, const char *property);
Expr* expr_set_property(Expr *object, const char *property, Expr *value);
Expr* expr_index(Expr *object, Expr *index);
Expr* expr_index_assign(Expr *object, Expr *index, Expr *value);
Expr* expr_function(char **param_names, Type **param_types, int num_params, Type *return_type, Stmt *body);
Expr* expr_object_literal(char **field_names, Expr **field_values, int num_fields);

// Statement constructors
Stmt* stmt_let(const char *name, Expr *value);
Stmt* stmt_let_typed(const char *name, Type *type_annotation, Expr *value);
Stmt* stmt_if(Expr *condition, Stmt *then_branch, Stmt *else_branch);
Stmt* stmt_while(Expr *condition, Stmt *body);
Stmt* stmt_block(Stmt **statements, int count);
Stmt* stmt_expr(Expr *expr);
Stmt* stmt_return(Expr *value);
Stmt* stmt_define_object(const char *name, char **field_names, Type **field_types,
                         int *field_optional, Expr **field_defaults, int num_fields);
Type* type_new(TypeKind kind);
void type_free(Type *type);

// Cleanup
void expr_free(Expr *expr);
void stmt_free(Stmt *stmt);

#endif // HEMLOCK_AST_H