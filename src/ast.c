#include "ast.h"
#include <stdlib.h>
#include <string.h>

// ========== EXPRESSION CONSTRUCTORS ==========

Expr* expr_number_int(int64_t value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NUMBER;
    expr->line = 0;
    expr->as.number.int_value = value;
    expr->as.number.is_float = 0;
    return expr;
}

Expr* expr_number_float(double value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NUMBER;
    expr->line = 0;
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
    expr->line = 0;
    expr->as.boolean = value ? 1 : 0;
    return expr;
}

Expr* expr_string(const char *str) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_STRING;
    expr->line = 0;
    expr->as.string = strdup(str);
    return expr;
}

Expr* expr_rune(uint32_t codepoint) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_RUNE;
    expr->line = 0;
    expr->as.rune = codepoint;
    return expr;
}

Expr* expr_ident(const char *name) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_IDENT;
    expr->line = 0;
    expr->as.ident = strdup(name);
    return expr;
}

Expr* expr_null(void) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NULL;
    expr->line = 0;
    return expr;
}

Expr* expr_binary(Expr *left, BinaryOp op, Expr *right) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_BINARY;
    expr->line = 0;
    expr->as.binary.left = left;
    expr->as.binary.op = op;
    expr->as.binary.right = right;
    return expr;
}

Expr* expr_unary(UnaryOp op, Expr *operand) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_UNARY;
    expr->line = 0;
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    return expr;
}

Expr* expr_ternary(Expr *condition, Expr *true_expr, Expr *false_expr) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_TERNARY;
    expr->line = 0;
    expr->as.ternary.condition = condition;
    expr->as.ternary.true_expr = true_expr;
    expr->as.ternary.false_expr = false_expr;
    return expr;
}

Expr* expr_call(Expr *func, Expr **args, int num_args) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_CALL;
    expr->line = 0;
    expr->as.call.func = func;
    expr->as.call.args = args;
    expr->as.call.num_args = num_args;
    return expr;
}

Expr* expr_assign(const char *name, Expr *value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_ASSIGN;
    expr->line = 0;
    expr->as.assign.name = strdup(name);
    expr->as.assign.value = value;
    return expr;
}

Expr* expr_get_property(Expr *object, const char *property) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_GET_PROPERTY;
    expr->line = 0;
    expr->as.get_property.object = object;
    expr->as.get_property.property = strdup(property);
    return expr;
}

Expr* expr_set_property(Expr *object, const char *property, Expr *value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_SET_PROPERTY;
    expr->line = 0;
    expr->as.set_property.object = object;
    expr->as.set_property.property = strdup(property);
    expr->as.set_property.value = value;
    return expr;
}

Expr* expr_index(Expr *object, Expr *index) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_INDEX;
    expr->line = 0;
    expr->as.index.object = object;
    expr->as.index.index = index;
    return expr;
}

Expr* expr_index_assign(Expr *object, Expr *index, Expr *value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_INDEX_ASSIGN;
    expr->line = 0;
    expr->as.index_assign.object = object;
    expr->as.index_assign.index = index;
    expr->as.index_assign.value = value;
    return expr;
}

Expr* expr_function(int is_async, char **param_names, Type **param_types, Expr **param_defaults, int num_params, Type *return_type, Stmt *body) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_FUNCTION;
    expr->line = 0;
    expr->as.function.is_async = is_async;
    expr->as.function.param_names = param_names;
    expr->as.function.param_types = param_types;
    expr->as.function.param_defaults = param_defaults;
    expr->as.function.num_params = num_params;
    expr->as.function.return_type = return_type;
    expr->as.function.body = body;
    return expr;
}

Expr* expr_array_literal(Expr **elements, int num_elements) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_ARRAY_LITERAL;
    expr->line = 0;
    expr->as.array_literal.elements = elements;
    expr->as.array_literal.num_elements = num_elements;
    return expr;
}

Expr* expr_object_literal(char **field_names, Expr **field_values, int num_fields) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_OBJECT_LITERAL;
    expr->line = 0;
    expr->as.object_literal.field_names = field_names;
    expr->as.object_literal.field_values = field_values;
    expr->as.object_literal.num_fields = num_fields;
    return expr;
}

