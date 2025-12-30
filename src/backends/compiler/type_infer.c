/*
 * Type Inference Implementation
 */

#include "type_infer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ========== TYPE CONSTRUCTORS ==========

InferredType infer_unknown(void) {
    return (InferredType){ .kind = INFER_UNKNOWN, .element_type = NULL };
}

InferredType infer_i32(void) {
    return (InferredType){ .kind = INFER_I32, .element_type = NULL };
}

InferredType infer_i64(void) {
    return (InferredType){ .kind = INFER_I64, .element_type = NULL };
}

InferredType infer_f64(void) {
    return (InferredType){ .kind = INFER_F64, .element_type = NULL };
}

InferredType infer_bool(void) {
    return (InferredType){ .kind = INFER_BOOL, .element_type = NULL };
}

InferredType infer_string(void) {
    return (InferredType){ .kind = INFER_STRING, .element_type = NULL };
}

InferredType infer_null(void) {
    return (InferredType){ .kind = INFER_NULL, .element_type = NULL };
}

InferredType infer_numeric(void) {
    return (InferredType){ .kind = INFER_NUMERIC, .element_type = NULL };
}

InferredType infer_integer(void) {
    return (InferredType){ .kind = INFER_INTEGER, .element_type = NULL };
}

// ========== TYPE OPERATIONS ==========

int infer_is_known(InferredType t) {
    return t.kind != INFER_UNKNOWN;
}

int infer_is_i32(InferredType t) {
    return t.kind == INFER_I32;
}

int infer_is_i64(InferredType t) {
    return t.kind == INFER_I64;
}

int infer_is_f64(InferredType t) {
    return t.kind == INFER_F64;
}

int infer_is_bool(InferredType t) {
    return t.kind == INFER_BOOL;
}

int infer_is_integer(InferredType t) {
    return t.kind == INFER_I32 || t.kind == INFER_I64 || t.kind == INFER_INTEGER;
}

int infer_is_numeric(InferredType t) {
    return t.kind == INFER_I32 || t.kind == INFER_I64 || t.kind == INFER_F64 ||
           t.kind == INFER_NUMERIC || t.kind == INFER_INTEGER;
}

// Meet: find common type (for merging control flow paths)
InferredType infer_meet(InferredType a, InferredType b) {
    // Same type -> keep it
    if (a.kind == b.kind) return a;

    // Unknown dominates
    if (a.kind == INFER_UNKNOWN) return a;
    if (b.kind == INFER_UNKNOWN) return b;

    // Both integers -> INTEGER
    if (infer_is_integer(a) && infer_is_integer(b)) {
        if (a.kind == INFER_I32 && b.kind == INFER_I32) return infer_i32();
        if (a.kind == INFER_I64 && b.kind == INFER_I64) return infer_i64();
        return infer_integer();
    }

    // Both numeric -> NUMERIC
    if (infer_is_numeric(a) && infer_is_numeric(b)) {
        return infer_numeric();
    }

    // Otherwise unknown
    return infer_unknown();
}

// Result type of binary operation
InferredType infer_binary_result(BinaryOp op, InferredType left, InferredType right) {
    switch (op) {
        // Arithmetic: result type depends on operands
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
            if (infer_is_f64(left) || infer_is_f64(right)) return infer_f64();
            if (infer_is_i64(left) || infer_is_i64(right)) return infer_i64();
            if (infer_is_i32(left) && infer_is_i32(right)) return infer_i32();
            if (infer_is_integer(left) && infer_is_integer(right)) return infer_integer();
            if (infer_is_numeric(left) && infer_is_numeric(right)) return infer_numeric();
            // String + anything = string
            if (op == OP_ADD && (left.kind == INFER_STRING || right.kind == INFER_STRING)) {
                return infer_string();
            }
            return infer_unknown();

        case OP_DIV:
            // Division always returns f64 in Hemlock
            return infer_f64();

        case OP_MOD:
            // Modulo: like other arithmetic but stays integer
            if (infer_is_i64(left) || infer_is_i64(right)) return infer_i64();
            if (infer_is_i32(left) && infer_is_i32(right)) return infer_i32();
            if (infer_is_integer(left) && infer_is_integer(right)) return infer_integer();
            return infer_numeric();

        // Comparison: always bool
        case OP_EQUAL:
        case OP_NOT_EQUAL:
        case OP_LESS:
        case OP_LESS_EQUAL:
        case OP_GREATER:
        case OP_GREATER_EQUAL:
            return infer_bool();

        // Logical: always bool
        case OP_AND:
        case OP_OR:
            return infer_bool();

        // Bitwise: result is integer type
        case OP_BIT_AND:
        case OP_BIT_OR:
        case OP_BIT_XOR:
        case OP_BIT_LSHIFT:
        case OP_BIT_RSHIFT:
            if (infer_is_i64(left) || infer_is_i64(right)) return infer_i64();
            if (infer_is_i32(left) && infer_is_i32(right)) return infer_i32();
            return infer_integer();

        default:
            return infer_unknown();
    }
}

