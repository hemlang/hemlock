#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ========== STRING OPERATIONS ==========

void string_free(String *str) {
    if (str) {
        free(str->data);
        free(str);
    }
}

String* string_new(const char *cstr) {
    int len = strlen(cstr);
    String *str = malloc(sizeof(String));
    str->length = len;
    str->capacity = len + 1;
    str->data = malloc(str->capacity);
    memcpy(str->data, cstr, len);
    str->data[len] = '\0';
    return str;
}

String* string_copy(String *str) {
    String *copy = malloc(sizeof(String));
    copy->length = str->length;
    copy->capacity = str->capacity;
    copy->data = malloc(copy->capacity);
    memcpy(copy->data, str->data, str->length + 1);
    return copy;
}

String* string_concat(String *a, String *b) {
    int new_len = a->length + b->length;
    String *result = malloc(sizeof(String));
    result->length = new_len;
    result->capacity = new_len + 1;
    result->data = malloc(result->capacity);
    
    memcpy(result->data, a->data, a->length);
    memcpy(result->data + a->length, b->data, b->length);
    result->data[new_len] = '\0';
    
    return result;
}

Value val_string(const char *str) {
    Value v;
    v.type = VAL_STRING;
    v.as.as_string = string_new(str);
    return v;
}

Value val_string_take(char *data, int length, int capacity) {
    Value v;
    v.type = VAL_STRING;
    String *str = malloc(sizeof(String));
    str->data = data;
    str->length = length;
    str->capacity = capacity;
    v.as.as_string = str;
    return v;
}

// ========== VALUE OPERATIONS ==========

Value val_int(int value) {
    Value v;
    v.type = VAL_INT;
    v.as.as_int = value;
    return v;
}

Value val_float(double value) {
    Value v;
    v.type = VAL_FLOAT;
    v.as.as_float = value;
    return v;
}

Value val_bool(int value) {
    Value v;
    v.type = VAL_BOOL;
    v.as.as_bool = value ? 1 : 0;
    return v;
}

Value val_null(void) {
    Value v;
    v.type = VAL_NULL;
    return v;
}

void print_value(Value val) {
    switch (val.type) {
        case VAL_INT:
            printf("%d", val.as.as_int);
            break;
        case VAL_FLOAT:
            printf("%g", val.as.as_float);  // %g removes trailing zeros
            break;
        case VAL_BOOL:
            printf(val.as.as_bool ? "true" : "false");
            break;
        case VAL_STRING:
            printf("%s", val.as.as_string->data);
            break;
        case VAL_NULL:
            printf("null");
            break;
    }
}

// ========== ENVIRONMENT ==========

Environment* env_new(Environment *parent) {
    Environment *env = malloc(sizeof(Environment));
    env->capacity = 16;
    env->count = 0;
    env->names = malloc(sizeof(char*) * env->capacity);
    env->values = malloc(sizeof(Value) * env->capacity);
    env->parent = parent;
    return env;
}

void env_free(Environment *env) {
    for (int i = 0; i < env->count; i++) {
        free(env->names[i]);
    }
    free(env->names);
    free(env->values);
    free(env);
}

static void env_grow(Environment *env) {
    env->capacity *= 2;
    env->names = realloc(env->names, sizeof(char*) * env->capacity);
    env->values = realloc(env->values, sizeof(Value) * env->capacity);
}

void env_set(Environment *env, const char *name, Value value) {
    // Check if variable already exists - update it
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            env->values[i] = value;
            return;
        }
    }
    
    // New variable
    if (env->count >= env->capacity) {
        env_grow(env);
    }
    
    env->names[env->count] = strdup(name);
    env->values[env->count] = value;
    env->count++;
}

Value env_get(Environment *env, const char *name) {
    // Search current scope
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            return env->values[i];
        }
    }
    
    // Search parent scope
    if (env->parent != NULL) {
        return env_get(env->parent, name);
    }
    
    // Variable not found
    fprintf(stderr, "Runtime error: Undefined variable '%s'\n", name);
    exit(1);
}

// ========== EXPRESSION EVALUATION ==========

