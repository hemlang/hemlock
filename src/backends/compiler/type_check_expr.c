/*
 * Hemlock Compiler - Expression Type Checking
 *
 * Validates expressions and checks method calls on built-in types.
 */

#include "type_check_internal.h"

// ========== METHOD TYPE CHECKING ==========

// Check method call arguments for built-in types (array, string)
// Returns 1 if method was found and checked, 0 if unknown method
int type_check_method_call(TypeCheckContext *ctx, CheckedType *receiver_type,
                           const char *method_name, Expr **args, int num_args, int line) {
    if (!receiver_type || !method_name) return 0;

    // Array methods
    if (receiver_type->kind == CHECKED_ARRAY) {
        CheckedType *elem_type = receiver_type->element_type;

        // Methods that take an element: push, unshift, insert, contains, find
        if (strcmp(method_name, "push") == 0 || strcmp(method_name, "unshift") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "array.%s() requires at least 1 argument", method_name);
                return 1;
            }
            if (elem_type && elem_type->kind != CHECKED_ANY) {
                for (int i = 0; i < num_args; i++) {
                    CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                    if (!type_is_assignable(elem_type, arg_type)) {
                        type_error(ctx, line,
                            "array.%s(): cannot add '%s' to array<%s>",
                            method_name, checked_type_name(arg_type),
                            checked_type_name(elem_type));
                    }
                    checked_type_free(arg_type);
                }
            }
            return 1;
        }

        if (strcmp(method_name, "insert") == 0) {
            if (num_args < 2) {
                type_error(ctx, line, "array.insert() requires 2 arguments (index, element)");
                return 1;
            }
            // First arg should be integer (index)
            CheckedType *idx_type = type_check_infer_expr(ctx, args[0]);
            if (!type_is_integer(idx_type) && idx_type->kind != CHECKED_ANY) {
                type_error(ctx, line, "array.insert(): index must be integer, got '%s'",
                    checked_type_name(idx_type));
            }
            checked_type_free(idx_type);

            // Second arg should be element type
            if (elem_type && elem_type->kind != CHECKED_ANY) {
                CheckedType *val_type = type_check_infer_expr(ctx, args[1]);
                if (!type_is_assignable(elem_type, val_type)) {
                    type_error(ctx, line,
                        "array.insert(): cannot insert '%s' into array<%s>",
                        checked_type_name(val_type), checked_type_name(elem_type));
                }
                checked_type_free(val_type);
            }
            return 1;
        }

        // Methods with no required args: pop, shift, first, last, clear, reverse
        if (strcmp(method_name, "pop") == 0 || strcmp(method_name, "shift") == 0 ||
            strcmp(method_name, "first") == 0 || strcmp(method_name, "last") == 0 ||
            strcmp(method_name, "clear") == 0 || strcmp(method_name, "reverse") == 0) {
            return 1;  // No type checking needed for arguments
        }

        // reserve(capacity) - pre-allocate capacity
        if (strcmp(method_name, "reserve") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "array.reserve() requires 1 argument (capacity)");
                return 1;
            }
            CheckedType *cap_type = type_check_infer_expr(ctx, args[0]);
            if (!type_is_integer(cap_type) && cap_type->kind != CHECKED_ANY) {
                type_error(ctx, line, "array.reserve(): capacity must be integer, got '%s'",
                    checked_type_name(cap_type));
            }
            checked_type_free(cap_type);
            return 1;
        }

        // Methods that take index: remove, slice
        if (strcmp(method_name, "remove") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "array.remove() requires 1 argument (index)");
                return 1;
            }
            CheckedType *idx_type = type_check_infer_expr(ctx, args[0]);
            if (!type_is_integer(idx_type) && idx_type->kind != CHECKED_ANY) {
                type_error(ctx, line, "array.remove(): index must be integer, got '%s'",
                    checked_type_name(idx_type));
            }
            checked_type_free(idx_type);
            return 1;
        }

        if (strcmp(method_name, "slice") == 0) {
            for (int i = 0; i < num_args && i < 2; i++) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                if (!type_is_integer(arg_type) && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "array.slice(): argument %d must be integer, got '%s'",
                        i + 1, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // join takes a string separator
        if (strcmp(method_name, "join") == 0) {
            if (num_args > 0) {
                CheckedType *sep_type = type_check_infer_expr(ctx, args[0]);
                if (sep_type->kind != CHECKED_STRING && sep_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "array.join(): separator must be string, got '%s'",
                        checked_type_name(sep_type));
                }
                checked_type_free(sep_type);
            }
            return 1;
        }

        // map, filter, reduce take functions
        if (strcmp(method_name, "map") == 0 || strcmp(method_name, "filter") == 0 ||
            strcmp(method_name, "reduce") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "array.%s() requires a function argument", method_name);
            }
            return 1;
        }

        // contains, find take an element
        if (strcmp(method_name, "contains") == 0 || strcmp(method_name, "find") == 0) {
            return 1;  // Permissive for now
        }

        // concat takes another array
        if (strcmp(method_name, "concat") == 0) {
            if (num_args > 0) {
                CheckedType *other_type = type_check_infer_expr(ctx, args[0]);
                if (other_type->kind != CHECKED_ARRAY && other_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "array.concat(): argument must be array, got '%s'",
                        checked_type_name(other_type));
                }
                checked_type_free(other_type);
            }
            return 1;
        }
    }

    // String methods
    if (receiver_type->kind == CHECKED_STRING) {
        // substr requires exactly 2 integer arguments (start, length)
        if (strcmp(method_name, "substr") == 0) {
            if (num_args != 2) {
                type_error(ctx, line,
                    "string.substr() expects 2 arguments (start, length), got %d",
                    num_args);
            }
            for (int i = 0; i < num_args && i < 2; i++) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                if (!type_is_integer(arg_type) && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "string.substr(): argument %d must be integer, got '%s'",
                        i + 1, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // Methods that take integer indices
        if (strcmp(method_name, "slice") == 0 ||
            strcmp(method_name, "char_at") == 0 || strcmp(method_name, "byte_at") == 0) {
            for (int i = 0; i < num_args && i < 2; i++) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                if (!type_is_integer(arg_type) && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "string.%s(): argument %d must be integer, got '%s'",
                        method_name, i + 1, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // Methods that take a string argument
        if (strcmp(method_name, "find") == 0 || strcmp(method_name, "contains") == 0 ||
            strcmp(method_name, "starts_with") == 0 || strcmp(method_name, "ends_with") == 0 ||
            strcmp(method_name, "split") == 0) {
            if (num_args > 0) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[0]);
                if (arg_type->kind != CHECKED_STRING && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "string.%s(): argument must be string, got '%s'",
                        method_name, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // replace/replace_all take two strings
        if (strcmp(method_name, "replace") == 0 || strcmp(method_name, "replace_all") == 0) {
            if (num_args < 2) {
                type_error(ctx, line, "string.%s() requires 2 arguments (pattern, replacement)",
                    method_name);
                return 1;
            }
            for (int i = 0; i < 2; i++) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                if (arg_type->kind != CHECKED_STRING && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "string.%s(): argument %d must be string, got '%s'",
                        method_name, i + 1, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // repeat takes an integer
        if (strcmp(method_name, "repeat") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "string.repeat() requires 1 argument (count)");
                return 1;
            }
            CheckedType *arg_type = type_check_infer_expr(ctx, args[0]);
            if (!type_is_integer(arg_type) && arg_type->kind != CHECKED_ANY) {
                type_error(ctx, line, "string.repeat(): count must be integer, got '%s'",
                    checked_type_name(arg_type));
            }
            checked_type_free(arg_type);
            return 1;
        }

        // No-arg string methods
        if (strcmp(method_name, "trim") == 0 || strcmp(method_name, "to_upper") == 0 ||
            strcmp(method_name, "to_lower") == 0 || strcmp(method_name, "chars") == 0 ||
            strcmp(method_name, "bytes") == 0 || strcmp(method_name, "byte_ptr") == 0 ||
            strcmp(method_name, "to_bytes") == 0 ||
            strcmp(method_name, "deserialize") == 0) {
            return 1;
        }
    }

    return 0;  // Unknown method, not checked
}

// ========== EXPRESSION TYPE CHECKING ==========

void type_check_expr(TypeCheckContext *ctx, Expr *expr) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_BINARY: {
            type_check_expr(ctx, expr->as.binary.left);
            type_check_expr(ctx, expr->as.binary.right);

            CheckedType *left = type_check_infer_expr(ctx, expr->as.binary.left);
            CheckedType *right = type_check_infer_expr(ctx, expr->as.binary.right);

            BinaryOp op = expr->as.binary.op;

            // Check operator compatibility
            switch (op) {
                case OP_ADD:
                    // Allow string + anything (concatenation)
                    // Allow numeric + numeric
                    // Allow pointer + integer (pointer arithmetic)
                    if (left->kind != CHECKED_STRING && right->kind != CHECKED_STRING) {
                        // Pointer arithmetic: ptr + int or int + ptr
                        int is_ptr_arith = (left->kind == CHECKED_PTR && type_is_integer(right)) ||
                                           (type_is_integer(left) && right->kind == CHECKED_PTR);
                        int is_numeric_op = type_is_numeric(left) && type_is_numeric(right);
                        int is_any = left->kind == CHECKED_ANY || right->kind == CHECKED_ANY;
                        if (!is_ptr_arith && !is_numeric_op && !is_any) {
                            type_error(ctx, expr->line,
                                "cannot add '%s' and '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_SUB:
                    // Allow pointer - integer (pointer arithmetic)
                    // Allow numeric - numeric
                    {
                        int is_ptr_arith = (left->kind == CHECKED_PTR && type_is_integer(right));
                        int is_numeric_op = type_is_numeric(left) && type_is_numeric(right);
                        int is_any = left->kind == CHECKED_ANY || right->kind == CHECKED_ANY;
                        if (!is_ptr_arith && !is_numeric_op && !is_any) {
                            type_error(ctx, expr->line,
                                "cannot subtract '%s' and '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_MUL:
                case OP_MOD:
                    if (!type_is_numeric(left) || !type_is_numeric(right)) {
                        if (left->kind != CHECKED_ANY && right->kind != CHECKED_ANY) {
                            const char *op_name = op == OP_MUL ? "multiply" : "modulo";
                            type_error(ctx, expr->line,
                                "cannot %s '%s' and '%s'",
                                op_name, checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_DIV:
                    if (!type_is_numeric(left) || !type_is_numeric(right)) {
                        if (left->kind != CHECKED_ANY && right->kind != CHECKED_ANY) {
                            type_error(ctx, expr->line,
                                "cannot divide '%s' by '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_BIT_AND:
                case OP_BIT_OR:
                case OP_BIT_XOR:
                case OP_BIT_LSHIFT:
                case OP_BIT_RSHIFT:
                    if (!type_is_integer(left) || !type_is_integer(right)) {
                        if (left->kind != CHECKED_ANY && right->kind != CHECKED_ANY) {
                            type_error(ctx, expr->line,
                                "bitwise operation requires integer operands, got '%s' and '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_AND:
                case OP_OR:
                    // These are more permissive - any truthy/falsy value works
                    break;

                case OP_LESS:
                case OP_LESS_EQUAL:
                case OP_GREATER:
                case OP_GREATER_EQUAL:
                    // Ordering comparisons require comparable types
                    if (left->kind != CHECKED_ANY && right->kind != CHECKED_ANY) {
                        int both_numeric = type_is_numeric(left) && type_is_numeric(right);
                        int both_string = left->kind == CHECKED_STRING && right->kind == CHECKED_STRING;
                        int both_rune = left->kind == CHECKED_RUNE && right->kind == CHECKED_RUNE;
                        if (!both_numeric && !both_string && !both_rune) {
                            type_warning(ctx, expr->line,
                                "comparison between incompatible types '%s' and '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_EQUAL:
                case OP_NOT_EQUAL:
                    // Equality comparisons are more permissive, but warn on obviously wrong types
                    if (left->kind != CHECKED_ANY && right->kind != CHECKED_ANY &&
                        left->kind != CHECKED_NULL && right->kind != CHECKED_NULL) {
                        int both_numeric = type_is_numeric(left) && type_is_numeric(right);
                        int same_kind = left->kind == right->kind;
                        // Warn if comparing completely different types (not just different numeric types)
                        if (!both_numeric && !same_kind &&
                            !(left->kind == CHECKED_OBJECT && right->kind == CHECKED_CUSTOM) &&
                            !(left->kind == CHECKED_CUSTOM && right->kind == CHECKED_OBJECT)) {
                            type_warning(ctx, expr->line,
                                "equality comparison between different types '%s' and '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                default:
                    break;
            }

            checked_type_free(left);
            checked_type_free(right);
            break;
        }

        case EXPR_UNARY: {
            type_check_expr(ctx, expr->as.unary.operand);
            CheckedType *operand = type_check_infer_expr(ctx, expr->as.unary.operand);

            switch (expr->as.unary.op) {
                case UNARY_NEGATE:
                    if (!type_is_numeric(operand) && operand->kind != CHECKED_ANY) {
                        type_error(ctx, expr->line,
                            "cannot negate '%s'", checked_type_name(operand));
                    }
                    break;
                case UNARY_BIT_NOT:
                    if (!type_is_integer(operand) && operand->kind != CHECKED_ANY) {
                        type_error(ctx, expr->line,
                            "bitwise NOT requires integer operand, got '%s'",
                            checked_type_name(operand));
                    }
                    break;
                default:
                    break;
            }

            checked_type_free(operand);
            break;
        }

        case EXPR_CALL: {
            type_check_expr(ctx, expr->as.call.func);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                type_check_expr(ctx, expr->as.call.args[i]);
            }

            // Check function call argument types
            if (expr->as.call.func->type == EXPR_IDENT) {
                const char *name = expr->as.call.func->as.ident.name;
                FunctionSig *sig = type_check_lookup_function(ctx, name);

                if (sig) {
                    // Check argument count
                    int provided = expr->as.call.num_args;

                    if (!sig->has_rest_param && provided > sig->num_params) {
                        type_error(ctx, expr->line,
                            "too many arguments to '%s': expected %d, got %d",
                            name, sig->num_params, provided);
                    }

                    // Check for too few arguments (must have at least num_required)
                    if (provided < sig->num_required) {
                        if (sig->num_required == sig->num_params) {
                            type_error(ctx, expr->line,
                                "too few arguments to '%s': expected %d, got %d",
                                name, sig->num_required, provided);
                        } else {
                            type_error(ctx, expr->line,
                                "too few arguments to '%s': expected at least %d, got %d",
                                name, sig->num_required, provided);
                        }
                    }

                    // Check argument types
                    for (int i = 0; i < provided && i < sig->num_params; i++) {
                        // Determine which parameter this argument corresponds to
                        int param_idx = i;  // Default: positional argument

                        // Check if this is a named argument
                        if (expr->as.call.arg_names && expr->as.call.arg_names[i]) {
                            const char *arg_name = expr->as.call.arg_names[i];
                            param_idx = -1;  // Will be set if we find the parameter

                            // Find the parameter by name
                            if (sig->param_names) {
                                for (int j = 0; j < sig->num_params; j++) {
                                    if (sig->param_names[j] && strcmp(sig->param_names[j], arg_name) == 0) {
                                        param_idx = j;
                                        break;
                                    }
                                }
                            }

                            // If parameter not found, skip type checking (runtime will catch it)
                            if (param_idx < 0) continue;
                        }

                        if (!sig->param_types[param_idx]) continue;

                        CheckedType *arg_type = type_check_infer_expr(ctx, expr->as.call.args[i]);
                        if (!type_is_assignable(sig->param_types[param_idx], arg_type)) {
                            const char *param_name = (sig->param_names && sig->param_names[param_idx])
                                ? sig->param_names[param_idx]
                                : NULL;
                            const char *arg_name = (expr->as.call.arg_names && expr->as.call.arg_names[i])
                                ? expr->as.call.arg_names[i]
                                : NULL;

                            if (arg_name) {
                                type_error(ctx, expr->line,
                                    "argument '%s' to '%s': expected '%s', got '%s'",
                                    arg_name, name,
                                    checked_type_name(sig->param_types[param_idx]),
                                    checked_type_name(arg_type));
                            } else if (param_name) {
                                type_error(ctx, expr->line,
                                    "argument %d ('%s') to '%s': expected '%s', got '%s'",
                                    i + 1, param_name, name,
                                    checked_type_name(sig->param_types[param_idx]),
                                    checked_type_name(arg_type));
                            } else {
                                type_error(ctx, expr->line,
                                    "argument %d to '%s': expected '%s', got '%s'",
                                    i + 1, name,
                                    checked_type_name(sig->param_types[param_idx]),
                                    checked_type_name(arg_type));
                            }
                        }
                        checked_type_free(arg_type);
                    }
                }
            }
            // Check method calls (e.g., arr.push(x), str.split(","))
            else if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                Expr *prop_expr = expr->as.call.func;
                CheckedType *receiver_type = type_check_infer_expr(ctx, prop_expr->as.get_property.object);
                const char *method_name = prop_expr->as.get_property.property;

                type_check_method_call(ctx, receiver_type, method_name,
                    expr->as.call.args, expr->as.call.num_args, expr->line);

                checked_type_free(receiver_type);
            }
            break;
        }

        case EXPR_ASSIGN: {
            type_check_expr(ctx, expr->as.assign.value);

            // Check if variable is const
            if (type_check_is_const(ctx, expr->as.assign.name)) {
                type_error(ctx, expr->line,
                    "cannot reassign const variable '%s'", expr->as.assign.name);
            }

            // Check type compatibility
            CheckedType *var_type = type_check_lookup(ctx, expr->as.assign.name);
            // Variables initialized with null (and no type annotation) are dynamically typed
            // and can accept any value on reassignment - this supports ??= patterns
            if (var_type && var_type->kind != CHECKED_ANY && var_type->kind != CHECKED_NULL) {
                CheckedType *val_type = type_check_infer_expr(ctx, expr->as.assign.value);
                if (!type_is_assignable(var_type, val_type)) {
                    type_error(ctx, expr->line,
                        "cannot assign '%s' to variable '%s' of type '%s'",
                        checked_type_name(val_type), expr->as.assign.name,
                        checked_type_name(var_type));
                }
                checked_type_free(val_type);
            }
            break;
        }

        case EXPR_INDEX: {
            type_check_expr(ctx, expr->as.index.object);
            type_check_expr(ctx, expr->as.index.index);

            // Validate index type based on object type
            // - Arrays and strings require integer indices
            // - Objects allow string indices (property access)
            CheckedType *obj_type = type_check_infer_expr(ctx, expr->as.index.object);
            CheckedType *idx_type = type_check_infer_expr(ctx, expr->as.index.index);
            if (obj_type && idx_type && idx_type->kind != CHECKED_ANY) {
                int needs_int = (obj_type->kind == CHECKED_ARRAY || obj_type->kind == CHECKED_STRING);
                if (needs_int && !type_is_integer(idx_type)) {
                    type_error(ctx, expr->line, "array/string index must be integer, got '%s'",
                        checked_type_name(idx_type));
                }
            }
            checked_type_free(obj_type);
            checked_type_free(idx_type);
            break;
        }

        case EXPR_INDEX_ASSIGN: {
            type_check_expr(ctx, expr->as.index_assign.object);
            type_check_expr(ctx, expr->as.index_assign.index);
            type_check_expr(ctx, expr->as.index_assign.value);

            // Validate index type based on object type
            CheckedType *obj_type = type_check_infer_expr(ctx, expr->as.index_assign.object);
            CheckedType *idx_type = type_check_infer_expr(ctx, expr->as.index_assign.index);
            if (obj_type && idx_type && idx_type->kind != CHECKED_ANY) {
                int needs_int = (obj_type->kind == CHECKED_ARRAY || obj_type->kind == CHECKED_STRING);
                if (needs_int && !type_is_integer(idx_type)) {
                    type_error(ctx, expr->line, "array/string index must be integer, got '%s'",
                        checked_type_name(idx_type));
                }
            }
            checked_type_free(idx_type);

            // Validate value type matches array element type (skip for null/any element types)
            if (obj_type && obj_type->kind == CHECKED_ARRAY && obj_type->element_type &&
                obj_type->element_type->kind != CHECKED_NULL &&
                obj_type->element_type->kind != CHECKED_ANY) {
                CheckedType *val_type = type_check_infer_expr(ctx, expr->as.index_assign.value);
                if (val_type && val_type->kind != CHECKED_ANY &&
                    !type_is_assignable(obj_type->element_type, val_type)) {
                    type_warning(ctx, expr->line,
                        "assigning '%s' to array<%s> element",
                        checked_type_name(val_type), checked_type_name(obj_type->element_type));
                }
                checked_type_free(val_type);
            }
            checked_type_free(obj_type);
            break;
        }

        case EXPR_GET_PROPERTY: {
            type_check_expr(ctx, expr->as.get_property.object);

            // Validate property access against object definition
            CheckedType *obj_type = type_check_infer_expr(ctx, expr->as.get_property.object);
            if (obj_type && obj_type->kind == CHECKED_CUSTOM && obj_type->type_name) {
                ObjectDef *def = type_check_lookup_object(ctx, obj_type->type_name);
                if (def) {
                    const char *prop = expr->as.get_property.property;
                    int found = 0;
                    // Check fields
                    for (int i = 0; i < def->num_fields; i++) {
                        if (strcmp(def->field_names[i], prop) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    // Also check methods
                    if (!found) {
                        for (int i = 0; i < def->num_methods; i++) {
                            if (strcmp(def->method_names[i], prop) == 0) {
                                found = 1;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        type_warning(ctx, expr->line,
                            "property '%s' not defined in type '%s'",
                            prop, obj_type->type_name);
                    }
                }
            }
            checked_type_free(obj_type);
            break;
        }

        case EXPR_SET_PROPERTY: {
            type_check_expr(ctx, expr->as.set_property.object);
            type_check_expr(ctx, expr->as.set_property.value);

            // Validate property and type against object definition
            CheckedType *obj_type = type_check_infer_expr(ctx, expr->as.set_property.object);
            if (obj_type && obj_type->kind == CHECKED_CUSTOM && obj_type->type_name) {
                ObjectDef *def = type_check_lookup_object(ctx, obj_type->type_name);
                if (def) {
                    const char *prop = expr->as.set_property.property;
                    int found = 0;
                    CheckedType *field_type = NULL;
                    // Check fields
                    for (int i = 0; i < def->num_fields; i++) {
                        if (strcmp(def->field_names[i], prop) == 0) {
                            found = 1;
                            field_type = def->field_types[i];
                            break;
                        }
                    }
                    // Also check methods
                    if (!found) {
                        for (int i = 0; i < def->num_methods; i++) {
                            if (strcmp(def->method_names[i], prop) == 0) {
                                found = 1;
                                field_type = def->method_types[i];
                                break;
                            }
                        }
                    }
                    if (!found) {
                        type_warning(ctx, expr->line,
                            "property '%s' not defined in type '%s'",
                            prop, obj_type->type_name);
                    } else if (field_type && field_type->kind != CHECKED_ANY) {
                        // Check that assigned value matches field type
                        CheckedType *val_type = type_check_infer_expr(ctx, expr->as.set_property.value);
                        if (!type_is_assignable(field_type, val_type)) {
                            type_error(ctx, expr->line,
                                "cannot assign '%s' to property '%s' of type '%s'",
                                checked_type_name(val_type), prop,
                                checked_type_name(field_type));
                        }
                        checked_type_free(val_type);
                    }
                }
            }
            checked_type_free(obj_type);
            break;
        }

        case EXPR_TERNARY:
            type_check_expr(ctx, expr->as.ternary.condition);
            type_check_expr(ctx, expr->as.ternary.true_expr);
            type_check_expr(ctx, expr->as.ternary.false_expr);
            break;

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                type_check_expr(ctx, expr->as.array_literal.elements[i]);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                type_check_expr(ctx, expr->as.object_literal.field_values[i]);
            }
            break;

        case EXPR_FUNCTION:
            // Check function body in its own scope
            type_check_function_body(ctx, expr, NULL);
            break;

        case EXPR_AWAIT:
            type_check_expr(ctx, expr->as.await_expr.awaited_expr);
            break;

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                type_check_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
            }
            break;

        case EXPR_OPTIONAL_CHAIN:
            type_check_expr(ctx, expr->as.optional_chain.object);
            if (expr->as.optional_chain.index) {
                type_check_expr(ctx, expr->as.optional_chain.index);
            }
            if (expr->as.optional_chain.args) {
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    type_check_expr(ctx, expr->as.optional_chain.args[i]);
                }
            }
            break;

        case EXPR_NULL_COALESCE:
            type_check_expr(ctx, expr->as.null_coalesce.left);
            type_check_expr(ctx, expr->as.null_coalesce.right);
            break;

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            type_check_expr(ctx, expr->as.prefix_inc.operand);
            break;

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            type_check_expr(ctx, expr->as.postfix_inc.operand);
            break;

        default:
            break;
    }
}
