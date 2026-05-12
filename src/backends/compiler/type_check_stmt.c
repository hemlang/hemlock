/*
 * Hemlock Compiler - Statement Type Checking
 *
 * Statement validation, function body checking, signature collection,
 * and the main type_check_program entry point.
 */

#include "type_check_internal.h"

// ========== FUNCTION BODY CHECKING ==========

void type_check_function_body(TypeCheckContext *ctx, Expr *func, const char *name) {
    type_check_push_scope(ctx);

    // Save current function context
    CheckedType *saved_return_type = ctx->current_return_type;
    char *saved_func_name = ctx->current_function_name;
    int saved_async = ctx->in_async_function;

    // Set up function context
    ctx->current_return_type = func->as.function.return_type
        ? checked_type_from_ast_ctx(ctx, func->as.function.return_type)
        : NULL;
    ctx->current_function_name = name ? strdup(name) : NULL;
    ctx->in_async_function = func->as.function.is_async;

    // Bind parameters
    for (int i = 0; i < func->as.function.num_params; i++) {
        CheckedType *param_type;
        if (func->as.function.param_types[i]) {
            param_type = checked_type_from_ast_ctx(ctx, func->as.function.param_types[i]);
        } else {
            param_type = checked_type_primitive(CHECKED_ANY);
        }
        type_check_bind(ctx, func->as.function.param_names[i], param_type, 0, 1, 0);  // is_param=1
    }

    // Bind rest parameter if present
    if (func->as.function.rest_param) {
        CheckedType *rest_type = checked_type_array(
            func->as.function.rest_param_type
                ? checked_type_from_ast_ctx(ctx, func->as.function.rest_param_type)
                : checked_type_primitive(CHECKED_ANY)
        );
        type_check_bind(ctx, func->as.function.rest_param, rest_type, 0, 1, 0);  // is_param=1
    }

    // Check body
    if (func->as.function.body) {
        type_check_stmt(ctx, func->as.function.body);
    }

    // Restore context
    checked_type_free(ctx->current_return_type);
    free(ctx->current_function_name);
    ctx->current_return_type = saved_return_type;
    ctx->current_function_name = saved_func_name;
    ctx->in_async_function = saved_async;

    type_check_pop_scope(ctx);
}

// ========== STRUCTURAL VALIDATION FOR OBJECT LITERALS ==========

// Check if a type contains any type parameters (generics) that can't be validated
int type_contains_param(CheckedType *type) {
    if (!type) return 0;
    if (type->kind == CHECKED_PARAM) return 1;
    if (type->element_type && type_contains_param(type->element_type)) return 1;
    if (type->return_type && type_contains_param(type->return_type)) return 1;
    for (int i = 0; i < type->num_params; i++) {
        if (type->param_types && type_contains_param(type->param_types[i])) return 1;
    }
    for (int i = 0; i < type->num_compound_types; i++) {
        if (type->compound_types && type_contains_param(type->compound_types[i])) return 1;
    }
    return 0;
}

// Validate an object literal against a custom type definition at compile time.
// This provides compile-time safety for duck typing by checking field presence and types.
void type_check_validate_object_literal(TypeCheckContext *ctx, Expr *expr,
                                        const char *type_name, int line) {
    if (!ctx || !expr || !type_name) return;
    if (expr->type != EXPR_OBJECT_LITERAL) return;

    ObjectDef *def = type_check_lookup_object(ctx, type_name);
    if (!def) {
        // Unknown type - let runtime handle it (might be defined elsewhere)
        return;
    }

    // Check each required field in the type definition
    for (int i = 0; i < def->num_fields; i++) {
        const char *field_name = def->field_names[i];
        int is_optional = def->field_optional && def->field_optional[i];

        // Skip optional fields
        if (is_optional) continue;

        // Look for this field in the object literal
        int found = 0;
        Expr *field_value = NULL;
        for (int j = 0; j < expr->as.object_literal.num_fields; j++) {
            // Handle spread operator (field_name is NULL for spreads)
            if (expr->as.object_literal.field_names[j] == NULL) {
                // Spread - we can't statically verify, so assume it might provide the field
                found = 1;
                break;
            }
            if (strcmp(expr->as.object_literal.field_names[j], field_name) == 0) {
                found = 1;
                field_value = expr->as.object_literal.field_values[j];
                break;
            }
        }

        if (!found) {
            type_error(ctx, line, "missing required field '%s' for type '%s'",
                      field_name, type_name);
            continue;
        }

        // Type check the field value if we have type information
        if (field_value && def->field_types && def->field_types[i]) {
            CheckedType *expected_type = def->field_types[i];
            // Skip type checking for types containing type parameters - can't validate without substitution
            if (type_contains_param(expected_type)) {
                continue;
            }
            CheckedType *actual_type = type_check_infer_expr(ctx, field_value);
            if (actual_type && !type_is_assignable(expected_type, actual_type)) {
                type_error(ctx, line, "field '%s' of type '%s' expects '%s', got '%s'",
                          field_name, type_name,
                          checked_type_name(expected_type),
                          checked_type_name(actual_type));
            }
            checked_type_free(actual_type);
        }
    }
}

