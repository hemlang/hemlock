/*
 * Hemlock Compiler - Unboxing Optimization Analysis
 *
 * Analyzes variables to determine which can use native C types instead of
 * HmlValue (unboxing). Includes loop counter, accumulator, and typed
 * variable analysis.
 */

#include "type_check_internal.h"

// ========== UNBOXING OPTIMIZATION ==========

void type_check_mark_unboxable(TypeCheckContext *ctx, const char *name,
                               CheckedTypeKind native_type, int is_loop_counter,
                               int is_accumulator, int is_typed_var) {
    if (!ctx) return;

    // Check if already marked
    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            // Update if more specific
            if (native_type != CHECKED_UNKNOWN) {
                u->native_type = native_type;
            }
            u->is_loop_counter |= is_loop_counter;
            u->is_accumulator |= is_accumulator;
            u->is_typed_var |= is_typed_var;
            return;
        }
    }
    // Add new entry
    UnboxableVar *u = malloc(sizeof(UnboxableVar));
    u->name = strdup(name);
    u->native_type = native_type;
    u->is_loop_counter = is_loop_counter;
    u->is_accumulator = is_accumulator;
    u->is_typed_var = is_typed_var;
    u->next = ctx->unboxable_vars;
    ctx->unboxable_vars = u;
}

CheckedTypeKind type_check_get_unboxable(TypeCheckContext *ctx, const char *name) {
    if (!ctx) return CHECKED_UNKNOWN;

    // First check if this is a function parameter - parameters are always HmlValue
    // and cannot be unboxed, even if a variable with the same name was marked
    // as unboxable in a different scope
    for (TypeCheckEnv *env = ctx->current_env; env; env = env->parent) {
        for (TypeCheckBinding *b = env->bindings; b; b = b->next) {
            if (strcmp(b->name, name) == 0) {
                if (b->is_param) {
                    return CHECKED_UNKNOWN;  // Parameters cannot be unboxed
                }
                break;  // Found the binding, not a parameter
            }
        }
    }

    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->native_type;
        }
    }
    return CHECKED_UNKNOWN;
}

void type_check_clear_unboxable(TypeCheckContext *ctx, const char *name) {
    if (!ctx || !name) return;

    UnboxableVar **prev = &ctx->unboxable_vars;
    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            // Remove from list
            *prev = u->next;
            free(u->name);
            free(u);
            return;
        }
        prev = &u->next;
    }
}

void type_check_clear_all_unboxable(TypeCheckContext *ctx) {
    if (!ctx) return;

    UnboxableVar *u = ctx->unboxable_vars;
    while (u) {
        UnboxableVar *next = u->next;
        free(u->name);
        free(u);
        u = next;
    }
    ctx->unboxable_vars = NULL;
}

int type_check_is_loop_counter(TypeCheckContext *ctx, const char *name) {
    if (!ctx) return 0;

    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->is_loop_counter;
        }
    }
    return 0;
}

int type_check_is_accumulator(TypeCheckContext *ctx, const char *name) {
    if (!ctx) return 0;

    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->is_accumulator;
        }
    }
    return 0;
}

int type_check_is_typed_var(TypeCheckContext *ctx, const char *name) {
    if (!ctx) return 0;

    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->is_typed_var;
        }
    }
    return 0;
}

CheckedTypeKind type_check_can_unbox_annotation(Type *type_annotation) {
    if (!type_annotation) return CHECKED_UNKNOWN;

    switch (type_annotation->kind) {
        case TYPE_I8: return CHECKED_I8;
        case TYPE_I16: return CHECKED_I16;
        case TYPE_I32: return CHECKED_I32;
        case TYPE_I64: return CHECKED_I64;
        case TYPE_U8: return CHECKED_U8;
        case TYPE_U16: return CHECKED_U16;
        case TYPE_U32: return CHECKED_U32;
        case TYPE_U64: return CHECKED_U64;
        case TYPE_F32: return CHECKED_F32;
        case TYPE_F64: return CHECKED_F64;
        case TYPE_BOOL: return CHECKED_BOOL;
        default:
            // Non-primitive types cannot be unboxed
            return CHECKED_UNKNOWN;
    }
}

