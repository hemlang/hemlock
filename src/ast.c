#include "ast.h"
#include <stdlib.h>
#include <string.h>

// ========== EXPRESSION CONSTRUCTORS ==========

Expr* expr_number_int(int value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NUMBER;
    expr->as.number.int_value = value;
    expr->as.number.is_float = 0;
    return expr;
}

Expr* expr_number_float(double value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NUMBER;
    expr->as.number.float_value = value;
    expr->as.number.is_float = 1;
    return expr;
}

Expr* expr_number(int value) {
    return expr_number_int(value);
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

Expr* expr_null(void) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NULL;
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

Expr* expr_call(Expr *func, Expr **args, int num_args) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_CALL;
    expr->as.call.func = func;
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

Expr* expr_get_property(Expr *object, const char *property) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_GET_PROPERTY;
    expr->as.get_property.object = object;
    expr->as.get_property.property = strdup(property);
    return expr;
}

Expr* expr_set_property(Expr *object, const char *property, Expr *value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_SET_PROPERTY;
    expr->as.set_property.object = object;
    expr->as.set_property.property = strdup(property);
    expr->as.set_property.value = value;
    return expr;
}

Expr* expr_index(Expr *object, Expr *index) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_INDEX;
    expr->as.index.object = object;
    expr->as.index.index = index;
    return expr;
}

Expr* expr_index_assign(Expr *object, Expr *index, Expr *value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_INDEX_ASSIGN;
    expr->as.index_assign.object = object;
    expr->as.index_assign.index = index;
    expr->as.index_assign.value = value;
    return expr;
}

Expr* expr_function(char **param_names, Type **param_types, int num_params, Type *return_type, Stmt *body) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_FUNCTION;
    expr->as.function.param_names = param_names;
    expr->as.function.param_types = param_types;
    expr->as.function.num_params = num_params;
    expr->as.function.return_type = return_type;
    expr->as.function.body = body;
    return expr;
}

Expr* expr_object_literal(char **field_names, Expr **field_values, int num_fields) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_OBJECT_LITERAL;
    expr->as.object_literal.field_names = field_names;
    expr->as.object_literal.field_values = field_values;
    expr->as.object_literal.num_fields = num_fields;
    return expr;
}

// ========== TYPE CONSTRUCTORS ==========

Type* type_new(TypeKind kind) {
    Type *type = malloc(sizeof(Type));
    type->kind = kind;
    type->type_name = NULL;
    return type;
}

void type_free(Type *type) {
    if (type) {
        if (type->type_name) {
            free(type->type_name);
        }
        free(type);
    }
}

// ========== STATEMENT CONSTRUCTORS ==========

Stmt* stmt_let_typed(const char *name, Type *type_annotation, Expr *value) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_LET;
    stmt->as.let.name = strdup(name);
    stmt->as.let.type_annotation = type_annotation;  // Can be NULL
    stmt->as.let.value = value;
    return stmt;
}

Stmt* stmt_let(const char *name, Expr *value) {
    return stmt_let_typed(name, NULL, value);
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

Stmt* stmt_return(Expr *value) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_RETURN;
    stmt->as.return_stmt.value = value;
    return stmt;
}

Stmt* stmt_define_object(const char *name, char **field_names, Type **field_types,
                         int *field_optional, Expr **field_defaults, int num_fields) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_DEFINE_OBJECT;
    stmt->as.define_object.name = strdup(name);
    stmt->as.define_object.field_names = field_names;
    stmt->as.define_object.field_types = field_types;
    stmt->as.define_object.field_optional = field_optional;
    stmt->as.define_object.field_defaults = field_defaults;
    stmt->as.define_object.num_fields = num_fields;
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
            expr_free(expr->as.call.func);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                expr_free(expr->as.call.args[i]);
            }
            free(expr->as.call.args);
            break;
        case EXPR_ASSIGN:
            free(expr->as.assign.name);
            expr_free(expr->as.assign.value);
            break;
        case EXPR_GET_PROPERTY:
            expr_free(expr->as.get_property.object);
            free(expr->as.get_property.property);
            break;
        case EXPR_SET_PROPERTY:
            expr_free(expr->as.set_property.object);
            free(expr->as.set_property.property);
            expr_free(expr->as.set_property.value);
            break;
        case EXPR_INDEX:
            expr_free(expr->as.index.object);
            expr_free(expr->as.index.index);
            break;
        case EXPR_INDEX_ASSIGN:
            expr_free(expr->as.index_assign.object);
            expr_free(expr->as.index_assign.index);
            expr_free(expr->as.index_assign.value);
            break;
        case EXPR_FUNCTION:
            // Free parameter names
            for (int i = 0; i < expr->as.function.num_params; i++) {
                free(expr->as.function.param_names[i]);
                if (expr->as.function.param_types[i]) {
                    type_free(expr->as.function.param_types[i]);
                }
            }
            free(expr->as.function.param_names);
            free(expr->as.function.param_types);
            // Free return type
            if (expr->as.function.return_type) {
                type_free(expr->as.function.return_type);
            }
            // Free body
            stmt_free(expr->as.function.body);
            break;
        case EXPR_OBJECT_LITERAL:
            // Free field names and values
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                free(expr->as.object_literal.field_names[i]);
                expr_free(expr->as.object_literal.field_values[i]);
            }
            free(expr->as.object_literal.field_names);
            free(expr->as.object_literal.field_values);
            break;
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_NULL:
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
            type_free(stmt->as.let.type_annotation);
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
        case STMT_RETURN:
            expr_free(stmt->as.return_stmt.value);
            break;
        case STMT_DEFINE_OBJECT:
            free(stmt->as.define_object.name);
            for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                free(stmt->as.define_object.field_names[i]);
                if (stmt->as.define_object.field_types[i]) {
                    type_free(stmt->as.define_object.field_types[i]);
                }
                if (stmt->as.define_object.field_defaults[i]) {
                    expr_free(stmt->as.define_object.field_defaults[i]);
                }
            }
            free(stmt->as.define_object.field_names);
            free(stmt->as.define_object.field_types);
            free(stmt->as.define_object.field_optional);
            free(stmt->as.define_object.field_defaults);
            break;
    }

    free(stmt);
}