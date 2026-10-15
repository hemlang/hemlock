/*
 * Hemlock Compiler - Type Inference
 *
 * Infers the type of expressions for compile-time type checking.
 */

#include "type_check_internal.h"

CheckedType* type_check_infer_property_type(TypeCheckContext *ctx, CheckedType *obj_type,
                                            const char *property) {
    if (!ctx || !obj_type || !property) return NULL;

    if (obj_type->kind == CHECKED_CUSTOM && obj_type->type_name) {
        ObjectDef *def = type_check_lookup_object(ctx, obj_type->type_name);
        if (def) {
            for (int i = 0; i < def->num_fields; i++) {
                if (strcmp(def->field_names[i], property) == 0) {
                    CheckedType *field = def->field_types && def->field_types[i]
                        ? checked_type_clone(def->field_types[i])
                        : checked_type_primitive(CHECKED_ANY);
                    if (def->field_optional && def->field_optional[i]) {
                        field->nullable = 1;
                    }
                    return field;
                }
            }

            for (int i = 0; i < def->num_methods; i++) {
                if (strcmp(def->method_names[i], property) == 0) {
                    return def->method_types && def->method_types[i]
                        ? checked_type_clone(def->method_types[i])
                        : checked_type_primitive(CHECKED_ANY);
                }
            }
        }
    }

    return NULL;
}

// ========== TYPE INFERENCE ==========