// Result type of unary operation
InferredType infer_unary_result(UnaryOp op, InferredType operand) {
    switch (op) {
        case UNARY_NEGATE:
            // Negation preserves numeric type
            return operand;

        case UNARY_NOT:
            // Logical not always produces bool
            return infer_bool();

        case UNARY_BIT_NOT:
            // Bitwise not preserves integer type
            return operand;

        default:
            return infer_unknown();
    }
}

// ========== ENVIRONMENT OPERATIONS ==========

TypeInferContext* type_infer_new(void) {
    TypeInferContext *ctx = malloc(sizeof(TypeInferContext));
    ctx->current_env = malloc(sizeof(TypeEnv));
    ctx->current_env->bindings = NULL;
    ctx->current_env->parent = NULL;
    ctx->func_returns = NULL;
    ctx->changed = 0;
    ctx->unboxable_vars = NULL;
    return ctx;
}

static void free_bindings(TypeBinding *b) {
    while (b) {
        TypeBinding *next = b->next;
        free(b->name);
        if (b->type.element_type) free(b->type.element_type);
        free(b);
        b = next;
    }
}

void type_infer_free(TypeInferContext *ctx) {
    while (ctx->current_env) {
        TypeEnv *parent = ctx->current_env->parent;
        free_bindings(ctx->current_env->bindings);
        free(ctx->current_env);
        ctx->current_env = parent;
    }
    // Free function return type registry
    FuncReturnType *f = ctx->func_returns;
    while (f) {
        FuncReturnType *next = f->next;
        free(f->name);
        free(f);
        f = next;
    }
    // Free unboxable variables list
    UnboxableVar *u = ctx->unboxable_vars;
    while (u) {
        UnboxableVar *next = u->next;
        free(u->name);
        free(u);
        u = next;
    }
    free(ctx);
}

void type_env_push(TypeInferContext *ctx) {
    TypeEnv *env = malloc(sizeof(TypeEnv));
    env->bindings = NULL;
    env->parent = ctx->current_env;
    ctx->current_env = env;
}

void type_env_pop(TypeInferContext *ctx) {
    TypeEnv *old = ctx->current_env;
    ctx->current_env = old->parent;
    free_bindings(old->bindings);
    free(old);
}

void type_env_bind(TypeInferContext *ctx, const char *name, InferredType type) {
    TypeBinding *b = malloc(sizeof(TypeBinding));
    b->name = strdup(name);
    b->type = type;
    b->next = ctx->current_env->bindings;
    ctx->current_env->bindings = b;
}

InferredType type_env_lookup(TypeInferContext *ctx, const char *name) {
    for (TypeEnv *env = ctx->current_env; env; env = env->parent) {
        for (TypeBinding *b = env->bindings; b; b = b->next) {
            if (strcmp(b->name, name) == 0) {
                return b->type;
            }
        }
    }
    return infer_unknown();
}

void type_env_refine(TypeInferContext *ctx, const char *name, InferredType type) {
    // Find and update existing binding if new type is more specific
    for (TypeEnv *env = ctx->current_env; env; env = env->parent) {
        for (TypeBinding *b = env->bindings; b; b = b->next) {
            if (strcmp(b->name, name) == 0) {
                // Only refine if going from less specific to more specific
                if (b->type.kind == INFER_UNKNOWN && type.kind != INFER_UNKNOWN) {
                    b->type = type;
                    ctx->changed = 1;
                } else if (b->type.kind == INFER_NUMERIC && infer_is_integer(type)) {
                    b->type = type;
                    ctx->changed = 1;
                } else if (b->type.kind == INFER_INTEGER &&
                           (type.kind == INFER_I32 || type.kind == INFER_I64)) {
                    b->type = type;
                    ctx->changed = 1;
                }
                return;
            }
        }
    }
}

// ========== FUNCTION RETURN TYPE TRACKING ==========

void type_register_func_return(TypeInferContext *ctx, const char *name, InferredType ret_type) {
    // Check if already registered
    for (FuncReturnType *f = ctx->func_returns; f; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            f->return_type = ret_type;  // Update existing
            return;
        }
    }
    // Add new entry
    FuncReturnType *f = malloc(sizeof(FuncReturnType));
    f->name = strdup(name);
    f->return_type = ret_type;
    f->next = ctx->func_returns;
    ctx->func_returns = f;
}