// ========== LOOP ANALYSIS ==========

// Check if an expression is a simple increment pattern: i = i + 1, i++, etc.
int is_simple_increment(Expr *expr, const char *var_name) {
    if (!expr) return 0;

    if (expr->type == EXPR_ASSIGN && strcmp(expr->as.assign.name, var_name) == 0) {
        Expr *val = expr->as.assign.value;
        if (val->type == EXPR_BINARY) {
            if (val->as.binary.left->type == EXPR_IDENT &&
                strcmp(val->as.binary.left->as.ident.name, var_name) == 0) {
                if (val->as.binary.right->type == EXPR_NUMBER &&
                    !val->as.binary.right->as.number.is_float) {
                    BinaryOp op = val->as.binary.op;
                    return op == OP_ADD || op == OP_SUB;
                }
            }
        }
    }

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

// Check if a condition is a simple comparison on the variable
int is_simple_comparison(Expr *expr, const char *var_name) {
    if (!expr || expr->type != EXPR_BINARY) return 0;

    BinaryOp op = expr->as.binary.op;
    if (op != OP_LESS && op != OP_LESS_EQUAL &&
        op != OP_GREATER && op != OP_GREATER_EQUAL &&
        op != OP_EQUAL && op != OP_NOT_EQUAL) {
        return 0;
    }

    Expr *left = expr->as.binary.left;
    Expr *right = expr->as.binary.right;

    int left_is_var = (left->type == EXPR_IDENT && strcmp(left->as.ident.name, var_name) == 0);
    int right_is_var = (right->type == EXPR_IDENT && strcmp(right->as.ident.name, var_name) == 0);

    if (left_is_var) {
        return right->type == EXPR_NUMBER ||
               right->type == EXPR_IDENT ||
               right->type == EXPR_GET_PROPERTY;
    }
    if (right_is_var) {
        return left->type == EXPR_NUMBER ||
               left->type == EXPR_IDENT ||
               left->type == EXPR_GET_PROPERTY;
    }

    return 0;
}

void type_check_analyze_for_loop(TypeCheckContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt || stmt->type != STMT_FOR) return;

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
    if (variable_escapes_in_stmt_internal(body, var_name)) {
        return;
    }

    // All checks passed - this loop counter can be unboxed!
    CheckedTypeKind native_type = CHECKED_I32;
    if (init_value->as.number.int_value > 2147483647LL ||
        init_value->as.number.int_value < -2147483648LL) {
        native_type = CHECKED_I64;
    }

    type_check_mark_unboxable(ctx, var_name, native_type, 1, 0, 0);
}

// Helper: Check if a statement modifies a variable as an accumulator
static int is_accumulator_update(Stmt *stmt, const char *var_name) {
    if (!stmt || stmt->type != STMT_EXPR) return 0;
    Expr *expr = stmt->as.expr;
    if (!expr || expr->type != EXPR_ASSIGN) return 0;

    if (strcmp(expr->as.assign.name, var_name) != 0) return 0;

    Expr *val = expr->as.assign.value;
    if (!val || val->type != EXPR_BINARY) return 0;

    if (val->as.binary.left->type != EXPR_IDENT) return 0;
    if (strcmp(val->as.binary.left->as.ident.name, var_name) != 0) return 0;

    BinaryOp op = val->as.binary.op;
    return op == OP_ADD || op == OP_SUB || op == OP_MUL ||
           op == OP_BIT_OR || op == OP_BIT_XOR || op == OP_BIT_AND;
}

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