CheckedType* type_check_infer_expr(TypeCheckContext *ctx, Expr *expr) {
    if (!expr) return checked_type_primitive(CHECKED_ANY);

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return checked_type_primitive(CHECKED_F64);
            }
            if (expr->as.number.is_u64) {
                return checked_type_primitive(CHECKED_U64);
            }
            // Check if it fits in i32
            if (expr->as.number.int_value >= -2147483648LL &&
                expr->as.number.int_value <= 2147483647LL) {
                return checked_type_primitive(CHECKED_I32);
            }
            return checked_type_primitive(CHECKED_I64);

        case EXPR_BOOL:
            return checked_type_primitive(CHECKED_BOOL);

        case EXPR_STRING:
            return checked_type_primitive(CHECKED_STRING);

        case EXPR_RUNE:
            return checked_type_primitive(CHECKED_RUNE);

        case EXPR_NULL:
            return checked_type_primitive(CHECKED_NULL);

        case EXPR_IDENT: {
            const char *name = expr->as.ident.name;
            char *key = type_check_expr_key(expr);
            CheckedType *narrowed = type_check_lookup_narrowing(ctx, key);
            free(key);
            if (narrowed) return checked_type_clone(narrowed);

            CheckedType *type = type_check_lookup(ctx, name);
            if (type) {
                return checked_type_clone(type);
            }

            // Check for built-in functions using unified registry
            if (hml_is_builtin(name)) {
                return checked_type_primitive(CHECKED_ANY);
            }

            // Also check for type constructors (not in registry as builtins)
            static const char *type_constructors[] = {
                "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                "f32", "f64", "bool", "rune", "string", "integer", "number", "byte",
                NULL
            };
            for (int i = 0; type_constructors[i]; i++) {
                if (strcmp(name, type_constructors[i]) == 0) {
                    return checked_type_primitive(CHECKED_ANY);
                }
            }

            // Check if it's a registered function
            if (type_check_lookup_function(ctx, name)) {
                return checked_type_primitive(CHECKED_ANY);
            }

            // Check if it's an enum name
            if (type_check_lookup_enum(ctx, name)) {
                return checked_type_primitive(CHECKED_ENUM);
            }

            // Unknown identifier - warn if configured. A name declared by a
            // top-level let/const further down the file is not unknown; the
            // body referencing it only runs after module init.
            if (ctx->warn_implicit_any && !type_check_is_module_level(ctx, name)) {
                type_warning(ctx, expr->line,
                    "identifier '%s' has unknown type", name);
            }

            return checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_BINARY: {
            CheckedType *left = type_check_infer_expr(ctx, expr->as.binary.left);
            CheckedType *right = type_check_infer_expr(ctx, expr->as.binary.right);
            CheckedType *result = NULL;

            switch (expr->as.binary.op) {
                // Comparison operators return bool
                case OP_EQUAL:
                case OP_NOT_EQUAL:
                case OP_LESS:
                case OP_LESS_EQUAL:
                case OP_GREATER:
                case OP_GREATER_EQUAL:
                    result = checked_type_primitive(CHECKED_BOOL);
                    break;

                // Logical operators return bool
                case OP_AND:
                case OP_OR:
                    result = checked_type_primitive(CHECKED_BOOL);
                    break;

                // Arithmetic operators
                case OP_ADD:
                    // String concatenation
                    if (left->kind == CHECKED_STRING || right->kind == CHECKED_STRING) {
                        result = checked_type_primitive(CHECKED_STRING);
                    } else {
                        result = type_common(left, right);
                    }
                    break;

                case OP_SUB:
                case OP_MUL:
                case OP_MOD:
                    result = type_common(left, right);
                    break;

                case OP_DIV:
                    // Division always returns float in Hemlock
                    result = checked_type_primitive(CHECKED_F64);
                    break;

                // Bitwise operators require integers, return integer
                case OP_BIT_AND:
                case OP_BIT_OR:
                case OP_BIT_XOR:
                case OP_BIT_LSHIFT:
                case OP_BIT_RSHIFT:
                    if (type_is_integer(left)) {
                        result = checked_type_clone(left);
                    } else if (type_is_integer(right)) {
                        result = checked_type_clone(right);
                    } else {
                        result = checked_type_primitive(CHECKED_I32);
                    }
                    break;

                default:
                    result = checked_type_primitive(CHECKED_ANY);
            }

            checked_type_free(left);
            checked_type_free(right);
            return result;
        }

        case EXPR_UNARY: {
            CheckedType *operand = type_check_infer_expr(ctx, expr->as.unary.operand);
            CheckedType *result = NULL;

            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    result = checked_type_primitive(CHECKED_BOOL);
                    break;
                case UNARY_NEGATE:
                    result = checked_type_clone(operand);
                    break;
                case UNARY_BIT_NOT:
                    result = checked_type_clone(operand);
                    break;
                default:
                    result = checked_type_primitive(CHECKED_ANY);
            }

            checked_type_free(operand);
            return result;
        }

        case EXPR_TERNARY: {
            CheckedType *true_type = type_check_infer_expr(ctx, expr->as.ternary.true_expr);
            CheckedType *false_type = type_check_infer_expr(ctx, expr->as.ternary.false_expr);
            CheckedType *result = type_common(true_type, false_type);
            checked_type_free(true_type);
            checked_type_free(false_type);
            return result;
        }

        case EXPR_CALL: {
            // Check if calling a known function
            if (expr->as.call.func->type == EXPR_IDENT) {
                const char *name = expr->as.call.func->as.ident.name;

                // Check for type constructor functions
                if (strcmp(name, "i8") == 0) return checked_type_primitive(CHECKED_I8);
                if (strcmp(name, "i16") == 0) return checked_type_primitive(CHECKED_I16);
                if (strcmp(name, "i32") == 0) return checked_type_primitive(CHECKED_I32);
                if (strcmp(name, "i64") == 0) return checked_type_primitive(CHECKED_I64);
                if (strcmp(name, "u8") == 0) return checked_type_primitive(CHECKED_U8);
                if (strcmp(name, "u16") == 0) return checked_type_primitive(CHECKED_U16);
                if (strcmp(name, "u32") == 0) return checked_type_primitive(CHECKED_U32);
                if (strcmp(name, "u64") == 0) return checked_type_primitive(CHECKED_U64);
                if (strcmp(name, "f32") == 0) return checked_type_primitive(CHECKED_F32);
                if (strcmp(name, "f64") == 0) return checked_type_primitive(CHECKED_F64);
                if (strcmp(name, "bool") == 0) return checked_type_primitive(CHECKED_BOOL);
                if (strcmp(name, "string") == 0) return checked_type_primitive(CHECKED_STRING);
                if (strcmp(name, "integer") == 0) return checked_type_primitive(CHECKED_I32);
                if (strcmp(name, "number") == 0) return checked_type_primitive(CHECKED_F64);
                if (strcmp(name, "byte") == 0) return checked_type_primitive(CHECKED_U8);

                // Built-in functions
                if (strcmp(name, "typeof") == 0) return checked_type_primitive(CHECKED_STRING);
                if (strcmp(name, "typeid") == 0) return checked_type_primitive(CHECKED_I32);
                if (strcmp(name, "len") == 0) return checked_type_primitive(CHECKED_I32);
                if (strcmp(name, "alloc") == 0) return checked_type_primitive(CHECKED_PTR);
                if (strcmp(name, "buffer") == 0) return checked_type_primitive(CHECKED_BUFFER);
                if (strcmp(name, "open") == 0) return checked_type_primitive(CHECKED_FILE);
                if (strcmp(name, "channel") == 0) return checked_type_primitive(CHECKED_CHANNEL);
                if (strcmp(name, "spawn") == 0) return checked_type_primitive(CHECKED_TASK);
                if (strcmp(name, "read_line") == 0) {
                    CheckedType *t = checked_type_primitive(CHECKED_STRING);
                    t->nullable = 1;
                    return t;
                }

                // Check registered functions
                FunctionSig *sig = type_check_lookup_function(ctx, name);
                if (sig && sig->return_type) {
                    return checked_type_clone(sig->return_type);
                }

                // Check if it's a variable holding a function
                CheckedType *var_type = type_check_lookup(ctx, name);
                if (var_type && var_type->kind == CHECKED_FUNCTION && var_type->return_type) {
                    return checked_type_clone(var_type->return_type);
                }
            }

            return checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_ARRAY_LITERAL: {
            if (expr->as.array_literal.num_elements == 0) {
                return checked_type_array(NULL);
            }
            // Infer element type from first element
            CheckedType *elem = type_check_infer_expr(ctx, expr->as.array_literal.elements[0]);
            return checked_type_array(elem);
        }

        case EXPR_OBJECT_LITERAL:
            return checked_type_primitive(CHECKED_OBJECT);

        case EXPR_FUNCTION: {
            Expr *func = expr;
            CheckedType **param_types = NULL;
            if (func->as.function.num_params > 0) {
                param_types = calloc(func->as.function.num_params, sizeof(CheckedType*));
                for (int i = 0; i < func->as.function.num_params; i++) {
                    if (func->as.function.param_types[i]) {
                        param_types[i] = checked_type_from_ast_ctx(ctx, func->as.function.param_types[i]);
                    } else {
                        param_types[i] = checked_type_primitive(CHECKED_ANY);
                    }
                }
            }
            CheckedType *ret = func->as.function.return_type
                ? checked_type_from_ast_ctx(ctx, func->as.function.return_type)
                : checked_type_primitive(CHECKED_ANY);
            CheckedType *result = checked_type_function(param_types, func->as.function.num_params,
                                         ret, func->as.function.rest_param != NULL);
            free(param_types);  // Free the temporary array (elements were transferred)
            return result;
        }

        case EXPR_INDEX: {
            CheckedType *obj = type_check_infer_expr(ctx, expr->as.index.object);
            CheckedType *result = NULL;

            if (obj->kind == CHECKED_ARRAY && obj->element_type) {
                result = checked_type_clone(obj->element_type);
            } else if (obj->kind == CHECKED_STRING) {
                result = checked_type_primitive(CHECKED_RUNE);
            } else {
                result = checked_type_primitive(CHECKED_ANY);
            }

            checked_type_free(obj);
            return result;
        }

        case EXPR_GET_PROPERTY: {
            char *key = type_check_expr_key(expr);
            CheckedType *narrowed = type_check_lookup_narrowing(ctx, key);
            free(key);
            if (narrowed) return checked_type_clone(narrowed);

            CheckedType *obj = type_check_infer_expr(ctx, expr->as.get_property.object);
            CheckedType *result = type_check_infer_property_type(ctx, obj, expr->as.get_property.property);
            checked_type_free(obj);
            return result ? result : checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_AWAIT: {
            // await on a task returns the task's result type
            // For now, return ANY
            return checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_STRING_INTERPOLATION:
            return checked_type_primitive(CHECKED_STRING);

        case EXPR_OPTIONAL_CHAIN: {
            char *key = type_check_expr_key(expr);
            CheckedType *narrowed = type_check_lookup_narrowing(ctx, key);
            free(key);
            if (narrowed) return checked_type_clone(narrowed);

            CheckedType *obj = type_check_infer_expr(ctx, expr->as.optional_chain.object);
            CheckedType *result = NULL;

            if (expr->as.optional_chain.is_property) {
                result = type_check_infer_property_type(ctx, obj, expr->as.optional_chain.property);
            } else if (expr->as.optional_chain.is_call) {
                if (obj && obj->kind == CHECKED_FUNCTION && obj->return_type) {
                    result = checked_type_clone(obj->return_type);
                }
            } else {
                if (obj && obj->kind == CHECKED_ARRAY && obj->element_type) {
                    result = checked_type_clone(obj->element_type);
                } else if (obj && obj->kind == CHECKED_STRING) {
                    result = checked_type_primitive(CHECKED_RUNE);
                }
            }

            checked_type_free(obj);
            if (!result) result = checked_type_primitive(CHECKED_ANY);
            if (result->kind != CHECKED_ANY && result->kind != CHECKED_NULL) {
                result->nullable = 1;
            }
            return result;
        }

        case EXPR_NULL_COALESCE: {
            CheckedType *left = type_check_infer_expr(ctx, expr->as.null_coalesce.left);
            CheckedType *right = type_check_infer_expr(ctx, expr->as.null_coalesce.right);
            // Result is non-nullable version of left, or right's type
            CheckedType *result = type_common(left, right);
            if (result) result->nullable = 0;
            checked_type_free(left);
            checked_type_free(right);
            return result ? result : checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return type_check_infer_expr(ctx, expr->as.prefix_inc.operand);

        default:
            return checked_type_primitive(CHECKED_ANY);
    }
}