Expr* expr_prefix_inc(Expr *operand) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_PREFIX_INC;
    expr->line = 0;
    expr->as.prefix_inc.operand = operand;
    return expr;
}

Expr* expr_prefix_dec(Expr *operand) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_PREFIX_DEC;
    expr->line = 0;
    expr->as.prefix_dec.operand = operand;
    return expr;
}

Expr* expr_postfix_inc(Expr *operand) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_POSTFIX_INC;
    expr->line = 0;
    expr->as.postfix_inc.operand = operand;
    return expr;
}

Expr* expr_postfix_dec(Expr *operand) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_POSTFIX_DEC;
    expr->line = 0;
    expr->as.postfix_dec.operand = operand;
    return expr;
}

Expr* expr_await(Expr *awaited_expr) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_AWAIT;
    expr->line = 0;
    expr->as.await_expr.awaited_expr = awaited_expr;
    return expr;
}

Expr* expr_string_interpolation(char **string_parts, Expr **expr_parts, int num_parts) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_STRING_INTERPOLATION;
    expr->line = 0;
    expr->as.string_interpolation.string_parts = string_parts;
    expr->as.string_interpolation.expr_parts = expr_parts;
    expr->as.string_interpolation.num_parts = num_parts;
    return expr;
}

Expr* expr_optional_chain_property(Expr *object, const char *property) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_OPTIONAL_CHAIN;
    expr->line = 0;
    expr->as.optional_chain.object = object;
    expr->as.optional_chain.property = strdup(property);
    expr->as.optional_chain.index = NULL;
    expr->as.optional_chain.args = NULL;
    expr->as.optional_chain.num_args = 0;
    expr->as.optional_chain.is_property = 1;
    expr->as.optional_chain.is_call = 0;
    return expr;
}

Expr* expr_optional_chain_index(Expr *object, Expr *index) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_OPTIONAL_CHAIN;
    expr->line = 0;
    expr->as.optional_chain.object = object;
    expr->as.optional_chain.property = NULL;
    expr->as.optional_chain.index = index;
    expr->as.optional_chain.args = NULL;
    expr->as.optional_chain.num_args = 0;
    expr->as.optional_chain.is_property = 0;
    expr->as.optional_chain.is_call = 0;
    return expr;
}

Expr* expr_optional_chain_call(Expr *object, Expr **args, int num_args) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_OPTIONAL_CHAIN;
    expr->line = 0;
    expr->as.optional_chain.object = object;
    expr->as.optional_chain.property = NULL;
    expr->as.optional_chain.index = NULL;
    expr->as.optional_chain.args = args;
    expr->as.optional_chain.num_args = num_args;
    expr->as.optional_chain.is_property = 0;
    expr->as.optional_chain.is_call = 1;
    return expr;
}

Expr* expr_null_coalesce(Expr *left, Expr *right) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NULL_COALESCE;
    expr->line = 0;
    expr->as.null_coalesce.left = left;
    expr->as.null_coalesce.right = right;
    return expr;
}

// ========== TYPE CONSTRUCTORS ==========

Type* type_new(TypeKind kind) {
    Type *type = malloc(sizeof(Type));
    type->kind = kind;
    type->type_name = NULL;
    type->element_type = NULL;
    return type;
}

void type_free(Type *type) {
    if (type) {
        if (type->type_name) {
            free(type->type_name);
        }
        if (type->element_type) {
            type_free(type->element_type);
        }
        free(type);
    }
}

// ========== STATEMENT CONSTRUCTORS ==========

Stmt* stmt_let_typed(const char *name, Type *type_annotation, Expr *value) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_LET;
    stmt->line = 0;
    stmt->as.let.name = strdup(name);
    stmt->as.let.type_annotation = type_annotation;  // Can be NULL
    stmt->as.let.value = value;
    return stmt;
}

Stmt* stmt_let(const char *name, Expr *value) {
    return stmt_let_typed(name, NULL, value);
}

Stmt* stmt_const_typed(const char *name, Type *type_annotation, Expr *value) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_CONST;
    stmt->line = 0;
    stmt->as.const_stmt.name = strdup(name);
    stmt->as.const_stmt.type_annotation = type_annotation;  // Can be NULL
    stmt->as.const_stmt.value = value;
    return stmt;
}

