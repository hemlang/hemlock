#include "expressions_internal.h"

// ========== PATTERN MATCHING ==========

// Check if a type matches a value's type
int type_matches_value(Type *type, Value value) {
    if (!type) return 0;

    switch (type->kind) {
        case TYPE_I8: return value.type == VAL_I8;
        case TYPE_I16: return value.type == VAL_I16;
        case TYPE_I32: return value.type == VAL_I32;
        case TYPE_I64: return value.type == VAL_I64;
        case TYPE_U8: return value.type == VAL_U8;
        case TYPE_U16: return value.type == VAL_U16;
        case TYPE_U32: return value.type == VAL_U32;
        case TYPE_U64: return value.type == VAL_U64;
        case TYPE_F32: return value.type == VAL_F32;
        case TYPE_F64: return value.type == VAL_F64;
        case TYPE_BOOL: return value.type == VAL_BOOL;
        case TYPE_STRING: return value.type == VAL_STRING;
        case TYPE_RUNE: return value.type == VAL_RUNE;
        case TYPE_PTR: return value.type == VAL_PTR;
        case TYPE_BUFFER: return value.type == VAL_BUFFER;
        case TYPE_ARRAY: return value.type == VAL_ARRAY;
        case TYPE_NULL: return value.type == VAL_NULL;
        case TYPE_GENERIC_OBJECT: return value.type == VAL_OBJECT;
        case TYPE_CUSTOM_OBJECT:
            // Check if object has a matching type name
            if (value.type == VAL_OBJECT && type->type_name) {
                // For now, match any object (could check _type field later)
                return 1;
            }
            return 0;
        case TYPE_FUNCTION:
            return value.type == VAL_FUNCTION || value.type == VAL_BUILTIN_FN || value.type == VAL_FFI_FUNCTION;
        default:
            return 0;
    }
}

// Match a pattern against a value, binding variables in the environment
// Returns 1 if pattern matches, 0 otherwise
int match_pattern(Pattern *pattern, Value value, Environment *env, ExecutionContext *ctx) {
    if (!pattern) return 0;

    switch (pattern->type) {
        case PATTERN_WILDCARD:
            // Wildcard always matches
            return 1;

        case PATTERN_LITERAL: {
            // Match literal value
            Value literal = eval_expr(pattern->as.literal, env, ctx);
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(literal);
                return 0;
            }
            int matched = values_equal(value, literal);
            VALUE_RELEASE(literal);
            return matched;
        }

        case PATTERN_BINDING: {
            // Bind the value to the variable name
            VALUE_RETAIN(value);
            env_define(env, pattern->as.binding.name, value, 0, ctx);
            return 1;
        }

        case PATTERN_TYPED: {
            // Check type first
            if (!type_matches_value(pattern->as.typed.type_annotation, value)) {
                return 0;
            }
            // If name is provided, bind the value
            if (pattern->as.typed.name) {
                VALUE_RETAIN(value);
                env_define(env, pattern->as.typed.name, value, 0, ctx);
            }
            return 1;
        }

        case PATTERN_OR: {
            // Try each alternative in order
            for (int i = 0; i < pattern->as.or_pattern.num_alternatives; i++) {
                // Create a temporary environment for bindings
                Environment *temp_env = env_new(env);
                int matched = match_pattern(pattern->as.or_pattern.alternatives[i], value, temp_env, ctx);
                if (matched) {
                    // Copy bindings from temp_env to env
                    // For now, just match without bindings in OR patterns
                    // (bindings in OR patterns are complex - they must bind the same vars)
                    env_free(temp_env);
                    return 1;
                }
                env_free(temp_env);
            }
            return 0;
        }

        case PATTERN_OBJECT: {
            // Value must be an object
            if (value.type != VAL_OBJECT) {
                return 0;
            }

            Object *obj = value.as.as_object;

            // Match each field pattern
            for (int i = 0; i < pattern->as.object.num_fields; i++) {
                ObjectFieldPattern *field_pat = &pattern->as.object.fields[i];

                // Find the field in the object using hash lookup
                int field_idx = object_lookup_field(obj, field_pat->name);
                if (field_idx < 0) {
                    // Field doesn't exist - pattern doesn't match
                    return 0;
                }

                Value field_val = obj->fields[field_idx].value;

                if (field_pat->pattern) {
                    // Explicit pattern for this field
                    if (!match_pattern(field_pat->pattern, field_val, env, ctx)) {
                        return 0;
                    }
                } else {
                    // Shorthand: bind field value to field name
                    VALUE_RETAIN(field_val);
                    env_define(env, field_pat->name, field_val, 0, ctx);
                }
            }

            // Handle rest pattern: ...name collects remaining fields
            if (pattern->as.object.has_rest && pattern->as.object.rest_name) {
                // Create a new object with remaining fields
                Object *rest_obj = object_new(NULL, 8);
                for (int i = 0; i < obj->num_fields; i++) {
                    const char *key = obj->fields[i].name;
                    if (!key) continue;

                    // Check if this key was matched by a field pattern
                    int was_matched = 0;
                    for (int j = 0; j < pattern->as.object.num_fields; j++) {
                        if (strcmp(key, pattern->as.object.fields[j].name) == 0) {
                            was_matched = 1;
                            break;
                        }
                    }

                    if (!was_matched) {
                        Value v = obj->fields[i].value;
                        VALUE_RETAIN(v);
                        // Add field to rest object (unified storage - single realloc)
                        if (rest_obj->num_fields >= rest_obj->capacity) {
                            // SECURITY: Check for integer overflow before doubling capacity
                            if (rest_obj->capacity > INT_MAX / 2) {
                                runtime_error(ctx, "Object field capacity overflow");
                                return 0;
                            }
                            int new_capacity = rest_obj->capacity * 2;
                            FieldEntry *new_fields = object_grow_fields(rest_obj, new_capacity);
                            if (!new_fields) {
                                runtime_error(ctx, "Out of memory expanding object fields");
                                return 0;
                            }
                        }
                        rest_obj->fields[rest_obj->num_fields].name = strdup(key);
                        rest_obj->fields[rest_obj->num_fields].value = v;
                        rest_obj->num_fields++;
                    }
                }
                Value rest_val = val_object(rest_obj);
                env_define(env, pattern->as.object.rest_name, rest_val, 0, ctx);
                VALUE_RELEASE(rest_val);  // Release caller's reference (env_define retained it)
            }

            return 1;
        }

        case PATTERN_ARRAY: {
            // Value must be an array
            if (value.type != VAL_ARRAY) {
                return 0;
            }

            Array *arr = value.as.as_array;
            int arr_len = arr->length;

            // Find rest pattern position if any
            int rest_index = -1;
            for (int i = 0; i < pattern->as.array.num_elements; i++) {
                if (pattern->as.array.elements[i].is_rest) {
                    rest_index = i;
                    break;
                }
            }

            // Calculate required elements
            int required_before_rest = rest_index >= 0 ? rest_index : pattern->as.array.num_elements;
            int required_after_rest = rest_index >= 0 ? (pattern->as.array.num_elements - rest_index - 1) : 0;
            int min_required = required_before_rest + required_after_rest;

            // Without a rest pattern, need exact length match
            if (rest_index < 0) {
                if (arr_len != min_required) {
                    return 0;
                }
            } else {
                // With rest pattern, need at least min_required elements
                if (arr_len < min_required) {
                    return 0;
                }
            }

            // Match elements before rest
            for (int i = 0; i < required_before_rest; i++) {
                Value elem = array_get(arr, i, ctx);
                if (!match_pattern(pattern->as.array.elements[i].pattern, elem, env, ctx)) {
                    return 0;
                }
            }

            // Handle rest pattern
            if (rest_index >= 0) {
                int rest_count = arr_len - min_required;
                ArrayElementPattern *rest_pat = &pattern->as.array.elements[rest_index];

                // Create array with rest elements
                Array *rest_arr = array_new();
                for (int i = 0; i < rest_count; i++) {
                    Value elem = array_get(arr, required_before_rest + i, ctx);
                    VALUE_RETAIN(elem);
                    array_push(rest_arr, elem);
                }
                Value rest_val = val_array(rest_arr);
                env_define(env, rest_pat->rest_name, rest_val, 0, ctx);
                VALUE_RELEASE(rest_val);  // Release caller's reference (env_define retained it)

                // Match elements after rest
                for (int i = 0; i < required_after_rest; i++) {
                    int arr_idx = arr_len - required_after_rest + i;
                    int pat_idx = rest_index + 1 + i;
                    Value elem = array_get(arr, arr_idx, ctx);
                    if (!match_pattern(pattern->as.array.elements[pat_idx].pattern, elem, env, ctx)) {
                        return 0;
                    }
                }
            }

            return 1;
        }
    }

    return 0;
}