InferredType type_lookup_func_return(TypeInferContext *ctx, const char *name) {
    for (FuncReturnType *f = ctx->func_returns; f; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            return f->return_type;
        }
    }
    return infer_unknown();
}

// ========== INFERENCE ==========

InferredType infer_expr(TypeInferContext *ctx, Expr *expr) {
    if (!expr) return infer_unknown();

    switch (expr->type) {
        case EXPR_NUMBER:
            // Check if it's a float
            if (expr->as.number.is_float) {
                return infer_f64();
            }
            // Check if value fits in i32
            if (expr->as.number.int_value >= -2147483648LL &&
                expr->as.number.int_value <= 2147483647LL) {
                return infer_i32();
            }
            return infer_i64();

        case EXPR_BOOL:
            return infer_bool();

        case EXPR_STRING:
        case EXPR_STRING_INTERPOLATION:
            return infer_string();

        case EXPR_NULL:
            return infer_null();

        case EXPR_IDENT:
            return type_env_lookup(ctx, expr->as.ident.name);

        case EXPR_BINARY: {
            InferredType left = infer_expr(ctx, expr->as.binary.left);
            InferredType right = infer_expr(ctx, expr->as.binary.right);
            return infer_binary_result(expr->as.binary.op, left, right);
        }

        case EXPR_UNARY: {
            InferredType operand = infer_expr(ctx, expr->as.unary.operand);
            return infer_unary_result(expr->as.unary.op, operand);
        }

        case EXPR_ASSIGN: {
            InferredType value = infer_expr(ctx, expr->as.assign.value);
            // Refine the variable's type
            type_env_refine(ctx, expr->as.assign.name, value);
            return value;
        }

        case EXPR_TERNARY: {
            InferredType then_t = infer_expr(ctx, expr->as.ternary.true_expr);
            InferredType else_t = infer_expr(ctx, expr->as.ternary.false_expr);
            return infer_meet(then_t, else_t);
        }

        case EXPR_CALL:
            // Look up function return type if it's a direct call to a known function
            if (expr->as.call.func && expr->as.call.func->type == EXPR_IDENT) {
                return type_lookup_func_return(ctx, expr->as.call.func->as.ident.name);
            }
            return infer_unknown();

        case EXPR_ARRAY_LITERAL:
            return (InferredType){ .kind = INFER_ARRAY, .element_type = NULL };

        case EXPR_OBJECT_LITERAL:
            return (InferredType){ .kind = INFER_OBJECT, .element_type = NULL };

        case EXPR_FUNCTION:
            return (InferredType){ .kind = INFER_FUNCTION, .element_type = NULL };

        case EXPR_INDEX:
            // Array indexing - would need element type tracking
            return infer_unknown();

        case EXPR_GET_PROPERTY:
            // Object property access - would need object type tracking
            return infer_unknown();

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            return infer_expr(ctx, expr->as.prefix_inc.operand);

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return infer_expr(ctx, expr->as.postfix_inc.operand);

        case EXPR_RUNE:
            // Rune is an integer type (unicode codepoint)
            return infer_i32();

        case EXPR_AWAIT:
            // Await returns unknown (depends on task return type)
            return infer_unknown();

        case EXPR_NULL_COALESCE: {
            InferredType left = infer_expr(ctx, expr->as.null_coalesce.left);
            InferredType right = infer_expr(ctx, expr->as.null_coalesce.right);
            // If left is null, return right's type, otherwise unknown
            if (left.kind == INFER_NULL) return right;
            return infer_meet(left, right);
        }

        default:
            return infer_unknown();
    }
}

