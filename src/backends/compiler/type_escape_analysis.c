/*
 * Hemlock Compiler - Escape Analysis and Tail Recursion Detection
 *
 * Determines whether variables escape their scope (passed to functions,
 * stored in data structures, etc.) and detects tail-recursive functions.
 */

#include "type_check_internal.h"

// ========== ESCAPE ANALYSIS ==========

int variable_escapes_in_expr_internal(Expr *expr, const char *var_name) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_IDENT:
            // Variable usage itself doesn't escape
            return 0;

        case EXPR_CALL:
            // Passing a variable as a function argument does NOT escape it for
            // unboxing purposes. Hemlock passes primitives by value — the codegen
            // boxes the value (e.g., hml_val_i32(var)) when passing to a function,
            // so the function receives a copy. The unboxed local is not exposed.
            // We still need to check if the variable escapes within complex
            // argument expressions (e.g., func({field: var}) would escape).
            for (int i = 0; i < expr->as.call.num_args; i++) {
                Expr *arg = expr->as.call.args[i];
                // Simple identifier as argument - safe, will be boxed at call site
                if (arg->type == EXPR_IDENT && strcmp(arg->as.ident.name, var_name) == 0) {
                    continue;  // Not an escape - value is copied/boxed
                }
                if (variable_escapes_in_expr_internal(arg, var_name)) return 1;
            }
            return variable_escapes_in_expr_internal(expr->as.call.func, var_name);

        case EXPR_BINARY:
            return variable_escapes_in_expr_internal(expr->as.binary.left, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.binary.right, var_name);

        case EXPR_UNARY:
            return variable_escapes_in_expr_internal(expr->as.unary.operand, var_name);

        case EXPR_ASSIGN:
            return variable_escapes_in_expr_internal(expr->as.assign.value, var_name);

        case EXPR_INDEX:
            if (expr->as.index.object->type == EXPR_IDENT &&
                strcmp(expr->as.index.object->as.ident.name, var_name) == 0) {
                return 1;  // Using variable as array - escapes
            }
            return variable_escapes_in_expr_internal(expr->as.index.index, var_name);

        case EXPR_INDEX_ASSIGN:
            if (expr->as.index_assign.value->type == EXPR_IDENT &&
                strcmp(expr->as.index_assign.value->as.ident.name, var_name) == 0) {
                return 1;  // Storing variable in array - escapes
            }
            return variable_escapes_in_expr_internal(expr->as.index_assign.object, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.index_assign.index, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.index_assign.value, var_name);

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                Expr *elem = expr->as.array_literal.elements[i];
                if (elem->type == EXPR_IDENT && strcmp(elem->as.ident.name, var_name) == 0) {
                    return 1;
                }
                if (variable_escapes_in_expr_internal(elem, var_name)) return 1;
            }
            return 0;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                Expr *val = expr->as.object_literal.field_values[i];
                if (val->type == EXPR_IDENT && strcmp(val->as.ident.name, var_name) == 0) {
                    return 1;
                }
                if (variable_escapes_in_expr_internal(val, var_name)) return 1;
            }
            return 0;

        case EXPR_TERNARY:
            return variable_escapes_in_expr_internal(expr->as.ternary.condition, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.ternary.true_expr, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.ternary.false_expr, var_name);

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            return variable_escapes_in_expr_internal(expr->as.prefix_inc.operand, var_name);

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return variable_escapes_in_expr_internal(expr->as.postfix_inc.operand, var_name);

        case EXPR_FUNCTION:
            // Conservative: assume function captures variable
            return 1;

        case EXPR_GET_PROPERTY:
            return variable_escapes_in_expr_internal(expr->as.get_property.object, var_name);

        case EXPR_SET_PROPERTY:
            if (expr->as.set_property.value->type == EXPR_IDENT &&
                strcmp(expr->as.set_property.value->as.ident.name, var_name) == 0) {
                return 1;  // Storing variable in an object field - escapes
            }
            return variable_escapes_in_expr_internal(expr->as.set_property.object, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.set_property.value, var_name);

        case EXPR_STRING_INTERPOLATION:
            // A closure embedded in a template string (`${fn(){...}()}`)
            // must be seen - falling through to "no escape" let mutations
            // made through such closures be lost to unboxing.
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                if (variable_escapes_in_expr_internal(expr->as.string_interpolation.expr_parts[i], var_name)) {
                    return 1;
                }
            }
            return 0;

        case EXPR_AWAIT:
            return variable_escapes_in_expr_internal(expr->as.await_expr.awaited_expr, var_name);

        case EXPR_NULL_COALESCE:
            return variable_escapes_in_expr_internal(expr->as.null_coalesce.left, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.null_coalesce.right, var_name);

        case EXPR_OPTIONAL_CHAIN:
            if (variable_escapes_in_expr_internal(expr->as.optional_chain.object, var_name)) return 1;
            if (expr->as.optional_chain.index &&
                variable_escapes_in_expr_internal(expr->as.optional_chain.index, var_name)) return 1;
            for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                if (expr->as.optional_chain.args &&
                    variable_escapes_in_expr_internal(expr->as.optional_chain.args[i], var_name)) return 1;
            }
            return 0;

        case EXPR_MATCH:
            // Match arms can contain closures (and their bodies/guards any
            // other escaping construct); treat like any compound expression.
            if (variable_escapes_in_expr_internal(expr->as.match_expr.scrutinee, var_name)) return 1;
            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                MatchArm *arm = &expr->as.match_expr.arms[i];
                if (arm->guard && variable_escapes_in_expr_internal(arm->guard, var_name)) return 1;
                if (variable_escapes_in_expr_internal(arm->body, var_name)) return 1;
            }
            return 0;

        default:
            return 0;
    }
}