// Validate an object literal against a compound type (A & B & C)
void type_check_validate_compound_object(TypeCheckContext *ctx, Expr *expr,
                                         Type *compound_type, int line) {
    if (!ctx || !expr || !compound_type) return;
    if (expr->type != EXPR_OBJECT_LITERAL) return;
    if (compound_type->kind != TYPE_COMPOUND) return;

    // Validate against each constituent type
    for (int i = 0; i < compound_type->num_compound_types; i++) {
        Type *constituent = compound_type->compound_types[i];
        if (constituent->kind == TYPE_CUSTOM_OBJECT && constituent->type_name) {
            type_check_validate_object_literal(ctx, expr, constituent->type_name, line);
        }
    }
}


static int stmt_definitely_returns(Stmt *stmt) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case STMT_RETURN:
            return 1;
        case STMT_BLOCK:
            if (stmt->as.block.count == 0) return 0;
            return stmt_definitely_returns(stmt->as.block.statements[stmt->as.block.count - 1]);
        case STMT_IF:
            return stmt->as.if_stmt.then_branch && stmt->as.if_stmt.else_branch &&
                   stmt_definitely_returns(stmt->as.if_stmt.then_branch) &&
                   stmt_definitely_returns(stmt->as.if_stmt.else_branch);
        default:
            return 0;
    }
}

static Expr* null_checked_expr(Expr *condition, int *non_null_when_false) {
    if (!condition || condition->type != EXPR_BINARY) return NULL;

    BinaryOp op = condition->as.binary.op;
    if (op != OP_EQUAL && op != OP_NOT_EQUAL) return NULL;

    Expr *left = condition->as.binary.left;
    Expr *right = condition->as.binary.right;
    Expr *checked = NULL;

    if (left && left->type == EXPR_NULL) {
        checked = right;
    } else if (right && right->type == EXPR_NULL) {
        checked = left;
    } else {
        return NULL;
    }

    *non_null_when_false = (op == OP_EQUAL);
    return checked;
}

static void add_non_null_narrowing(TypeCheckContext *ctx, Expr *expr) {
    char *key = type_check_expr_key(expr);
    if (!key) return;

    CheckedType *type = type_check_infer_expr(ctx, expr);
    if (type && type->kind != CHECKED_ANY && type->kind != CHECKED_NULL) {
        type->nullable = 0;
        type_check_add_narrowing(ctx, key, type);
    }

    checked_type_free(type);
    free(key);
}

// ========== STATEMENT TYPE CHECKING ==========