void infer_stmt(TypeInferContext *ctx, Stmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_LET: {
            InferredType init_type = infer_unknown();
            if (stmt->as.let.value) {
                init_type = infer_expr(ctx, stmt->as.let.value);
            }
            // If there's a type annotation, use it
            if (stmt->as.let.type_annotation) {
                switch (stmt->as.let.type_annotation->kind) {
                    case TYPE_I32: init_type = infer_i32(); break;
                    case TYPE_I64: init_type = infer_i64(); break;
                    case TYPE_F32:
                    case TYPE_F64: init_type = infer_f64(); break;
                    case TYPE_BOOL: init_type = infer_bool(); break;
                    case TYPE_STRING: init_type = infer_string(); break;
                    default: break;
                }
            }
            type_env_bind(ctx, stmt->as.let.name, init_type);
            break;
        }

        case STMT_CONST: {
            InferredType init_type = infer_unknown();
            if (stmt->as.const_stmt.value) {
                init_type = infer_expr(ctx, stmt->as.const_stmt.value);
            }
            if (stmt->as.const_stmt.type_annotation) {
                switch (stmt->as.const_stmt.type_annotation->kind) {
                    case TYPE_I32: init_type = infer_i32(); break;
                    case TYPE_I64: init_type = infer_i64(); break;
                    case TYPE_F32:
                    case TYPE_F64: init_type = infer_f64(); break;
                    case TYPE_BOOL: init_type = infer_bool(); break;
                    case TYPE_STRING: init_type = infer_string(); break;
                    default: break;
                }
            }
            type_env_bind(ctx, stmt->as.const_stmt.name, init_type);
            break;
        }

        case STMT_BLOCK: {
            type_env_push(ctx);
            for (int i = 0; i < stmt->as.block.count; i++) {
                infer_stmt(ctx, stmt->as.block.statements[i]);
            }
            type_env_pop(ctx);
            break;
        }

        case STMT_IF: {
            infer_expr(ctx, stmt->as.if_stmt.condition);
            infer_stmt(ctx, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                infer_stmt(ctx, stmt->as.if_stmt.else_branch);
            }
            break;
        }

        case STMT_WHILE: {
            infer_expr(ctx, stmt->as.while_stmt.condition);
            infer_stmt(ctx, stmt->as.while_stmt.body);
            break;
        }

        case STMT_FOR: {
            type_env_push(ctx);
            if (stmt->as.for_loop.initializer) {
                infer_stmt(ctx, stmt->as.for_loop.initializer);
            }
            if (stmt->as.for_loop.condition) {
                infer_expr(ctx, stmt->as.for_loop.condition);
            }
            if (stmt->as.for_loop.increment) {
                infer_expr(ctx, stmt->as.for_loop.increment);
            }
            infer_stmt(ctx, stmt->as.for_loop.body);
            type_env_pop(ctx);
            break;
        }

        case STMT_EXPR:
            infer_expr(ctx, stmt->as.expr);
            break;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                infer_expr(ctx, stmt->as.return_stmt.value);
            }
            break;

        default:
            break;
    }
}

void infer_function(TypeInferContext *ctx, Expr *func_expr) {
    if (!func_expr || func_expr->type != EXPR_FUNCTION) return;

    // Analyze function body in new scope
    type_env_push(ctx);
    for (int i = 0; i < func_expr->as.function.num_params; i++) {
        InferredType param_type = infer_unknown();
        if (func_expr->as.function.param_types && func_expr->as.function.param_types[i]) {
            switch (func_expr->as.function.param_types[i]->kind) {
                case TYPE_I32: param_type = infer_i32(); break;
                case TYPE_I64: param_type = infer_i64(); break;
                case TYPE_F32:
                case TYPE_F64: param_type = infer_f64(); break;
                case TYPE_BOOL: param_type = infer_bool(); break;
                case TYPE_STRING: param_type = infer_string(); break;
                default: break;
            }
        }
        type_env_bind(ctx, func_expr->as.function.param_names[i], param_type);
    }
    infer_stmt(ctx, func_expr->as.function.body);
    type_env_pop(ctx);
}

// ========== ESCAPE ANALYSIS & UNBOXING ==========

void type_mark_unboxable(TypeInferContext *ctx, const char *name,
                         InferredTypeKind native_type, int is_loop_counter, int is_accumulator) {
    // Check if already marked
    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            // Update if more specific
            if (native_type != INFER_UNKNOWN) {
                u->native_type = native_type;
            }
            u->is_loop_counter |= is_loop_counter;
            u->is_accumulator |= is_accumulator;
            return;
        }
    }
    // Add new entry
    UnboxableVar *u = malloc(sizeof(UnboxableVar));
    u->name = strdup(name);
    u->native_type = native_type;
    u->is_loop_counter = is_loop_counter;
    u->is_accumulator = is_accumulator;
    u->next = ctx->unboxable_vars;
    ctx->unboxable_vars = u;
}

InferredTypeKind type_get_unboxable(TypeInferContext *ctx, const char *name) {
    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->native_type;
        }
    }
    return INFER_UNKNOWN;
}

int type_is_loop_counter(TypeInferContext *ctx, const char *name) {
    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->is_loop_counter;
        }
    }
    return 0;
}

int type_is_accumulator(TypeInferContext *ctx, const char *name) {
    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->is_accumulator;
        }
    }
    return 0;
}