Stmt* stmt_const(const char *name, Expr *value) {
    return stmt_const_typed(name, NULL, value);
}

Stmt* stmt_if(Expr *condition, Stmt *then_branch, Stmt *else_branch) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_IF;
    stmt->line = 0;
    stmt->as.if_stmt.condition = condition;
    stmt->as.if_stmt.then_branch = then_branch;
    stmt->as.if_stmt.else_branch = else_branch;
    return stmt;
}

Stmt* stmt_while(Expr *condition, Stmt *body) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_WHILE;
    stmt->line = 0;
    stmt->as.while_stmt.condition = condition;
    stmt->as.while_stmt.body = body;
    return stmt;
}

Stmt* stmt_for(Stmt *initializer, Expr *condition, Expr *increment, Stmt *body) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_FOR;
    stmt->line = 0;
    stmt->as.for_loop.initializer = initializer;
    stmt->as.for_loop.condition = condition;
    stmt->as.for_loop.increment = increment;
    stmt->as.for_loop.body = body;
    return stmt;
}

Stmt* stmt_for_in(char *key_var, char *value_var, Expr *iterable, Stmt *body) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_FOR_IN;
    stmt->line = 0;
    stmt->as.for_in.key_var = key_var;
    stmt->as.for_in.value_var = value_var;
    stmt->as.for_in.iterable = iterable;
    stmt->as.for_in.body = body;
    return stmt;
}

Stmt* stmt_break(void) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_BREAK;
    stmt->line = 0;
    return stmt;
}

Stmt* stmt_continue(void) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_CONTINUE;
    stmt->line = 0;
    return stmt;
}

Stmt* stmt_block(Stmt **statements, int count) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_BLOCK;
    stmt->line = 0;
    stmt->as.block.statements = statements;
    stmt->as.block.count = count;
    return stmt;
}

Stmt* stmt_expr(Expr *expr) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_EXPR;
    stmt->line = 0;
    stmt->as.expr = expr;
    return stmt;
}

Stmt* stmt_return(Expr *value) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_RETURN;
    stmt->line = 0;
    stmt->as.return_stmt.value = value;
    return stmt;
}

Stmt* stmt_define_object(const char *name, char **field_names, Type **field_types,
                         int *field_optional, Expr **field_defaults, int num_fields) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_DEFINE_OBJECT;
    stmt->line = 0;
    stmt->as.define_object.name = strdup(name);
    stmt->as.define_object.field_names = field_names;
    stmt->as.define_object.field_types = field_types;
    stmt->as.define_object.field_optional = field_optional;
    stmt->as.define_object.field_defaults = field_defaults;
    stmt->as.define_object.num_fields = num_fields;
    return stmt;
}

Stmt* stmt_enum(const char *name, char **variant_names, Expr **variant_values, int num_variants) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_ENUM;
    stmt->line = 0;
    stmt->as.enum_decl.name = strdup(name);
    stmt->as.enum_decl.variant_names = variant_names;
    stmt->as.enum_decl.variant_values = variant_values;
    stmt->as.enum_decl.num_variants = num_variants;
    return stmt;
}

Stmt* stmt_try(Stmt *try_block, char *catch_param, Stmt *catch_block, Stmt *finally_block) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_TRY;
    stmt->line = 0;
    stmt->as.try_stmt.try_block = try_block;
    stmt->as.try_stmt.catch_param = catch_param;
    stmt->as.try_stmt.catch_block = catch_block;
    stmt->as.try_stmt.finally_block = finally_block;
    return stmt;
}

Stmt* stmt_throw(Expr *value) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_THROW;
    stmt->line = 0;
    stmt->as.throw_stmt.value = value;
    return stmt;
}

Stmt* stmt_switch(Expr *expr, Expr **case_values, Stmt **case_bodies, int num_cases) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_SWITCH;
    stmt->line = 0;
    stmt->as.switch_stmt.expr = expr;
    stmt->as.switch_stmt.case_values = case_values;
    stmt->as.switch_stmt.case_bodies = case_bodies;
    stmt->as.switch_stmt.num_cases = num_cases;
    return stmt;
}

