/*
 * Tricycle - Ownership Analysis
 *
 * Implements ownership tracking and memory safety analysis.
 * This is the core analysis engine that walks the AST and
 * detects memory safety violations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tricycle/tricycle.h"

// ========== FORWARD DECLARATIONS ==========

static void analyze_let_stmt(TricycleContext *ctx, Stmt *stmt);
static void analyze_expr_stmt(TricycleContext *ctx, Stmt *stmt);
static void analyze_if_stmt(TricycleContext *ctx, Stmt *stmt);
static void analyze_while_stmt(TricycleContext *ctx, Stmt *stmt);
static void analyze_for_stmt(TricycleContext *ctx, Stmt *stmt);
static void analyze_block_stmt(TricycleContext *ctx, Stmt *stmt);
static void analyze_return_stmt(TricycleContext *ctx, Stmt *stmt);
static void analyze_function_expr(TricycleContext *ctx, Expr *expr);
static void analyze_call_expr(TricycleContext *ctx, Expr *expr);
static void analyze_assign_expr(TricycleContext *ctx, Expr *expr);

// ========== STATEMENT ANALYSIS ==========

int tricycle_analyze_program(TricycleContext *ctx, Stmt **stmts, int stmt_count) {
    if (!ctx || !stmts) return 0;

    ctx->program = stmts;
    ctx->stmt_count = stmt_count;

    // Analyze each top-level statement
    for (int i = 0; i < stmt_count; i++) {
        if (stmts[i]) {
            tricycle_analyze_stmt(ctx, stmts[i]);
        }

        // Stop if we hit an error and not in collect_all mode
        if (ctx->error_count > 0 && !ctx->collect_all) {
            break;
        }
    }

    // Check for leaks at program end
    tricycle_check_leaks(ctx);

    return ctx->error_count;
}

void tricycle_analyze_stmt(TricycleContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt) return;

    switch (stmt->type) {
        case STMT_LET:
        case STMT_CONST:
            analyze_let_stmt(ctx, stmt);
            break;

        case STMT_EXPR:
            analyze_expr_stmt(ctx, stmt);
            break;

        case STMT_IF:
            analyze_if_stmt(ctx, stmt);
            break;

        case STMT_WHILE:
        case STMT_LOOP:
            analyze_while_stmt(ctx, stmt);
            break;

        case STMT_FOR:
        case STMT_FOR_IN:
            analyze_for_stmt(ctx, stmt);
            break;

        case STMT_BLOCK:
            analyze_block_stmt(ctx, stmt);
            break;

        case STMT_RETURN:
            analyze_return_stmt(ctx, stmt);
            break;

        case STMT_TRY:
            // Analyze try block
            if (stmt->as.try_stmt.try_block) {
                tricycle_push_scope(ctx, SCOPE_TRY);
                tricycle_analyze_stmt(ctx, stmt->as.try_stmt.try_block);
                tricycle_pop_scope(ctx);
            }
            // Analyze catch block
            if (stmt->as.try_stmt.catch_block) {
                tricycle_push_scope(ctx, SCOPE_BLOCK);
                if (stmt->as.try_stmt.catch_param) {
                    // Bind catch parameter as unknown (exception value)
                    tricycle_bind(ctx, stmt->as.try_stmt.catch_param,
                                  MEM_UNKNOWN, stmt->line, stmt->column, 0);
                }
                tricycle_analyze_stmt(ctx, stmt->as.try_stmt.catch_block);
                tricycle_pop_scope(ctx);
            }
            // Analyze finally block
            if (stmt->as.try_stmt.finally_block) {
                tricycle_push_scope(ctx, SCOPE_BLOCK);
                tricycle_analyze_stmt(ctx, stmt->as.try_stmt.finally_block);
                tricycle_pop_scope(ctx);
            }
            break;

        case STMT_DEFER:
            // Analyze the deferred expression
            if (stmt->as.defer_stmt.call) {
                tricycle_analyze_expr(ctx, stmt->as.defer_stmt.call);
            }
            break;

        case STMT_SWITCH:
            // Analyze switch expression
            if (stmt->as.switch_stmt.expr) {
                tricycle_analyze_expr(ctx, stmt->as.switch_stmt.expr);
            }
            // Analyze each case
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i]) {
                    tricycle_analyze_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                }
                if (stmt->as.switch_stmt.case_bodies[i]) {
                    tricycle_push_scope(ctx, SCOPE_BLOCK);
                    tricycle_analyze_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
                    tricycle_pop_scope(ctx);
                }
            }
            break;

        case STMT_BREAK:
        case STMT_CONTINUE:
            // No memory analysis needed for control flow
            break;

        case STMT_THROW:
            if (stmt->as.throw_stmt.value) {
                tricycle_analyze_expr(ctx, stmt->as.throw_stmt.value);
            }
            break;

        case STMT_DEFINE_OBJECT:
        case STMT_ENUM:
        case STMT_IMPORT:
        case STMT_EXPORT:
        case STMT_IMPORT_FFI:
        case STMT_EXTERN_FN:
        case STMT_TYPE_ALIAS:
            // No runtime memory analysis needed for declarations
            break;

        default:
            break;
    }
}

static void analyze_let_stmt(TricycleContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt) return;

    const char *name = (stmt->type == STMT_LET) ?
                       stmt->as.let.name : stmt->as.const_stmt.name;
    Expr *value = (stmt->type == STMT_LET) ?
                  stmt->as.let.value : stmt->as.const_stmt.value;
    Type *type_annotation = (stmt->type == STMT_LET) ?
                            stmt->as.let.type_annotation : stmt->as.const_stmt.type_annotation;

    int is_ptr = tricycle_is_ptr_type(type_annotation);

    if (value) {
        // Analyze the initialization expression
        tricycle_analyze_expr(ctx, value);

        if (tricycle_is_alloc_call(value)) {
            // Direct allocation: let p = alloc(64);
            tricycle_bind(ctx, name, MEM_ALLOCATED, stmt->line, stmt->column, 1);
        } else if (value->type == EXPR_IDENT) {
            // Assignment from another variable: let q = p;
            const char *source_name = value->as.ident.name;
            OwnershipBinding *source = tricycle_lookup(ctx, source_name);

            if (source && source->is_ptr_type) {
                // This is ownership transfer
                if (source->state.kind == MEM_ALLOCATED) {
                    // Transfer ownership from source to this variable
                    tricycle_bind(ctx, name, MEM_ALLOCATED, stmt->line, stmt->column, 1);
                    // Copy allocation info to new owner
                    OwnershipBinding *new_binding = tricycle_lookup(ctx, name);
                    if (new_binding) {
                        new_binding->state.allocation_line = source->state.allocation_line;
                        new_binding->state.allocation_column = source->state.allocation_column;
                    }
                    // Mark source as moved
                    tricycle_mark_moved(ctx, source_name, name, stmt->line, stmt->column);
                } else if (source->state.kind == MEM_UNALLOCATED) {
                    // Use after free
                    tricycle_error(ctx, TRIC_USE_AFTER_FREE, stmt->line, stmt->column,
                                   "use of freed memory '%s'", source_name);
                    tricycle_add_related(ctx, source->state.free_line, source->state.free_column,
                                         "freed here");
                    tricycle_add_hint(ctx, "do not use memory after it has been freed");
                    tricycle_bind(ctx, name, MEM_UNKNOWN, stmt->line, stmt->column, 1);
                } else if (source->state.kind == MEM_MOVED) {
                    // Use after move
                    tricycle_error(ctx, TRIC_USE_AFTER_MOVE, stmt->line, stmt->column,
                                   "use of moved value '%s'", source_name);
                    tricycle_add_related(ctx, source->state.move_line, source->state.move_column,
                                         "moved here");
                    if (source->state.moved_to) {
                        char hint[256];
                        snprintf(hint, sizeof(hint), "use '%s' instead, which now owns the memory",
                                 source->state.moved_to);
                        tricycle_add_hint(ctx, hint);
                    }
                    tricycle_bind(ctx, name, MEM_UNKNOWN, stmt->line, stmt->column, 1);
                } else {
                    // Unknown or borrowed - bind as unknown
                    tricycle_bind(ctx, name, source->state.kind, stmt->line, stmt->column, 1);
                }
            } else {
                // Non-pointer type
                tricycle_bind(ctx, name, MEM_UNKNOWN, stmt->line, stmt->column, is_ptr);
            }
        } else if (value->type == EXPR_NULL) {
            // let p = null;
            tricycle_bind(ctx, name, MEM_UNALLOCATED, stmt->line, stmt->column, is_ptr);
        } else {
            // Other expressions
            tricycle_bind(ctx, name, MEM_UNKNOWN, stmt->line, stmt->column,
                          is_ptr || tricycle_is_ptr_expr(ctx, value));
        }
    } else {
        // Uninitialized (shouldn't happen in valid Hemlock, but handle it)
        tricycle_bind(ctx, name, MEM_UNKNOWN, stmt->line, stmt->column, is_ptr);
    }
}

static void analyze_expr_stmt(TricycleContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt || !stmt->as.expr) return;
    tricycle_analyze_expr(ctx, stmt->as.expr);
}

static void analyze_if_stmt(TricycleContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt) return;

    // Analyze condition
    if (stmt->as.if_stmt.condition) {
        tricycle_analyze_expr(ctx, stmt->as.if_stmt.condition);
    }

    // For control flow sensitivity, we'd need to track state snapshots
    // For now, analyze both branches independently

    // Analyze then branch
    if (stmt->as.if_stmt.then_branch) {
        tricycle_push_scope(ctx, SCOPE_IF);
        tricycle_analyze_stmt(ctx, stmt->as.if_stmt.then_branch);
        tricycle_pop_scope(ctx);
    }

    // Analyze else branch
    if (stmt->as.if_stmt.else_branch) {
        tricycle_push_scope(ctx, SCOPE_IF);
        tricycle_analyze_stmt(ctx, stmt->as.if_stmt.else_branch);
        tricycle_pop_scope(ctx);
    }
}

static void analyze_while_stmt(TricycleContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt) return;

    // For loop/while statements
    if (stmt->type == STMT_WHILE) {
        if (stmt->as.while_stmt.condition) {
            tricycle_analyze_expr(ctx, stmt->as.while_stmt.condition);
        }

        tricycle_push_scope_labeled(ctx, SCOPE_LOOP, stmt->as.while_stmt.label);
        if (stmt->as.while_stmt.body) {
            tricycle_analyze_stmt(ctx, stmt->as.while_stmt.body);
        }
        tricycle_pop_scope(ctx);
    } else if (stmt->type == STMT_LOOP) {
        tricycle_push_scope_labeled(ctx, SCOPE_LOOP, stmt->as.loop_stmt.label);
        if (stmt->as.loop_stmt.body) {
            tricycle_analyze_stmt(ctx, stmt->as.loop_stmt.body);
        }
        tricycle_pop_scope(ctx);
    }
}

static void analyze_for_stmt(TricycleContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt) return;

    if (stmt->type == STMT_FOR) {
        tricycle_push_scope_labeled(ctx, SCOPE_LOOP, stmt->as.for_loop.label);

        // Analyze initializer
        if (stmt->as.for_loop.initializer) {
            tricycle_analyze_stmt(ctx, stmt->as.for_loop.initializer);
        }

        // Analyze condition
        if (stmt->as.for_loop.condition) {
            tricycle_analyze_expr(ctx, stmt->as.for_loop.condition);
        }

        // Analyze increment
        if (stmt->as.for_loop.increment) {
            tricycle_analyze_expr(ctx, stmt->as.for_loop.increment);
        }

        // Analyze body
        if (stmt->as.for_loop.body) {
            tricycle_analyze_stmt(ctx, stmt->as.for_loop.body);
        }

        tricycle_pop_scope(ctx);
    } else if (stmt->type == STMT_FOR_IN) {
        // Analyze iterable
        if (stmt->as.for_in.iterable) {
            tricycle_analyze_expr(ctx, stmt->as.for_in.iterable);
        }

        tricycle_push_scope_labeled(ctx, SCOPE_LOOP, stmt->as.for_in.label);

        // Bind iteration variables
        if (stmt->as.for_in.key_var) {
            tricycle_bind(ctx, stmt->as.for_in.key_var, MEM_UNKNOWN,
                          stmt->line, stmt->column, 0);
        }
        if (stmt->as.for_in.value_var) {
            tricycle_bind(ctx, stmt->as.for_in.value_var, MEM_UNKNOWN,
                          stmt->line, stmt->column, 0);
        }

        // Analyze body
        if (stmt->as.for_in.body) {
            tricycle_analyze_stmt(ctx, stmt->as.for_in.body);
        }

        tricycle_pop_scope(ctx);
    }
}

static void analyze_block_stmt(TricycleContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt) return;

    tricycle_push_scope(ctx, SCOPE_BLOCK);

    for (int i = 0; i < stmt->as.block.count; i++) {
        if (stmt->as.block.statements[i]) {
            tricycle_analyze_stmt(ctx, stmt->as.block.statements[i]);
        }
    }

    tricycle_pop_scope(ctx);
}

static void analyze_return_stmt(TricycleContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt) return;

    if (stmt->as.return_stmt.value) {
        tricycle_analyze_expr(ctx, stmt->as.return_stmt.value);

        // If returning a pointer, ownership transfers to caller
        Expr *value = stmt->as.return_stmt.value;
        if (value->type == EXPR_IDENT) {
            const char *name = value->as.ident.name;
            OwnershipBinding *binding = tricycle_lookup(ctx, name);
            if (binding && binding->is_ptr_type && binding->state.kind == MEM_ALLOCATED) {
                // Mark as moved (ownership to caller)
                tricycle_update_state(ctx, name, MEM_MOVED, stmt->line, stmt->column);
            }
        }
    }
}

// ========== EXPRESSION ANALYSIS ==========

void tricycle_analyze_expr(TricycleContext *ctx, Expr *expr) {
    if (!ctx || !expr) return;

    switch (expr->type) {
        case EXPR_IDENT:
            // Check for use of freed/moved memory
            tricycle_check_use(ctx, expr->as.ident.name, expr->line, expr->column);
            break;

        case EXPR_CALL:
            analyze_call_expr(ctx, expr);
            break;

        case EXPR_ASSIGN:
            analyze_assign_expr(ctx, expr);
            break;

        case EXPR_FUNCTION:
            analyze_function_expr(ctx, expr);
            break;

        case EXPR_BINARY:
            tricycle_analyze_expr(ctx, expr->as.binary.left);
            tricycle_analyze_expr(ctx, expr->as.binary.right);
            break;

        case EXPR_UNARY:
            tricycle_analyze_expr(ctx, expr->as.unary.operand);
            break;

        case EXPR_TERNARY:
            tricycle_analyze_expr(ctx, expr->as.ternary.condition);
            tricycle_analyze_expr(ctx, expr->as.ternary.true_expr);
            tricycle_analyze_expr(ctx, expr->as.ternary.false_expr);
            break;

        case EXPR_INDEX:
            tricycle_analyze_expr(ctx, expr->as.index.object);
            tricycle_analyze_expr(ctx, expr->as.index.index);
            break;

        case EXPR_INDEX_ASSIGN:
            tricycle_analyze_expr(ctx, expr->as.index_assign.object);
            tricycle_analyze_expr(ctx, expr->as.index_assign.index);
            tricycle_analyze_expr(ctx, expr->as.index_assign.value);
            break;

        case EXPR_GET_PROPERTY:
            tricycle_analyze_expr(ctx, expr->as.get_property.object);
            break;

        case EXPR_SET_PROPERTY:
            tricycle_analyze_expr(ctx, expr->as.set_property.object);
            tricycle_analyze_expr(ctx, expr->as.set_property.value);
            break;

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                if (expr->as.array_literal.elements[i]) {
                    tricycle_analyze_expr(ctx, expr->as.array_literal.elements[i]);
                }
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (expr->as.object_literal.field_values[i]) {
                    tricycle_analyze_expr(ctx, expr->as.object_literal.field_values[i]);
                }
            }
            break;

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            tricycle_analyze_expr(ctx, expr->as.prefix_inc.operand);
            break;

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            tricycle_analyze_expr(ctx, expr->as.postfix_inc.operand);
            break;

        case EXPR_AWAIT:
            tricycle_analyze_expr(ctx, expr->as.await_expr.awaited_expr);
            break;

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                if (expr->as.string_interpolation.expr_parts[i]) {
                    tricycle_analyze_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
                }
            }
            break;

        case EXPR_OPTIONAL_CHAIN:
            if (expr->as.optional_chain.object) {
                tricycle_analyze_expr(ctx, expr->as.optional_chain.object);
            }
            if (expr->as.optional_chain.index) {
                tricycle_analyze_expr(ctx, expr->as.optional_chain.index);
            }
            for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                if (expr->as.optional_chain.args[i]) {
                    tricycle_analyze_expr(ctx, expr->as.optional_chain.args[i]);
                }
            }
            break;

        case EXPR_NULL_COALESCE:
            tricycle_analyze_expr(ctx, expr->as.null_coalesce.left);
            tricycle_analyze_expr(ctx, expr->as.null_coalesce.right);
            break;

        case EXPR_MATCH:
            if (expr->as.match_expr.scrutinee) {
                tricycle_analyze_expr(ctx, expr->as.match_expr.scrutinee);
            }
            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                MatchArm *arm = &expr->as.match_expr.arms[i];
                if (arm->guard) {
                    tricycle_analyze_expr(ctx, arm->guard);
                }
                if (arm->body) {
                    tricycle_analyze_expr(ctx, arm->body);
                }
            }
            break;

        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_STRING:
        case EXPR_RUNE:
        case EXPR_NULL:
            // Literals don't need memory analysis
            break;

        default:
            break;
    }
}

static void analyze_call_expr(TricycleContext *ctx, Expr *expr) {
    if (!ctx || !expr || expr->type != EXPR_CALL) return;

    // Analyze function expression
    if (expr->as.call.func) {
        tricycle_analyze_expr(ctx, expr->as.call.func);
    }

    // Check for special calls
    if (tricycle_is_free_call(expr)) {
        // Check for double-free
        tricycle_check_free(ctx, expr, expr->line, expr->column);
        return;
    }

    if (tricycle_is_spawn_call(expr)) {
        // Check async safety
        tricycle_check_spawn(ctx, expr, expr->line, expr->column);
    }

    if (tricycle_is_join_call(expr)) {
        // Handle join - releases async borrows
        tricycle_check_join(ctx, expr, expr->line, expr->column);
    }

    if (tricycle_is_ptr_op(expr)) {
        // Pointer operation - check the pointer argument is valid
        if (expr->as.call.num_args > 0 && expr->as.call.args[0]) {
            Expr *ptr_arg = expr->as.call.args[0];
            if (ptr_arg->type == EXPR_IDENT) {
                tricycle_check_use(ctx, ptr_arg->as.ident.name, expr->line, expr->column);
            }
        }
    }

    // Analyze all arguments
    for (int i = 0; i < expr->as.call.num_args; i++) {
        if (expr->as.call.args[i]) {
            tricycle_analyze_expr(ctx, expr->as.call.args[i]);
        }
    }
}

static void analyze_assign_expr(TricycleContext *ctx, Expr *expr) {
    if (!ctx || !expr || expr->type != EXPR_ASSIGN) return;

    const char *name = expr->as.assign.name;
    Expr *value = expr->as.assign.value;

    // Analyze the value first
    if (value) {
        tricycle_analyze_expr(ctx, value);
    }

    OwnershipBinding *binding = tricycle_lookup(ctx, name);
    if (!binding) {
        // New variable in this scope - bind it
        if (tricycle_is_alloc_call(value)) {
            tricycle_bind(ctx, name, MEM_ALLOCATED, expr->line, expr->column, 1);
        } else if (value && value->type == EXPR_IDENT) {
            const char *source_name = value->as.ident.name;
            OwnershipBinding *source = tricycle_lookup(ctx, source_name);
            if (source && source->is_ptr_type && source->state.kind == MEM_ALLOCATED) {
                tricycle_bind(ctx, name, MEM_ALLOCATED, expr->line, expr->column, 1);
                tricycle_mark_moved(ctx, source_name, name, expr->line, expr->column);
            } else {
                tricycle_bind(ctx, name, MEM_UNKNOWN, expr->line, expr->column, 0);
            }
        } else {
            tricycle_bind(ctx, name, MEM_UNKNOWN, expr->line, expr->column,
                          tricycle_is_ptr_expr(ctx, value));
        }
        return;
    }

    // Existing variable - check for memory leak if overwriting allocated memory
    if (binding->is_ptr_type && binding->state.kind == MEM_ALLOCATED) {
        tricycle_warning(ctx, TRIC_MEMORY_LEAK, expr->line, expr->column,
                         "overwriting '%s' without freeing previously allocated memory", name);
        tricycle_add_related(ctx, binding->state.allocation_line,
                             binding->state.allocation_column, "allocated here");
        tricycle_add_hint(ctx, "call free() before reassigning");
    }

    // Update state based on new value
    if (tricycle_is_alloc_call(value)) {
        tricycle_update_state(ctx, name, MEM_ALLOCATED, expr->line, expr->column);
    } else if (value && value->type == EXPR_IDENT) {
        const char *source_name = value->as.ident.name;
        OwnershipBinding *source = tricycle_lookup(ctx, source_name);
        if (source && source->is_ptr_type && source->state.kind == MEM_ALLOCATED) {
            tricycle_update_state(ctx, name, MEM_ALLOCATED, expr->line, expr->column);
            tricycle_mark_moved(ctx, source_name, name, expr->line, expr->column);
        }
    } else if (value && value->type == EXPR_NULL) {
        tricycle_update_state(ctx, name, MEM_UNALLOCATED, expr->line, expr->column);
    }
}

static void analyze_function_expr(TricycleContext *ctx, Expr *expr) {
    if (!ctx || !expr || expr->type != EXPR_FUNCTION) return;

    // Push function scope
    tricycle_push_scope(ctx, SCOPE_FUNCTION);

    // Bind parameters
    for (int i = 0; i < expr->as.function.num_params; i++) {
        const char *param_name = expr->as.function.param_names[i];
        Type *param_type = expr->as.function.param_types[i];
        int is_ptr = tricycle_is_ptr_type(param_type);

        // For now, treat all params as borrowed by default
        tricycle_bind_param(ctx, param_name, OWNER_BORROW, expr->line, expr->column, is_ptr);
    }

    // Analyze function body
    if (expr->as.function.body) {
        tricycle_analyze_stmt(ctx, expr->as.function.body);
    }

    // Pop function scope
    tricycle_pop_scope(ctx);
}

// ========== SPECIFIC CHECKS ==========

void tricycle_check_free(TricycleContext *ctx, Expr *call_expr, int line, int column) {
    if (!ctx || !call_expr || call_expr->type != EXPR_CALL) return;
    if (call_expr->as.call.num_args < 1) return;

    Expr *ptr_arg = call_expr->as.call.args[0];
    const char *var_name = tricycle_get_var_name(ptr_arg);
    if (!var_name) {
        // Complex expression - analyze it
        tricycle_analyze_expr(ctx, ptr_arg);
        return;
    }

    OwnershipBinding *binding = tricycle_lookup(ctx, var_name);
    if (!binding) {
        // Unknown variable - might be okay
        return;
    }

    if (!binding->is_ptr_type) {
        // Not a pointer - freeing non-pointer is an error
        tricycle_error(ctx, TRIC_INVALID_OWNERSHIP, line, column,
                       "freeing non-pointer value '%s'", var_name);
        return;
    }

    switch (binding->state.kind) {
        case MEM_UNALLOCATED:
            // Double free!
            tricycle_error(ctx, TRIC_DOUBLE_FREE, line, column,
                           "double free detected for '%s'", var_name);
            tricycle_add_related(ctx, binding->state.allocation_line,
                                 binding->state.allocation_column, "allocated here");
            tricycle_add_related(ctx, binding->state.free_line,
                                 binding->state.free_column, "first freed here");
            tricycle_add_hint(ctx, "remove this free() call - memory was already freed");
            break;

        case MEM_MOVED:
            // Use after move
            tricycle_error(ctx, TRIC_USE_AFTER_MOVE, line, column,
                           "freeing moved value '%s'", var_name);
            tricycle_add_related(ctx, binding->state.move_line,
                                 binding->state.move_column, "moved here");
            if (binding->state.moved_to) {
                char hint[256];
                snprintf(hint, sizeof(hint), "free '%s' instead, which now owns the memory",
                         binding->state.moved_to);
                tricycle_add_hint(ctx, hint);
            }
            break;

        case MEM_BORROWED:
            // Freeing borrowed memory
            tricycle_error(ctx, TRIC_FREE_OF_BORROWED, line, column,
                           "freeing borrowed value '%s'", var_name);
            tricycle_add_hint(ctx, "borrowed values should not be freed - the owner is responsible");
            break;

        case MEM_ALLOCATED:
            // Valid free - mark as unallocated
            tricycle_update_state(ctx, var_name, MEM_UNALLOCATED, line, column);

            // Check if this memory is borrowed by an async task
            AsyncBorrow *async = ctx->async_borrows;
            while (async) {
                if (strcmp(async->var_name, var_name) == 0 && !async->is_joined) {
                    tricycle_error(ctx, TRIC_DANGLING_ASYNC, line, column,
                                   "freeing '%s' while async task may still use it", var_name);
                    tricycle_add_related(ctx, async->spawn_line, async->spawn_column,
                                         "passed to spawned task here");
                    tricycle_add_hint(ctx, "call join() on the task before freeing");
                    break;
                }
                async = async->next;
            }
            break;

        case MEM_UNKNOWN:
            // Unknown state - warn
            tricycle_warning(ctx, TRIC_UNKNOWN_LIFETIME, line, column,
                             "freeing '%s' with unknown ownership", var_name);
            tricycle_add_hint(ctx, "ensure this memory was allocated and is owned by this code");
            tricycle_update_state(ctx, var_name, MEM_UNALLOCATED, line, column);
            break;
    }
}

void tricycle_check_use(TricycleContext *ctx, const char *var_name, int line, int column) {
    if (!ctx || !var_name) return;

    OwnershipBinding *binding = tricycle_lookup(ctx, var_name);
    if (!binding || !binding->is_ptr_type) return;

    switch (binding->state.kind) {
        case MEM_UNALLOCATED:
            tricycle_error(ctx, TRIC_USE_AFTER_FREE, line, column,
                           "use of freed memory '%s'", var_name);
            tricycle_add_related(ctx, binding->state.allocation_line,
                                 binding->state.allocation_column, "allocated here");
            tricycle_add_related(ctx, binding->state.free_line,
                                 binding->state.free_column, "freed here");
            tricycle_add_hint(ctx, "do not use memory after it has been freed");
            break;

        case MEM_MOVED:
            tricycle_error(ctx, TRIC_USE_AFTER_MOVE, line, column,
                           "use of moved value '%s'", var_name);
            tricycle_add_related(ctx, binding->state.move_line,
                                 binding->state.move_column, "moved here");
            if (binding->state.moved_to) {
                char hint[256];
                snprintf(hint, sizeof(hint), "use '%s' instead, which now owns the memory",
                         binding->state.moved_to);
                tricycle_add_hint(ctx, hint);
            }
            break;

        default:
            // Valid use
            break;
    }
}

void tricycle_check_leaks(TricycleContext *ctx) {
    if (!ctx || !ctx->current_env) return;

    // Check for leaks in any scope - memory should be freed before scope ends
    // This handles variables declared in blocks, loops, etc.
    // Note: We only warn (not error) since the memory will eventually be freed
    // when the program exits, but it's still a resource management issue.

    OwnershipBinding *binding = ctx->current_env->bindings;
    while (binding) {
        if (binding->is_ptr_type && binding->state.kind == MEM_ALLOCATED) {
            // Memory allocated but not freed before scope ends
            tricycle_warning(ctx, TRIC_MEMORY_LEAK, binding->declaration_line,
                             binding->declaration_column,
                             "memory leak: '%s' is never freed", binding->name);
            tricycle_add_related(ctx, binding->state.allocation_line,
                                 binding->state.allocation_column, "allocated here");
            tricycle_add_hint(ctx, "add free() before the scope ends, or return the pointer to transfer ownership");
        }
        binding = binding->next;
    }
}

void tricycle_check_spawn(TricycleContext *ctx, Expr *call_expr, int line, int column) {
    if (!ctx || !call_expr) return;

    // Check pointer arguments passed to spawn
    static int task_counter = 0;
    task_counter++;

    for (int i = 1; i < call_expr->as.call.num_args; i++) {
        Expr *arg = call_expr->as.call.args[i];
        const char *var_name = tricycle_get_var_name(arg);
        if (!var_name) continue;

        OwnershipBinding *binding = tricycle_lookup(ctx, var_name);
        if (!binding || !binding->is_ptr_type) continue;

        if (binding->state.kind != MEM_ALLOCATED) {
            tricycle_error(ctx, TRIC_DANGLING_ASYNC, line, column,
                           "passing invalid pointer '%s' to spawned task", var_name);
            if (binding->state.kind == MEM_UNALLOCATED) {
                tricycle_add_related(ctx, binding->state.free_line,
                                     binding->state.free_column, "freed here");
            } else if (binding->state.kind == MEM_MOVED) {
                tricycle_add_related(ctx, binding->state.move_line,
                                     binding->state.move_column, "moved here");
            }
            continue;
        }

        // Add async borrow record
        AsyncBorrow *borrow = calloc(1, sizeof(AsyncBorrow));
        if (borrow) {
            borrow->var_name = strdup(var_name);
            borrow->spawn_line = line;
            borrow->spawn_column = column;
            borrow->task_id = task_counter;
            borrow->is_joined = 0;
            borrow->next = ctx->async_borrows;
            ctx->async_borrows = borrow;
        }
    }
}

void tricycle_check_join(TricycleContext *ctx, Expr *task_expr, int line, int column) {
    (void)task_expr;
    (void)line;
    (void)column;

    if (!ctx) return;

    // Mark all async borrows as joined
    // (In a more sophisticated analysis, we'd track which task is being joined)
    AsyncBorrow *borrow = ctx->async_borrows;
    while (borrow) {
        borrow->is_joined = 1;
        borrow = borrow->next;
    }
}