void type_check_analyze_while_loop(TypeCheckContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt || stmt->type != STMT_WHILE) return;

    Stmt *body = stmt->as.while_stmt.body;
    if (!body) return;

    // Look for accumulator patterns in the type environment
    for (TypeCheckBinding *b = ctx->current_env->bindings; b; b = b->next) {
        // Skip function parameters - they are always HmlValue and cannot be unboxed
        if (b->is_param) continue;

        CheckedTypeKind kind = b->type ? b->type->kind : CHECKED_UNKNOWN;
        if ((kind == CHECKED_I32 || kind == CHECKED_I64) &&
            find_accumulator_in_block(body, b->name)) {
            if (!variable_escapes_in_stmt_internal(body, b->name)) {
                type_check_mark_unboxable(ctx, b->name, kind, 0, 1, 0);
            }
        }
    }
}

// ========== TYPED VARIABLE UNBOXING ==========

// Helper: Infer the native type of an expression for unboxing
// Returns CHECKED_UNKNOWN if the type cannot be determined or is not unboxable
CheckedTypeKind infer_expr_native_type(TypeCheckContext *ctx, Expr *expr) {
    if (!expr) return CHECKED_UNKNOWN;

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return CHECKED_F64;
            }
            // Determine integer type based on value range
            if (expr->as.number.int_value >= -128 && expr->as.number.int_value <= 127) {
                return CHECKED_I32;  // Promote small integers to i32 for performance
            }
            if (expr->as.number.int_value >= -2147483648LL && expr->as.number.int_value <= 2147483647LL) {
                return CHECKED_I32;
            }
            return CHECKED_I64;

        case EXPR_BOOL:
            return CHECKED_BOOL;

        case EXPR_RUNE:
            return CHECKED_I32;  // Runes are stored as i32 codepoints

        case EXPR_IDENT: {
            // Check if this variable is already marked as unboxable
            if (ctx) {
                CheckedTypeKind kind = type_check_get_unboxable(ctx, expr->as.ident.name);
                if (kind != CHECKED_UNKNOWN) {
                    return kind;
                }
                // Also check the type environment for typed variables
                CheckedType *var_type = type_check_lookup(ctx, expr->as.ident.name);
                if (var_type) {
                    switch (var_type->kind) {
                        case CHECKED_I8: return CHECKED_I8;
                        case CHECKED_I16: return CHECKED_I16;
                        case CHECKED_I32: return CHECKED_I32;
                        case CHECKED_I64: return CHECKED_I64;
                        case CHECKED_U8: return CHECKED_U8;
                        case CHECKED_U16: return CHECKED_U16;
                        case CHECKED_U32: return CHECKED_U32;
                        case CHECKED_U64: return CHECKED_U64;
                        case CHECKED_F32: return CHECKED_F32;
                        case CHECKED_F64: return CHECKED_F64;
                        case CHECKED_BOOL: return CHECKED_BOOL;
                        default: break;
                    }
                }
            }
            return CHECKED_UNKNOWN;
        }

        case EXPR_BINARY: {
            // Division always returns f64
            if (expr->as.binary.op == OP_DIV) {
                return CHECKED_F64;
            }

            CheckedTypeKind left = infer_expr_native_type(ctx, expr->as.binary.left);
            CheckedTypeKind right = infer_expr_native_type(ctx, expr->as.binary.right);

            if (left == CHECKED_UNKNOWN || right == CHECKED_UNKNOWN) {
                return CHECKED_UNKNOWN;
            }

            // Comparison operators return bool
            if (expr->as.binary.op >= OP_EQUAL && expr->as.binary.op <= OP_GREATER_EQUAL) {
                return CHECKED_BOOL;
            }

            // Logical operators return bool
            if (expr->as.binary.op == OP_AND || expr->as.binary.op == OP_OR) {
                return CHECKED_BOOL;
            }

            // Type promotion for arithmetic/bitwise operators
            // Float always wins
            if (left == CHECKED_F64 || right == CHECKED_F64) return CHECKED_F64;
            if (left == CHECKED_F32 || right == CHECKED_F32) {
                // i64/u64 + f32 -> f64 to preserve precision
                if (left == CHECKED_I64 || left == CHECKED_U64 ||
                    right == CHECKED_I64 || right == CHECKED_U64) {
                    return CHECKED_F64;
                }
                return CHECKED_F32;
            }

            // Integer promotion: i64 > i32 > smaller types
            if (left == CHECKED_I64 || right == CHECKED_I64) return CHECKED_I64;
            if (left == CHECKED_U64 || right == CHECKED_U64) return CHECKED_U64;
            if (left == CHECKED_I32 || right == CHECKED_I32) return CHECKED_I32;
            if (left == CHECKED_U32 || right == CHECKED_U32) return CHECKED_U32;

            // For smaller integers, promote to i32
            return CHECKED_I32;
        }

        case EXPR_UNARY:
            if (expr->as.unary.op == UNARY_NOT) {
                return CHECKED_BOOL;
            }
            // Negation and bit-not preserve type
            return infer_expr_native_type(ctx, expr->as.unary.operand);

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            return infer_expr_native_type(ctx, expr->as.prefix_inc.operand);

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return infer_expr_native_type(ctx, expr->as.postfix_inc.operand);

        case EXPR_TERNARY: {
            // Both branches must have compatible types
            CheckedTypeKind true_type = infer_expr_native_type(ctx, expr->as.ternary.true_expr);
            CheckedTypeKind false_type = infer_expr_native_type(ctx, expr->as.ternary.false_expr);
            if (true_type == CHECKED_UNKNOWN || false_type == CHECKED_UNKNOWN) {
                return CHECKED_UNKNOWN;
            }
            if (true_type == false_type) {
                return true_type;
            }
            // Use the same promotion rules as binary operators
            if (true_type == CHECKED_F64 || false_type == CHECKED_F64) return CHECKED_F64;
            if (true_type == CHECKED_I64 || false_type == CHECKED_I64) return CHECKED_I64;
            if (true_type == CHECKED_I32 || false_type == CHECKED_I32) return CHECKED_I32;
            return CHECKED_UNKNOWN;
        }

        default:
            return CHECKED_UNKNOWN;
    }
}

