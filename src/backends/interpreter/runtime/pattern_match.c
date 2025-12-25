#include "internal.h"

// ========== PATTERN MATCHING ==========

// Forward declaration
static int match_pattern_internal(Pattern *pattern, Value value, Environment *env, ExecutionContext *ctx);

// Check if a value matches a type
static int matches_type(Value value, Type *type) {
    switch (type->kind) {
        case TYPE_I8:    return value.type == VAL_I8;
        case TYPE_I16:   return value.type == VAL_I16;
        case TYPE_I32:   return value.type == VAL_I32;
        case TYPE_I64:   return value.type == VAL_I64;
        case TYPE_U8:    return value.type == VAL_U8;
        case TYPE_U16:   return value.type == VAL_U16;
        case TYPE_U32:   return value.type == VAL_U32;
        case TYPE_U64:   return value.type == VAL_U64;
        case TYPE_F32:   return value.type == VAL_F32;
        case TYPE_F64:   return value.type == VAL_F64;
        case TYPE_BOOL:  return value.type == VAL_BOOL;
        case TYPE_STRING: return value.type == VAL_STRING;
        case TYPE_RUNE:  return value.type == VAL_RUNE;
        case TYPE_PTR:   return value.type == VAL_PTR;
        case TYPE_BUFFER: return value.type == VAL_BUFFER;
        case TYPE_ARRAY: return value.type == VAL_ARRAY;
        case TYPE_NULL:  return value.type == VAL_NULL;
        case TYPE_GENERIC_OBJECT: return value.type == VAL_OBJECT;
        case TYPE_CUSTOM_OBJECT:
            if (value.type != VAL_OBJECT) return 0;
            return type->type_name && value.as.as_object->type_name &&
                   strcmp(type->type_name, value.as.as_object->type_name) == 0;
        default: return 0;
    }
}

// Check if a numeric value is in range [start, end]
static int value_in_range(Value value, Value start, Value end) {
    if (!is_numeric(value) || !is_numeric(start) || !is_numeric(end)) {
        return 0;
    }

    double v = value_to_float(value);
    double s = value_to_float(start);
    double e = value_to_float(end);

    return v >= s && v <= e;
}

// Match a pattern and bind variables if successful
// Returns 1 if matched, 0 if not matched
int pattern_match(Pattern *pattern, Value value, Environment *env, ExecutionContext *ctx) {
    return match_pattern_internal(pattern, value, env, ctx);
}