int variable_escapes_in_stmt_internal(Stmt *stmt, const char *var_name) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case STMT_EXPR:
            return variable_escapes_in_expr_internal(stmt->as.expr, var_name);

        case STMT_LET:
        case STMT_CONST:
            if (stmt->as.let.value) {
                return variable_escapes_in_expr_internal(stmt->as.let.value, var_name);
            }
            return 0;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                if (stmt->as.return_stmt.value->type == EXPR_IDENT &&
                    strcmp(stmt->as.return_stmt.value->as.ident.name, var_name) == 0) {
                    return 1;  // Returning the variable - escapes
                }
                return variable_escapes_in_expr_internal(stmt->as.return_stmt.value, var_name);
            }
            return 0;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (variable_escapes_in_stmt_internal(stmt->as.block.statements[i], var_name)) {
                    return 1;
                }
            }
            return 0;

        case STMT_IF:
            return variable_escapes_in_expr_internal(stmt->as.if_stmt.condition, var_name) ||
                   variable_escapes_in_stmt_internal(stmt->as.if_stmt.then_branch, var_name) ||
                   (stmt->as.if_stmt.else_branch && variable_escapes_in_stmt_internal(stmt->as.if_stmt.else_branch, var_name));

        case STMT_WHILE:
            return variable_escapes_in_expr_internal(stmt->as.while_stmt.condition, var_name) ||
                   variable_escapes_in_stmt_internal(stmt->as.while_stmt.body, var_name);

        case STMT_LOOP:
            return variable_escapes_in_stmt_internal(stmt->as.loop_stmt.body, var_name);

        case STMT_FOR:
            return (stmt->as.for_loop.initializer && variable_escapes_in_stmt_internal(stmt->as.for_loop.initializer, var_name)) ||
                   (stmt->as.for_loop.condition && variable_escapes_in_expr_internal(stmt->as.for_loop.condition, var_name)) ||
                   (stmt->as.for_loop.increment && variable_escapes_in_expr_internal(stmt->as.for_loop.increment, var_name)) ||
                   variable_escapes_in_stmt_internal(stmt->as.for_loop.body, var_name);

        case STMT_FOR_IN:
            return variable_escapes_in_expr_internal(stmt->as.for_in.iterable, var_name) ||
                   variable_escapes_in_stmt_internal(stmt->as.for_in.body, var_name);

        case STMT_TRY:
            return variable_escapes_in_stmt_internal(stmt->as.try_stmt.try_block, var_name) ||
                   (stmt->as.try_stmt.catch_block && variable_escapes_in_stmt_internal(stmt->as.try_stmt.catch_block, var_name)) ||
                   (stmt->as.try_stmt.finally_block && variable_escapes_in_stmt_internal(stmt->as.try_stmt.finally_block, var_name));

        case STMT_SWITCH:
            if (variable_escapes_in_expr_internal(stmt->as.switch_stmt.expr, var_name)) return 1;
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (variable_escapes_in_stmt_internal(stmt->as.switch_stmt.case_bodies[i], var_name)) {
                    return 1;
                }
            }
            return 0;

        case STMT_THROW:
            return variable_escapes_in_expr_internal(stmt->as.throw_stmt.value, var_name);

        case STMT_DEFER:
            // Deferred statements compile to closures evaluated at function
            // exit - treat every candidate variable as escaping so it stays
            // boxed and the closure sees later mutations.
            return 1;

        default:
            return 0;
    }
}