// Helper: Check if expression is unboxable (primitive literal, arithmetic, etc.)
int is_unboxable_expr(Expr *expr) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_IDENT:
        case EXPR_RUNE:
            return 1;

        case EXPR_BINARY:
            return is_unboxable_expr(expr->as.binary.left) &&
                   is_unboxable_expr(expr->as.binary.right);

        case EXPR_UNARY:
            return is_unboxable_expr(expr->as.unary.operand);

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            return is_unboxable_expr(expr->as.prefix_inc.operand);

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return is_unboxable_expr(expr->as.postfix_inc.operand);

        case EXPR_TERNARY:
            return is_unboxable_expr(expr->as.ternary.true_expr) &&
                   is_unboxable_expr(expr->as.ternary.false_expr);

        default:
            return 0;
    }
}

// Helper: Check for incompatible assignments
int has_incompatible_assignment_expr(Expr *expr, const char *var_name) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_ASSIGN:
            if (strcmp(expr->as.assign.name, var_name) == 0) {
                if (!is_unboxable_expr(expr->as.assign.value)) {
                    return 1;
                }
            }
            return has_incompatible_assignment_expr(expr->as.assign.value, var_name);

        case EXPR_BINARY:
            return has_incompatible_assignment_expr(expr->as.binary.left, var_name) ||
                   has_incompatible_assignment_expr(expr->as.binary.right, var_name);

        case EXPR_UNARY:
            return has_incompatible_assignment_expr(expr->as.unary.operand, var_name);

        case EXPR_CALL:
            if (has_incompatible_assignment_expr(expr->as.call.func, var_name)) return 1;
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (has_incompatible_assignment_expr(expr->as.call.args[i], var_name)) return 1;
            }
            return 0;

        case EXPR_TERNARY:
            return has_incompatible_assignment_expr(expr->as.ternary.condition, var_name) ||
                   has_incompatible_assignment_expr(expr->as.ternary.true_expr, var_name) ||
                   has_incompatible_assignment_expr(expr->as.ternary.false_expr, var_name);

        default:
            return 0;
    }
}