// Check if an expression only uses the variable in simple arithmetic
static int is_simple_increment(Expr *expr, const char *var_name) {
    if (!expr) return 0;

    // i = i + 1, i = i - 1, etc.
    if (expr->type == EXPR_ASSIGN && strcmp(expr->as.assign.name, var_name) == 0) {
        Expr *val = expr->as.assign.value;
        if (val->type == EXPR_BINARY) {
            // Check if left operand is the variable
            if (val->as.binary.left->type == EXPR_IDENT &&
                strcmp(val->as.binary.left->as.ident.name, var_name) == 0) {
                // Right operand should be a constant
                if (val->as.binary.right->type == EXPR_NUMBER &&
                    !val->as.binary.right->as.number.is_float) {
                    BinaryOp op = val->as.binary.op;
                    return op == OP_ADD || op == OP_SUB;
                }
            }
        }
    }

    // ++i, --i, i++, i--
    if (expr->type == EXPR_PREFIX_INC || expr->type == EXPR_PREFIX_DEC) {
        if (expr->as.prefix_inc.operand->type == EXPR_IDENT &&
            strcmp(expr->as.prefix_inc.operand->as.ident.name, var_name) == 0) {
            return 1;
        }
    }
    if (expr->type == EXPR_POSTFIX_INC || expr->type == EXPR_POSTFIX_DEC) {
        if (expr->as.postfix_inc.operand->type == EXPR_IDENT &&
            strcmp(expr->as.postfix_inc.operand->as.ident.name, var_name) == 0) {
            return 1;
        }
    }

    return 0;
}

// Check if a condition uses only simple comparisons on the variable
static int is_simple_comparison(Expr *expr, const char *var_name) {
    if (!expr || expr->type != EXPR_BINARY) return 0;

    BinaryOp op = expr->as.binary.op;
    if (op != OP_LESS && op != OP_LESS_EQUAL &&
        op != OP_GREATER && op != OP_GREATER_EQUAL &&
        op != OP_EQUAL && op != OP_NOT_EQUAL) {
        return 0;
    }

    // Check if one side is the variable and other is constant or known i32
    Expr *left = expr->as.binary.left;
    Expr *right = expr->as.binary.right;

    int left_is_var = (left->type == EXPR_IDENT && strcmp(left->as.ident.name, var_name) == 0);
    int right_is_var = (right->type == EXPR_IDENT && strcmp(right->as.ident.name, var_name) == 0);

    if (left_is_var) {
        // Right should be constant or another variable (length, etc.)
        return right->type == EXPR_NUMBER ||
               right->type == EXPR_IDENT ||
               right->type == EXPR_GET_PROPERTY;  // e.g., arr.length
    }
    if (right_is_var) {
        return left->type == EXPR_NUMBER ||
               left->type == EXPR_IDENT ||
               left->type == EXPR_GET_PROPERTY;
    }

    return 0;
}

// Check if the variable escapes (used in a way that requires HmlValue)
static int variable_escapes_in_expr(Expr *expr, const char *var_name);
static int variable_escapes_in_stmt(Stmt *stmt, const char *var_name);