// ========== EXPR_MATCH handler ==========

Value eval_match_expr(Expr *expr, Environment *env, ExecutionContext *ctx) {
    // Evaluate the scrutinee (value being matched)
    Value scrutinee = eval_expr(expr->as.match_expr.scrutinee, env, ctx);
    if (ctx->exception_state.is_throwing) {
        VALUE_RELEASE(scrutinee);
        return val_null();
    }

    // Create environment once and reuse across arms (optimization)
    // This avoids allocating/freeing for each non-matching arm
    Environment *match_env = env_new(env);

    // Try each arm in order
    for (int i = 0; i < expr->as.match_expr.num_arms; i++) {
        MatchArm *arm = &expr->as.match_expr.arms[i];

        // Clear environment for this arm's bindings
        env_clear(match_env);

        // Try to match the pattern
        int matched = match_pattern(arm->pattern, scrutinee, match_env, ctx);

        if (matched) {
            // Check guard if present
            if (arm->guard) {
                Value guard_result = eval_expr(arm->guard, match_env, ctx);
                if (ctx->exception_state.is_throwing) {
                    env_free(match_env);
                    VALUE_RELEASE(scrutinee);
                    return val_null();
                }
                int guard_passed = value_is_truthy(guard_result);
                VALUE_RELEASE(guard_result);

                if (!guard_passed) {
                    // Guard failed, try next arm
                    continue;
                }
            }

            // Pattern matched (and guard passed if present)
            // Evaluate the arm body
            Value result = eval_expr(arm->body, match_env, ctx);
            env_free(match_env);
            VALUE_RELEASE(scrutinee);
            return result;
        }
    }

    // No arm matched - clean up and error
    env_free(match_env);
    VALUE_RELEASE(scrutinee);
    runtime_error(ctx, "No pattern matched in match expression");
    return val_null();
}