int type_check_variable_escapes(const char *var_name, Stmt *stmt) {
    return variable_escapes_in_stmt_internal(stmt, var_name);
}

int type_check_variable_escapes_in_expr(const char *var_name, Expr *expr) {
    return variable_escapes_in_expr_internal(expr, var_name);
}

// ========== TAIL CALL OPTIMIZATION ==========

// Helper: Check if expression contains a call to the given function (non-tail position)
int contains_recursive_call(Expr *expr, const char *func_name) {
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

        case EXPR_MATCH:
            // Check the scrutinee expression
            if (contains_recursive_call(expr->as.match_expr.scrutinee, func_name)) return 1;
            // Check all match arms (guards and bodies)
            for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
                MatchArm *arm = &expr->as.match_expr.arms[i];
                if (arm->guard && contains_recursive_call(arm->guard, func_name)) return 1;
                if (contains_recursive_call(arm->body, func_name)) return 1;
            }
            return 0;

        case EXPR_NULL_COALESCE:
            return contains_recursive_call(expr->as.null_coalesce.left, func_name) ||
                   contains_recursive_call(expr->as.null_coalesce.right, func_name);

        case EXPR_OPTIONAL_CHAIN:
            if (contains_recursive_call(expr->as.optional_chain.object, func_name)) return 1;
            if (expr->as.optional_chain.index &&
                contains_recursive_call(expr->as.optional_chain.index, func_name)) return 1;
            // Check method call arguments
            for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                if (expr->as.optional_chain.args &&
                    contains_recursive_call(expr->as.optional_chain.args[i], func_name)) return 1;
            }
            return 0;

        case EXPR_GET_PROPERTY:
            return contains_recursive_call(expr->as.get_property.object, func_name);

        case EXPR_SET_PROPERTY:
            return contains_recursive_call(expr->as.set_property.object, func_name) ||
                   contains_recursive_call(expr->as.set_property.value, func_name);

        default:
            return 0;
    }
}