static int match_pattern_internal(Pattern *pattern, Value value, Environment *env, ExecutionContext *ctx) {
    if (!pattern) return 0;

    switch (pattern->type) {
        case PATTERN_WILDCARD:
            // Wildcard matches everything
            return 1;

        case PATTERN_LITERAL: {
            // Evaluate the literal and compare
            Value lit = eval_expr(pattern->as.literal, env, ctx);
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(lit);
                return 0;
            }
            int result = values_equal(value, lit);
            VALUE_RELEASE(lit);
            return result;
        }

        case PATTERN_BINDING:
            // Bind the value to the variable name
            VALUE_RETAIN(value);
            env_define(env, pattern->as.binding.name, value, 0, ctx);
            VALUE_RELEASE(value);
            return 1;

        case PATTERN_ARRAY: {
            if (value.type != VAL_ARRAY) return 0;
            Array *arr = value.as.as_array;
            int num_elements = pattern->as.array.num_elements;
            char *rest_name = pattern->as.array.rest_name;

            // Check array length
            if (rest_name) {
                // With rest pattern, array must have at least num_elements items
                if (arr->length < num_elements) return 0;
            } else {
                // Without rest, must match exactly
                if (arr->length != num_elements) return 0;
            }

            // Match each element
            for (int i = 0; i < num_elements; i++) {
                Value elem = array_get(arr, i, ctx);
                if (ctx->exception_state.is_throwing) {
                    VALUE_RELEASE(elem);
                    return 0;
                }
                int matched = match_pattern_internal(pattern->as.array.elements[i], elem, env, ctx);
                VALUE_RELEASE(elem);
                if (!matched) return 0;
            }

            // Bind rest if present
            if (rest_name) {
                Array *rest_arr = array_new();
                for (int i = num_elements; i < arr->length; i++) {
                    Value elem = array_get(arr, i, ctx);
                    if (ctx->exception_state.is_throwing) {
                        VALUE_RELEASE(elem);
                        array_free(rest_arr);
                        return 0;
                    }
                    array_push(rest_arr, elem);
                    VALUE_RELEASE(elem);
                }
                Value rest_val = val_array(rest_arr);
                env_define(env, rest_name, rest_val, 0, ctx);
                VALUE_RELEASE(rest_val);
            }

            return 1;
        }

        case PATTERN_OBJECT: {
            if (value.type != VAL_OBJECT) return 0;
            Object *obj = value.as.as_object;

            // Match each field
            for (int i = 0; i < pattern->as.object.num_fields; i++) {
                char *field_name = pattern->as.object.field_names[i];

                // Find the field in the object
                int field_idx = object_lookup_field(obj, field_name);
                if (field_idx < 0) return 0;  // Field not found

                Value field_value = obj->field_values[field_idx];
                VALUE_RETAIN(field_value);

                Pattern *field_pattern = pattern->as.object.field_patterns[i];
                int matched = match_pattern_internal(field_pattern, field_value, env, ctx);
                VALUE_RELEASE(field_value);
                if (!matched) return 0;
            }

            // Bind rest if present (collect remaining fields)
            if (pattern->as.object.rest_name) {
                Object *rest_obj = object_new(NULL, 4);
                for (int i = 0; i < obj->num_fields; i++) {
                    if (obj->field_names[i] != NULL) {
                        // Check if this field was already matched
                        int was_matched = 0;
                        for (int j = 0; j < pattern->as.object.num_fields; j++) {
                            if (strcmp(obj->field_names[i], pattern->as.object.field_names[j]) == 0) {
                                was_matched = 1;
                                break;
                            }
                        }
                        if (!was_matched) {
                            Value field_val = obj->field_values[i];
                            VALUE_RETAIN(field_val);
                            // Add field to rest object - use simplified approach
                            // Just add to arrays directly since rest objects are created fresh
                            if (rest_obj->num_fields >= rest_obj->capacity) {
                                int new_cap = rest_obj->capacity * 2;
                                rest_obj->field_names = realloc(rest_obj->field_names, new_cap * sizeof(char*));
                                rest_obj->field_values = realloc(rest_obj->field_values, new_cap * sizeof(Value));
                                rest_obj->capacity = new_cap;
                            }
                            rest_obj->field_names[rest_obj->num_fields] = strdup(obj->field_names[i]);
                            rest_obj->field_values[rest_obj->num_fields] = field_val;
                            rest_obj->num_fields++;
                        }
                    }
                }
                Value rest_val = val_object(rest_obj);
                env_define(env, pattern->as.object.rest_name, rest_val, 0, ctx);
                VALUE_RELEASE(rest_val);
            }

            return 1;
        }

        case PATTERN_RANGE: {
            // Evaluate start and end
            Value start = eval_expr(pattern->as.range.start, env, ctx);
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(start);
                return 0;
            }
            Value end = eval_expr(pattern->as.range.end, env, ctx);
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(start);
                VALUE_RELEASE(end);
                return 0;
            }

            int result = value_in_range(value, start, end);
            VALUE_RELEASE(start);
            VALUE_RELEASE(end);
            return result;
        }

        case PATTERN_TYPE:
            return matches_type(value, pattern->as.type_pattern.match_type);

        case PATTERN_OR: {
            // Try each alternative pattern
            for (int i = 0; i < pattern->as.or_pattern.num_patterns; i++) {
                if (match_pattern_internal(pattern->as.or_pattern.patterns[i], value, env, ctx)) {
                    return 1;
                }
            }
            return 0;
        }
    }

    return 0;
}
