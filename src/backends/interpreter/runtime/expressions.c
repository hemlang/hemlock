#include "internal.h"
#include "expressions_internal.h"

// Forward declaration for binary operations (in binary_ops.c)
Value eval_binary_expr(Expr *expr, Environment *env, ExecutionContext *ctx);


// ========== HELPER FUNCTIONS ==========

// Get the type name of a value for error messages
const char* get_value_type_name(Value val) {
    switch (val.type) {
        case VAL_NULL: return "null";
        case VAL_BOOL: return "bool";
        case VAL_I8: return "i8";
        case VAL_I16: return "i16";
        case VAL_I32: return "i32";
        case VAL_I64: return "i64";
        case VAL_U8: return "u8";
        case VAL_U16: return "u16";
        case VAL_U32: return "u32";
        case VAL_U64: return "u64";
        case VAL_F32: return "f32";
        case VAL_F64: return "f64";
        case VAL_RUNE: return "rune";
        case VAL_STRING: return "string";
        case VAL_ARRAY: return "array";
        case VAL_OBJECT: return "object";
        case VAL_FUNCTION: return "function";
        case VAL_BUILTIN_FN: return "builtin function";
        case VAL_FFI_FUNCTION: return "ffi function";
        case VAL_PTR: return "pointer";
        case VAL_BUFFER: return "buffer";
        case VAL_FILE: return "file";
        case VAL_TASK: return "task";
        case VAL_CHANNEL: return "channel";
        case VAL_SOCKET: return "socket";
        default: return "unknown";
    }
}

// Helper to add two values (for increment operations).
// Computes in 64-bit and truncates to the operand's own type, so i64 values
// above 32 bits increment correctly (previously truncated through int32) and
// narrow types wrap at their own width (u16 65535++ -> 0).
Value value_add_one(Value val, ExecutionContext *ctx) {
    if (is_float(val)) {
        double v = value_to_float(val);
        return (val.type == VAL_F32) ? val_f32((float)(v + 1.0)) : val_f64(v + 1.0);
    } else if (is_integer(val)) {
        uint64_t v = (uint64_t)value_to_int64(val);
        return promote_value(val_i64((int64_t)(v + 1)), val.type);
    } else {
        runtime_error(ctx, "Can only increment numeric values");
        return val_null();  // Unreachable
    }
}

// Helper to subtract one from a value (for decrement operations)
Value value_sub_one(Value val, ExecutionContext *ctx) {
    if (is_float(val)) {
        double v = value_to_float(val);
        return (val.type == VAL_F32) ? val_f32((float)(v - 1.0)) : val_f64(v - 1.0);
    } else if (is_integer(val)) {
        uint64_t v = (uint64_t)value_to_int64(val);
        return promote_value(val_i64((int64_t)(v - 1)), val.type);
    } else {
        runtime_error(ctx, "Can only decrement numeric values");
        return val_null();  // Unreachable
    }
}

// Pattern matching moved to eval_pattern_match.c