void type_check_stmt(TypeCheckContext *ctx, Stmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_LET: {
            if (stmt->as.let.value) {
                type_check_expr(ctx, stmt->as.let.value);
            }

            CheckedType *declared_type = NULL;
            if (stmt->as.let.type_annotation) {
                declared_type = checked_type_from_ast_ctx(ctx, stmt->as.let.type_annotation);

                // Check that initializer matches declared type
                if (stmt->as.let.value) {
                    CheckedType *init_type = type_check_infer_expr(ctx, stmt->as.let.value);
                    if (!type_is_assignable(declared_type, init_type)) {
                        type_error(ctx, stmt->line,
                            "cannot initialize '%s' of type '%s' with '%s'",
                            stmt->as.let.name, checked_type_name(declared_type),
                            checked_type_name(init_type));
                    }
                    checked_type_free(init_type);

                    // Structural validation for object literals assigned to custom types
                    if (stmt->as.let.value->type == EXPR_OBJECT_LITERAL) {
                        if (stmt->as.let.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
                            stmt->as.let.type_annotation->type_name) {
                            type_check_validate_object_literal(ctx, stmt->as.let.value,
                                stmt->as.let.type_annotation->type_name, stmt->line);
                        } else if (stmt->as.let.type_annotation->kind == TYPE_COMPOUND) {
                            type_check_validate_compound_object(ctx, stmt->as.let.value,
                                stmt->as.let.type_annotation, stmt->line);
                        }
                    }
                }
            } else if (stmt->as.let.value) {
                declared_type = type_check_infer_expr(ctx, stmt->as.let.value);
            } else {
                declared_type = checked_type_primitive(CHECKED_ANY);
            }

            type_check_bind(ctx, stmt->as.let.name, declared_type, 0, 0, stmt->line);
            break;
        }

        case STMT_CONST: {
            if (stmt->as.const_stmt.value) {
                type_check_expr(ctx, stmt->as.const_stmt.value);
            }

            CheckedType *declared_type = NULL;
            if (stmt->as.const_stmt.type_annotation) {
                declared_type = checked_type_from_ast_ctx(ctx, stmt->as.const_stmt.type_annotation);

                if (stmt->as.const_stmt.value) {
                    CheckedType *init_type = type_check_infer_expr(ctx, stmt->as.const_stmt.value);
                    if (!type_is_assignable(declared_type, init_type)) {
                        type_error(ctx, stmt->line,
                            "cannot initialize const '%s' of type '%s' with '%s'",
                            stmt->as.const_stmt.name, checked_type_name(declared_type),
                            checked_type_name(init_type));
                    }
                    checked_type_free(init_type);

                    // Structural validation for object literals assigned to custom types
                    if (stmt->as.const_stmt.value->type == EXPR_OBJECT_LITERAL) {
                        if (stmt->as.const_stmt.type_annotation->kind == TYPE_CUSTOM_OBJECT &&
                            stmt->as.const_stmt.type_annotation->type_name) {
                            type_check_validate_object_literal(ctx, stmt->as.const_stmt.value,
                                stmt->as.const_stmt.type_annotation->type_name, stmt->line);
                        } else if (stmt->as.const_stmt.type_annotation->kind == TYPE_COMPOUND) {
                            type_check_validate_compound_object(ctx, stmt->as.const_stmt.value,
                                stmt->as.const_stmt.type_annotation, stmt->line);
                        }
                    }
                }
            } else if (stmt->as.const_stmt.value) {
                declared_type = type_check_infer_expr(ctx, stmt->as.const_stmt.value);
            } else {
                declared_type = checked_type_primitive(CHECKED_ANY);
            }

            type_check_bind(ctx, stmt->as.const_stmt.name, declared_type, 1, 0, stmt->line);
            break;
        }

        case STMT_EXPR:
            type_check_expr(ctx, stmt->as.expr);
            break;

        case STMT_IF: {
            type_check_expr(ctx, stmt->as.if_stmt.condition);

            int non_null_when_false = 0;
            Expr *checked = null_checked_expr(stmt->as.if_stmt.condition, &non_null_when_false);
            int then_returns = stmt_definitely_returns(stmt->as.if_stmt.then_branch);

            type_check_stmt(ctx, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                type_check_stmt(ctx, stmt->as.if_stmt.else_branch);
            }

            if (checked && !stmt->as.if_stmt.else_branch && then_returns && non_null_when_false) {
                add_non_null_narrowing(ctx, checked);
            }
            break;
        }

        case STMT_WHILE:
            type_check_expr(ctx, stmt->as.while_stmt.condition);
            type_check_stmt(ctx, stmt->as.while_stmt.body);
            break;

        case STMT_LOOP:
            type_check_stmt(ctx, stmt->as.loop_stmt.body);
            break;

        case STMT_FOR:
            type_check_push_scope(ctx);
            if (stmt->as.for_loop.initializer) {
                type_check_stmt(ctx, stmt->as.for_loop.initializer);
            }
            if (stmt->as.for_loop.condition) {
                type_check_expr(ctx, stmt->as.for_loop.condition);
            }
            if (stmt->as.for_loop.increment) {
                type_check_expr(ctx, stmt->as.for_loop.increment);
            }
            type_check_stmt(ctx, stmt->as.for_loop.body);
            type_check_pop_scope(ctx);
            break;

        case STMT_FOR_IN: {
            type_check_push_scope(ctx);
            type_check_expr(ctx, stmt->as.for_in.iterable);

            // Infer types for loop variables
            CheckedType *iter_type = type_check_infer_expr(ctx, stmt->as.for_in.iterable);
            CheckedType *value_type = checked_type_primitive(CHECKED_ANY);

            if (iter_type->kind == CHECKED_ARRAY && iter_type->element_type) {
                checked_type_free(value_type);
                value_type = checked_type_clone(iter_type->element_type);
            } else if (iter_type->kind == CHECKED_STRING) {
                checked_type_free(value_type);
                value_type = checked_type_primitive(CHECKED_RUNE);
            }

            if (stmt->as.for_in.key_var) {
                type_check_bind(ctx, stmt->as.for_in.key_var,
                    checked_type_primitive(CHECKED_I32), 0, 0, stmt->line);
            }
            type_check_bind(ctx, stmt->as.for_in.value_var, value_type, 0, 0, stmt->line);

            checked_type_free(iter_type);
            type_check_stmt(ctx, stmt->as.for_in.body);
            type_check_pop_scope(ctx);
            break;
        }

        case STMT_BLOCK:
            type_check_push_scope(ctx);
            for (int i = 0; i < stmt->as.block.count; i++) {
                type_check_stmt(ctx, stmt->as.block.statements[i]);
            }
            type_check_pop_scope(ctx);
            break;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                type_check_expr(ctx, stmt->as.return_stmt.value);

                if (ctx->current_return_type) {
                    CheckedType *ret_type = type_check_infer_expr(ctx, stmt->as.return_stmt.value);
                    if (!type_is_assignable(ctx->current_return_type, ret_type)) {
                        type_error(ctx, stmt->line,
                            "return type mismatch: expected '%s', got '%s'",
                            checked_type_name(ctx->current_return_type),
                            checked_type_name(ret_type));
                    }
                    checked_type_free(ret_type);
                }
            } else if (ctx->current_return_type &&
                       ctx->current_return_type->kind != CHECKED_VOID &&
                       ctx->current_return_type->kind != CHECKED_ANY) {
                type_warning(ctx, stmt->line,
                    "missing return value, expected '%s'",
                    checked_type_name(ctx->current_return_type));
            }
            break;

        case STMT_DEFINE_OBJECT: {
            // Register the object type
            CheckedType **field_types = NULL;
            if (stmt->as.define_object.num_fields > 0) {
                field_types = calloc(stmt->as.define_object.num_fields, sizeof(CheckedType*));
                for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                    if (stmt->as.define_object.field_types[i]) {
                        field_types[i] = checked_type_from_ast_ctx(ctx, stmt->as.define_object.field_types[i]);
                    } else {
                        field_types[i] = checked_type_primitive(CHECKED_ANY);
                    }
                }
            }
            // Convert method types
            CheckedType **method_types = NULL;
            if (stmt->as.define_object.num_methods > 0) {
                method_types = calloc(stmt->as.define_object.num_methods, sizeof(CheckedType*));
                for (int i = 0; i < stmt->as.define_object.num_methods; i++) {
                    if (stmt->as.define_object.method_types[i]) {
                        method_types[i] = checked_type_from_ast_ctx(ctx, stmt->as.define_object.method_types[i]);
                    } else {
                        method_types[i] = checked_type_primitive(CHECKED_ANY);
                    }
                }
            }
            type_check_register_object(ctx, stmt->as.define_object.name,
                stmt->as.define_object.type_params, stmt->as.define_object.num_type_params,
                stmt->as.define_object.field_names, field_types,
                stmt->as.define_object.field_optional,
                stmt->as.define_object.num_fields,
                stmt->as.define_object.method_names, method_types,
                stmt->as.define_object.method_optional,
                stmt->as.define_object.num_methods);
            break;
        }

        case STMT_TYPE_ALIAS:
            // Type aliases are registered during the first pass in collect_function_signatures
            // Nothing to do here during the type checking pass
            break;

        case STMT_ENUM: {
            type_check_register_enum(ctx, stmt->as.enum_decl.name,
                stmt->as.enum_decl.variant_names, stmt->as.enum_decl.num_variants);

            // Also bind enum to scope as a type
            CheckedType *enum_type = checked_type_primitive(CHECKED_ENUM);
            enum_type->type_name = strdup(stmt->as.enum_decl.name);
            type_check_bind(ctx, stmt->as.enum_decl.name, enum_type, 1, 0, stmt->line);
            break;
        }

        case STMT_TRY:
            type_check_stmt(ctx, stmt->as.try_stmt.try_block);
            if (stmt->as.try_stmt.catch_block) {
                type_check_push_scope(ctx);
                if (stmt->as.try_stmt.catch_param) {
                    type_check_bind(ctx, stmt->as.try_stmt.catch_param,
                        checked_type_primitive(CHECKED_ANY), 0, 0, stmt->line);
                }
                type_check_stmt(ctx, stmt->as.try_stmt.catch_block);
                type_check_pop_scope(ctx);
            }
            if (stmt->as.try_stmt.finally_block) {
                type_check_stmt(ctx, stmt->as.try_stmt.finally_block);
            }
            break;

        case STMT_THROW:
            type_check_expr(ctx, stmt->as.throw_stmt.value);
            break;

        case STMT_SWITCH:
            type_check_expr(ctx, stmt->as.switch_stmt.expr);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i]) {
                    type_check_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                }
                type_check_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
            }
            break;

        case STMT_DEFER:
            type_check_expr(ctx, stmt->as.defer_stmt.call);
            break;

        case STMT_EXPORT:
            if (stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
                type_check_stmt(ctx, stmt->as.export_stmt.declaration);
            }
            break;

        default:
            break;
    }
}

