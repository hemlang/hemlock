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
        int number;
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
Expr* expr_bool(int value);
Expr* expr_string(const char *str);
Expr* expr_ident(const char *name);
Expr* expr_binary(Expr *left, BinaryOp op, Expr *right);
Expr* expr_unary(UnaryOp op, Expr *operand);
Expr* expr_call(const char *name, Expr **args, int num_args);
Expr* expr_assign(const char *name, Expr *value);

// Statement constructors
Stmt* stmt_let(const char *name, Expr *value);
Stmt* stmt_if(Expr *condition, Stmt *then_branch, Stmt *else_branch);
Stmt* stmt_while(Expr *condition, Stmt *body);
Stmt* stmt_block(Stmt **statements, int count);
Stmt* stmt_expr(Expr *expr);

// Cleanup
void expr_free(Expr *expr);
void stmt_free(Stmt *stmt);

#endif // HEMLOCK_AST_H