int has_incompatible_assignment_stmt(Stmt *stmt, const char *var_name) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case STMT_EXPR:
            return has_incompatible_assignment_expr(stmt->as.expr, var_name);

        case STMT_LET:
        case STMT_CONST:
            if (stmt->as.let.value) {
                return has_incompatible_assignment_expr(stmt->as.let.value, var_name);
            }
            return 0;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                return has_incompatible_assignment_expr(stmt->as.return_stmt.value, var_name);
            }
            return 0;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (has_incompatible_assignment_stmt(stmt->as.block.statements[i], var_name)) {
                    return 1;
                }
            }
            return 0;

        case STMT_IF:
            return has_incompatible_assignment_expr(stmt->as.if_stmt.condition, var_name) ||
                   has_incompatible_assignment_stmt(stmt->as.if_stmt.then_branch, var_name) ||
                   (stmt->as.if_stmt.else_branch && has_incompatible_assignment_stmt(stmt->as.if_stmt.else_branch, var_name));

        case STMT_WHILE:
            return has_incompatible_assignment_expr(stmt->as.while_stmt.condition, var_name) ||
                   has_incompatible_assignment_stmt(stmt->as.while_stmt.body, var_name);

        case STMT_LOOP:
            return has_incompatible_assignment_stmt(stmt->as.loop_stmt.body, var_name);

        case STMT_FOR:
            return (stmt->as.for_loop.initializer && has_incompatible_assignment_stmt(stmt->as.for_loop.initializer, var_name)) ||
                   (stmt->as.for_loop.condition && has_incompatible_assignment_expr(stmt->as.for_loop.condition, var_name)) ||
                   (stmt->as.for_loop.increment && has_incompatible_assignment_expr(stmt->as.for_loop.increment, var_name)) ||
                   has_incompatible_assignment_stmt(stmt->as.for_loop.body, var_name);

        default:
            return 0;
    }
}

void type_check_analyze_typed_let(TypeCheckContext *ctx, Stmt *stmt,
                                  Stmt *containing_block, int stmt_index) {
    if (!ctx || !stmt || stmt->type != STMT_LET) return;
    if (!stmt->as.let.type_annotation) return;

    const char *var_name = stmt->as.let.name;

    CheckedTypeKind native_type = type_check_can_unbox_annotation(stmt->as.let.type_annotation);
    if (native_type == CHECKED_UNKNOWN) return;

    // Check if the initializer is unboxable
    if (stmt->as.let.value && !is_unboxable_expr(stmt->as.let.value)) {
        return;
    }

    // Check if variable escapes or has incompatible assignments in subsequent statements
    if (containing_block && containing_block->type == STMT_BLOCK) {
        for (int i = stmt_index + 1; i < containing_block->as.block.count; i++) {
            if (variable_escapes_in_stmt_internal(containing_block->as.block.statements[i], var_name)) {
                return;
            }
            if (has_incompatible_assignment_stmt(containing_block->as.block.statements[i], var_name)) {
                return;
            }
        }
    }

    // Variable can be unboxed!
    type_check_mark_unboxable(ctx, var_name, native_type, 0, 0, 1);
}