Stmt* stmt_defer(Expr *call) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_DEFER;
    stmt->as.defer_stmt.call = call;
    return stmt;
}

Stmt* stmt_import_named(char **import_names, char **import_aliases, int num_imports, const char *module_path) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_IMPORT;
    stmt->line = 0;
    stmt->as.import_stmt.is_namespace = 0;
    stmt->as.import_stmt.namespace_name = NULL;
    stmt->as.import_stmt.import_names = import_names;
    stmt->as.import_stmt.import_aliases = import_aliases;
    stmt->as.import_stmt.num_imports = num_imports;
    stmt->as.import_stmt.module_path = strdup(module_path);
    return stmt;
}

Stmt* stmt_import_namespace(const char *namespace_name, const char *module_path) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_IMPORT;
    stmt->line = 0;
    stmt->as.import_stmt.is_namespace = 1;
    stmt->as.import_stmt.namespace_name = strdup(namespace_name);
    stmt->as.import_stmt.import_names = NULL;
    stmt->as.import_stmt.import_aliases = NULL;
    stmt->as.import_stmt.num_imports = 0;
    stmt->as.import_stmt.module_path = strdup(module_path);
    return stmt;
}

Stmt* stmt_export_declaration(Stmt *declaration) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_EXPORT;
    stmt->line = 0;
    stmt->as.export_stmt.is_declaration = 1;
    stmt->as.export_stmt.is_reexport = 0;
    stmt->as.export_stmt.declaration = declaration;
    stmt->as.export_stmt.export_names = NULL;
    stmt->as.export_stmt.export_aliases = NULL;
    stmt->as.export_stmt.num_exports = 0;
    stmt->as.export_stmt.module_path = NULL;
    return stmt;
}

Stmt* stmt_export_list(char **export_names, char **export_aliases, int num_exports) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_EXPORT;
    stmt->line = 0;
    stmt->as.export_stmt.is_declaration = 0;
    stmt->as.export_stmt.is_reexport = 0;
    stmt->as.export_stmt.declaration = NULL;
    stmt->as.export_stmt.export_names = export_names;
    stmt->as.export_stmt.export_aliases = export_aliases;
    stmt->as.export_stmt.num_exports = num_exports;
    stmt->as.export_stmt.module_path = NULL;
    return stmt;
}

Stmt* stmt_export_reexport(char **export_names, char **export_aliases, int num_exports, const char *module_path) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_EXPORT;
    stmt->line = 0;
    stmt->as.export_stmt.is_declaration = 0;
    stmt->as.export_stmt.is_reexport = 1;
    stmt->as.export_stmt.declaration = NULL;
    stmt->as.export_stmt.export_names = export_names;
    stmt->as.export_stmt.export_aliases = export_aliases;
    stmt->as.export_stmt.num_exports = num_exports;
    stmt->as.export_stmt.module_path = strdup(module_path);
    return stmt;
}

Stmt* stmt_import_ffi(const char *library_path) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_IMPORT_FFI;
    stmt->line = 0;
    stmt->as.import_ffi.library_path = strdup(library_path);
    return stmt;
}

Stmt* stmt_extern_fn(const char *function_name, Type **param_types, int num_params, Type *return_type) {
    Stmt *stmt = malloc(sizeof(Stmt));
    stmt->type = STMT_EXTERN_FN;
    stmt->line = 0;
    stmt->as.extern_fn.function_name = strdup(function_name);
    stmt->as.extern_fn.param_types = param_types;
    stmt->as.extern_fn.num_params = num_params;
    stmt->as.extern_fn.return_type = return_type;
    return stmt;
}

// ========== CLONING ==========