// ========== FIRST PASS: COLLECT SIGNATURES ==========

void collect_function_signatures(TypeCheckContext *ctx, Stmt **stmts, int count) {
    // First pass: collect type aliases (so other types can reference them)
    for (int i = 0; i < count; i++) {
        Stmt *stmt = stmts[i];
        if (!stmt) continue;

        if (stmt->type == STMT_TYPE_ALIAS) {
            CheckedType *aliased = checked_type_from_ast(stmt->as.type_alias.aliased_type);
            type_check_register_type_alias(ctx, stmt->as.type_alias.name,
                aliased,
                stmt->as.type_alias.type_params,
                stmt->as.type_alias.num_type_params);
        }

        // Handle exported type aliases
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration) {
            Stmt *decl = stmt->as.export_stmt.declaration;
            if (decl && decl->type == STMT_TYPE_ALIAS) {
                CheckedType *aliased = checked_type_from_ast(decl->as.type_alias.aliased_type);
                type_check_register_type_alias(ctx, decl->as.type_alias.name,
                    aliased,
                    decl->as.type_alias.type_params,
                    decl->as.type_alias.num_type_params);
            }
        }
    }

    // Second pass: collect functions, objects, enums (which can now reference type aliases)
    for (int i = 0; i < count; i++) {
        Stmt *stmt = stmts[i];
        if (!stmt) continue;

        // Handle top-level function definitions (let/const with function value)
        if ((stmt->type == STMT_LET || stmt->type == STMT_CONST)) {
            const char *name = stmt->type == STMT_LET
                ? stmt->as.let.name
                : stmt->as.const_stmt.name;
            Expr *value = stmt->type == STMT_LET
                ? stmt->as.let.value
                : stmt->as.const_stmt.value;

            if (value && value->type == EXPR_FUNCTION) {
                Expr *func = value;

                // Collect parameter types and optional info
                CheckedType **param_types = NULL;
                char **param_names = NULL;
                int *param_optional = NULL;
                if (func->as.function.num_params > 0) {
                    param_types = calloc(func->as.function.num_params, sizeof(CheckedType*));
                    param_names = calloc(func->as.function.num_params, sizeof(char*));
                    param_optional = calloc(func->as.function.num_params, sizeof(int));
                    for (int j = 0; j < func->as.function.num_params; j++) {
                        if (func->as.function.param_types[j]) {
                            param_types[j] = checked_type_from_ast_ctx(ctx, func->as.function.param_types[j]);
                        }
                        param_names[j] = strdup(func->as.function.param_names[j]);
                        // Parameter is optional if it has a default value
                        param_optional[j] = (func->as.function.param_defaults &&
                                            func->as.function.param_defaults[j]) ? 1 : 0;
                    }
                }

                CheckedType *return_type = func->as.function.return_type
                    ? checked_type_from_ast_ctx(ctx, func->as.function.return_type)
                    : NULL;

                type_check_register_function(ctx, name, param_types, param_names,
                    param_optional, func->as.function.num_params, return_type,
                    func->as.function.rest_param != NULL,
                    func->as.function.is_async);
            }
        }

        // Handle export statements
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration) {
            Stmt *decl = stmt->as.export_stmt.declaration;
            if (decl && (decl->type == STMT_LET || decl->type == STMT_CONST)) {
                const char *name = decl->type == STMT_LET
                    ? decl->as.let.name
                    : decl->as.const_stmt.name;
                Expr *value = decl->type == STMT_LET
                    ? decl->as.let.value
                    : decl->as.const_stmt.value;

                if (value && value->type == EXPR_FUNCTION) {
                    Expr *func = value;

                    CheckedType **param_types = NULL;
                    char **param_names = NULL;
                    int *param_optional = NULL;
                    if (func->as.function.num_params > 0) {
                        param_types = calloc(func->as.function.num_params, sizeof(CheckedType*));
                        param_names = calloc(func->as.function.num_params, sizeof(char*));
                        param_optional = calloc(func->as.function.num_params, sizeof(int));
                        for (int j = 0; j < func->as.function.num_params; j++) {
                            if (func->as.function.param_types[j]) {
                                param_types[j] = checked_type_from_ast_ctx(ctx, func->as.function.param_types[j]);
                            }
                            param_names[j] = strdup(func->as.function.param_names[j]);
                            param_optional[j] = (func->as.function.param_defaults &&
                                                func->as.function.param_defaults[j]) ? 1 : 0;
                        }
                    }

                    CheckedType *return_type = func->as.function.return_type
                        ? checked_type_from_ast_ctx(ctx, func->as.function.return_type)
                        : NULL;

                    type_check_register_function(ctx, name, param_types, param_names,
                        param_optional, func->as.function.num_params, return_type,
                        func->as.function.rest_param != NULL,
                        func->as.function.is_async);
                }
            }
        }

        // Handle object definitions
        if (stmt->type == STMT_DEFINE_OBJECT) {
            CheckedType **field_types = NULL;
            if (stmt->as.define_object.num_fields > 0) {
                field_types = calloc(stmt->as.define_object.num_fields, sizeof(CheckedType*));
                for (int j = 0; j < stmt->as.define_object.num_fields; j++) {
                    if (stmt->as.define_object.field_types[j]) {
                        field_types[j] = checked_type_from_ast_ctx(ctx, stmt->as.define_object.field_types[j]);
                    } else {
                        field_types[j] = checked_type_primitive(CHECKED_ANY);
                    }
                }
            }
            // Convert method types
            CheckedType **method_types = NULL;
            if (stmt->as.define_object.num_methods > 0) {
                method_types = calloc(stmt->as.define_object.num_methods, sizeof(CheckedType*));
                for (int j = 0; j < stmt->as.define_object.num_methods; j++) {
                    if (stmt->as.define_object.method_types[j]) {
                        method_types[j] = checked_type_from_ast_ctx(ctx, stmt->as.define_object.method_types[j]);
                    } else {
                        method_types[j] = checked_type_primitive(CHECKED_ANY);
                    }
                }
            }
            type_check_register_object(ctx, stmt->as.define_object.name,
                stmt->as.define_object.type_params, stmt->as.define_object.num_type_params,
                stmt->as.define_object.field_names, field_types,
                stmt->as.define_object.field_optional,
                stmt->as.define_object.num_fields,
                stmt->as.define_object.method_names, method_types,
                stmt->as.define_object.method_optional,
                stmt->as.define_object.num_methods);
            // Free the temporary arrays (elements were transferred to the object def)
            free(field_types);
            free(method_types);
        }

        // Handle enum definitions
        if (stmt->type == STMT_ENUM) {
            type_check_register_enum(ctx, stmt->as.enum_decl.name,
                stmt->as.enum_decl.variant_names, stmt->as.enum_decl.num_variants);
        }
    }
}

// ========== MAIN ENTRY POINT ==========

int type_check_program(TypeCheckContext *ctx, Stmt **stmts, int stmt_count) {
    // First pass: collect all function signatures, object definitions, enums
    collect_function_signatures(ctx, stmts, stmt_count);

    // Second pass: type check all statements
    for (int i = 0; i < stmt_count; i++) {
        type_check_stmt(ctx, stmts[i]);
    }

    return ctx->error_count;
}