static int variable_escapes_in_expr(Expr *expr, const char *var_name) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_IDENT:
            // Variable usage itself doesn't escape - we're checking how it's used
            return 0;

        case EXPR_CALL:
            // If variable is passed to a function, it escapes
            for (int i = 0; i < expr->as.call.num_args; i++) {
                Expr *arg = expr->as.call.args[i];
                if (arg->type == EXPR_IDENT && strcmp(arg->as.ident.name, var_name) == 0) {
                    return 1;  // Variable passed to function - escapes
                }
                if (variable_escapes_in_expr(arg, var_name)) return 1;
            }
            return variable_escapes_in_expr(expr->as.call.func, var_name);

        case EXPR_BINARY:
            // Binary operations on the variable are fine (for integer ops)
            return variable_escapes_in_expr(expr->as.binary.left, var_name) ||
                   variable_escapes_in_expr(expr->as.binary.right, var_name);

        case EXPR_UNARY:
            return variable_escapes_in_expr(expr->as.unary.operand, var_name);

        case EXPR_ASSIGN:
            // Assigning the variable is fine
            return variable_escapes_in_expr(expr->as.assign.value, var_name);

        case EXPR_INDEX:
            // Using variable as array index is fine
            // But if the array result contains the variable, it escapes
            if (expr->as.index.object->type == EXPR_IDENT &&
                strcmp(expr->as.index.object->as.ident.name, var_name) == 0) {
                return 1;  // Using variable as array (not index) - escapes
            }
            return variable_escapes_in_expr(expr->as.index.index, var_name);

        case EXPR_INDEX_ASSIGN:
            // Storing variable in array - it escapes
            if (expr->as.index_assign.value->type == EXPR_IDENT &&
                strcmp(expr->as.index_assign.value->as.ident.name, var_name) == 0) {
                return 1;
            }
            return variable_escapes_in_expr(expr->as.index_assign.object, var_name) ||
                   variable_escapes_in_expr(expr->as.index_assign.index, var_name) ||
                   variable_escapes_in_expr(expr->as.index_assign.value, var_name);

        case EXPR_ARRAY_LITERAL:
            // If variable is in array literal, it escapes
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                Expr *elem = expr->as.array_literal.elements[i];
                if (elem->type == EXPR_IDENT && strcmp(elem->as.ident.name, var_name) == 0) {
                    return 1;
                }
                if (variable_escapes_in_expr(elem, var_name)) return 1;
            }
            return 0;

        case EXPR_OBJECT_LITERAL:
            // If variable is in object literal, it escapes
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                Expr *val = expr->as.object_literal.field_values[i];
                if (val->type == EXPR_IDENT && strcmp(val->as.ident.name, var_name) == 0) {
                    return 1;
                }
                if (variable_escapes_in_expr(val, var_name)) return 1;
            }
            return 0;

        case EXPR_TERNARY:
            return variable_escapes_in_expr(expr->as.ternary.condition, var_name) ||
                   variable_escapes_in_expr(expr->as.ternary.true_expr, var_name) ||
                   variable_escapes_in_expr(expr->as.ternary.false_expr, var_name);

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            return variable_escapes_in_expr(expr->as.prefix_inc.operand, var_name);

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return variable_escapes_in_expr(expr->as.postfix_inc.operand, var_name);

        case EXPR_FUNCTION:
            // If variable is captured by closure, it escapes
            // (Simplified: we'd need full closure analysis here)
            return 1;  // Conservative: assume function captures variable

        default:
            return 0;
    }
}

static int variable_escapes_in_stmt(Stmt *stmt, const char *var_name) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case STMT_EXPR:
            return variable_escapes_in_expr(stmt->as.expr, var_name);

        case STMT_LET:
        case STMT_CONST:
            if (stmt->as.let.value) {
                // If variable is used as initializer, check if it escapes
                return variable_escapes_in_expr(stmt->as.let.value, var_name);
            }
            return 0;

        case STMT_RETURN:
            // Returning the variable - it escapes
            if (stmt->as.return_stmt.value) {
                if (stmt->as.return_stmt.value->type == EXPR_IDENT &&
                    strcmp(stmt->as.return_stmt.value->as.ident.name, var_name) == 0) {
                    return 1;
                }
                return variable_escapes_in_expr(stmt->as.return_stmt.value, var_name);
            }
            return 0;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (variable_escapes_in_stmt(stmt->as.block.statements[i], var_name)) {
                    return 1;
                }
            }
            return 0;

        case STMT_IF:
            return variable_escapes_in_expr(stmt->as.if_stmt.condition, var_name) ||
                   variable_escapes_in_stmt(stmt->as.if_stmt.then_branch, var_name) ||
                   (stmt->as.if_stmt.else_branch && variable_escapes_in_stmt(stmt->as.if_stmt.else_branch, var_name));

        case STMT_WHILE:
            return variable_escapes_in_expr(stmt->as.while_stmt.condition, var_name) ||
                   variable_escapes_in_stmt(stmt->as.while_stmt.body, var_name);

        case STMT_FOR:
            return (stmt->as.for_loop.initializer && variable_escapes_in_stmt(stmt->as.for_loop.initializer, var_name)) ||
                   (stmt->as.for_loop.condition && variable_escapes_in_expr(stmt->as.for_loop.condition, var_name)) ||
                   (stmt->as.for_loop.increment && variable_escapes_in_expr(stmt->as.for_loop.increment, var_name)) ||
                   variable_escapes_in_stmt(stmt->as.for_loop.body, var_name);

        default:
            return 0;
    }
}