// Analyze an untyped let statement for inferred type unboxing
// This enables unboxing for patterns like: let x = 42; let sum = a + b;
static void type_check_analyze_inferred_let(TypeCheckContext *ctx, Stmt *stmt,
                                             Stmt *containing_block, int stmt_index) {
    if (!ctx || !stmt || stmt->type != STMT_LET) return;

    // Skip if already has explicit type annotation (handled by type_check_analyze_typed_let)
    if (stmt->as.let.type_annotation) return;

    // Must have an initializer to infer type from
    if (!stmt->as.let.value) return;

    const char *var_name = stmt->as.let.name;

    // Infer the type from the initializer expression
    CheckedTypeKind native_type = infer_expr_native_type(ctx, stmt->as.let.value);
    if (native_type == CHECKED_UNKNOWN) return;

    // Only unbox numeric types (i32, i64, f64) and bool for now
    // Skip i8/i16/u8/u16 etc. unless the expression explicitly produces them
    if (native_type != CHECKED_I32 && native_type != CHECKED_I64 &&
        native_type != CHECKED_F64 && native_type != CHECKED_BOOL) {
        return;
    }

    // Check if the initializer expression is structurally unboxable
    if (!is_unboxable_expr(stmt->as.let.value)) {
        return;
    }

    // Check if variable escapes or has incompatible assignments in subsequent statements
    if (containing_block && containing_block->type == STMT_BLOCK) {
        for (int i = stmt_index + 1; i < containing_block->as.block.count; i++) {
            if (variable_escapes_in_stmt_internal(containing_block->as.block.statements[i], var_name)) {
                return;
            }
            if (has_incompatible_assignment_stmt(containing_block->as.block.statements[i], var_name)) {
                return;
            }
        }
    }

    // Variable can be unboxed with inferred type!
    // Mark as typed_var since it behaves like a typed variable for codegen purposes
    type_check_mark_unboxable(ctx, var_name, native_type, 0, 0, 1);
}

void type_check_analyze_block_for_unboxing(TypeCheckContext *ctx, Stmt *block) {
    if (!ctx || !block) return;

    if (block->type == STMT_BLOCK) {
        for (int i = 0; i < block->as.block.count; i++) {
            Stmt *stmt = block->as.block.statements[i];

            // Analyze typed let statements
            if (stmt->type == STMT_LET && stmt->as.let.type_annotation) {
                type_check_analyze_typed_let(ctx, stmt, block, i);
            }
            // Analyze untyped let statements for inferred type unboxing
            else if (stmt->type == STMT_LET && !stmt->as.let.type_annotation && stmt->as.let.value) {
                type_check_analyze_inferred_let(ctx, stmt, block, i);
            }

            // Analyze for loops for loop counters
            if (stmt->type == STMT_FOR) {
                type_check_analyze_for_loop(ctx, stmt);
            }

            // Analyze while loops for accumulators
            if (stmt->type == STMT_WHILE) {
                type_check_analyze_while_loop(ctx, stmt);
            }

            // Recursively analyze nested blocks
            if (stmt->type == STMT_IF) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.if_stmt.then_branch);
                if (stmt->as.if_stmt.else_branch) {
                    type_check_analyze_block_for_unboxing(ctx, stmt->as.if_stmt.else_branch);
                }
            } else if (stmt->type == STMT_WHILE) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.while_stmt.body);
            } else if (stmt->type == STMT_FOR) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.for_loop.body);
            } else if (stmt->type == STMT_FOR_IN) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.for_in.body);
            } else if (stmt->type == STMT_BLOCK) {
                type_check_analyze_block_for_unboxing(ctx, stmt);
            } else if (stmt->type == STMT_TRY) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.try_stmt.try_block);
                if (stmt->as.try_stmt.catch_block) {
                    type_check_analyze_block_for_unboxing(ctx, stmt->as.try_stmt.catch_block);
                }
                if (stmt->as.try_stmt.finally_block) {
                    type_check_analyze_block_for_unboxing(ctx, stmt->as.try_stmt.finally_block);
                }
            }
        }
    } else if (block->type == STMT_LET) {
        // Handle single-statement let (both typed and untyped)
        if (block->as.let.type_annotation) {
            type_check_analyze_typed_let(ctx, block, NULL, 0);
        } else if (block->as.let.value) {
            type_check_analyze_inferred_let(ctx, block, NULL, 0);
        }
    }
}
