#include "internal.h"
#include <stdatomic.h>

// ========== PATTERN MATCHING HELPERS ==========

// Forward declaration for recursive pattern matching
static int match_pattern(Pattern *pattern, Value value, Environment *env, ExecutionContext *ctx);

// Try to match a pattern against a value
// Returns 1 if match succeeds, 0 if it fails
// On success, binds variables in the environment
static int match_pattern(Pattern *pattern, Value value, Environment *env, ExecutionContext *ctx) {
    switch (pattern->type) {
        case PATTERN_LITERAL: {
            // Evaluate the literal expression
            Value literal = eval_expr(pattern->as.literal, env, ctx);
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(literal);
                return 0;
            }
            int equal = values_equal(value, literal);
            VALUE_RELEASE(literal);
            return equal;
        }

        case PATTERN_IDENT: {
            // Wildcard "_" always matches but doesn't bind
            if (pattern->as.ident.is_wildcard) {
                return 1;
            }
            // Bind the value to the variable name
            VALUE_RETAIN(value);
            env_define(env, pattern->as.ident.name, value, 0, ctx);
            VALUE_RELEASE(value);
            return 1;
        }

        case PATTERN_CONSTRUCTOR: {
            // Constructor pattern matches objects with { tag: "Tag", value: ... }
            // The pattern Ok(x) matches { tag: "Ok", value: x }
            if (value.type != VAL_OBJECT) {
                return 0;
            }

            Object *obj = value.as.as_object;

            // Look up the tag field
            int tag_idx = object_lookup_field(obj, "tag");
            if (tag_idx < 0) {
                return 0;
            }

            // Check if tag matches
            Value tag_val = obj->field_values[tag_idx];
            if (tag_val.type != VAL_STRING) {
                return 0;
            }

            String *tag_str = tag_val.as.as_string;
            if (strcmp(tag_str->data, pattern->as.constructor.tag) != 0) {
                return 0;
            }

            // Match the fields
            if (pattern->as.constructor.num_fields == 0) {
                // No fields to extract, just matching the tag
                return 1;
            } else if (pattern->as.constructor.num_fields == 1) {
                // Single field: look for "value" field
                int value_idx = object_lookup_field(obj, "value");
                if (value_idx < 0) {
                    return 0;
                }
                return match_pattern(pattern->as.constructor.fields[0],
                                    obj->field_values[value_idx], env, ctx);
            } else {
                // Multiple fields: match against numbered fields (value0, value1, ...)
                // or use a "values" array field
                int values_idx = object_lookup_field(obj, "values");
                if (values_idx >= 0 && obj->field_values[values_idx].type == VAL_ARRAY) {
                    Array *arr = obj->field_values[values_idx].as.as_array;
                    if (arr->length != pattern->as.constructor.num_fields) {
                        return 0;
                    }
                    for (int i = 0; i < pattern->as.constructor.num_fields; i++) {
                        if (!match_pattern(pattern->as.constructor.fields[i],
                                          arr->elements[i], env, ctx)) {
                            return 0;
                        }
                    }
                    return 1;
                }
                return 0;
            }
        }

        case PATTERN_ARRAY: {
            if (value.type != VAL_ARRAY) {
                return 0;
            }

            Array *arr = value.as.as_array;

            // Check length requirement
            if (pattern->as.array.rest_name) {
                // With rest pattern, need at least num_elements items
                if (arr->length < pattern->as.array.num_elements) {
                    return 0;
                }
            } else {
                // Without rest, need exact match
                if (arr->length != pattern->as.array.num_elements) {
                    return 0;
                }
            }

            // Match elements
            for (int i = 0; i < pattern->as.array.num_elements; i++) {
                if (!match_pattern(pattern->as.array.elements[i],
                                  arr->elements[i], env, ctx)) {
                    return 0;
                }
            }

            // Bind rest if present
            if (pattern->as.array.rest_name) {
                Array *rest = array_new();
                for (int i = pattern->as.array.num_elements; i < arr->length; i++) {
                    VALUE_RETAIN(arr->elements[i]);
                    array_push(rest, arr->elements[i]);
                }
                Value rest_val = val_array(rest);
                env_define(env, pattern->as.array.rest_name, rest_val, 0, ctx);
                VALUE_RELEASE(rest_val);
            }

            return 1;
        }

        case PATTERN_OBJECT: {
            if (value.type != VAL_OBJECT) {
                return 0;
            }

            Object *obj = value.as.as_object;

            // Match each field
            for (int i = 0; i < pattern->as.object.num_fields; i++) {
                const char *field_name = pattern->as.object.field_names[i];
                int field_idx = object_lookup_field(obj, field_name);
                if (field_idx < 0) {
                    return 0;  // Field not found
                }

                Value field_val = obj->field_values[field_idx];
                if (!match_pattern(pattern->as.object.patterns[i], field_val, env, ctx)) {
                    return 0;
                }
            }

            return 1;
        }

        default:
            return 0;
    }
}