void type_analyze_for_loop(TypeInferContext *ctx, Stmt *stmt) {
    if (!stmt || stmt->type != STMT_FOR) return;

    // Check for classic for-loop pattern: for (let i = 0; i < N; i++)
    Stmt *init = stmt->as.for_loop.initializer;
    Expr *cond = stmt->as.for_loop.condition;
    Expr *inc = stmt->as.for_loop.increment;
    Stmt *body = stmt->as.for_loop.body;

    if (!init || init->type != STMT_LET) return;

    const char *var_name = init->as.let.name;
    Expr *init_value = init->as.let.value;

    // Check initializer is an integer literal
    if (!init_value || init_value->type != EXPR_NUMBER || init_value->as.number.is_float) {
        return;
    }

    // Check condition is a simple comparison
    if (!is_simple_comparison(cond, var_name)) {
        return;
    }

    // Check increment is a simple increment
    if (!is_simple_increment(inc, var_name)) {
        return;
    }

    // Check if variable escapes in the loop body
    if (variable_escapes_in_stmt(body, var_name)) {
        return;
    }

    // All checks passed - this loop counter can be unboxed!
    InferredTypeKind native_type = INFER_I32;
    if (init_value->as.number.int_value > 2147483647LL ||
        init_value->as.number.int_value < -2147483648LL) {
        native_type = INFER_I64;
    }

    type_mark_unboxable(ctx, var_name, native_type, 1, 0);
}

// Helper: Check if a statement modifies a variable as an accumulator
// Patterns: sum = sum + x, sum = sum * x, sum = sum - x
static int is_accumulator_update(Stmt *stmt, const char *var_name) {
    if (!stmt || stmt->type != STMT_EXPR) return 0;
    Expr *expr = stmt->as.expr;
    if (!expr || expr->type != EXPR_ASSIGN) return 0;

    // Check if assigning to the variable
    if (strcmp(expr->as.assign.name, var_name) != 0) return 0;

    // Check if the value is a binary operation with the variable as left operand
    Expr *val = expr->as.assign.value;
    if (!val || val->type != EXPR_BINARY) return 0;

    // Left side should be the variable
    if (val->as.binary.left->type != EXPR_IDENT) return 0;
    if (strcmp(val->as.binary.left->as.ident.name, var_name) != 0) return 0;

    // Check for accumulation operations (+, -, *, |, ^, &)
    BinaryOp op = val->as.binary.op;
    return op == OP_ADD || op == OP_SUB || op == OP_MUL ||
           op == OP_BIT_OR || op == OP_BIT_XOR || op == OP_BIT_AND;
}

// Helper: Find accumulator updates in a block
static int find_accumulator_in_block(Stmt *body, const char *var_name) {
    if (!body) return 0;

    if (body->type == STMT_BLOCK) {
        for (int i = 0; i < body->as.block.count; i++) {
            if (is_accumulator_update(body->as.block.statements[i], var_name)) {
                return 1;
            }
        }
    } else if (is_accumulator_update(body, var_name)) {
        return 1;
    }
    return 0;
}

void type_analyze_while_loop(TypeInferContext *ctx, Stmt *stmt) {
    // Accumulator detection for while loops
    // Detects patterns like:
    //   let sum = 0;
    //   while (cond) { sum = sum + value; }
    //
    // Proof of correctness:
    // - If a variable is initialized with an integer and only updated
    //   with patterns like sum = sum + x (where the original value is used
    //   before any modification), it stays as an integer throughout.
    // - The variable can be unboxed to a native C int for efficiency.
    if (!stmt || stmt->type != STMT_WHILE) return;

    Stmt *body = stmt->as.while_stmt.body;
    if (!body) return;

    // Look through the body for accumulator patterns
    // For now, we only detect direct accumulator updates in the loop body
    // A more sophisticated implementation would track declarations before the loop

    // Scan the type environment for variables that might be accumulators
    // (those bound in this scope with known integer types)
    for (TypeBinding *b = ctx->current_env->bindings; b; b = b->next) {
        if ((b->type.kind == INFER_I32 || b->type.kind == INFER_I64) &&
            find_accumulator_in_block(body, b->name)) {
            // Check if the variable escapes in the loop body
            if (!variable_escapes_in_stmt(body, b->name)) {
                // Mark as accumulator for potential unboxing
                type_mark_unboxable(ctx, b->name, b->type.kind, 0, 1);
            }
        }
    }
}

// ========== TAIL CALL OPTIMIZATION ==========

