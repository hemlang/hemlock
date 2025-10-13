#ifndef HEMLOCK_AST_H
#define HEMLOCK_AST_H

// Forward declarations
typedef struct Expr Expr;
typedef struct Stmt Stmt;

// ========== EXPRESSION TYPES ==========

typedef enum {
    EXPR_NUMBER,
    EXPR_IDENT,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL,
    EXPR_ASSIGN,
    EXPR_BOOL,
    EXPR_STRING,
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
            char *name;
            Expr **args;
            int num_args;
        } call;
        struct {
            char *name;
            Expr *value;
        } assign;
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
    TYPE_F16,
    TYPE_F32,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_NULL,
    TYPE_INFER,      // No annotation, infer from value
} TypeKind;

typedef struct {
    TypeKind kind;
} Type;

// ========== STATEMENT TYPES ==========

typedef enum {
    STMT_LET,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_BLOCK,
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
Expr* expr_binary(Expr *left, BinaryOp op, Expr *right);
Expr* expr_unary(UnaryOp op, Expr *operand);
Expr* expr_call(const char *name, Expr **args, int num_args);
Expr* expr_assign(const char *name, Expr *value);

// Statement constructors
Stmt* stmt_let(const char *name, Expr *value);
Stmt* stmt_let_typed(const char *name, Type *type_annotation, Expr *value);
Stmt* stmt_if(Expr *condition, Stmt *then_branch, Stmt *else_branch);
Stmt* stmt_while(Expr *condition, Stmt *body);
Stmt* stmt_block(Stmt **statements, int count);
Stmt* stmt_expr(Expr *expr);
Type* type_new(TypeKind kind);
void type_free(Type *type);

// Cleanup
void expr_free(Expr *expr);
void stmt_free(Stmt *stmt);

#endif // HEMLOCK_AST_H