// ========== STATEMENT EVALUATION ==========

void eval_stmt(Stmt *stmt, Environment *env, ExecutionContext *ctx) {
    switch (stmt->type) {
        case STMT_LET: {
            Value value = eval_expr(stmt->as.let.value, env, ctx);
            // Check for exception after evaluating value
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(value);
                break;
            }
            // If there's a type annotation, convert/check the value
            if (stmt->as.let.type_annotation != NULL) {
                value = convert_to_type(value, stmt->as.let.type_annotation, env, ctx);
                // Check for exception after type conversion
                if (ctx->exception_state.is_throwing) {
                    VALUE_RELEASE(value);
                    break;
                }
            }
            env_define(env, stmt->as.let.name, value, 0, ctx);  // 0 = mutable
            VALUE_RELEASE(value);  // Release original reference (env_define retains)
            break;
        }

        case STMT_CONST: {
            Value value = eval_expr(stmt->as.const_stmt.value, env, ctx);
            // Check for exception after evaluating value
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(value);
                break;
            }
            // If there's a type annotation, convert/check the value
            if (stmt->as.const_stmt.type_annotation != NULL) {
                value = convert_to_type(value, stmt->as.const_stmt.type_annotation, env, ctx);
                // Check for exception after type conversion
                if (ctx->exception_state.is_throwing) {
                    VALUE_RELEASE(value);
                    break;
                }
            }
            env_define(env, stmt->as.const_stmt.name, value, 1, ctx);  // 1 = const
            VALUE_RELEASE(value);  // Release original reference (env_define retains)
            break;
        }

        case STMT_EXPR: {
            Value result = eval_expr(stmt->as.expr, env, ctx);
            VALUE_RELEASE(result);  // Release unused expression result
            break;
        }

        case STMT_IF: {
            Value condition = eval_expr(stmt->as.if_stmt.condition, env, ctx);
            // Check for exception after evaluating condition
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(condition);
                break;
            }

            if (VALUE_IS_TRUTHY_FAST(condition)) {
                VALUE_RELEASE(condition);  // Release condition before branching
                eval_stmt(stmt->as.if_stmt.then_branch, env, ctx);
            } else if (stmt->as.if_stmt.else_branch != NULL) {
                VALUE_RELEASE(condition);  // Release condition before branching
                eval_stmt(stmt->as.if_stmt.else_branch, env, ctx);
            } else {
                VALUE_RELEASE(condition);  // Release condition if no else branch
            }
            // No need to check return here, it propagates automatically
            break;
        }

        case STMT_WHILE: {
            // Create iteration environment once, reuse each iteration
            Environment *iter_env = env_new(env);
            const char *loop_label = stmt->as.while_stmt.label;
            for (;;) {
                Value condition = eval_expr(stmt->as.while_stmt.condition, env, ctx);
                // Check for exception after evaluating condition
                if (ctx->exception_state.is_throwing) {
                    VALUE_RELEASE(condition);
                    break;
                }

                if (!VALUE_IS_TRUTHY_FAST(condition)) {
                    VALUE_RELEASE(condition);  // Release condition before breaking
                    break;
                }

                VALUE_RELEASE(condition);  // Release condition after checking

                // Clear and reuse iteration environment
                env_clear(iter_env);
                eval_stmt(stmt->as.while_stmt.body, iter_env, ctx);

                // Check for break/continue/return/exception
                if (ctx->loop_state.is_breaking) {
                    // Check if this break targets us
                    if (ctx->loop_state.target_label == NULL ||
                        (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                        // Break targets this loop
                        ctx->loop_state.is_breaking = 0;
                        if (ctx->loop_state.target_label) {
                            free(ctx->loop_state.target_label);
                            ctx->loop_state.target_label = NULL;
                        }
                        break;
                    }
                    // Break targets an outer loop, propagate
                    break;
                }
                if (ctx->loop_state.is_continuing) {
                    // Check if this continue targets us
                    if (ctx->loop_state.target_label == NULL ||
                        (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                        // Continue targets this loop
                        ctx->loop_state.is_continuing = 0;
                        if (ctx->loop_state.target_label) {
                            free(ctx->loop_state.target_label);
                            ctx->loop_state.target_label = NULL;
                        }
                        continue;
                    }
                    // Continue targets an outer loop, propagate
                    break;
                }
                if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                    break;
                }
            }
            env_release(iter_env);
            break;
        }

        case STMT_LOOP: {
            // Create iteration environment once, reuse each iteration
            Environment *iter_env = env_new(env);
            const char *loop_label = stmt->as.loop_stmt.label;
            for (;;) {
                // Clear and reuse iteration environment
                env_clear(iter_env);
                eval_stmt(stmt->as.loop_stmt.body, iter_env, ctx);

                // Check for break/continue/return/exception
                if (ctx->loop_state.is_breaking) {
                    // Check if this break targets us
                    if (ctx->loop_state.target_label == NULL ||
                        (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                        // Break targets this loop
                        ctx->loop_state.is_breaking = 0;
                        if (ctx->loop_state.target_label) {
                            free(ctx->loop_state.target_label);
                            ctx->loop_state.target_label = NULL;
                        }
                        break;
                    }
                    // Break targets an outer loop, propagate
                    break;
                }
                if (ctx->loop_state.is_continuing) {
                    // Check if this continue targets us
                    if (ctx->loop_state.target_label == NULL ||
                        (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                        // Continue targets this loop
                        ctx->loop_state.is_continuing = 0;
                        if (ctx->loop_state.target_label) {
                            free(ctx->loop_state.target_label);
                            ctx->loop_state.target_label = NULL;
                        }
                        continue;
                    }
                    // Continue targets an outer loop, propagate
                    break;
                }
                if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                    break;
                }
            }
            env_release(iter_env);
            break;
        }

        case STMT_FOR: {
            // Create new environment for loop scope
            Environment *loop_env = env_new(env);
            const char *loop_label = stmt->as.for_loop.label;

            // Execute initializer
            if (stmt->as.for_loop.initializer) {
                eval_stmt(stmt->as.for_loop.initializer, loop_env, ctx);
                // Check for exception/return after initializer
                if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                    env_release(loop_env);
                    break;
                }
            }

            // Create iteration environment once, reuse each iteration
            Environment *iter_env = env_new(loop_env);

            // Loop
            for (;;) {
                // Check condition
                if (stmt->as.for_loop.condition) {
                    Value cond = eval_expr(stmt->as.for_loop.condition, loop_env, ctx);
                    // Check for exception after condition evaluation
                    if (ctx->exception_state.is_throwing) {
                        VALUE_RELEASE(cond);  // Release condition before breaking
                        break;
                    }
                    if (!VALUE_IS_TRUTHY_FAST(cond)) {
                        VALUE_RELEASE(cond);  // Release condition before breaking
                        break;
                    }
                    VALUE_RELEASE(cond);  // Release condition after checking
                }

                // Clear and reuse iteration environment
                env_clear(iter_env);
                eval_stmt(stmt->as.for_loop.body, iter_env, ctx);

                // Check for break/continue/return/exception
                if (ctx->loop_state.is_breaking) {
                    // Check if this break targets us
                    if (ctx->loop_state.target_label == NULL ||
                        (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                        // Break targets this loop
                        ctx->loop_state.is_breaking = 0;
                        if (ctx->loop_state.target_label) {
                            free(ctx->loop_state.target_label);
                            ctx->loop_state.target_label = NULL;
                        }
                        break;
                    }
                    // Break targets an outer loop, propagate
                    break;
                }
                if (ctx->loop_state.is_continuing) {
                    // Check if this continue targets us
                    if (ctx->loop_state.target_label == NULL ||
                        (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                        // Continue targets this loop
                        ctx->loop_state.is_continuing = 0;
                        if (ctx->loop_state.target_label) {
                            free(ctx->loop_state.target_label);
                            ctx->loop_state.target_label = NULL;
                        }
                        // Fall through to increment
                    } else {
                        // Continue targets an outer loop, propagate
                        break;
                    }
                }
                if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                    break;
                }

                // Execute increment
                if (stmt->as.for_loop.increment) {
                    Value incr_result = eval_expr(stmt->as.for_loop.increment, loop_env, ctx);
                    VALUE_RELEASE(incr_result);  // Release increment expression result
                    // Check for exception after increment
                    if (ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            }

            env_release(iter_env);
            env_release(loop_env);
            break;
        }

        case STMT_FOR_IN: {
            Value iterable = eval_expr(stmt->as.for_in.iterable, env, ctx);
            const char *loop_label = stmt->as.for_in.label;

            // Check for exception after evaluating iterable
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(iterable);  // Release iterable before breaking
                break;
            }

            // Validate iterable type before creating loop environment
            if (iterable.type != VAL_ARRAY && iterable.type != VAL_OBJECT && iterable.type != VAL_STRING) {
                VALUE_RELEASE(iterable);  // Release iterable before breaking
                ctx->exception_state.exception_value = val_string("for-in requires array, object, or string");
                ctx->exception_state.is_throwing = 1;
                break;
            }

            Environment *loop_env = env_new(env);
            // Create iteration environment once, reuse each iteration
            Environment *iter_env = env_new(loop_env);

            if (iterable.type == VAL_ARRAY) {
                Array *arr = iterable.as.as_array;

                for (int i = 0; i < arr->length; i++) {
                    // Clear and reuse iteration environment
                    env_clear(iter_env);

                    // Bind variables
                    if (stmt->as.for_in.key_var) {
                        env_set(iter_env, stmt->as.for_in.key_var, val_i32(i), ctx);
                        // Check for exception from env_set
                        if (ctx->exception_state.is_throwing) {
                            break;
                        }
                    }
                    env_set(iter_env, stmt->as.for_in.value_var, arr->elements[i], ctx);
                    // Check for exception from env_set
                    if (ctx->exception_state.is_throwing) {
                        break;
                    }

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, iter_env, ctx);

                    // Check break/continue/return/exception
                    if (ctx->loop_state.is_breaking) {
                        // Check if this break targets us
                        if (ctx->loop_state.target_label == NULL ||
                            (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                            ctx->loop_state.is_breaking = 0;
                            if (ctx->loop_state.target_label) {
                                free(ctx->loop_state.target_label);
                                ctx->loop_state.target_label = NULL;
                            }
                            break;
                        }
                        break;  // Propagate to outer loop
                    }
                    if (ctx->loop_state.is_continuing) {
                        // Check if this continue targets us
                        if (ctx->loop_state.target_label == NULL ||
                            (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                            ctx->loop_state.is_continuing = 0;
                            if (ctx->loop_state.target_label) {
                                free(ctx->loop_state.target_label);
                                ctx->loop_state.target_label = NULL;
                            }
                            continue;
                        }
                        break;  // Propagate to outer loop
                    }
                    if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            } else if (iterable.type == VAL_OBJECT) {
                Object *obj = iterable.as.as_object;

                for (int i = 0; i < obj->num_fields; i++) {
                    // Clear and reuse iteration environment
                    env_clear(iter_env);

                    // Bind variables
                    if (stmt->as.for_in.key_var) {
                        env_set(iter_env, stmt->as.for_in.key_var, val_string(obj->field_names[i]), ctx);
                        // Check for exception from env_set
                        if (ctx->exception_state.is_throwing) {
                            break;
                        }
                    }
                    env_set(iter_env, stmt->as.for_in.value_var, obj->field_values[i], ctx);
                    // Check for exception from env_set
                    if (ctx->exception_state.is_throwing) {
                        break;
                    }

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, iter_env, ctx);

                    // Check break/continue/return/exception
                    if (ctx->loop_state.is_breaking) {
                        // Check if this break targets us
                        if (ctx->loop_state.target_label == NULL ||
                            (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                            ctx->loop_state.is_breaking = 0;
                            if (ctx->loop_state.target_label) {
                                free(ctx->loop_state.target_label);
                                ctx->loop_state.target_label = NULL;
                            }
                            break;
                        }
                        break;  // Propagate to outer loop
                    }
                    if (ctx->loop_state.is_continuing) {
                        // Check if this continue targets us
                        if (ctx->loop_state.target_label == NULL ||
                            (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                            ctx->loop_state.is_continuing = 0;
                            if (ctx->loop_state.target_label) {
                                free(ctx->loop_state.target_label);
                                ctx->loop_state.target_label = NULL;
                            }
                            continue;
                        }
                        break;  // Propagate to outer loop
                    }
                    if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            } else if (iterable.type == VAL_STRING) {
                String *str = iterable.as.as_string;

                // Compute character length if not cached
                if (str->char_length < 0) {
                    str->char_length = utf8_count_codepoints(str->data, str->length);
                }

                for (int i = 0; i < str->char_length; i++) {
                    // Clear and reuse iteration environment
                    env_clear(iter_env);

                    // Bind index if key_var is specified
                    if (stmt->as.for_in.key_var) {
                        env_set(iter_env, stmt->as.for_in.key_var, val_i32(i), ctx);
                        // Check for exception from env_set
                        if (ctx->exception_state.is_throwing) {
                            break;
                        }
                    }

                    // Get the rune at position i
                    int byte_pos = utf8_byte_offset(str->data, str->length, i);
                    uint32_t codepoint = utf8_decode_at(str->data, byte_pos);

                    env_set(iter_env, stmt->as.for_in.value_var, val_rune(codepoint), ctx);
                    // Check for exception from env_set
                    if (ctx->exception_state.is_throwing) {
                        break;
                    }

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, iter_env, ctx);

                    // Check break/continue/return/exception
                    if (ctx->loop_state.is_breaking) {
                        // Check if this break targets us
                        if (ctx->loop_state.target_label == NULL ||
                            (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                            ctx->loop_state.is_breaking = 0;
                            if (ctx->loop_state.target_label) {
                                free(ctx->loop_state.target_label);
                                ctx->loop_state.target_label = NULL;
                            }
                            break;
                        }
                        break;  // Propagate to outer loop
                    }
                    if (ctx->loop_state.is_continuing) {
                        // Check if this continue targets us
                        if (ctx->loop_state.target_label == NULL ||
                            (loop_label && strcmp(ctx->loop_state.target_label, loop_label) == 0)) {
                            ctx->loop_state.is_continuing = 0;
                            if (ctx->loop_state.target_label) {
                                free(ctx->loop_state.target_label);
                                ctx->loop_state.target_label = NULL;
                            }
                            continue;
                        }
                        break;  // Propagate to outer loop
                    }
                    if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            }

            env_release(iter_env);
            env_release(loop_env);
            VALUE_RELEASE(iterable);  // Release iterable after loop completes
            break;
        }

        case STMT_BREAK:
            ctx->loop_state.is_breaking = 1;
            // Store target label if present
            if (stmt->as.break_stmt.label) {
                ctx->loop_state.target_label = strdup(stmt->as.break_stmt.label);
            } else {
                ctx->loop_state.target_label = NULL;
            }
            break;

        case STMT_CONTINUE:
            ctx->loop_state.is_continuing = 1;
            // Store target label if present
            if (stmt->as.continue_stmt.label) {
                ctx->loop_state.target_label = strdup(stmt->as.continue_stmt.label);
            } else {
                ctx->loop_state.target_label = NULL;
            }
            break;

        case STMT_BLOCK: {
            // Create a new environment for the block to enable proper lexical scoping.
            // This allows variables declared with 'let' inside a block to shadow
            // outer variables, matching JavaScript's let/const semantics.
            Environment *block_env = env_new(env);
            for (int i = 0; i < stmt->as.block.count; i++) {
                eval_stmt(stmt->as.block.statements[i], block_env, ctx);
                // Check if a return/break/continue/exception happened
                if (ctx->return_state.is_returning || ctx->loop_state.is_breaking ||
                    ctx->loop_state.is_continuing || ctx->exception_state.is_throwing) {
                    break;
                }
            }
            env_release(block_env);
            break;
        }

        case STMT_RETURN: {
            // Evaluate return value (or null if none)
            if (stmt->as.return_stmt.value) {
                ctx->return_state.return_value = eval_expr(stmt->as.return_stmt.value, env, ctx);
                // Check for exception - don't set is_returning if exception occurred
                if (ctx->exception_state.is_throwing) {
                    VALUE_RELEASE(ctx->return_state.return_value);
                    ctx->return_state.return_value = val_null();
                    break;
                }
            } else {
                ctx->return_state.return_value = val_null();
            }
            ctx->return_state.is_returning = 1;
            break;
        }

        case STMT_DEFINE_OBJECT: {
            // Create object type definition
            ObjectType *type = malloc(sizeof(ObjectType));
            type->name = strdup(stmt->as.define_object.name);
            type->num_fields = stmt->as.define_object.num_fields;

            // Copy type parameters (for generic types like define Stack<T>)
            type->num_type_params = stmt->as.define_object.num_type_params;
            if (type->num_type_params > 0) {
                type->type_params = malloc(sizeof(char*) * type->num_type_params);
                for (int i = 0; i < type->num_type_params; i++) {
                    type->type_params[i] = strdup(stmt->as.define_object.type_params[i]);
                }
            } else {
                type->type_params = NULL;
            }

            // Copy field information
            type->field_names = malloc(sizeof(char*) * type->num_fields);
            type->field_types = malloc(sizeof(Type*) * type->num_fields);
            type->field_optional = malloc(sizeof(int) * type->num_fields);
            type->field_defaults = malloc(sizeof(Expr*) * type->num_fields);

            for (int i = 0; i < type->num_fields; i++) {
                type->field_names[i] = strdup(stmt->as.define_object.field_names[i]);
                type->field_types[i] = stmt->as.define_object.field_types[i];
                type->field_optional[i] = stmt->as.define_object.field_optional[i];
                type->field_defaults[i] = stmt->as.define_object.field_defaults[i];
            }

            // Copy method signature information
            type->num_methods = stmt->as.define_object.num_methods;
            if (type->num_methods > 0) {
                type->method_names = malloc(sizeof(char*) * type->num_methods);
                type->method_types = malloc(sizeof(Type*) * type->num_methods);
                type->method_optional = malloc(sizeof(int) * type->num_methods);
                type->method_defaults = malloc(sizeof(Expr*) * type->num_methods);

                for (int i = 0; i < type->num_methods; i++) {
                    type->method_names[i] = strdup(stmt->as.define_object.method_names[i]);
                    type->method_types[i] = stmt->as.define_object.method_types[i];
                    type->method_optional[i] = stmt->as.define_object.method_optional[i];
                    type->method_defaults[i] = stmt->as.define_object.method_defaults[i];
                }
            } else {
                type->method_names = NULL;
                type->method_types = NULL;
                type->method_optional = NULL;
                type->method_defaults = NULL;
            }

            // Register the type
            register_object_type(type);

            // Also register for FFI if all fields have type annotations
            // and it's not a generic type (generic types can't be used for FFI directly)
            // (FFI struct registration is lazy/best-effort - it only fails
            // when the struct is actually used in an FFI function)
            int has_all_types = 1;
            int has_type_params = (type->num_type_params > 0);
            for (int i = 0; i < type->num_fields; i++) {
                if (type->field_types[i] == NULL) {
                    has_all_types = 0;
                    break;
                }
                // Also check if any field type is a type parameter
                if (type->field_types[i]->kind == TYPE_PARAM) {
                    has_type_params = 1;
                    break;
                }
            }
            if (has_all_types && type->num_fields > 0 && !has_type_params) {
                ffi_register_struct(type->name, type->field_names,
                                    type->field_types, type->num_fields);
            }
            break;
        }

        case STMT_ENUM: {
            // Create enum type definition
            EnumType *type = malloc(sizeof(EnumType));
            type->name = strdup(stmt->as.enum_decl.name);
            type->num_variants = stmt->as.enum_decl.num_variants;

            // Copy variant names
            type->variant_names = malloc(sizeof(char*) * type->num_variants);
            type->variant_values = malloc(sizeof(int32_t) * type->num_variants);

            // Evaluate variant values (auto-increment or explicit)
            int32_t auto_value = 0;
            int had_error = 0;
            int variants_initialized = 0;  // Track how many variants were actually initialized
            for (int i = 0; i < type->num_variants; i++) {
                type->variant_names[i] = strdup(stmt->as.enum_decl.variant_names[i]);
                variants_initialized = i + 1;  // This variant name is now initialized

                if (stmt->as.enum_decl.variant_values[i] != NULL) {
                    // Explicit value - evaluate the expression
                    Value val = eval_expr(stmt->as.enum_decl.variant_values[i], env, ctx);
                    // Check for exception after evaluating variant value
                    if (ctx->exception_state.is_throwing) {
                        VALUE_RELEASE(val);
                        had_error = 1;
                        break;
                    }
                    if (val.type != VAL_I32) {
                        runtime_error(ctx, "Enum variant '%s' value must be i32",
                                     stmt->as.enum_decl.variant_names[i]);
                        VALUE_RELEASE(val);
                        had_error = 1;
                        break;
                    }
                    type->variant_values[i] = val.as.as_i32;
                    auto_value = val.as.as_i32 + 1;  // Next auto value
                    VALUE_RELEASE(val);
                } else {
                    // Auto value
                    type->variant_values[i] = auto_value;
                    auto_value++;
                }
            }
            if (had_error) {
                // Clean up only the variants that were actually initialized
                for (int i = 0; i < variants_initialized; i++) {
                    free(type->variant_names[i]);
                }
                free(type->variant_names);
                free(type->variant_values);
                free(type->name);
                free(type);
                break;
            }

            // Register the enum type
            register_enum_type(type);

            // Create a namespace object with the enum variants
            Object *obj = malloc(sizeof(Object));
            obj->type_name = strdup(type->name);
            obj->num_fields = type->num_variants;
            obj->capacity = type->num_variants;
            obj->field_names = malloc(sizeof(char*) * type->num_variants);
            obj->field_values = malloc(sizeof(Value) * type->num_variants);
            obj->ref_count = 1;
            atomic_store(&obj->freed, 0);  // Not freed
            obj->hash_table = NULL;  // No hash table - use linear search fallback
            obj->hash_capacity = 0;

            for (int i = 0; i < type->num_variants; i++) {
                obj->field_names[i] = strdup(type->variant_names[i]);
                obj->field_values[i] = val_i32(type->variant_values[i]);
            }

            Value enum_obj;
            enum_obj.type = VAL_OBJECT;
            enum_obj.as.as_object = obj;

            // Bind the enum namespace to the environment
            env_define(env, type->name, enum_obj, 1, ctx);  // 1 = const
            break;
        }

        case STMT_TRY: {
            // Execute try block
            eval_stmt(stmt->as.try_stmt.try_block, env, ctx);

            // Check if exception was thrown
            if (ctx->exception_state.is_throwing) {
                // Exception thrown - execute catch block if present
                if (stmt->as.try_stmt.catch_block != NULL) {
                    // Create new scope for catch parameter
                    Environment *catch_env = env_new(env);
                    // Use env_define (not env_set) to create a new variable that shadows outer scope
                    env_define(catch_env, stmt->as.try_stmt.catch_param, ctx->exception_state.exception_value, 0, ctx);

                    // Clear exception state and release the exception value
                    // (env_set retained it, so we can release the context's reference)
                    VALUE_RELEASE(ctx->exception_state.exception_value);
                    ctx->exception_state.is_throwing = 0;

                    // Execute catch block
                    eval_stmt(stmt->as.try_stmt.catch_block, catch_env, ctx);

                    env_release(catch_env);
                }
            }

            // Execute finally block if present (always executes)
            if (stmt->as.try_stmt.finally_block != NULL) {
                // Save current state (return/exception/break/continue)
                int was_returning = ctx->return_state.is_returning;
                Value saved_return = ctx->return_state.return_value;
                int was_throwing = ctx->exception_state.is_throwing;
                Value saved_exception = ctx->exception_state.exception_value;
                int was_breaking = ctx->loop_state.is_breaking;
                int was_continuing = ctx->loop_state.is_continuing;

                // Clear states before finally
                ctx->return_state.is_returning = 0;
                ctx->exception_state.is_throwing = 0;
                ctx->loop_state.is_breaking = 0;
                ctx->loop_state.is_continuing = 0;

                // Execute finally block
                eval_stmt(stmt->as.try_stmt.finally_block, env, ctx);

                // If finally didn't throw/return/break/continue, restore previous state
                if (!ctx->return_state.is_returning && !ctx->exception_state.is_throwing &&
                    !ctx->loop_state.is_breaking && !ctx->loop_state.is_continuing) {
                    ctx->return_state.is_returning = was_returning;
                    ctx->return_state.return_value = saved_return;
                    ctx->exception_state.is_throwing = was_throwing;
                    ctx->exception_state.exception_value = saved_exception;
                    ctx->loop_state.is_breaking = was_breaking;
                    ctx->loop_state.is_continuing = was_continuing;
                }
            }
            break;
        }

        case STMT_THROW: {
            // Evaluate the value to throw and retain it
            // (so it survives past environment cleanups during unwinding)
            ctx->exception_state.exception_value = eval_expr(stmt->as.throw_stmt.value, env, ctx);
            VALUE_RETAIN(ctx->exception_state.exception_value);
            ctx->exception_state.is_throwing = 1;

            // Push throw location onto stack trace
            call_stack_push_line(&ctx->call_stack, "<throw>", stmt->line);
            break;
        }

        case STMT_SWITCH: {
            // Evaluate the switch expression
            Value switch_value = eval_expr(stmt->as.switch_stmt.expr, env, ctx);
            // Check for exception after evaluating switch expression
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(switch_value);
                break;
            }

            // Find matching case or default
            int matched_case = -1;
            int default_case = -1;

            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i] == NULL) {
                    // This is the default case
                    default_case = i;
                } else {
                    // Evaluate case value and compare
                    Value case_value = eval_expr(stmt->as.switch_stmt.case_values[i], env, ctx);
                    // Check for exception after evaluating case value
                    if (ctx->exception_state.is_throwing) {
                        VALUE_RELEASE(case_value);
                        VALUE_RELEASE(switch_value);
                        break;
                    }

                    if (values_equal(switch_value, case_value)) {
                        VALUE_RELEASE(case_value);  // Release case value after comparison
                        matched_case = i;
                        break;
                    }
                    VALUE_RELEASE(case_value);  // Release case value after comparison
                }
            }
            // Check if we broke out of the loop due to an exception
            if (ctx->exception_state.is_throwing) {
                break;
            }

            // If no case matched, use default if available
            if (matched_case == -1 && default_case != -1) {
                matched_case = default_case;
            }

            // Execute from matched case onwards (fall-through behavior)
            if (matched_case != -1) {
                for (int i = matched_case; i < stmt->as.switch_stmt.num_cases; i++) {
                    eval_stmt(stmt->as.switch_stmt.case_bodies[i], env, ctx);

                    // Check for break, return, continue, or exception
                    if (ctx->loop_state.is_breaking) {
                        ctx->loop_state.is_breaking = 0;
                        break;
                    }
                    if (ctx->loop_state.is_continuing) {
                        // Continue propagates up to enclosing loop, exit switch
                        break;
                    }
                    if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                        break;
                    }
                }
            }

            VALUE_RELEASE(switch_value);  // Release switch value after switch completes
            break;
        }

        case STMT_DEFER: {
            // Push the deferred call onto the defer stack
            // It will be executed when the function returns (or exits with exception)
            defer_stack_push(&ctx->defer_stack, stmt->as.defer_stmt.call, env);
            break;
        }

        case STMT_IMPORT:
            // Import statements are handled by the module system during module loading
            // If we encounter one here, it's a no-op (module has already been loaded)
            break;

        case STMT_IMPORT_FFI:
            execute_import_ffi(stmt, ctx);
            break;

        case STMT_EXTERN_FN:
            execute_extern_fn(stmt, env, ctx);
            break;

        case STMT_EXPORT:
            // Export statements are handled by the module system during module loading
            // During normal execution, just execute the declaration if present
            if (stmt->as.export_stmt.is_declaration) {
                eval_stmt(stmt->as.export_stmt.declaration, env, ctx);
            }
            // Export lists and re-exports are no-ops during execution
            break;

        case STMT_TYPE_ALIAS: {
            // Create type alias definition
            TypeAlias *alias = malloc(sizeof(TypeAlias));
            alias->name = strdup(stmt->as.type_alias.name);
            alias->num_type_params = stmt->as.type_alias.num_type_params;

            // Copy type parameters
            if (alias->num_type_params > 0) {
                alias->type_params = malloc(sizeof(char*) * alias->num_type_params);
                for (int i = 0; i < alias->num_type_params; i++) {
                    alias->type_params[i] = strdup(stmt->as.type_alias.type_params[i]);
                }
            } else {
                alias->type_params = NULL;
            }

            // Store the aliased type (this is an AST pointer, don't free it)
            alias->aliased_type = stmt->as.type_alias.aliased_type;

            // Register the type alias
            register_type_alias(alias);
            break;
        }

        case STMT_MATCH: {
            // Evaluate the match expression
            Value match_value = eval_expr(stmt->as.match_stmt.expr, env, ctx);
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(match_value);
                break;
            }

            // Try each arm in order
            int matched = 0;
            for (int i = 0; i < stmt->as.match_stmt.num_arms && !matched; i++) {
                // Create a new scope for pattern bindings
                Environment *arm_env = env_new(env);

                // Try to match the pattern
                if (match_pattern(stmt->as.match_stmt.patterns[i], match_value, arm_env, ctx)) {
                    // Pattern matched - check guard if present
                    int guard_passed = 1;
                    if (stmt->as.match_stmt.guards[i] != NULL) {
                        Value guard_val = eval_expr(stmt->as.match_stmt.guards[i], arm_env, ctx);
                        if (ctx->exception_state.is_throwing) {
                            VALUE_RELEASE(guard_val);
                            env_release(arm_env);
                            VALUE_RELEASE(match_value);
                            break;
                        }
                        guard_passed = value_is_truthy(guard_val);
                        VALUE_RELEASE(guard_val);
                    }

                    if (guard_passed) {
                        // Execute the arm body
                        eval_stmt(stmt->as.match_stmt.bodies[i], arm_env, ctx);
                        matched = 1;
                    }
                }

                env_release(arm_env);

                // Check for control flow
                if (ctx->return_state.is_returning || ctx->exception_state.is_throwing) {
                    break;
                }
            }

            VALUE_RELEASE(match_value);

            // No match is not an error - just continue (like switch without default)
            break;
        }
    }
}

void eval_program(Stmt **stmts, int count, Environment *env, ExecutionContext *ctx) {
    for (int i = 0; i < count; i++) {
        eval_stmt(stmts[i], env, ctx);

        // Check for uncaught exception
        if (ctx->exception_state.is_throwing) {
            // Convert exception value to string for stderr output
            char *error_msg = value_to_string(ctx->exception_state.exception_value);
            fprintf(stderr, "Uncaught exception: %s\n", error_msg);
            free(error_msg);
            // Print stack trace
            call_stack_print(&ctx->call_stack);
            // Clear stack for next execution (REPL mode)
            call_stack_free(&ctx->call_stack);
            // Release exception value before exiting
            VALUE_RELEASE(ctx->exception_state.exception_value);
            exit(1);
        }
    }
}
