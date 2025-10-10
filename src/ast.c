#include "ast.h"
#include <stdlib.h>
#include <string.h>

// ========== EXPRESSION CONSTRUCTORS ==========

Expr* expr_number(int value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NUMBER;
    expr->as.number = value;
    return expr;
}

Expr* expr_bool(int value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_BOOL;
    expr->as.boolean = value ? 1 : 0;
    return expr;
}

Expr* expr_string(const char *str) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_STRING;
    expr->as.string = strdup(str);
    return expr;
}

Expr* expr_ident(const char *name) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_IDENT;
    expr->as.ident = strdup(name);
    return expr;
}

Expr* expr_binary(Expr *left, BinaryOp op, Expr *right) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_BINARY;
    expr->as.binary.left = left;
    expr->as.binary.op = op;
    expr->as.binary.right = right;
    return expr;
}

Expr* expr_unary(UnaryOp op, Expr *operand) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_UNARY;
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    return expr;
}

Expr* expr_call(const char *name, Expr **args, int num_args) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_CALL;
    expr->as.call.name = strdup(name);
    expr->as.call.args = args;
    expr->as.call.num_args = num_args;
    return expr;
}

Expr* expr_assign(const char *name, Expr *value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_ASSIGN;
    expr->as.assign.name = strdup(name);
    expr->as.assign.value = value;
    return expr;
}

// ========== STATEMENT CONSTRUCTORS ==========

Stmt* stmt_let(const char *name, Expr *value) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_LET;
    stmt->as.let.name = strdup(name);
    stmt->as.let.value = value;
    return stmt;
}

Stmt* stmt_if(Expr *condition, Stmt *then_branch, Stmt *else_branch) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_IF;
    stmt->as.if_stmt.condition = condition;
    stmt->as.if_stmt.then_branch = then_branch;
    stmt->as.if_stmt.else_branch = else_branch;
    return stmt;
}

Stmt* stmt_while(Expr *condition, Stmt *body) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_WHILE;
    stmt->as.while_stmt.condition = condition;
    stmt->as.while_stmt.body = body;
    return stmt;
}

Stmt* stmt_block(Stmt **statements, int count) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_BLOCK;
    stmt->as.block.statements = statements;
    stmt->as.block.count = count;
    return stmt;
}

Stmt* stmt_expr(Expr *expr) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_EXPR;
    stmt->as.expr = expr;
    return stmt;
}

// ========== CLEANUP ==========

void expr_free(Expr *expr) {
    if (!expr) return;
    
    switch (expr->type) {
        case EXPR_IDENT:
            free(expr->as.ident);
            break;
        case EXPR_STRING:
            free(expr->as.string);
            break;
        case EXPR_BINARY:
            expr_free(expr->as.binary.left);
            expr_free(expr->as.binary.right);
            break;
        case EXPR_UNARY:
            expr_free(expr->as.unary.operand);
            break;
        case EXPR_CALL:
            free(expr->as.call.name);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                expr_free(expr->as.call.args[i]);
            }
            free(expr->as.call.args);
            break;
        case EXPR_ASSIGN:
            free(expr->as.assign.name);
            expr_free(expr->as.assign.value);
            break;
        case EXPR_NUMBER:
        case EXPR_BOOL:
            // Nothing to free
            break;
    }
    
    free(expr);
}

void stmt_free(Stmt *stmt) {
    if (!stmt) return;
    
    switch (stmt->type) {
        case STMT_LET:
            free(stmt->as.let.name);
            expr_free(stmt->as.let.value);
            break;
        case STMT_EXPR:
            expr_free(stmt->as.expr);
            break;
        case STMT_IF:
            expr_free(stmt->as.if_stmt.condition);
            stmt_free(stmt->as.if_stmt.then_branch);
            stmt_free(stmt->as.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            expr_free(stmt->as.while_stmt.condition);
            stmt_free(stmt->as.while_stmt.body);
            break;
        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                stmt_free(stmt->as.block.statements[i]);
            }
            free(stmt->as.block.statements);
            break;
    }
    
    free(stmt);
}