// Helper: Check if expression contains a call to the given function (non-tail position)
static int contains_recursive_call(Expr *expr, const char *func_name) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_CALL:
            // Check if this is a call to the function
            if (expr->as.call.func->type == EXPR_IDENT &&
                strcmp(expr->as.call.func->as.ident.name, func_name) == 0) {
                return 1;
            }
            // Check callee and arguments
            if (contains_recursive_call(expr->as.call.func, func_name)) return 1;
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (contains_recursive_call(expr->as.call.args[i], func_name)) return 1;
            }
            return 0;

        case EXPR_BINARY:
            return contains_recursive_call(expr->as.binary.left, func_name) ||
                   contains_recursive_call(expr->as.binary.right, func_name);

        case EXPR_UNARY:
            return contains_recursive_call(expr->as.unary.operand, func_name);

        case EXPR_TERNARY:
            return contains_recursive_call(expr->as.ternary.condition, func_name) ||
                   contains_recursive_call(expr->as.ternary.true_expr, func_name) ||
                   contains_recursive_call(expr->as.ternary.false_expr, func_name);

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                if (contains_recursive_call(expr->as.array_literal.elements[i], func_name)) return 1;
            }
            return 0;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (contains_recursive_call(expr->as.object_literal.field_values[i], func_name)) return 1;
            }
            return 0;

        case EXPR_INDEX:
            return contains_recursive_call(expr->as.index.object, func_name) ||
                   contains_recursive_call(expr->as.index.index, func_name);

        case EXPR_INDEX_ASSIGN:
            return contains_recursive_call(expr->as.index_assign.object, func_name) ||
                   contains_recursive_call(expr->as.index_assign.index, func_name) ||
                   contains_recursive_call(expr->as.index_assign.value, func_name);

        case EXPR_ASSIGN:
            return contains_recursive_call(expr->as.assign.value, func_name);

        default:
            return 0;
    }
}

int is_tail_call_expr(Expr *expr, const char *func_name) {
    if (!expr || expr->type != EXPR_CALL) return 0;

    // Check if the callee is the function we're looking for
    if (expr->as.call.func->type != EXPR_IDENT) return 0;
    if (strcmp(expr->as.call.func->as.ident.name, func_name) != 0) return 0;

    // Check that arguments don't contain recursive calls (that would make it non-tail)
    for (int i = 0; i < expr->as.call.num_args; i++) {
        if (contains_recursive_call(expr->as.call.args[i], func_name)) return 0;
    }

    return 1;
}

int stmt_is_tail_recursive(Stmt *stmt, const char *func_name) {
    if (!stmt) return 1;  // Empty statement is fine

    switch (stmt->type) {
        case STMT_RETURN:
            if (!stmt->as.return_stmt.value) return 1;  // return; is fine
            // Either it's a tail call, or it doesn't contain recursive calls
            if (is_tail_call_expr(stmt->as.return_stmt.value, func_name)) return 1;
            return !contains_recursive_call(stmt->as.return_stmt.value, func_name);

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (!stmt_is_tail_recursive(stmt->as.block.statements[i], func_name)) {
                    return 0;
                }
            }
            return 1;

        case STMT_IF:
            // Both branches must be tail recursive
            if (!stmt_is_tail_recursive(stmt->as.if_stmt.then_branch, func_name)) return 0;
            if (stmt->as.if_stmt.else_branch) {
                if (!stmt_is_tail_recursive(stmt->as.if_stmt.else_branch, func_name)) return 0;
            }
            // Condition must not contain recursive calls
            return !contains_recursive_call(stmt->as.if_stmt.condition, func_name);

        case STMT_EXPR:
            // Expression statements can't contain recursive calls in tail position
            return !contains_recursive_call(stmt->as.expr, func_name);

        case STMT_LET:
        case STMT_CONST:
            // Variable declarations can't have recursive calls in value
            if (stmt->as.let.value) {
                return !contains_recursive_call(stmt->as.let.value, func_name);
            }
            return 1;

        case STMT_WHILE:
        case STMT_FOR:
        case STMT_FOR_IN:
            // Loops are not compatible with tail call optimization
            // (they could contain recursive calls in non-tail position)
            return 0;

        case STMT_TRY:
            // Try-catch is not compatible with simple tail call optimization
            return 0;

        case STMT_DEFER:
            // Defer is not compatible with tail call optimization
            return 0;

        default:
            return 1;
    }
}

int is_tail_recursive_function(Stmt *body, const char *func_name) {
    if (!body || !func_name) return 0;

    // The body must contain at least one tail call to be worth optimizing
    // and all returns must be either base cases or tail calls
    return stmt_is_tail_recursive(body, func_name);
}

// ========== DEBUG ==========

const char* infer_type_name(InferredType t) {
    switch (t.kind) {
        case INFER_UNKNOWN: return "unknown";
        case INFER_I32: return "i32";
        case INFER_I64: return "i64";
        case INFER_F64: return "f64";
        case INFER_BOOL: return "bool";
        case INFER_STRING: return "string";
        case INFER_NULL: return "null";
        case INFER_ARRAY: return "array";
        case INFER_OBJECT: return "object";
        case INFER_FUNCTION: return "function";
        case INFER_NUMERIC: return "numeric";
        case INFER_INTEGER: return "integer";
        default: return "?";
    }
}