Expr* expr_clone(const Expr *expr) {
    if (!expr) return NULL;

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return expr_number_float(expr->as.number.float_value);
            } else {
                return expr_number_int(expr->as.number.int_value);
            }

        case EXPR_BOOL:
            return expr_bool(expr->as.boolean);

        case EXPR_STRING:
            return expr_string(expr->as.string);

        case EXPR_RUNE:
            return expr_rune(expr->as.rune);

        case EXPR_IDENT:
            return expr_ident(expr->as.ident);

        case EXPR_NULL:
            return expr_null();

        case EXPR_BINARY:
            return expr_binary(
                expr_clone(expr->as.binary.left),
                expr->as.binary.op,
                expr_clone(expr->as.binary.right)
            );

        case EXPR_UNARY:
            return expr_unary(
                expr->as.unary.op,
                expr_clone(expr->as.unary.operand)
            );

        case EXPR_TERNARY:
            return expr_ternary(
                expr_clone(expr->as.ternary.condition),
                expr_clone(expr->as.ternary.true_expr),
                expr_clone(expr->as.ternary.false_expr)
            );

        case EXPR_CALL: {
            Expr **args_copy = malloc(sizeof(Expr*) * expr->as.call.num_args);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                args_copy[i] = expr_clone(expr->as.call.args[i]);
            }
            return expr_call(
                expr_clone(expr->as.call.func),
                args_copy,
                expr->as.call.num_args
            );
        }

        case EXPR_ASSIGN:
            return expr_assign(
                expr->as.assign.name,
                expr_clone(expr->as.assign.value)
            );

        case EXPR_GET_PROPERTY:
            return expr_get_property(
                expr_clone(expr->as.get_property.object),
                expr->as.get_property.property
            );

        case EXPR_SET_PROPERTY:
            return expr_set_property(
                expr_clone(expr->as.set_property.object),
                expr->as.set_property.property,
                expr_clone(expr->as.set_property.value)
            );

        case EXPR_INDEX:
            return expr_index(
                expr_clone(expr->as.index.object),
                expr_clone(expr->as.index.index)
            );

        case EXPR_INDEX_ASSIGN:
            return expr_index_assign(
                expr_clone(expr->as.index_assign.object),
                expr_clone(expr->as.index_assign.index),
                expr_clone(expr->as.index_assign.value)
            );

        case EXPR_FUNCTION:
            // Note: We don't clone functions for now - this is complex
            // and not needed for compound assignments
            return NULL;

        case EXPR_ARRAY_LITERAL: {
            Expr **elements_copy = malloc(sizeof(Expr*) * expr->as.array_literal.num_elements);
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                elements_copy[i] = expr_clone(expr->as.array_literal.elements[i]);
            }
            return expr_array_literal(
                elements_copy,
                expr->as.array_literal.num_elements
            );
        }

        case EXPR_OBJECT_LITERAL: {
            char **field_names_copy = malloc(sizeof(char*) * expr->as.object_literal.num_fields);
            Expr **field_values_copy = malloc(sizeof(Expr*) * expr->as.object_literal.num_fields);
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                field_names_copy[i] = strdup(expr->as.object_literal.field_names[i]);
                field_values_copy[i] = expr_clone(expr->as.object_literal.field_values[i]);
            }
            return expr_object_literal(
                field_names_copy,
                field_values_copy,
                expr->as.object_literal.num_fields
            );
        }

        case EXPR_PREFIX_INC:
            return expr_prefix_inc(expr_clone(expr->as.prefix_inc.operand));

        case EXPR_PREFIX_DEC:
            return expr_prefix_dec(expr_clone(expr->as.prefix_dec.operand));

        case EXPR_POSTFIX_INC:
            return expr_postfix_inc(expr_clone(expr->as.postfix_inc.operand));

        case EXPR_POSTFIX_DEC:
            return expr_postfix_dec(expr_clone(expr->as.postfix_dec.operand));

        case EXPR_AWAIT:
            return expr_await(expr_clone(expr->as.await_expr.awaited_expr));

        case EXPR_STRING_INTERPOLATION: {
            // Clone string parts
            int n = expr->as.string_interpolation.num_parts;
            char **new_string_parts = malloc(sizeof(char*) * (n + 1));
            for (int i = 0; i <= n; i++) {
                new_string_parts[i] = strdup(expr->as.string_interpolation.string_parts[i]);
            }

            // Clone expr parts
            Expr **new_expr_parts = malloc(sizeof(Expr*) * n);
            for (int i = 0; i < n; i++) {
                new_expr_parts[i] = expr_clone(expr->as.string_interpolation.expr_parts[i]);
            }

            return expr_string_interpolation(new_string_parts, new_expr_parts, n);
        }

        case EXPR_OPTIONAL_CHAIN:
            if (expr->as.optional_chain.is_property) {
                return expr_optional_chain_property(
                    expr_clone(expr->as.optional_chain.object),
                    expr->as.optional_chain.property
                );
            } else if (expr->as.optional_chain.is_call) {
                Expr **args_copy = NULL;
                if (expr->as.optional_chain.num_args > 0) {
                    args_copy = malloc(sizeof(Expr*) * expr->as.optional_chain.num_args);
                    for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                        args_copy[i] = expr_clone(expr->as.optional_chain.args[i]);
                    }
                }
                return expr_optional_chain_call(
                    expr_clone(expr->as.optional_chain.object),
                    args_copy,
                    expr->as.optional_chain.num_args
                );
            } else {
                return expr_optional_chain_index(
                    expr_clone(expr->as.optional_chain.object),
                    expr_clone(expr->as.optional_chain.index)
                );
            }

        case EXPR_NULL_COALESCE:
            return expr_null_coalesce(
                expr_clone(expr->as.null_coalesce.left),
                expr_clone(expr->as.null_coalesce.right)
            );
    }

    return NULL;
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
        case EXPR_RUNE:
            // No cleanup needed for rune (primitive value)
            break;
        case EXPR_BINARY:
            expr_free(expr->as.binary.left);
            expr_free(expr->as.binary.right);
            break;
        case EXPR_UNARY:
            expr_free(expr->as.unary.operand);
            break;
        case EXPR_TERNARY:
            expr_free(expr->as.ternary.condition);
            expr_free(expr->as.ternary.true_expr);
            expr_free(expr->as.ternary.false_expr);
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
            // Free parameter names, types, and defaults
            for (int i = 0; i < expr->as.function.num_params; i++) {
                free(expr->as.function.param_names[i]);
                if (expr->as.function.param_types[i]) {
                    type_free(expr->as.function.param_types[i]);
                }
                if (expr->as.function.param_defaults && expr->as.function.param_defaults[i]) {
                    expr_free(expr->as.function.param_defaults[i]);
                }
            }
            free(expr->as.function.param_names);
            free(expr->as.function.param_types);
            if (expr->as.function.param_defaults) {
                free(expr->as.function.param_defaults);
            }
            // Free return type
            if (expr->as.function.return_type) {
                type_free(expr->as.function.return_type);
            }
            // Free body
            stmt_free(expr->as.function.body);
            break;
        case EXPR_ARRAY_LITERAL:
            // Free array elements
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                expr_free(expr->as.array_literal.elements[i]);
            }
            free(expr->as.array_literal.elements);
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
        case EXPR_PREFIX_INC:
            expr_free(expr->as.prefix_inc.operand);
            break;
        case EXPR_PREFIX_DEC:
            expr_free(expr->as.prefix_dec.operand);
            break;
        case EXPR_POSTFIX_INC:
            expr_free(expr->as.postfix_inc.operand);
            break;
        case EXPR_POSTFIX_DEC:
            expr_free(expr->as.postfix_dec.operand);
            break;
        case EXPR_AWAIT:
            expr_free(expr->as.await_expr.awaited_expr);
            break;
        case EXPR_STRING_INTERPOLATION: {
            // Free string parts
            int n = expr->as.string_interpolation.num_parts;
            for (int i = 0; i <= n; i++) {
                free(expr->as.string_interpolation.string_parts[i]);
            }
            free(expr->as.string_interpolation.string_parts);

            // Free expr parts
            for (int i = 0; i < n; i++) {
                expr_free(expr->as.string_interpolation.expr_parts[i]);
            }
            free(expr->as.string_interpolation.expr_parts);
            break;
        }
        case EXPR_OPTIONAL_CHAIN:
            expr_free(expr->as.optional_chain.object);
            if (expr->as.optional_chain.property) {
                free(expr->as.optional_chain.property);
            }
            if (expr->as.optional_chain.index) {
                expr_free(expr->as.optional_chain.index);
            }
            if (expr->as.optional_chain.args) {
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    expr_free(expr->as.optional_chain.args[i]);
                }
                free(expr->as.optional_chain.args);
            }
            break;
        case EXPR_NULL_COALESCE:
            expr_free(expr->as.null_coalesce.left);
            expr_free(expr->as.null_coalesce.right);
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
        case STMT_CONST:
            free(stmt->as.const_stmt.name);
            type_free(stmt->as.const_stmt.type_annotation);
            expr_free(stmt->as.const_stmt.value);
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
        case STMT_FOR:
            stmt_free(stmt->as.for_loop.initializer);
            expr_free(stmt->as.for_loop.condition);
            expr_free(stmt->as.for_loop.increment);
            stmt_free(stmt->as.for_loop.body);
            break;
        case STMT_FOR_IN:
            free(stmt->as.for_in.key_var);
            free(stmt->as.for_in.value_var);
            expr_free(stmt->as.for_in.iterable);
            stmt_free(stmt->as.for_in.body);
            break;
        case STMT_BREAK:
        case STMT_CONTINUE:
            // No fields to free
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
        case STMT_ENUM:
            free(stmt->as.enum_decl.name);
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                free(stmt->as.enum_decl.variant_names[i]);
                if (stmt->as.enum_decl.variant_values && stmt->as.enum_decl.variant_values[i]) {
                    expr_free(stmt->as.enum_decl.variant_values[i]);
                }
            }
            free(stmt->as.enum_decl.variant_names);
            if (stmt->as.enum_decl.variant_values) {
                free(stmt->as.enum_decl.variant_values);
            }
            break;
        case STMT_TRY:
            stmt_free(stmt->as.try_stmt.try_block);
            free(stmt->as.try_stmt.catch_param);
            stmt_free(stmt->as.try_stmt.catch_block);
            stmt_free(stmt->as.try_stmt.finally_block);
            break;
        case STMT_THROW:
            expr_free(stmt->as.throw_stmt.value);
            break;
        case STMT_SWITCH:
            expr_free(stmt->as.switch_stmt.expr);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                expr_free(stmt->as.switch_stmt.case_values[i]);  // NULL for default is OK
                stmt_free(stmt->as.switch_stmt.case_bodies[i]);
            }
            free(stmt->as.switch_stmt.case_values);
            free(stmt->as.switch_stmt.case_bodies);
            break;
        case STMT_DEFER:
            expr_free(stmt->as.defer_stmt.call);
            break;
        case STMT_IMPORT:
            free(stmt->as.import_stmt.namespace_name);
            free(stmt->as.import_stmt.module_path);
            if (stmt->as.import_stmt.import_names) {
                for (int i = 0; i < stmt->as.import_stmt.num_imports; i++) {
                    free(stmt->as.import_stmt.import_names[i]);
                    if (stmt->as.import_stmt.import_aliases && stmt->as.import_stmt.import_aliases[i]) {
                        free(stmt->as.import_stmt.import_aliases[i]);
                    }
                }
                free(stmt->as.import_stmt.import_names);
                free(stmt->as.import_stmt.import_aliases);
            }
            break;
        case STMT_EXPORT:
            stmt_free(stmt->as.export_stmt.declaration);
            free(stmt->as.export_stmt.module_path);
            if (stmt->as.export_stmt.export_names) {
                for (int i = 0; i < stmt->as.export_stmt.num_exports; i++) {
                    free(stmt->as.export_stmt.export_names[i]);
                    if (stmt->as.export_stmt.export_aliases && stmt->as.export_stmt.export_aliases[i]) {
                        free(stmt->as.export_stmt.export_aliases[i]);
                    }
                }
                free(stmt->as.export_stmt.export_names);
                free(stmt->as.export_stmt.export_aliases);
            }
            break;
        case STMT_IMPORT_FFI:
            free(stmt->as.import_ffi.library_path);
            break;
        case STMT_EXTERN_FN:
            free(stmt->as.extern_fn.function_name);
            for (int i = 0; i < stmt->as.extern_fn.num_params; i++) {
                if (stmt->as.extern_fn.param_types[i]) {
                    type_free(stmt->as.extern_fn.param_types[i]);
                }
            }
            free(stmt->as.extern_fn.param_types);
            if (stmt->as.extern_fn.return_type) {
                type_free(stmt->as.extern_fn.return_type);
            }
            break;
    }

    free(stmt);
}