Value eval_expr(Expr *expr, Environment *env) {
    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return val_float(expr->as.number.float_value);
            } else {
                return val_int(expr->as.number.int_value);
            }
            break;

        case EXPR_BOOL:
            return val_bool(expr->as.boolean);

        case EXPR_STRING:
            return val_string(expr->as.string);

        case EXPR_UNARY: {
            Value operand = eval_expr(expr->as.unary.operand, env);
            
            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    if (operand.type == VAL_BOOL) {
                        return val_bool(!operand.as.as_bool);
                    } else if (operand.type == VAL_INT) {
                        return val_bool(operand.as.as_int == 0);
                    }
                    break;
                    
                case UNARY_NEGATE:
                    if (operand.type == VAL_INT) {
                        return val_int(-operand.as.as_int);
                    }
                    break;
            }
            break;
        }
            
        case EXPR_IDENT:
            return env_get(env, expr->as.ident);

        case EXPR_ASSIGN: {
            Value value = eval_expr(expr->as.assign.value, env);
            env_set(env, expr->as.assign.name, value);
            return value;
        }
           
        case EXPR_BINARY: {
            // For && and ||, short-circuit evaluation
            if (expr->as.binary.op == OP_AND) {
                Value left = eval_expr(expr->as.binary.left, env);
                int left_bool = (left.type == VAL_BOOL) ? left.as.as_bool : (left.as.as_int != 0);
                if (!left_bool) return val_bool(0);
                
                Value right = eval_expr(expr->as.binary.right, env);
                int right_bool = (right.type == VAL_BOOL) ? right.as.as_bool : (right.as.as_int != 0);
                return val_bool(right_bool);
            }
            
            if (expr->as.binary.op == OP_OR) {
                Value left = eval_expr(expr->as.binary.left, env);
                int left_bool = (left.type == VAL_BOOL) ? left.as.as_bool : (left.as.as_int != 0);
                if (left_bool) return val_bool(1);
                
                Value right = eval_expr(expr->as.binary.right, env);
                int right_bool = (right.type == VAL_BOOL) ? right.as.as_bool : (right.as.as_int != 0);
                return val_bool(right_bool);
            }

            // Handle string concatenation
            if (expr->as.binary.op == OP_ADD) {
                Value left = eval_expr(expr->as.binary.left, env);
                Value right = eval_expr(expr->as.binary.right, env);

                if (left.type == VAL_STRING && right.type == VAL_STRING) {
                    String *result = string_concat(left.as.as_string, right.as.as_string);
                    return (Value){ .type = VAL_STRING, .as.as_string = result };
                }
                // TODO: throw error if types are incompatible
            }

            // Regular binary operations:
            Value left = eval_expr(expr->as.binary.left, env);
            Value right = eval_expr(expr->as.binary.right, env);
            
            // Type check for arithmetic
            if (left.type != VAL_INT || right.type != VAL_INT) {
                fprintf(stderr, "Runtime error: Binary operation requires integers\n");
                exit(1);
            }

            int l = left.as.as_int;
            int r = right.as.as_int;
            
            switch (expr->as.binary.op) {
                case OP_ADD: return val_int(l + r);
                case OP_SUB: return val_int(l - r);
                case OP_MUL: return val_int(l * r);
                case OP_DIV:
                    if (r == 0) {
                        fprintf(stderr, "Runtime error: Division by zero\n");
                        exit(1);
                    }
                    return val_int(l / r);
                    
                // Comparison operators
                case OP_EQUAL: return val_bool(l == r);
                case OP_NOT_EQUAL: return val_bool(l != r);
                case OP_LESS: return val_bool(l < r);
                case OP_LESS_EQUAL: return val_bool(l <= r);
                case OP_GREATER: return val_bool(l > r);
                case OP_GREATER_EQUAL: return val_bool(l >= r);
                case OP_AND: // Handled above
                case OP_OR:  // Handled above
                    break;
            }
            break;
        }
            
        case EXPR_CALL: {
            if (strcmp(expr->as.call.name, "print") == 0) {
                if (expr->as.call.num_args != 1) {
                    fprintf(stderr, "Runtime error: print() expects 1 argument\n");
                    exit(1);
                }
                
                Value arg = eval_expr(expr->as.call.args[0], env);
                print_value(arg);
                printf("\n");
                return val_null();
            }
            
            fprintf(stderr, "Runtime error: Unknown function '%s'\n", expr->as.call.name);
            exit(1);
        }
    }
    
    return val_null();
}

// ========== STATEMENT EVALUATION ==========

void eval_stmt(Stmt *stmt, Environment *env) {
    switch (stmt->type) {
        case STMT_LET: {
            Value value = eval_expr(stmt->as.let.value, env);
            env_set(env, stmt->as.let.name, value);
            break;
        }
            
        case STMT_EXPR: {
            eval_expr(stmt->as.expr, env);
            break;
        }
            
        case STMT_IF: {
            Value condition = eval_expr(stmt->as.if_stmt.condition, env);
            
            // Check if condition is truthy
            int is_true = 0;
            if (condition.type == VAL_BOOL) {
                is_true = condition.as.as_bool;
            } else if (condition.type == VAL_INT) {
                is_true = condition.as.as_int != 0;
            }
            
            if (is_true) {
                eval_stmt(stmt->as.if_stmt.then_branch, env);
            } else if (stmt->as.if_stmt.else_branch != NULL) {
                eval_stmt(stmt->as.if_stmt.else_branch, env);
            }
            break;
        }
            
        case STMT_WHILE: {
            for (;;) {
                Value condition = eval_expr(stmt->as.while_stmt.condition, env);
                
                int is_true = 0;
                if (condition.type == VAL_BOOL) {
                    is_true = condition.as.as_bool;
                } else if (condition.type == VAL_INT) {
                    is_true = condition.as.as_int != 0;
                }
                
                if (!is_true) break;
                
                eval_stmt(stmt->as.while_stmt.body, env);
            }
            break;
        }
            
        case STMT_BLOCK: {
            for (int i = 0; i < stmt->as.block.count; i++) {
                eval_stmt(stmt->as.block.statements[i], env);
            }
            break;
        }
    }
}

void eval_program(Stmt **stmts, int count, Environment *env) {
    for (int i = 0; i < count; i++) {
        eval_stmt(stmts[i], env);
    }
}