// ========== EXPRESSION EVALUATION ==========

Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx) {
    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return val_float(expr->as.number.float_value);
            } else if (expr->as.number.is_u64) {
                return val_u64(expr->as.number.uint_value);
            } else {
                int64_t value = expr->as.number.int_value;
                // Use i32 for values that fit in 32-bit range, otherwise i64
                if (value >= INT32_MIN && value <= INT32_MAX) {
                    return val_int((int32_t)value);
                } else {
                    return val_i64(value);
                }
            }
            break;

        case EXPR_BOOL:
            return val_bool(expr->as.boolean);

        case EXPR_NULL:
            return val_null();

        case EXPR_STRING:
            return val_string(expr->as.string);

        case EXPR_RUNE:
            return val_rune(expr->as.rune);

        case EXPR_UNARY: {
            Value operand = eval_expr(expr->as.unary.operand, env, ctx);
            Value unary_result = val_null();

            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    unary_result = val_bool(!value_is_truthy(operand));
                    break;

                case UNARY_NEGATE:
                    if (is_float(operand)) {
                        // Preserve the float width when negating
                        if (operand.type == VAL_F32) {
                            unary_result = val_f32(-operand.as.as_f32);
                        } else {
                            unary_result = val_f64(-value_to_float(operand));
                        }
                    } else if (is_integer(operand)) {
                        // Preserve the original type when negating.
                        // Narrow types wrap; i32/i64 MIN negation overflows
                        // and throws (consistent with checked +,-,*).
                        switch (operand.type) {
                            case VAL_I8: unary_result = val_i8((int8_t)(-(int16_t)operand.as.as_i8)); break;
                            case VAL_I16: unary_result = val_i16((int16_t)(-(int32_t)operand.as.as_i16)); break;
                            case VAL_I32:
                                if (operand.as.as_i32 == INT32_MIN) {
                                    runtime_error(ctx, "Integer overflow: i32 negation");
                                } else {
                                    unary_result = val_i32(-operand.as.as_i32);
                                }
                                break;
                            case VAL_I64:
                                if (operand.as.as_i64 == INT64_MIN) {
                                    runtime_error(ctx, "Integer overflow: i64 negation");
                                } else {
                                    unary_result = val_i64(-operand.as.as_i64);
                                }
                                break;
                            case VAL_U8: unary_result = val_i16(-(int16_t)operand.as.as_u8); break;  // promote to i16
                            case VAL_U16: unary_result = val_i32(-(int32_t)operand.as.as_u16); break; // promote to i32
                            case VAL_U32: unary_result = val_i64(-(int64_t)operand.as.as_u32); break; // promote to i64
                            case VAL_U64: {
                                // Special case: u64 negation - check if value fits in i64
                                if (operand.as.as_u64 <= INT64_MAX) {
                                    unary_result = val_i64(-(int64_t)operand.as.as_u64);
                                } else {
                                    runtime_error(ctx, "Cannot negate u64 value larger than INT64_MAX");
                                }
                                break;
                            }
                            default:
                                runtime_error(ctx, "Cannot negate non-integer value");
                        }
                    } else {
                        runtime_error(ctx, "Cannot negate non-numeric value");
                    }
                    break;

                case UNARY_BIT_NOT:
                    if (is_integer(operand)) {
                        // Bitwise NOT - preserve the original type
                        switch (operand.type) {
                            case VAL_I8: unary_result = val_i8(~operand.as.as_i8); break;
                            case VAL_I16: unary_result = val_i16(~operand.as.as_i16); break;
                            case VAL_I32: unary_result = val_i32(~operand.as.as_i32); break;
                            case VAL_I64: unary_result = val_i64(~operand.as.as_i64); break;
                            case VAL_U8: unary_result = val_u8(~operand.as.as_u8); break;
                            case VAL_U16: unary_result = val_u16(~operand.as.as_u16); break;
                            case VAL_U32: unary_result = val_u32(~operand.as.as_u32); break;
                            case VAL_U64: unary_result = val_u64(~operand.as.as_u64); break;
                            default:
                                runtime_error(ctx, "Cannot apply bitwise NOT to non-integer value");
                        }
                    } else {
                        runtime_error(ctx, "Cannot apply bitwise NOT to non-integer value");
                    }
                    break;
            }
            // Release operand after unary operation
            VALUE_RELEASE(operand);
            return unary_result;
        }

        case EXPR_TERNARY: {
            Value condition = eval_expr(expr->as.ternary.condition, env, ctx);
            Value result;
            if (value_is_truthy(condition)) {
                result = eval_expr(expr->as.ternary.true_expr, env, ctx);
            } else {
                result = eval_expr(expr->as.ternary.false_expr, env, ctx);
            }
            VALUE_RELEASE(condition);  // Release condition after checking
            return result;
        }

        case EXPR_IDENT: {
            // Use fast resolved lookup if available, else fall back to hash lookup
            Value val;
            if (expr->as.ident.resolved.is_resolved) {
                val = env_get_resolved(env, expr->as.ident.resolved.depth, expr->as.ident.resolved.slot);
            } else {
                val = env_get(env, expr->as.ident.name, ctx);
            }
            // Auto-dereference if this is a reference (from ref parameter)
            if (val.type == VAL_REF) {
                Value deref_val = ref_deref(val.as.as_ref, ctx);
                VALUE_RELEASE(val);  // Release the reference itself
                return deref_val;
            }
            return val;
        }

        case EXPR_ASSIGN: {
            Value new_value = eval_expr(expr->as.assign.value, env, ctx);
            if (ctx->exception_state.is_throwing) {
                VALUE_RELEASE(new_value);
                return val_null();
            }
            // Check if target is a reference (from ref parameter)
            Value target;
            if (expr->as.assign.resolved.is_resolved) {
                target = env_get_resolved(env, expr->as.assign.resolved.depth, expr->as.assign.resolved.slot);
            } else {
                target = env_get(env, expr->as.assign.name, ctx);
            }
            if (target.type == VAL_REF) {
                // Write through the reference to the original location
                ref_assign(target.as.as_ref, new_value, ctx);
                VALUE_RELEASE(target);
                return new_value;
            }
            VALUE_RELEASE(target);
            // Enforce the variable's declared type on reassignment using the
            // same conversion as the declaration (annotations are enforced at
            // runtime by the interpreter — CLAUDE.md/docs/language-guide/types.md)
            {
                Type *declared = expr->as.assign.resolved.is_resolved
                    ? env_get_declared_type_resolved(env, expr->as.assign.resolved.depth,
                                                     expr->as.assign.resolved.slot)
                    : env_get_declared_type(env, expr->as.assign.name);
                if (declared != NULL) {
                    new_value = convert_to_type(new_value, declared, env, ctx);
                    if (ctx->exception_state.is_throwing) {
                        VALUE_RELEASE(new_value);
                        return val_null();
                    }
                }
            }
            // Regular assignment
            if (expr->as.assign.resolved.is_resolved) {
                env_set_resolved(env, expr->as.assign.resolved.depth, expr->as.assign.resolved.slot, new_value, ctx);
            } else {
                env_set(env, expr->as.assign.name, new_value, ctx);
            }
            return new_value;
        }

        case EXPR_BINARY:
            return eval_binary_expr(expr, env, ctx);

        case EXPR_CALL:
            return eval_call_expr(expr, env, ctx);

        case EXPR_GET_PROPERTY: {
            // Save line for error reporting (sub-expression eval may change it)
            int saved_line = expr->line;

            Value object = eval_expr(expr->as.get_property.object, env, ctx);
            const char *property = expr->as.get_property.property;
            Value result = {0};

            // Restore line for any errors in this expression
            ctx->current_line = saved_line;

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                // .length property - returns codepoint count
                if (strcmp(property, "length") == 0) {
                    // Compute character length if not cached
                    if (str->char_length < 0) {
                        str->char_length = utf8_count_codepoints(str->data, str->length);
                    }
                    result = val_i32(str->char_length);
                } else if (strcmp(property, "byte_length") == 0) {
                    // .byte_length property - returns byte count
                    result = val_i32(str->length);
                } else {
                    runtime_error(ctx, "Unknown property '%s' for string", property);
                }
            } else if (object.type == VAL_BUFFER) {
                if (strcmp(property, "length") == 0) {
                    result = val_int(object.as.as_buffer->length);
                } else if (strcmp(property, "capacity") == 0) {
                    result = val_int(object.as.as_buffer->capacity);
                } else {
                    runtime_error(ctx, "Unknown property '%s' for buffer", property);
                }
            } else if (object.type == VAL_FILE) {
                FileHandle *file = object.as.as_file;
                if (strcmp(property, "path") == 0) {
                    result = val_string(file->path);
                } else if (strcmp(property, "mode") == 0) {
                    result = val_string(file->mode);
                } else if (strcmp(property, "closed") == 0) {
                    result = val_bool(file->closed);
                } else {
                    runtime_error(ctx, "Unknown property '%s' for file", property);
                }
            } else if (object.type == VAL_SOCKET) {
                // Socket properties (read-only)
                result = get_socket_property(object.as.as_socket, property, ctx);
            } else if (object.type == VAL_ARRAY) {
                // Array properties
                if (strcmp(property, "length") == 0) {
                    result = val_i32(object.as.as_array->length);
                } else {
                    runtime_error(ctx, "Array has no property '%s'", property);
                }
            } else if (object.type == VAL_OBJECT) {
                // Look up field in object using inline cache or hash table
                Object *obj = object.as.as_object;
                int idx;

                // The per-AST-node property inline cache (expr->...ic) is
                // SHARED across every spawned task thread — the interpreter
                // runs async tasks as real pthreads over one AST — and is
                // mutated on every access with no synchronization. Concurrent
                // threads race on the (cached_object, cached_field_index,
                // ic_state) group and resolve a TORN pair, so
                // obj->fields[idx] reads the wrong/garbage field; it surfaces
                // far away as e.g. "Only strings, buffers, arrays, and objects
                // have properties" inside a worker. Once any task has been
                // spawned, bypass the cache entirely and do the lock-free
                // read-only lookup (object_lookup_field_with_hash does not
                // mutate obj — see the 2.4.5 object-hash thread-safety work);
                // we neither read nor write the shared IC, so no race.
                // Single-threaded programs keep the fast path unchanged (no
                // perf regression — the IC only matters single-threaded, and
                // correctness wins over a micro-opt that corrupts results).
                if (__atomic_load_n(&g_interp_has_spawned, __ATOMIC_SEQ_CST)) {
                    idx = object_lookup_field_with_hash(obj, property, hash_string(property));
                } else {
                    PropertyIC *ic = &expr->as.get_property.ic;
                    idx = -1;

                    // INLINE CACHE FAST PATH:
                    // If we're accessing the same object and the cache is valid, use cached index
                    if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                        ic->cached_object == (void*)obj &&
                        ic->cached_field_index >= 0) {
                        // Validate the cached index still points to the correct field
                        if (object_validate_ic(obj, ic->cached_field_index, property)) {
                            idx = ic->cached_field_index;  // Cache hit!
                        } else {
                            // Cache is stale (object was modified), invalidate
                            ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                            ic->cached_object = NULL;
                            ic->cached_field_index = -1;
                        }
                    }

                    // CACHE MISS: Do full lookup and update cache
                    if (idx < 0) {
                        // Compute hash if not cached
                        if (ic->cached_hash == 0) {
                            ic->cached_hash = hash_string(property);
                        }
                        idx = object_lookup_field_with_hash(obj, property, ic->cached_hash);

                        // Update inline cache if not megamorphic
                        if (idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                            if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                                // First access - initialize cache
                                ic->cached_object = (void*)obj;
                                ic->cached_field_index = idx;
                                ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                                ic->miss_count = 0;
                            } else if (ic->cached_object != (void*)obj) {
                                // Different object - this is polymorphic
                                ic->miss_count++;
                                if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                    // Too many different objects, go megamorphic
                                    ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                                } else {
                                    // Update cache to new object
                                    ic->cached_object = (void*)obj;
                                    ic->cached_field_index = idx;
                                }
                            }
                        }
                    }
                }

                if (idx >= 0) {
                    result = obj->fields[idx].value;
                    // Retain the field value so it survives object release
                    VALUE_RETAIN(result);

                    // If the result is a function, bind 'self' to the object
                    // This enables method references like spawn(obj.method, ...)
                    if (result.type == VAL_FUNCTION) {
                            Function *orig_fn = result.as.as_function;

                            // Create a new environment with 'self' bound
                            Environment *bound_env = env_new(orig_fn->closure_env);
                            env_define(bound_env, "self", object, 0, ctx);

                            // Create a shallow copy of the function with the new closure
                            Function *bound_fn = fn_pool_alloc();
                            if (!bound_fn) {
                                bound_fn = malloc(sizeof(Function));
                                bound_fn->is_pooled = 0;
                            }
                            bound_fn->is_async = orig_fn->is_async;
                            bound_fn->param_names = orig_fn->param_names;
                            bound_fn->param_types = orig_fn->param_types;
                            bound_fn->param_defaults = orig_fn->param_defaults;
                            bound_fn->param_is_ref = orig_fn->param_is_ref;  // Share ref flags
                            bound_fn->param_hashes = orig_fn->param_hashes;  // Share pre-computed hashes
                            bound_fn->num_params = orig_fn->num_params;
                            bound_fn->rest_param = orig_fn->rest_param;  // Share rest param name
                            bound_fn->rest_param_type = orig_fn->rest_param_type;  // Share type
                            bound_fn->return_type = orig_fn->return_type;
                            bound_fn->body = orig_fn->body;
                            bound_fn->closure_env = bound_env;
                            bound_fn->ref_count = 1;
                            bound_fn->is_bound = 1;  // Mark as bound - don't free param arrays
                            bound_fn->borrows_ast_params = orig_fn->borrows_ast_params;

                            // Release the original function value - bound_fn shares param arrays
                            // with it via AST borrowing, so the data remains valid even after
                            // orig_fn is released (AST outlives all closures).
                            VALUE_RELEASE(result);

                            // Release our eval_expr reference to object - env_define in
                            // bound_env already retained it for 'self' binding.
                            VALUE_RELEASE(object);

                            return val_function(bound_fn);
                        }

                    VALUE_RELEASE(object);
                    return result;
                }
                runtime_error(ctx, "Object has no field '%s' (use ?. for optional access)", property);
            } else {
                runtime_error(ctx, "Only strings, buffers, arrays, and objects have properties");
            }

            // Release object after accessing property
            VALUE_RELEASE(object);
            return result;
        }

        case EXPR_INDEX: {
            // Save line for error reporting (sub-expression evals may change it)
            int saved_line = expr->line;

            Value object = eval_expr(expr->as.index.object, env, ctx);
            Value index_val = eval_expr(expr->as.index.index, env, ctx);
            Value result;

            // Restore line for any errors in this expression
            ctx->current_line = saved_line;

            // FAST PATH: array[i32] - most common indexing case
            if (object.type == VAL_ARRAY && index_val.type == VAL_I32) {
                Array *arr = object.as.as_array;
                int32_t index = index_val.as.as_i32;
                if (index >= 0 && index < arr->length) {
                    result = arr->elements[index];
                    VALUE_RETAIN(result);
                    VALUE_RELEASE(object);
                    return result;
                }
                // Fall through to normal path for bounds error
            }

            // Object property access with string key
            if (object.type == VAL_OBJECT && index_val.type == VAL_STRING) {
                Object *obj = object.as.as_object;
                const char *key = index_val.as.as_string->data;

                // Look up field by key using hash table
                int idx = object_lookup_field(obj, key);
                if (idx >= 0) {
                    result = obj->fields[idx].value;
                    VALUE_RETAIN(result);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return result;
                }

                // Field not found, return null
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return val_null();
            }

            // Object property access with non-string key (auto-coerce to string)
            if (object.type == VAL_OBJECT) {
                char key_buf[64];
                if (value_coerce_to_key(index_val, key_buf, sizeof(key_buf))) {
                    Object *obj = object.as.as_object;
                    int idx = object_lookup_field(obj, key_buf);
                    if (idx >= 0) {
                        result = obj->fields[idx].value;
                        VALUE_RETAIN(result);
                        VALUE_RELEASE(object);
                        VALUE_RELEASE(index_val);
                        return result;
                    }

                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return val_null();
                }
            }

            // For arrays, strings, and buffers, index must be an integer
            if (!is_integer(index_val)) {
                runtime_error(ctx, "Index must be an integer");
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                // Compute character length if not cached
                if (str->char_length < 0) {
                    str->char_length = utf8_count_codepoints(str->data, str->length);
                }

                // Check bounds using character count (not byte count)
                if (index < 0 || index >= str->char_length) {
                    runtime_error(ctx, "String index %d out of bounds (length=%d)", index, str->char_length);
                }

                // Find byte offset of the i-th codepoint
                int byte_pos = utf8_byte_offset(str->data, str->length, index);

                // Decode the codepoint at that position
                uint32_t codepoint = utf8_decode_at(str->data, byte_pos);

                result = val_rune(codepoint);  // New value, safe to release object
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    runtime_error(ctx, "Buffer index %d out of bounds (length %d)", index, buf->length);
                }

                // Return the byte as an integer (u8)
                result = val_u8(((unsigned char *)buf->data)[index]);  // New value, safe to release object
            } else if (object.type == VAL_ARRAY) {
                // Array indexing
                result = array_get(object.as.as_array, index, ctx);
                // Retain the element so it survives array release
                VALUE_RETAIN(result);
            } else if (object.type == VAL_PTR) {
                // Raw pointer indexing - no bounds checking (unsafe!)
                void *ptr = object.as.as_ptr;
                if (ptr == NULL) {
                    runtime_error(ctx, "Cannot index into null pointer");
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return val_null();
                }
                // Return the byte as u8
                result = val_u8(((unsigned char *)ptr)[index]);
            } else {
                runtime_error(ctx, "Only strings, buffers, arrays, pointers, and objects can be indexed");
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return val_null();
            }

            // Release the object and index after use
            VALUE_RELEASE(object);
            VALUE_RELEASE(index_val);
            return result;
        }

        case EXPR_INDEX_ASSIGN: {
            Value object = eval_expr(expr->as.index_assign.object, env, ctx);
            Value index_val = eval_expr(expr->as.index_assign.index, env, ctx);
            Value value = eval_expr(expr->as.index_assign.value, env, ctx);

            // FAST PATH: array[i32] = value - most common assignment case
            if (object.type == VAL_ARRAY && index_val.type == VAL_I32) {
                Array *arr = object.as.as_array;
                int32_t index = index_val.as.as_i32;
                // Check bounds and untyped array (most common case)
                if (index >= 0 && index < arr->length && !arr->element_type) {
                    // Release old value, retain new value
                    VALUE_RELEASE(arr->elements[index]);
                    VALUE_RETAIN(value);
                    arr->elements[index] = value;
                    VALUE_RELEASE(object);
                    return value;
                }
                // Fall through to normal path for bounds extension, typed arrays, or error
            }

            // Object property assignment with string key
            if (object.type == VAL_OBJECT && index_val.type == VAL_STRING) {
                Object *obj = object.as.as_object;
                const char *key = index_val.as.as_string->data;

                // Look for existing field using hash table
                int idx = object_lookup_field(obj, key);
                if (idx >= 0) {
                    // Update existing field (unified storage)
                    VALUE_RELEASE(obj->fields[idx].value);
                    obj->fields[idx].value = value;
                    VALUE_RETAIN(value);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return value;
                }

                // Add new field - invalidate hash table (will fall back to linear search)
                if (obj->hash_table) {
                    free(obj->hash_table);
                    obj->hash_table = NULL;
                    obj->hash_capacity = 0;
                }
                int new_num_fields = obj->num_fields + 1;
                // Grow field array (handles pooled objects safely)
                FieldEntry *new_fields = object_grow_fields(obj, new_num_fields);
                if (!new_fields) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Failed to expand object fields");
                    return val_null();
                }
                obj->num_fields = new_num_fields;
                obj->fields[obj->num_fields - 1].name = strdup(key);
                obj->fields[obj->num_fields - 1].value = value;
                VALUE_RETAIN(value);
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return value;
            }

            // Object property assignment with non-string key (auto-coerce to string)
            if (object.type == VAL_OBJECT) {
                char key_buf[64];
                if (value_coerce_to_key(index_val, key_buf, sizeof(key_buf))) {
                    Object *obj = object.as.as_object;
                    int idx = object_lookup_field(obj, key_buf);
                    if (idx >= 0) {
                        VALUE_RELEASE(obj->fields[idx].value);
                        obj->fields[idx].value = value;
                        VALUE_RETAIN(value);
                        VALUE_RELEASE(object);
                        VALUE_RELEASE(index_val);
                        return value;
                    }

                    if (obj->hash_table) {
                        free(obj->hash_table);
                        obj->hash_table = NULL;
                        obj->hash_capacity = 0;
                    }
                    int new_num_fields = obj->num_fields + 1;
                    FieldEntry *new_fields = object_grow_fields(obj, new_num_fields);
                    if (!new_fields) {
                        VALUE_RELEASE(object);
                        VALUE_RELEASE(index_val);
                        runtime_error(ctx, "Failed to expand object fields");
                        return val_null();
                    }
                    obj->num_fields = new_num_fields;
                    obj->fields[obj->num_fields - 1].name = strdup(key_buf);
                    obj->fields[obj->num_fields - 1].value = value;
                    VALUE_RETAIN(value);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return value;
                }
            }

            // For arrays, strings, and buffers, index must be an integer
            if (!is_integer(index_val)) {
                runtime_error(ctx, "Index must be an integer");
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_ARRAY) {
                // Array assignment - value can be any type
                array_set(object.as.as_array, index, value, ctx);
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return value;
            }

            // For strings and buffers, value must be an integer (byte) or rune
            if (!is_integer(value) && value.type != VAL_RUNE) {
                runtime_error(ctx, "Index value must be an integer (byte) or rune for strings/buffers");
            }

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                if (index < 0 || index >= str->length) {
                    runtime_error(ctx, "String index %d out of bounds (length %d)", index, str->length);
                }

                // Get the rune value (either from rune type or integer)
                uint32_t rune_val;
                if (value.type == VAL_RUNE) {
                    rune_val = value.as.as_rune;
                } else {
                    rune_val = (uint32_t)value_to_int(value);
                }

                // Calculate bytes needed for new rune
                int new_len;
                if (rune_val < 0x80) new_len = 1;
                else if (rune_val < 0x800) new_len = 2;
                else if (rune_val < 0x10000) new_len = 3;
                else new_len = 4;

                // Get byte length of existing character at this position
                int old_len = utf8_char_byte_length((unsigned char)str->data[index]);
                if (index + old_len > str->length) {
                    old_len = str->length - index;
                }

                if (new_len == old_len) {
                    // Same size - just overwrite in place
                    utf8_encode(rune_val, str->data + index);
                } else {
                    // Different size - need to resize string
                    int new_total = str->length - old_len + new_len;
                    char *new_data = malloc(new_total + 1);
                    if (!new_data) {
                        runtime_error(ctx, "Failed to allocate memory for string resize");
                        VALUE_RELEASE(object);
                        VALUE_RELEASE(index_val);
                        VALUE_RELEASE(value);
                        return val_null();
                    }

                    // Copy prefix (before index)
                    memcpy(new_data, str->data, index);

                    // Encode new rune
                    utf8_encode(rune_val, new_data + index);

                    // Copy suffix (after old character)
                    int suffix_start = index + old_len;
                    int suffix_len = str->length - suffix_start;
                    if (suffix_len > 0) {
                        memcpy(new_data + index + new_len, str->data + suffix_start, suffix_len);
                    }

                    new_data[new_total] = '\0';

                    // Replace string data
                    free(str->data);
                    str->data = new_data;
                    str->length = new_total;
                    str->char_length = -1;  // Invalidate cached character count
                }

                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                // Don't release value - it's returned
                return value;
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    runtime_error(ctx, "Buffer index %d out of bounds (length %d)", index, buf->length);
                }

                // Buffers are mutable - set the byte
                ((unsigned char *)buf->data)[index] = (unsigned char)value_to_int(value);
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                // Don't release value - it's returned
                return value;
            } else if (object.type == VAL_PTR) {
                // Raw pointer indexing - no bounds checking (unsafe!)
                void *ptr = object.as.as_ptr;
                if (ptr == NULL) {
                    runtime_error(ctx, "Cannot index into null pointer");
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    VALUE_RELEASE(value);
                    return val_null();
                }
                // Treat as byte array
                ((unsigned char *)ptr)[index] = (unsigned char)value_to_int(value);
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                return value;
            } else {
                runtime_error(ctx, "Only strings, buffers, arrays, pointers, and objects support index assignment");
                VALUE_RELEASE(object);
                VALUE_RELEASE(index_val);
                VALUE_RELEASE(value);
                return val_null();
            }
        }

        case EXPR_FUNCTION: {
            // Create function object and capture current environment.
            // OPTIMIZATION: Borrow param arrays directly from AST instead of
            // deep-copying them.  The AST outlives all closures, so this is safe.
            // This eliminates ~6 malloc + N strdup calls per closure creation.
            // The function pool provides O(1) struct allocation for the common case.
            Function *fn = fn_pool_alloc();
            if (!fn) {
                fn = malloc(sizeof(Function));
                fn->is_pooled = 0;
            }

            fn->is_async = expr->as.function.is_async;
            fn->num_params = expr->as.function.num_params;

            // Borrow parameter arrays directly from AST (no copies)
            fn->param_names = expr->as.function.param_names;
            fn->param_types = expr->as.function.param_types;
            fn->param_defaults = expr->as.function.param_defaults;
            fn->param_is_ref = expr->as.function.param_is_ref;

            // Lazily compute and cache param hashes on the AST node.
            // First closure from this AST node pays the cost; subsequent ones reuse.
            if (expr->as.function.num_params > 0) {
                if (!expr->as.function.param_hashes) {
                    expr->as.function.param_hashes = malloc(sizeof(uint32_t) * expr->as.function.num_params);
                    for (int i = 0; i < expr->as.function.num_params; i++) {
                        expr->as.function.param_hashes[i] = hash_string(expr->as.function.param_names[i]);
                    }
                }
                fn->param_hashes = expr->as.function.param_hashes;
            } else {
                fn->param_hashes = NULL;
            }

            // Borrow rest param and return type from AST (shared, not copied)
            fn->rest_param = expr->as.function.rest_param;
            fn->rest_param_type = expr->as.function.rest_param_type;
            fn->return_type = expr->as.function.return_type;

            // Store body AST (shared, not copied)
            fn->body = expr->as.function.body;

            // CRITICAL: Capture current environment and retain it
            fn->closure_env = env;
            env_retain(env);  // Increment ref count since closure captures env

            // Initialize reference count to 1 (creator owns the first reference)
            // This ensures that when stored in the environment and later retained by tasks,
            // the function isn't prematurely freed when the environment is cleaned up
            fn->ref_count = 1;
            fn->is_bound = 0;
            fn->borrows_ast_params = 1;  // All param arrays point to AST data

            return val_function(fn);
        }

        case EXPR_ARRAY_LITERAL: {
            // Create array and evaluate elements
            Array *arr = array_new();

            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                Value element = eval_expr(expr->as.array_literal.elements[i], env, ctx);
                // Exception safety: if element evaluation threw, release partial array
                if (ctx->exception_state.is_throwing) {
                    VALUE_RELEASE(element);
                    Value arr_val = val_array(arr);
                    VALUE_RELEASE(arr_val);
                    return val_null();
                }
                array_push(arr, element);
                VALUE_RELEASE(element);  // array_push retained it
            }

            return val_array(arr);
        }

        case EXPR_OBJECT_LITERAL: {
            // Create anonymous object
            Object *obj = object_new(NULL, expr->as.object_literal.num_fields);

            // Evaluate and store fields
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (expr->as.object_literal.field_names[i] == NULL) {
                    // Spread operator: ...expr
                    // Evaluate the spread expression and copy all its fields
                    Value spread_val = eval_expr(expr->as.object_literal.field_values[i], env, ctx);
                    // Exception safety: if spread evaluation threw, release partial object
                    if (ctx->exception_state.is_throwing) {
                        VALUE_RELEASE(spread_val);
                        Value obj_val = val_object(obj);
                        VALUE_RELEASE(obj_val);
                        return val_null();
                    }
                    if (spread_val.type != VAL_OBJECT) {
                        VALUE_RELEASE(spread_val);
                        runtime_error(ctx, "Spread operator requires an object");
                        return val_object(obj);
                    }
                    Object *spread_obj = spread_val.as.as_object;

                    // Copy all fields from spread object (using unified storage)
                    for (int j = 0; j < spread_obj->num_fields; j++) {
                        // Check if field already exists (overwrite if so)
                        int existing_idx = -1;
                        for (int k = 0; k < obj->num_fields; k++) {
                            if (strcmp(obj->fields[k].name, spread_obj->fields[j].name) == 0) {
                                existing_idx = k;
                                break;
                            }
                        }

                        if (existing_idx >= 0) {
                            // Overwrite existing field (unified storage)
                            VALUE_RELEASE(obj->fields[existing_idx].value);
                            obj->fields[existing_idx].value = spread_obj->fields[j].value;
                            VALUE_RETAIN(obj->fields[existing_idx].value);
                        } else {
                            // Add new field - grow if needed (single realloc for unified storage)
                            if (obj->num_fields >= obj->capacity) {
                                // SECURITY: Check for integer overflow before doubling capacity
                                if (obj->capacity > INT_MAX / 2) {
                                    VALUE_RELEASE(spread_val);
                                    Value obj_val = val_object(obj);
                                    VALUE_RELEASE(obj_val);
                                    runtime_error(ctx, "Object field capacity overflow");
                                    return val_null();
                                }
                                int new_capacity = obj->capacity * 2;
                                FieldEntry *new_fields = object_grow_fields(obj, new_capacity);
                                if (!new_fields) {
                                    VALUE_RELEASE(spread_val);
                                    Value obj_val = val_object(obj);
                                    VALUE_RELEASE(obj_val);
                                    runtime_error(ctx, "Failed to expand object fields");
                                    return val_null();
                                }
                            }
                            obj->fields[obj->num_fields].name = strdup(spread_obj->fields[j].name);
                            obj->fields[obj->num_fields].value = spread_obj->fields[j].value;
                            VALUE_RETAIN(obj->fields[obj->num_fields].value);
                            obj->num_fields++;
                        }
                    }
                    VALUE_RELEASE(spread_val);
                } else {
                    // Normal field: name: value
                    // Check if field already exists (from previous spread)
                    int existing_idx = -1;
                    for (int k = 0; k < obj->num_fields; k++) {
                        if (strcmp(obj->fields[k].name, expr->as.object_literal.field_names[i]) == 0) {
                            existing_idx = k;
                            break;
                        }
                    }

                    Value field_val = eval_expr(expr->as.object_literal.field_values[i], env, ctx);
                    // Exception safety: if field evaluation threw, release partial object
                    if (ctx->exception_state.is_throwing) {
                        VALUE_RELEASE(field_val);
                        Value obj_val = val_object(obj);
                        VALUE_RELEASE(obj_val);
                        return val_null();
                    }

                    if (existing_idx >= 0) {
                        // Overwrite existing field (from spread) - unified storage
                        VALUE_RELEASE(obj->fields[existing_idx].value);
                        obj->fields[existing_idx].value = field_val;
                    } else {
                        // Add new field - grow if needed (single realloc for unified storage)
                        if (obj->num_fields >= obj->capacity) {
                            // SECURITY: Check for integer overflow before doubling capacity
                            if (obj->capacity > INT_MAX / 2) {
                                VALUE_RELEASE(field_val);
                                Value obj_val = val_object(obj);
                                VALUE_RELEASE(obj_val);
                                runtime_error(ctx, "Object field capacity overflow");
                                return val_null();
                            }
                            int new_capacity = (obj->capacity == 0) ? 4 : obj->capacity * 2;
                            FieldEntry *new_fields = object_grow_fields(obj, new_capacity);
                            if (!new_fields) {
                                VALUE_RELEASE(field_val);
                                Value obj_val = val_object(obj);
                                VALUE_RELEASE(obj_val);
                                runtime_error(ctx, "Failed to expand object fields");
                                return val_null();
                            }
                        }
                        obj->fields[obj->num_fields].name = strdup(expr->as.object_literal.field_names[i]);
                        obj->fields[obj->num_fields].value = field_val;
                        obj->num_fields++;
                    }
                }
            }

            return val_object(obj);
        }

        case EXPR_SET_PROPERTY: {
            Value object = eval_expr(expr->as.set_property.object, env, ctx);
            const char *property = expr->as.set_property.property;
            Value value = eval_expr(expr->as.set_property.value, env, ctx);

            if (object.type != VAL_OBJECT) {
                VALUE_RELEASE(object);
                VALUE_RELEASE(value);
                runtime_error(ctx, "Only objects can have properties set");
                return val_null();  // Return after error
            }

            Object *obj = object.as.as_object;
            int idx;

            // Same shared-AST-node inline-cache race as the property READ
            // path above: expr->as.set_property.ic is shared across all
            // spawned task threads and mutated unsynchronized. Once any
            // task has been spawned, bypass the cache and do the lock-free
            // read-only field lookup (the actual field store below is the
            // caller's responsibility to serialize, exactly as before —
            // this only removes the torn-IC misresolution). Single-threaded
            // keeps the fast path unchanged.
            if (__atomic_load_n(&g_interp_has_spawned, __ATOMIC_SEQ_CST)) {
                idx = object_lookup_field_with_hash(obj, property, hash_string(property));
            } else {
                PropertyIC *ic = &expr->as.set_property.ic;
                idx = -1;

                // INLINE CACHE FAST PATH:
                // If we're accessing the same object and the cache is valid, use cached index
                if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                    ic->cached_object == (void*)obj &&
                    ic->cached_field_index >= 0) {
                    // Validate the cached index still points to the correct field
                    if (object_validate_ic(obj, ic->cached_field_index, property)) {
                        idx = ic->cached_field_index;  // Cache hit!
                    } else {
                        // Cache is stale (object was modified), invalidate
                        ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                        ic->cached_object = NULL;
                        ic->cached_field_index = -1;
                    }
                }

                // CACHE MISS: Do full lookup and update cache
                if (idx < 0) {
                    // Compute hash if not cached
                    if (ic->cached_hash == 0) {
                        ic->cached_hash = hash_string(property);
                    }
                    idx = object_lookup_field_with_hash(obj, property, ic->cached_hash);

                    // Update inline cache if field exists and not megamorphic
                    if (idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                        if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                            // First access - initialize cache
                            ic->cached_object = (void*)obj;
                            ic->cached_field_index = idx;
                            ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                            ic->miss_count = 0;
                        } else if (ic->cached_object != (void*)obj) {
                            // Different object - this is polymorphic
                            ic->miss_count++;
                            if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                // Too many different objects, go megamorphic
                                ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                            } else {
                                // Update cache to new object
                                ic->cached_object = (void*)obj;
                                ic->cached_field_index = idx;
                            }
                        }
                    }
                }
            }

            if (idx >= 0) {
                // Release old value, store new value (unified storage)
                VALUE_RELEASE(obj->fields[idx].value);
                obj->fields[idx].value = value;
                // eval_expr gave us ownership, object now owns the value
                // Return the value (retained for caller)
                VALUE_RETAIN(value);
                VALUE_RELEASE(object);
                return value;
            }

            // Field doesn't exist - add it dynamically!
            if (obj->num_fields >= obj->capacity) {
                // SECURITY: Check for integer overflow before doubling capacity
                if (obj->capacity > INT_MAX / 2) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(value);
                    runtime_error(ctx, "Object field capacity overflow");
                    return val_null();
                }
                // Grow field array (handles pooled objects safely)
                int new_capacity = (obj->capacity == 0) ? 4 : obj->capacity * 2;
                FieldEntry *new_fields = object_grow_fields(obj, new_capacity);
                if (!new_fields) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(value);
                    runtime_error(ctx, "Failed to grow object capacity");
                    return val_null();
                }
            }

            int new_field_index = obj->num_fields;
            obj->fields[new_field_index].name = strdup(property);
            // Store value (object now owns it)
            obj->fields[new_field_index].value = value;
            obj->num_fields++;

            // Insert new field into hash table (grows table if needed, avoids full rebuild)
            object_hash_insert(obj, property, new_field_index);

            // Update inline cache to point to the new field (single-threaded
            // only — see the gate above; the shared IC must not be written
            // once any task has been spawned).
            if (!__atomic_load_n(&g_interp_has_spawned, __ATOMIC_SEQ_CST)) {
                PropertyIC *ic = &expr->as.set_property.ic;
                ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                ic->cached_object = (void*)obj;
                ic->cached_field_index = new_field_index;
                ic->miss_count = 0;
            }

            // Return the value (retained for caller)
            VALUE_RETAIN(value);
            VALUE_RELEASE(object);
            return value;
        }

        case EXPR_PREFIX_INC: {
            // ++x: increment then return new value
            Expr *operand = expr->as.prefix_inc.operand;

            if (operand->type == EXPR_IDENT) {
                // Simple variable: ++x
                Value old_val = env_get(env, operand->as.ident.name, ctx);  // Retains old value
                Value new_val = value_add_one(old_val, ctx);
                VALUE_RELEASE(old_val);  // Release old value after incrementing
                env_set(env, operand->as.ident.name, new_val, ctx);
                return new_val;
            } else if (operand->type == EXPR_INDEX) {
                // Array/buffer/string index: ++arr[i]
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                    return val_null();
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index, ctx);
                    Value new_val = value_add_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val, ctx);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return new_val;
                } else {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Can only use ++ on array elements");
                    return val_null();
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                // Object property: ++obj.field (use inline cache from child expression)
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    VALUE_RELEASE(object);
                    runtime_error(ctx, "Can only increment object properties");
                    return val_null();
                }
                Object *obj = object.as.as_object;
                PropertyIC *ic = &operand->as.get_property.ic;
                int idx = -1;

                // INLINE CACHE FAST PATH
                if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                    ic->cached_object == (void*)obj &&
                    ic->cached_field_index >= 0) {
                    if (object_validate_ic(obj, ic->cached_field_index, property)) {
                        idx = ic->cached_field_index;
                    } else {
                        ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                        ic->cached_object = NULL;
                        ic->cached_field_index = -1;
                    }
                }

                // CACHE MISS: Do full lookup
                if (idx < 0) {
                    if (ic->cached_hash == 0) {
                        ic->cached_hash = hash_string(property);
                    }
                    idx = object_lookup_field_with_hash(obj, property, ic->cached_hash);
                    if (idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                        if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                            ic->cached_object = (void*)obj;
                            ic->cached_field_index = idx;
                            ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                            ic->miss_count = 0;
                        } else if (ic->cached_object != (void*)obj) {
                            ic->miss_count++;
                            if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                            } else {
                                ic->cached_object = (void*)obj;
                                ic->cached_field_index = idx;
                            }
                        }
                    }
                }

                if (idx >= 0) {
                    Value old_val = obj->fields[idx].value;
                    Value new_val = value_add_one(old_val, ctx);
                    obj->fields[idx].value = new_val;
                    VALUE_RELEASE(object);
                    return new_val;
                }
                VALUE_RELEASE(object);
                runtime_error(ctx, "Property '%s' not found", property);
            } else {
                runtime_error(ctx, "Invalid operand for ++");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_PREFIX_DEC: {
            // --x: decrement then return new value
            Expr *operand = expr->as.prefix_dec.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident.name, ctx);  // Retains old value
                Value new_val = value_sub_one(old_val, ctx);
                VALUE_RELEASE(old_val);  // Release old value after decrementing
                env_set(env, operand->as.ident.name, new_val, ctx);
                return new_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                    return val_null();
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index, ctx);
                    Value new_val = value_sub_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val, ctx);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return new_val;
                } else {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Can only use -- on array elements");
                    return val_null();
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                // Object property: --obj.field (use inline cache from child expression)
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    VALUE_RELEASE(object);
                    runtime_error(ctx, "Can only decrement object properties");
                    return val_null();
                }
                Object *obj = object.as.as_object;
                PropertyIC *ic = &operand->as.get_property.ic;
                int idx = -1;

                // INLINE CACHE FAST PATH
                if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                    ic->cached_object == (void*)obj &&
                    ic->cached_field_index >= 0) {
                    if (object_validate_ic(obj, ic->cached_field_index, property)) {
                        idx = ic->cached_field_index;
                    } else {
                        ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                        ic->cached_object = NULL;
                        ic->cached_field_index = -1;
                    }
                }

                // CACHE MISS: Do full lookup
                if (idx < 0) {
                    if (ic->cached_hash == 0) {
                        ic->cached_hash = hash_string(property);
                    }
                    idx = object_lookup_field_with_hash(obj, property, ic->cached_hash);
                    if (idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                        if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                            ic->cached_object = (void*)obj;
                            ic->cached_field_index = idx;
                            ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                            ic->miss_count = 0;
                        } else if (ic->cached_object != (void*)obj) {
                            ic->miss_count++;
                            if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                            } else {
                                ic->cached_object = (void*)obj;
                                ic->cached_field_index = idx;
                            }
                        }
                    }
                }

                if (idx >= 0) {
                    Value old_val = obj->fields[idx].value;
                    Value new_val = value_sub_one(old_val, ctx);
                    obj->fields[idx].value = new_val;
                    VALUE_RELEASE(object);
                    return new_val;
                }
                VALUE_RELEASE(object);
                runtime_error(ctx, "Property '%s' not found", property);
                return val_null();
            } else {
                runtime_error(ctx, "Invalid operand for --");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_POSTFIX_INC: {
            // x++: return old value then increment
            Expr *operand = expr->as.postfix_inc.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident.name, ctx);  // Retains old value
                Value new_val = value_add_one(old_val, ctx);
                env_set(env, operand->as.ident.name, new_val, ctx);
                // Return old value (still retained from env_get, caller now owns it)
                return old_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                    return val_null();
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index, ctx);
                    Value new_val = value_add_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val, ctx);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return old_val;
                } else {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Can only use ++ on array elements");
                    return val_null();
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                // Object property: obj.field++ (use inline cache from child expression)
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    VALUE_RELEASE(object);
                    runtime_error(ctx, "Can only increment object properties");
                    return val_null();
                }
                Object *obj = object.as.as_object;
                PropertyIC *ic = &operand->as.get_property.ic;
                int idx = -1;

                // INLINE CACHE FAST PATH
                if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                    ic->cached_object == (void*)obj &&
                    ic->cached_field_index >= 0) {
                    if (object_validate_ic(obj, ic->cached_field_index, property)) {
                        idx = ic->cached_field_index;
                    } else {
                        ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                        ic->cached_object = NULL;
                        ic->cached_field_index = -1;
                    }
                }

                // CACHE MISS: Do full lookup
                if (idx < 0) {
                    if (ic->cached_hash == 0) {
                        ic->cached_hash = hash_string(property);
                    }
                    idx = object_lookup_field_with_hash(obj, property, ic->cached_hash);
                    if (idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                        if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                            ic->cached_object = (void*)obj;
                            ic->cached_field_index = idx;
                            ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                            ic->miss_count = 0;
                        } else if (ic->cached_object != (void*)obj) {
                            ic->miss_count++;
                            if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                            } else {
                                ic->cached_object = (void*)obj;
                                ic->cached_field_index = idx;
                            }
                        }
                    }
                }

                if (idx >= 0) {
                    Value old_val = obj->fields[idx].value;
                    Value new_val = value_add_one(old_val, ctx);
                    obj->fields[idx].value = new_val;
                    VALUE_RETAIN(old_val);  // Retain for caller
                    VALUE_RELEASE(object);
                    return old_val;
                }
                VALUE_RELEASE(object);
                runtime_error(ctx, "Property '%s' not found", property);
                return val_null();
            } else {
                runtime_error(ctx, "Invalid operand for ++");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_POSTFIX_DEC: {
            // x--: return old value then decrement
            Expr *operand = expr->as.postfix_dec.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident.name, ctx);  // Retains old value
                Value new_val = value_sub_one(old_val, ctx);
                env_set(env, operand->as.ident.name, new_val, ctx);
                // Return old value (still retained from env_get, caller now owns it)
                return old_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                    return val_null();
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index, ctx);
                    Value new_val = value_sub_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val, ctx);
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    return old_val;
                } else {
                    VALUE_RELEASE(object);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Can only use -- on array elements");
                    return val_null();
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                // Object property: obj.field-- (use inline cache from child expression)
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    VALUE_RELEASE(object);
                    runtime_error(ctx, "Can only decrement object properties");
                    return val_null();
                }
                Object *obj = object.as.as_object;
                PropertyIC *ic = &operand->as.get_property.ic;
                int idx = -1;

                // INLINE CACHE FAST PATH
                if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                    ic->cached_object == (void*)obj &&
                    ic->cached_field_index >= 0) {
                    if (object_validate_ic(obj, ic->cached_field_index, property)) {
                        idx = ic->cached_field_index;
                    } else {
                        ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                        ic->cached_object = NULL;
                        ic->cached_field_index = -1;
                    }
                }

                // CACHE MISS: Do full lookup
                if (idx < 0) {
                    if (ic->cached_hash == 0) {
                        ic->cached_hash = hash_string(property);
                    }
                    idx = object_lookup_field_with_hash(obj, property, ic->cached_hash);
                    if (idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                        if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                            ic->cached_object = (void*)obj;
                            ic->cached_field_index = idx;
                            ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                            ic->miss_count = 0;
                        } else if (ic->cached_object != (void*)obj) {
                            ic->miss_count++;
                            if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                            } else {
                                ic->cached_object = (void*)obj;
                                ic->cached_field_index = idx;
                            }
                        }
                    }
                }

                if (idx >= 0) {
                    Value old_val = obj->fields[idx].value;
                    Value new_val = value_sub_one(old_val, ctx);
                    obj->fields[idx].value = new_val;
                    VALUE_RETAIN(old_val);  // Retain for caller
                    VALUE_RELEASE(object);
                    return old_val;
                }
                VALUE_RELEASE(object);
                runtime_error(ctx, "Property '%s' not found", property);
                return val_null();
            } else {
                runtime_error(ctx, "Invalid operand for --");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_STRING_INTERPOLATION: {
            // Evaluate string interpolation: "prefix ${expr1} middle ${expr2} suffix"
            // Build the final string by concatenating string parts and evaluated expressions

            int num_parts = expr->as.string_interpolation.num_parts;
            char **string_parts = expr->as.string_interpolation.string_parts;
            Expr **expr_parts = expr->as.string_interpolation.expr_parts;

            if (num_parts < 0) {
                runtime_error(ctx, "Invalid string interpolation");
                return val_null();
            }

            // Fast path: no expression parts — just return the static string segment
            if (num_parts == 0) {
                return val_string(string_parts[0]);
            }

            // Calculate total length needed
            int total_len = 0;
            for (int i = 0; i <= num_parts; i++) {
                total_len += strlen(string_parts[i]);
            }

            // Evaluate expression parts and convert to strings
            // Cast to size_t after the num_parts > 0 guard above to avoid
            // -Walloc-size-larger-than triggering on the signed->size_t multiply.
            char **expr_strings = malloc(sizeof(char*) * (size_t)num_parts);
            if (!expr_strings) {
                runtime_error(ctx, "Out of memory in string interpolation");
                return val_null();
            }
            for (int i = 0; i < num_parts; i++) {
                Value expr_val = eval_expr(expr_parts[i], env, ctx);
                // Check for exception after evaluating expression
                if (ctx->exception_state.is_throwing) {
                    VALUE_RELEASE(expr_val);
                    // Free already allocated strings
                    for (int j = 0; j < i; j++) {
                        free(expr_strings[j]);
                    }
                    free(expr_strings);
                    return val_null();
                }
                expr_strings[i] = value_to_string(expr_val);
                VALUE_RELEASE(expr_val);  // Release after converting to string
                total_len += strlen(expr_strings[i]);
            }

            // Build final string using memcpy with offset tracking (O(n) instead of O(n²))
            char *result = malloc(total_len + 1);
            if (!result) {
                for (int i = 0; i < num_parts; i++) {
                    free(expr_strings[i]);
                }
                free(expr_strings);
                runtime_error(ctx, "Out of memory in string interpolation");
                return val_null();
            }
            size_t offset = 0;

            for (int i = 0; i < num_parts; i++) {
                size_t part_len = strlen(string_parts[i]);
                memcpy(result + offset, string_parts[i], part_len);
                offset += part_len;

                size_t expr_len = strlen(expr_strings[i]);
                memcpy(result + offset, expr_strings[i], expr_len);
                offset += expr_len;
                free(expr_strings[i]);
            }
            // Final string part
            size_t final_len = strlen(string_parts[num_parts]);
            memcpy(result + offset, string_parts[num_parts], final_len);
            offset += final_len;
            result[offset] = '\0';

            free(expr_strings);

            Value val = val_string(result);
            free(result);
            return val;
        }

        case EXPR_AWAIT: {
            // Evaluate the expression
            Value awaited = eval_expr(expr->as.await_expr.awaited_expr, env, ctx);

            // If it's a task handle, automatically join it
            if (awaited.type == VAL_TASK) {
                Value args[1] = { awaited };
                Value result = builtin_join(args, 1, ctx);
                VALUE_RELEASE(awaited);  // Release task handle after joining
                return result;
            }

            // For other values (including direct async function calls),
            // just return the value as-is (already evaluated)
            return awaited;
        }

        case EXPR_OPTIONAL_CHAIN: {
            // Evaluate the object expression
            Value object_val = eval_expr(expr->as.optional_chain.object, env, ctx);

            // If object is null, short-circuit and return null
            if (object_val.type == VAL_NULL) {
                return val_null();
            }

            // Otherwise, perform the operation based on the type
            if (expr->as.optional_chain.is_property) {
                // Optional property access: obj?.property
                const char *property = expr->as.optional_chain.property;
                Value result;

                // Handle property access for different types (similar to EXPR_GET_PROPERTY)
                if (object_val.type == VAL_STRING) {
                    String *str = object_val.as.as_string;

                    if (strcmp(property, "length") == 0) {
                        if (str->char_length < 0) {
                            str->char_length = utf8_count_codepoints(str->data, str->length);
                        }
                        result = val_i32(str->char_length);
                    } else if (strcmp(property, "byte_length") == 0) {
                        result = val_i32(str->length);
                    } else {
                        VALUE_RELEASE(object_val);
                        runtime_error(ctx, "Unknown property '%s' for string", property);
                        return val_null();
                    }
                } else if (object_val.type == VAL_ARRAY) {
                    if (strcmp(property, "length") == 0) {
                        result = val_i32(object_val.as.as_array->length);
                    } else {
                        VALUE_RELEASE(object_val);
                        runtime_error(ctx, "Unknown property '%s' for array", property);
                        return val_null();
                    }
                } else if (object_val.type == VAL_BUFFER) {
                    if (strcmp(property, "length") == 0) {
                        result = val_i32(object_val.as.as_buffer->length);
                    } else if (strcmp(property, "capacity") == 0) {
                        result = val_i32(object_val.as.as_buffer->capacity);
                    } else {
                        VALUE_RELEASE(object_val);
                        runtime_error(ctx, "Unknown property '%s' for buffer", property);
                        return val_null();
                    }
                } else if (object_val.type == VAL_FILE) {
                    FileHandle *f = object_val.as.as_file;
                    if (strcmp(property, "path") == 0) {
                        result = val_string(f->path);
                    } else if (strcmp(property, "mode") == 0) {
                        result = val_string(f->mode);
                    } else if (strcmp(property, "closed") == 0) {
                        result = val_bool(f->closed);
                    } else {
                        VALUE_RELEASE(object_val);
                        runtime_error(ctx, "Unknown property '%s' for file", property);
                        return val_null();
                    }
                } else if (object_val.type == VAL_OBJECT) {
                    // Use inline cache for O(1) property access (same as EXPR_GET_PROPERTY)
                    Object *obj = object_val.as.as_object;
                    PropertyIC *ic = &expr->as.optional_chain.ic;
                    int idx = -1;

                    // INLINE CACHE FAST PATH
                    if (ic->ic_state == HML_IC_STATE_MONOMORPHIC &&
                        ic->cached_object == (void*)obj &&
                        ic->cached_field_index >= 0) {
                        if (object_validate_ic(obj, ic->cached_field_index, property)) {
                            idx = ic->cached_field_index;  // Cache hit!
                        } else {
                            // Cache is stale, invalidate
                            ic->ic_state = HML_IC_STATE_UNINITIALIZED;
                            ic->cached_object = NULL;
                            ic->cached_field_index = -1;
                        }
                    }

                    // CACHE MISS: Do full lookup with hash table
                    if (idx < 0) {
                        if (ic->cached_hash == 0) {
                            ic->cached_hash = hash_string(property);
                        }
                        idx = object_lookup_field_with_hash(obj, property, ic->cached_hash);

                        // Update inline cache if not megamorphic
                        if (idx >= 0 && ic->ic_state != HML_IC_STATE_MEGAMORPHIC) {
                            if (ic->ic_state == HML_IC_STATE_UNINITIALIZED) {
                                ic->cached_object = (void*)obj;
                                ic->cached_field_index = idx;
                                ic->ic_state = HML_IC_STATE_MONOMORPHIC;
                                ic->miss_count = 0;
                            } else if (ic->cached_object != (void*)obj) {
                                ic->miss_count++;
                                if (ic->miss_count >= HML_IC_MAX_MISSES) {
                                    ic->ic_state = HML_IC_STATE_MEGAMORPHIC;
                                } else {
                                    ic->cached_object = (void*)obj;
                                    ic->cached_field_index = idx;
                                }
                            }
                        }
                    }

                    if (idx >= 0) {
                        result = obj->fields[idx].value;
                        VALUE_RETAIN(result);
                        VALUE_RELEASE(object_val);
                        return result;
                    }
                    // For optional chaining, return null for missing properties
                    VALUE_RELEASE(object_val);
                    return val_null();
                } else {
                    VALUE_RELEASE(object_val);
                    runtime_error(ctx, "Cannot access property on non-object value");
                    return val_null();
                }

                VALUE_RELEASE(object_val);
                return result;
            } else if (expr->as.optional_chain.is_call) {
                // obj?.(args) - call obj directly if not null
                int num_args = expr->as.optional_chain.num_args;
                Value *args = NULL;
                if (num_args > 0) {
                    args = malloc(sizeof(Value) * num_args);
                    for (int i = 0; i < num_args; i++) {
                        args[i] = eval_expr(expr->as.optional_chain.args[i], env, ctx);
                    }
                }

                Value result;
                if (object_val.type == VAL_FUNCTION) {
                    Function *fn = object_val.as.as_function;

                    // Create call environment with closure_env as parent
                    Environment *call_env = env_new(fn->closure_env);

                    // Bind parameters
                    for (int i = 0; i < fn->num_params; i++) {
                        Value arg_value = (i < num_args) ? args[i] : val_null();

                        // Type check if parameter has type annotation
                        if (fn->param_types[i]) {
                            arg_value = convert_to_type(arg_value, fn->param_types[i], call_env, ctx);
                        }

                        env_set(call_env, fn->param_names[i], arg_value, ctx);
                    }

                    // Execute body
                    ctx->return_state.is_returning = 0;
                    eval_stmt(fn->body, call_env, ctx);

                    // Get return value
                    result = ctx->return_state.is_returning ? ctx->return_state.return_value : val_null();
                    ctx->return_state.is_returning = 0;

                    // Clean up
                    env_release(call_env);
                } else if (object_val.type == VAL_BUILTIN_FN) {
                    BuiltinFn fn = object_val.as.as_builtin_fn;
                    // Set source location for profiler allocation tracking
                    ctx->current_source_file = get_current_source_file();
                    ctx->current_line = expr->line;
                    result = fn(args, num_args, ctx);
                } else {
                    runtime_error(ctx, "Cannot call non-function value");
                    result = val_null();
                }

                // Cleanup args
                if (args) {
                    for (int i = 0; i < num_args; i++) {
                        VALUE_RELEASE(args[i]);
                    }
                    free(args);
                }
                VALUE_RELEASE(object_val);
                return result;
            } else {
                // Optional indexing: obj?.[index] or obj?[index]
                Value index_val = eval_expr(expr->as.optional_chain.index, env, ctx);
                Value result;

                // Object: string or coerced string-key lookup, returns null on miss
                if (object_val.type == VAL_OBJECT) {
                    Object *obj = object_val.as.as_object;
                    int idx = -1;
                    if (index_val.type == VAL_STRING) {
                        idx = object_lookup_field(obj, index_val.as.as_string->data);
                    } else {
                        char key_buf[64];
                        if (value_coerce_to_key(index_val, key_buf, sizeof(key_buf))) {
                            idx = object_lookup_field(obj, key_buf);
                        }
                    }
                    if (idx >= 0) {
                        result = obj->fields[idx].value;
                        VALUE_RETAIN(result);
                    } else {
                        result = val_null();
                    }
                    VALUE_RELEASE(object_val);
                    VALUE_RELEASE(index_val);
                    return result;
                }

                if (!is_integer(index_val)) {
                    VALUE_RELEASE(object_val);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Index must be an integer");
                    return val_null();
                }

                int32_t index = value_to_int(index_val);

                if (object_val.type == VAL_ARRAY) {
                    result = array_get(object_val.as.as_array, index, ctx);
                    VALUE_RETAIN(result);
                } else if (object_val.type == VAL_STRING) {
                    String *str = object_val.as.as_string;

                    // Compute character length if not cached
                    if (str->char_length < 0) {
                        str->char_length = utf8_count_codepoints(str->data, str->length);
                    }

                    // Check bounds using character count (not byte count)
                    if (index < 0 || index >= str->char_length) {
                        VALUE_RELEASE(object_val);
                        VALUE_RELEASE(index_val);
                        runtime_error(ctx, "String index out of bounds");
                        return val_null();
                    }

                    // Find byte offset of the i-th codepoint
                    int byte_pos = utf8_byte_offset(str->data, str->length, index);

                    // Decode the codepoint at that position
                    uint32_t codepoint = utf8_decode_at(str->data, byte_pos);

                    result = val_rune(codepoint);
                } else if (object_val.type == VAL_BUFFER) {
                    Buffer *buf = object_val.as.as_buffer;

                    if (index < 0 || index >= buf->length) {
                        VALUE_RELEASE(object_val);
                        VALUE_RELEASE(index_val);
                        runtime_error(ctx, "Buffer index out of bounds");
                        return val_null();
                    }

                    result = val_u8(((unsigned char *)buf->data)[index]);
                } else {
                    VALUE_RELEASE(object_val);
                    VALUE_RELEASE(index_val);
                    runtime_error(ctx, "Cannot index non-array/non-string/non-buffer value");
                    return val_null();
                }

                VALUE_RELEASE(object_val);
                VALUE_RELEASE(index_val);
                return result;
            }
            break;  // Prevent fall-through
        }

        case EXPR_NULL_COALESCE: {
            // Evaluate the left operand
            Value left_val = eval_expr(expr->as.null_coalesce.left, env, ctx);

            // If left is not null, return it
            if (left_val.type != VAL_NULL) {
                return left_val;
            }

            // Left is null - release it (no-op for null, but consistent)
            VALUE_RELEASE(left_val);
            // Evaluate and return the right operand
            return eval_expr(expr->as.null_coalesce.right, env, ctx);
        }

        case EXPR_MATCH:
            return eval_match_expr(expr, env, ctx);
    }

    return val_null();
}