// Helper: Check if a statement contains a recursive call anywhere (for loop analysis)
int stmt_contains_recursive_call(Stmt *stmt, const char *func_name) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case STMT_EXPR:
            return contains_recursive_call(stmt->as.expr, func_name);

        case STMT_LET:
        case STMT_CONST:
            return stmt->as.let.value && contains_recursive_call(stmt->as.let.value, func_name);

        case STMT_RETURN:
            return stmt->as.return_stmt.value &&
                   contains_recursive_call(stmt->as.return_stmt.value, func_name);

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (stmt_contains_recursive_call(stmt->as.block.statements[i], func_name)) return 1;
            }
            return 0;

        case STMT_IF:
            if (contains_recursive_call(stmt->as.if_stmt.condition, func_name)) return 1;
            if (stmt_contains_recursive_call(stmt->as.if_stmt.then_branch, func_name)) return 1;
            if (stmt->as.if_stmt.else_branch &&
                stmt_contains_recursive_call(stmt->as.if_stmt.else_branch, func_name)) return 1;
            return 0;

        case STMT_WHILE:
            if (contains_recursive_call(stmt->as.while_stmt.condition, func_name)) return 1;
            return stmt_contains_recursive_call(stmt->as.while_stmt.body, func_name);

        case STMT_LOOP:
            return stmt_contains_recursive_call(stmt->as.loop_stmt.body, func_name);

        case STMT_FOR:
            if (stmt->as.for_loop.initializer && stmt_contains_recursive_call(stmt->as.for_loop.initializer, func_name)) return 1;
            if (stmt->as.for_loop.condition &&
                contains_recursive_call(stmt->as.for_loop.condition, func_name)) return 1;
            if (stmt->as.for_loop.increment &&
                contains_recursive_call(stmt->as.for_loop.increment, func_name)) return 1;
            return stmt_contains_recursive_call(stmt->as.for_loop.body, func_name);

        case STMT_FOR_IN:
            if (contains_recursive_call(stmt->as.for_in.iterable, func_name)) return 1;
            return stmt_contains_recursive_call(stmt->as.for_in.body, func_name);

        case STMT_SWITCH:
            if (contains_recursive_call(stmt->as.switch_stmt.expr, func_name)) return 1;
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] &&
                    contains_recursive_call(stmt->as.switch_stmt.case_values[i], func_name)) return 1;
                if (stmt_contains_recursive_call(stmt->as.switch_stmt.case_bodies[i], func_name)) return 1;
            }
            return 0;

        case STMT_TRY:
            if (stmt_contains_recursive_call(stmt->as.try_stmt.try_block, func_name)) return 1;
            if (stmt->as.try_stmt.catch_block &&
                stmt_contains_recursive_call(stmt->as.try_stmt.catch_block, func_name)) return 1;
            if (stmt->as.try_stmt.finally_block &&
                stmt_contains_recursive_call(stmt->as.try_stmt.finally_block, func_name)) return 1;
            return 0;

        case STMT_THROW:
            return contains_recursive_call(stmt->as.throw_stmt.value, func_name);

        case STMT_DEFER:
            return contains_recursive_call(stmt->as.defer_stmt.call, func_name);

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
            // Loops are OK for TCO if they don't contain recursive calls
            // This allows patterns like: for (init) { ... } return f(x);
            if (stmt_contains_recursive_call(stmt, func_name)) return 0;
            return 1;

        case STMT_LOOP:
            // Loop statements are OK if they don't contain recursive calls
            if (stmt_contains_recursive_call(stmt, func_name)) return 0;
            return 1;

        case STMT_FOR:
            // For loops are OK if they don't contain recursive calls
            if (stmt_contains_recursive_call(stmt, func_name)) return 0;
            return 1;

        case STMT_FOR_IN:
            // For-in loops are OK if they don't contain recursive calls
            if (stmt_contains_recursive_call(stmt, func_name)) return 0;
            return 1;

        case STMT_SWITCH:
            // Switch is compatible with TCO if condition doesn't recurse
            // and all case bodies are tail recursive
            if (contains_recursive_call(stmt->as.switch_stmt.expr, func_name)) return 0;
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                // Check case values for recursive calls
                if (stmt->as.switch_stmt.case_values[i] &&
                    contains_recursive_call(stmt->as.switch_stmt.case_values[i], func_name)) {
                    return 0;
                }
                // Check case bodies are tail recursive
                if (!stmt_is_tail_recursive(stmt->as.switch_stmt.case_bodies[i], func_name)) {
                    return 0;
                }
            }
            return 1;

        case STMT_TRY:
            // Try-catch is not compatible with simple tail call optimization
            // because we need to maintain the exception handler on the stack
            return 0;

        case STMT_DEFER:
            // Defer is not compatible with tail call optimization
            // because defers must execute before the function returns
            return 0;

        case STMT_BREAK:
        case STMT_CONTINUE:
            // Control flow statements are fine
            return 1;

        case STMT_THROW:
            // Throw just needs to not contain recursive calls
            return !contains_recursive_call(stmt->as.throw_stmt.value, func_name);

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
