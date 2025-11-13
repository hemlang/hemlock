#include "internal.h"


// ========== HELPER FUNCTIONS ==========

// Helper to add two values (for increment operations)
static Value value_add_one(Value val, ExecutionContext *ctx) {
    if (is_float(val)) {
        double v = value_to_float(val);
        return (val.type == VAL_F32) ? val_f32((float)(v + 1.0)) : val_f64(v + 1.0);
    } else if (is_integer(val)) {
        int64_t v = value_to_int(val);
        ValueType result_type = val.type;
        return promote_value(val_i32((int32_t)(v + 1)), result_type);
    } else {
        runtime_error(ctx, "Can only increment numeric values");
        return val_null();  // Unreachable
    }
}

// Helper to subtract one from a value (for decrement operations)
static Value value_sub_one(Value val, ExecutionContext *ctx) {
    if (is_float(val)) {
        double v = value_to_float(val);
        return (val.type == VAL_F32) ? val_f32((float)(v - 1.0)) : val_f64(v - 1.0);
    } else if (is_integer(val)) {
        int64_t v = value_to_int(val);
        ValueType result_type = val.type;
        return promote_value(val_i32((int32_t)(v - 1)), result_type);
    } else {
        runtime_error(ctx, "Can only decrement numeric values");
        return val_null();  // Unreachable
    }
}

// ========== EXPRESSION EVALUATION ==========

Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx) {
    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return val_float(expr->as.number.float_value);
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

            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    return val_bool(!value_is_truthy(operand));

                case UNARY_NEGATE:
                    if (is_float(operand)) {
                        return val_f64(-value_to_float(operand));
                    } else if (is_integer(operand)) {
                        // Preserve the original type when negating
                        switch (operand.type) {
                            case VAL_I8: return val_i8(-operand.as.as_i8);
                            case VAL_I16: return val_i16(-operand.as.as_i16);
                            case VAL_I32: return val_i32(-operand.as.as_i32);
                            case VAL_I64: return val_i64(-operand.as.as_i64);
                            case VAL_U8: return val_i16(-(int16_t)operand.as.as_u8);  // promote to i16
                            case VAL_U16: return val_i32(-(int32_t)operand.as.as_u16); // promote to i32
                            case VAL_U32: return val_i64(-(int64_t)operand.as.as_u32); // promote to i64
                            case VAL_U64: {
                                // Special case: u64 negation - check if value fits in i64
                                if (operand.as.as_u64 <= INT64_MAX) {
                                    return val_i64(-(int64_t)operand.as.as_u64);
                                } else {
                                    runtime_error(ctx, "Cannot negate u64 value larger than INT64_MAX");
                                }
                            }
                            default:
                                runtime_error(ctx, "Cannot negate non-integer value");
                        }
                    }
                    runtime_error(ctx, "Cannot negate non-numeric value");

                case UNARY_BIT_NOT:
                    if (is_integer(operand)) {
                        // Bitwise NOT - preserve the original type
                        switch (operand.type) {
                            case VAL_I8: return val_i8(~operand.as.as_i8);
                            case VAL_I16: return val_i16(~operand.as.as_i16);
                            case VAL_I32: return val_i32(~operand.as.as_i32);
                            case VAL_I64: return val_i64(~operand.as.as_i64);
                            case VAL_U8: return val_u8(~operand.as.as_u8);
                            case VAL_U16: return val_u16(~operand.as.as_u16);
                            case VAL_U32: return val_u32(~operand.as.as_u32);
                            case VAL_U64: return val_u64(~operand.as.as_u64);
                            default:
                                runtime_error(ctx, "Cannot apply bitwise NOT to non-integer value");
                        }
                    }
                    runtime_error(ctx, "Cannot apply bitwise NOT to non-integer value");
            }
            break;
        }

        case EXPR_TERNARY: {
            Value condition = eval_expr(expr->as.ternary.condition, env, ctx);
            if (value_is_truthy(condition)) {
                return eval_expr(expr->as.ternary.true_expr, env, ctx);
            } else {
                return eval_expr(expr->as.ternary.false_expr, env, ctx);
            }
        }

        case EXPR_IDENT:
            return env_get(env, expr->as.ident, ctx);

        case EXPR_ASSIGN: {
            Value value = eval_expr(expr->as.assign.value, env, ctx);
            env_set(env, expr->as.assign.name, value, ctx);
            return value;
        }

        case EXPR_BINARY: {
            // Handle && and || with short-circuit evaluation
            if (expr->as.binary.op == OP_AND) {
                Value left = eval_expr(expr->as.binary.left, env, ctx);
                if (!value_is_truthy(left)) return val_bool(0);

                Value right = eval_expr(expr->as.binary.right, env, ctx);
                return val_bool(value_is_truthy(right));
            }

            if (expr->as.binary.op == OP_OR) {
                Value left = eval_expr(expr->as.binary.left, env, ctx);
                if (value_is_truthy(left)) return val_bool(1);

                Value right = eval_expr(expr->as.binary.right, env, ctx);
                return val_bool(value_is_truthy(right));
            }

            // Evaluate both operands
            Value left = eval_expr(expr->as.binary.left, env, ctx);
            Value right = eval_expr(expr->as.binary.right, env, ctx);

            // String concatenation
            if (expr->as.binary.op == OP_ADD && left.type == VAL_STRING && right.type == VAL_STRING) {
                String *result = string_concat(left.as.as_string, right.as.as_string);
                return (Value){ .type = VAL_STRING, .as.as_string = result };
            }

            // String + rune concatenation
            if (expr->as.binary.op == OP_ADD && left.type == VAL_STRING && right.type == VAL_RUNE) {
                // Encode rune to UTF-8
                char rune_bytes[5];  // Max 4 bytes + null terminator
                int rune_len = utf8_encode(right.as.as_rune, rune_bytes);
                rune_bytes[rune_len] = '\0';

                // Create temporary string from rune
                String *rune_str = string_new(rune_bytes);
                String *result = string_concat(left.as.as_string, rune_str);
                free(rune_str);  // Free temporary string
                return (Value){ .type = VAL_STRING, .as.as_string = result };
            }

            // Rune + string concatenation
            if (expr->as.binary.op == OP_ADD && left.type == VAL_RUNE && right.type == VAL_STRING) {
                // Encode rune to UTF-8
                char rune_bytes[5];
                int rune_len = utf8_encode(left.as.as_rune, rune_bytes);
                rune_bytes[rune_len] = '\0';

                // Create temporary string from rune
                String *rune_str = string_new(rune_bytes);
                String *result = string_concat(rune_str, right.as.as_string);
                free(rune_str);  // Free temporary string
                return (Value){ .type = VAL_STRING, .as.as_string = result };
            }

            // Pointer arithmetic
            if (left.type == VAL_PTR && is_integer(right)) {
                if (expr->as.binary.op == OP_ADD) {
                    void *ptr = left.as.as_ptr;
                    int32_t offset = value_to_int(right);
                    return val_ptr((char *)ptr + offset);
                } else if (expr->as.binary.op == OP_SUB) {
                    void *ptr = left.as.as_ptr;
                    int32_t offset = value_to_int(right);
                    return val_ptr((char *)ptr - offset);
                }
            }

            if (is_integer(left) && right.type == VAL_PTR) {
                if (expr->as.binary.op == OP_ADD) {
                    int32_t offset = value_to_int(left);
                    void *ptr = right.as.as_ptr;
                    return val_ptr((char *)ptr + offset);
                }
            }

            // Boolean comparisons
            if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
                if (expr->as.binary.op == OP_EQUAL) {
                    return val_bool(left.as.as_bool == right.as.as_bool);
                } else if (expr->as.binary.op == OP_NOT_EQUAL) {
                    return val_bool(left.as.as_bool != right.as.as_bool);
                }
            }

            // String comparisons
            if (left.type == VAL_STRING && right.type == VAL_STRING) {
                if (expr->as.binary.op == OP_EQUAL) {
                    String *left_str = left.as.as_string;
                    String *right_str = right.as.as_string;
                    if (left_str->length != right_str->length) {
                        return val_bool(0);
                    }
                    int cmp = memcmp(left_str->data, right_str->data, left_str->length);
                    return val_bool(cmp == 0);
                } else if (expr->as.binary.op == OP_NOT_EQUAL) {
                    String *left_str = left.as.as_string;
                    String *right_str = right.as.as_string;
                    if (left_str->length != right_str->length) {
                        return val_bool(1);
                    }
                    int cmp = memcmp(left_str->data, right_str->data, left_str->length);
                    return val_bool(cmp != 0);
                }
            }

            // Null comparisons
            if (left.type == VAL_NULL || right.type == VAL_NULL) {
                if (expr->as.binary.op == OP_EQUAL) {
                    // Both null -> true, one null -> false
                    return val_bool(left.type == VAL_NULL && right.type == VAL_NULL);
                } else if (expr->as.binary.op == OP_NOT_EQUAL) {
                    // Both null -> false, one null -> true
                    return val_bool(!(left.type == VAL_NULL && right.type == VAL_NULL));
                }
            }

            // Object comparisons (reference equality)
            if (left.type == VAL_OBJECT && right.type == VAL_OBJECT) {
                if (expr->as.binary.op == OP_EQUAL) {
                    return val_bool(left.as.as_object == right.as.as_object);
                } else if (expr->as.binary.op == OP_NOT_EQUAL) {
                    return val_bool(left.as.as_object != right.as.as_object);
                }
            }

            // Cross-type equality comparisons (before numeric type check)
            // If types are different and not both numeric, == returns false, != returns true
            if (expr->as.binary.op == OP_EQUAL || expr->as.binary.op == OP_NOT_EQUAL) {
                int left_is_numeric = is_numeric(left);
                int right_is_numeric = is_numeric(right);

                // If one is numeric and the other is not, types don't match
                if (left_is_numeric != right_is_numeric) {
                    return val_bool(expr->as.binary.op == OP_NOT_EQUAL);
                }

                // If both are non-numeric but types are different (handled above for strings/bools/runes)
                // this handles any remaining cases
                if (!left_is_numeric && !right_is_numeric && left.type != right.type) {
                    return val_bool(expr->as.binary.op == OP_NOT_EQUAL);
                }
            }

            // Numeric operations
            if (!is_numeric(left) || !is_numeric(right)) {
                runtime_error(ctx, "Binary operation requires numeric operands");
            }

            // Determine result type and promote operands
            ValueType result_type = promote_types(left.type, right.type);
            left = promote_value(left, result_type);
            right = promote_value(right, result_type);

            // Perform operation based on result type
            if (is_float(left)) {
                // Float operation
                double l = value_to_float(left);
                double r = value_to_float(right);

                switch (expr->as.binary.op) {
                    case OP_ADD:
                        return (result_type == VAL_F32) ? val_f32((float)(l + r)) : val_f64(l + r);
                    case OP_SUB:
                        return (result_type == VAL_F32) ? val_f32((float)(l - r)) : val_f64(l - r);
                    case OP_MUL:
                        return (result_type == VAL_F32) ? val_f32((float)(l * r)) : val_f64(l * r);
                    case OP_DIV:
                        if (r == 0.0) {
                            runtime_error(ctx, "Division by zero");
                        }
                        return (result_type == VAL_F32) ? val_f32((float)(l / r)) : val_f64(l / r);
                    case OP_EQUAL: return val_bool(l == r);
                    case OP_NOT_EQUAL: return val_bool(l != r);
                    case OP_LESS: return val_bool(l < r);
                    case OP_LESS_EQUAL: return val_bool(l <= r);
                    case OP_GREATER: return val_bool(l > r);
                    case OP_GREATER_EQUAL: return val_bool(l >= r);
                    default: break;
                }
            } else {
                // Integer operation - handle each result type properly to avoid truncation
                switch (expr->as.binary.op) {
                    case OP_ADD:
                    case OP_SUB:
                    case OP_MUL:
                    case OP_DIV: {
                        // Extract values according to the promoted type
                        switch (result_type) {
                            case VAL_I8: {
                                int8_t l = left.as.as_i8;
                                int8_t r = right.as.as_i8;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    runtime_error(ctx, "Division by zero");
                                }
                                int8_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                               (expr->as.binary.op == OP_SUB) ? (l - r) :
                                               (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_i8(result);
                            }
                            case VAL_I16: {
                                int16_t l = left.as.as_i16;
                                int16_t r = right.as.as_i16;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    runtime_error(ctx, "Division by zero");
                                }
                                int16_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_i16(result);
                            }
                            case VAL_I32: {
                                int32_t l = left.as.as_i32;
                                int32_t r = right.as.as_i32;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    runtime_error(ctx, "Division by zero");
                                }
                                int32_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_i32(result);
                            }
                            case VAL_I64: {
                                int64_t l = left.as.as_i64;
                                int64_t r = right.as.as_i64;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    runtime_error(ctx, "Division by zero");
                                }
                                int64_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_i64(result);
                            }
                            case VAL_U8: {
                                uint8_t l = left.as.as_u8;
                                uint8_t r = right.as.as_u8;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    runtime_error(ctx, "Division by zero");
                                }
                                uint8_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_u8(result);
                            }
                            case VAL_U16: {
                                uint16_t l = left.as.as_u16;
                                uint16_t r = right.as.as_u16;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    runtime_error(ctx, "Division by zero");
                                }
                                uint16_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                 (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                 (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_u16(result);
                            }
                            case VAL_U32: {
                                uint32_t l = left.as.as_u32;
                                uint32_t r = right.as.as_u32;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    runtime_error(ctx, "Division by zero");
                                }
                                uint32_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                 (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                 (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_u32(result);
                            }
                            case VAL_U64: {
                                uint64_t l = left.as.as_u64;
                                uint64_t r = right.as.as_u64;
                                if (expr->as.binary.op == OP_DIV && r == 0) {
                                    runtime_error(ctx, "Division by zero");
                                }
                                uint64_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                                 (expr->as.binary.op == OP_SUB) ? (l - r) :
                                                 (expr->as.binary.op == OP_MUL) ? (l * r) : (l / r);
                                return val_u64(result);
                            }
                            default:
                                runtime_error(ctx, "Invalid integer type for arithmetic");
                        }
                    }

                    // Comparison operations - can use wider types for comparison
                    case OP_EQUAL:
                    case OP_NOT_EQUAL:
                    case OP_LESS:
                    case OP_LESS_EQUAL:
                    case OP_GREATER:
                    case OP_GREATER_EQUAL: {
                        // For comparisons, we need to handle signed vs unsigned properly
                        int is_signed = (result_type == VAL_I8 || result_type == VAL_I16 ||
                                        result_type == VAL_I32 || result_type == VAL_I64);

                        if (is_signed) {
                            int64_t l, r;
                            switch (result_type) {
                                case VAL_I8: l = left.as.as_i8; r = right.as.as_i8; break;
                                case VAL_I16: l = left.as.as_i16; r = right.as.as_i16; break;
                                case VAL_I32: l = left.as.as_i32; r = right.as.as_i32; break;
                                case VAL_I64: l = left.as.as_i64; r = right.as.as_i64; break;
                                default: l = r = 0; break;
                            }
                            switch (expr->as.binary.op) {
                                case OP_EQUAL: return val_bool(l == r);
                                case OP_NOT_EQUAL: return val_bool(l != r);
                                case OP_LESS: return val_bool(l < r);
                                case OP_LESS_EQUAL: return val_bool(l <= r);
                                case OP_GREATER: return val_bool(l > r);
                                case OP_GREATER_EQUAL: return val_bool(l >= r);
                                default: break;
                            }
                        } else {
                            uint64_t l, r;
                            switch (result_type) {
                                case VAL_U8: l = left.as.as_u8; r = right.as.as_u8; break;
                                case VAL_U16: l = left.as.as_u16; r = right.as.as_u16; break;
                                case VAL_U32: l = left.as.as_u32; r = right.as.as_u32; break;
                                case VAL_U64: l = left.as.as_u64; r = right.as.as_u64; break;
                                default: l = r = 0; break;
                            }
                            switch (expr->as.binary.op) {
                                case OP_EQUAL: return val_bool(l == r);
                                case OP_NOT_EQUAL: return val_bool(l != r);
                                case OP_LESS: return val_bool(l < r);
                                case OP_LESS_EQUAL: return val_bool(l <= r);
                                case OP_GREATER: return val_bool(l > r);
                                case OP_GREATER_EQUAL: return val_bool(l >= r);
                                default: break;
                            }
                        }
                        break;
                    }

                    // Bitwise operations - only for integers
                    case OP_BIT_AND:
                    case OP_BIT_OR:
                    case OP_BIT_XOR:
                    case OP_BIT_LSHIFT:
                    case OP_BIT_RSHIFT: {
                        // Bitwise operations require both operands to be integers
                        int is_signed = (result_type == VAL_I8 || result_type == VAL_I16 ||
                                        result_type == VAL_I32 || result_type == VAL_I64);

                        if (is_signed) {
                            // Signed integer bitwise operations
                            int64_t l, r;
                            switch (result_type) {
                                case VAL_I8: l = left.as.as_i8; r = right.as.as_i8; break;
                                case VAL_I16: l = left.as.as_i16; r = right.as.as_i16; break;
                                case VAL_I32: l = left.as.as_i32; r = right.as.as_i32; break;
                                case VAL_I64: l = left.as.as_i64; r = right.as.as_i64; break;
                                default: l = r = 0; break;
                            }

                            int64_t result;
                            switch (expr->as.binary.op) {
                                case OP_BIT_AND: result = l & r; break;
                                case OP_BIT_OR: result = l | r; break;
                                case OP_BIT_XOR: result = l ^ r; break;
                                case OP_BIT_LSHIFT: result = l << r; break;
                                case OP_BIT_RSHIFT: result = l >> r; break;
                                default: result = 0; break;
                            }

                            // Return with the original type
                            switch (result_type) {
                                case VAL_I8: return val_i8((int8_t)result);
                                case VAL_I16: return val_i16((int16_t)result);
                                case VAL_I32: return val_i32((int32_t)result);
                                case VAL_I64: return val_i64(result);
                                default: break;
                            }
                        } else {
                            // Unsigned integer bitwise operations
                            uint64_t l, r;
                            switch (result_type) {
                                case VAL_U8: l = left.as.as_u8; r = right.as.as_u8; break;
                                case VAL_U16: l = left.as.as_u16; r = right.as.as_u16; break;
                                case VAL_U32: l = left.as.as_u32; r = right.as.as_u32; break;
                                case VAL_U64: l = left.as.as_u64; r = right.as.as_u64; break;
                                default: l = r = 0; break;
                            }

                            uint64_t result;
                            switch (expr->as.binary.op) {
                                case OP_BIT_AND: result = l & r; break;
                                case OP_BIT_OR: result = l | r; break;
                                case OP_BIT_XOR: result = l ^ r; break;
                                case OP_BIT_LSHIFT: result = l << r; break;
                                case OP_BIT_RSHIFT: result = l >> r; break;
                                default: result = 0; break;
                            }

                            // Return with the original type
                            switch (result_type) {
                                case VAL_U8: return val_u8((uint8_t)result);
                                case VAL_U16: return val_u16((uint16_t)result);
                                case VAL_U32: return val_u32((uint32_t)result);
                                case VAL_U64: return val_u64(result);
                                default: break;
                            }
                        }
                        break;
                    }
                    default: break;
                }
            }

            runtime_error(ctx, "Unknown binary operator");
        }

        case EXPR_CALL: {
            // Check if this is a method call (obj.method(...))
            int is_method_call = 0;
            Value method_self;

            if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                is_method_call = 1;
                method_self = eval_expr(expr->as.call.func->as.get_property.object, env, ctx);

                // Special handling for file methods
                if (method_self.type == VAL_FILE) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_file_method(method_self.as.as_file, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }

                // Special handling for array methods
                if (method_self.type == VAL_ARRAY) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_array_method(method_self.as.as_array, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }

                // Special handling for string methods
                if (method_self.type == VAL_STRING) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_string_method(method_self.as.as_string, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }

                // Special handling for channel methods
                if (method_self.type == VAL_CHANNEL) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                        }
                    }

                    Value result = call_channel_method(method_self.as.as_channel, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }

                // Special handling for object built-in methods (e.g., serialize)
                if (method_self.type == VAL_OBJECT) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Only handle built-in object methods here (currently just serialize)
                    if (strcmp(method, "serialize") == 0) {
                        // Evaluate arguments
                        Value *args = NULL;
                        if (expr->as.call.num_args > 0) {
                            args = malloc(sizeof(Value) * expr->as.call.num_args);
                            for (int i = 0; i < expr->as.call.num_args; i++) {
                                args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                            }
                        }

                        Value result = call_object_method(method_self.as.as_object, method, args, expr->as.call.num_args);
                        if (args) free(args);
                        return result;
                    }
                    // For user-defined methods, fall through to normal function call handling
                }
            }

            // Evaluate the function expression
            Value func = eval_expr(expr->as.call.func, env, ctx);

            // Evaluate arguments
            Value *args = NULL;
            if (expr->as.call.num_args > 0) {
                args = malloc(sizeof(Value) * expr->as.call.num_args);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    args[i] = eval_expr(expr->as.call.args[i], env, ctx);
                }
            }

            Value result;

            if (func.type == VAL_BUILTIN_FN) {
                // Call builtin function
                BuiltinFn fn = func.as.as_builtin_fn;
                result = fn(args, expr->as.call.num_args, ctx);
            } else if (func.type == VAL_FUNCTION) {
                // Call user-defined function
                Function *fn = func.as.as_function;

                // Check argument count
                if (expr->as.call.num_args != fn->num_params) {
                    runtime_error(ctx, "Function expects %d arguments, got %d",
                            fn->num_params, expr->as.call.num_args);
                }

                // Determine function name for stack trace
                const char *fn_name = "<anonymous>";
                if (is_method_call && expr->as.call.func->type == EXPR_GET_PROPERTY) {
                    fn_name = expr->as.call.func->as.get_property.property;
                } else if (expr->as.call.func->type == EXPR_IDENT) {
                    fn_name = expr->as.call.func->as.ident;
                }

                // Push call onto stack trace (with line number from function body)
                int line = (fn->body != NULL) ? fn->body->line : 0;
                call_stack_push_line(&ctx->call_stack, fn_name, line);

                // Create call environment with closure_env as parent
                Environment *call_env = env_new(fn->closure_env);

                // Inject 'self' if this is a method call
                if (is_method_call) {
                    env_set(call_env, "self", method_self, ctx);
                }

                // Bind parameters
                for (int i = 0; i < fn->num_params; i++) {
                    Value arg_value = args[i];

                    // Type check if parameter has type annotation
                    if (fn->param_types[i]) {
                        arg_value = convert_to_type(arg_value, fn->param_types[i], call_env, ctx);
                    }

                    env_set(call_env, fn->param_names[i], arg_value, ctx);
                }

                // Save defer stack depth before executing function body
                int defer_depth_before = ctx->defer_stack.count;

                // Execute body
                ctx->return_state.is_returning = 0;
                eval_stmt(fn->body, call_env, ctx);

                // Execute deferred calls (in LIFO order) before returning
                // This happens even if there was an exception
                if (ctx->defer_stack.count > defer_depth_before) {
                    // Create a temporary defer stack with just this function's defers
                    DeferStack local_defers;
                    local_defers.count = ctx->defer_stack.count - defer_depth_before;
                    local_defers.capacity = local_defers.count;
                    local_defers.calls = &ctx->defer_stack.calls[defer_depth_before];
                    local_defers.envs = &ctx->defer_stack.envs[defer_depth_before];

                    // Execute the defers
                    defer_stack_execute(&local_defers, ctx);

                    // Restore defer stack to pre-function depth
                    ctx->defer_stack.count = defer_depth_before;
                }

                // Get result
                result = ctx->return_state.return_value;

                // Check return type if specified
                if (fn->return_type) {
                    if (!ctx->return_state.is_returning) {
                        runtime_error(ctx, "Function with return type must return a value");
                    }
                    result = convert_to_type(result, fn->return_type, call_env, ctx);
                }

                // Reset return state
                ctx->return_state.is_returning = 0;

                // Pop call from stack trace (but not if exception is active - preserve stack for error reporting)
                if (!ctx->exception_state.is_throwing) {
                    call_stack_pop(&ctx->call_stack);
                }

                // Release call environment (reference counted - will be freed when no longer used)
                env_release(call_env);
            } else if (func.type == VAL_FFI_FUNCTION) {
                // Call FFI function
                FFIFunction *ffi_func = (FFIFunction*)func.as.as_ffi_function;
                result = ffi_call_function(ffi_func, args, expr->as.call.num_args, ctx);
            } else {
                runtime_error(ctx, "Value is not a function");
            }

            if (args) free(args);
            return result;
        }

        case EXPR_GET_PROPERTY: {
            Value object = eval_expr(expr->as.get_property.object, env, ctx);
            const char *property = expr->as.get_property.property;

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                // .length property - returns codepoint count
                if (strcmp(property, "length") == 0) {
                    // Compute character length if not cached
                    if (str->char_length < 0) {
                        str->char_length = utf8_count_codepoints(str->data, str->length);
                    }
                    return val_i32(str->char_length);
                }

                // .byte_length property - returns byte count
                if (strcmp(property, "byte_length") == 0) {
                    return val_i32(str->length);
                }

                runtime_error(ctx, "Unknown property '%s' for string", property);
            } else if (object.type == VAL_BUFFER) {
                if (strcmp(property, "length") == 0) {
                    return val_int(object.as.as_buffer->length);
                } else if (strcmp(property, "capacity") == 0) {
                    return val_int(object.as.as_buffer->capacity);
                }
                runtime_error(ctx, "Unknown property '%s' for buffer", property);
            } else if (object.type == VAL_FILE) {
                FileHandle *file = object.as.as_file;
                if (strcmp(property, "path") == 0) {
                    return val_string(file->path);
                } else if (strcmp(property, "mode") == 0) {
                    return val_string(file->mode);
                } else if (strcmp(property, "closed") == 0) {
                    return val_bool(file->closed);
                }
                runtime_error(ctx, "Unknown property '%s' for file", property);
            } else if (object.type == VAL_ARRAY) {
                // Array properties
                if (strcmp(property, "length") == 0) {
                    return val_i32(object.as.as_array->length);
                }
                runtime_error(ctx, "Array has no property '%s'", property);
            } else if (object.type == VAL_OBJECT) {
                // Look up field in object
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        return obj->field_values[i];
                    }
                }
                runtime_error(ctx, "Object has no field '%s'", property);
            } else {
                runtime_error(ctx, "Only strings, buffers, arrays, and objects have properties");
            }
        }

        case EXPR_INDEX: {
            Value object = eval_expr(expr->as.index.object, env, ctx);
            Value index_val = eval_expr(expr->as.index.index, env, ctx);

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

                return val_rune(codepoint);
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    runtime_error(ctx, "Buffer index %d out of bounds (length %d)", index, buf->length);
                }

                // Return the byte as an integer (u8)
                return val_u8(((unsigned char *)buf->data)[index]);
            } else if (object.type == VAL_ARRAY) {
                // Array indexing
                return array_get(object.as.as_array, index);
            } else {
                runtime_error(ctx, "Only strings, buffers, and arrays can be indexed");
            }
        }

        case EXPR_INDEX_ASSIGN: {
            Value object = eval_expr(expr->as.index_assign.object, env, ctx);
            Value index_val = eval_expr(expr->as.index_assign.index, env, ctx);
            Value value = eval_expr(expr->as.index_assign.value, env, ctx);

            if (!is_integer(index_val)) {
                runtime_error(ctx, "Index must be an integer");
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_ARRAY) {
                // Array assignment - value can be any type
                array_set(object.as.as_array, index, value);
                return value;
            }

            // For strings and buffers, value must be an integer (byte)
            if (!is_integer(value)) {
                runtime_error(ctx, "Index value must be an integer (byte) for strings/buffers");
            }

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                if (index < 0 || index >= str->length) {
                    runtime_error(ctx, "String index %d out of bounds (length %d)", index, str->length);
                }

                // Strings are mutable - set the byte
                str->data[index] = (char)value_to_int(value);
                return value;
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    runtime_error(ctx, "Buffer index %d out of bounds (length %d)", index, buf->length);
                }

                // Buffers are mutable - set the byte
                ((unsigned char *)buf->data)[index] = (unsigned char)value_to_int(value);
                return value;
            } else {
                runtime_error(ctx, "Only strings, buffers, and arrays support index assignment");
            }
        }

        case EXPR_FUNCTION: {
            // Create function object and capture current environment
            Function *fn = malloc(sizeof(Function));

            // Copy is_async flag
            fn->is_async = expr->as.function.is_async;

            // Copy parameter names
            fn->param_names = malloc(sizeof(char*) * expr->as.function.num_params);
            for (int i = 0; i < expr->as.function.num_params; i++) {
                fn->param_names[i] = strdup(expr->as.function.param_names[i]);
            }

            // Copy parameter types (may be NULL)
            fn->param_types = malloc(sizeof(Type*) * expr->as.function.num_params);
            for (int i = 0; i < expr->as.function.num_params; i++) {
                if (expr->as.function.param_types[i]) {
                    fn->param_types[i] = type_new(expr->as.function.param_types[i]->kind);
                } else {
                    fn->param_types[i] = NULL;
                }
            }

            fn->num_params = expr->as.function.num_params;

            // Copy return type (may be NULL)
            if (expr->as.function.return_type) {
                fn->return_type = type_new(expr->as.function.return_type->kind);
            } else {
                fn->return_type = NULL;
            }

            // Store body AST (shared, not copied)
            fn->body = expr->as.function.body;

            // CRITICAL: Capture current environment and retain it
            fn->closure_env = env;
            env_retain(env);  // Increment ref count since closure captures env

            return val_function(fn);
        }

        case EXPR_ARRAY_LITERAL: {
            // Create array and evaluate elements
            Array *arr = array_new();

            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                Value element = eval_expr(expr->as.array_literal.elements[i], env, ctx);
                array_push(arr, element);
            }

            return val_array(arr);
        }

        case EXPR_OBJECT_LITERAL: {
            // Create anonymous object
            Object *obj = object_new(NULL, expr->as.object_literal.num_fields);

            // Evaluate and store fields
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                obj->field_names[i] = strdup(expr->as.object_literal.field_names[i]);
                obj->field_values[i] = eval_expr(expr->as.object_literal.field_values[i], env, ctx);
                obj->num_fields++;
            }

            return val_object(obj);
        }

        case EXPR_SET_PROPERTY: {
            Value object = eval_expr(expr->as.set_property.object, env, ctx);
            const char *property = expr->as.set_property.property;
            Value value = eval_expr(expr->as.set_property.value, env, ctx);

            if (object.type != VAL_OBJECT) {
                runtime_error(ctx, "Only objects can have properties set");
            }

            Object *obj = object.as.as_object;

            // Look for existing field
            for (int i = 0; i < obj->num_fields; i++) {
                if (strcmp(obj->field_names[i], property) == 0) {
                    obj->field_values[i] = value;
                    return value;
                }
            }

            // Field doesn't exist - add it dynamically!
            if (obj->num_fields >= obj->capacity) {
                // Grow arrays (handle capacity=0 case)
                obj->capacity = (obj->capacity == 0) ? 4 : obj->capacity * 2;
                char **new_names = realloc(obj->field_names, sizeof(char*) * obj->capacity);
                Value *new_values = realloc(obj->field_values, sizeof(Value) * obj->capacity);
                if (!new_names || !new_values) {
                    runtime_error(ctx, "Failed to grow object capacity");
                }
                obj->field_names = new_names;
                obj->field_values = new_values;
            }

            obj->field_names[obj->num_fields] = strdup(property);
            obj->field_values[obj->num_fields] = value;
            obj->num_fields++;

            return value;
        }

        case EXPR_PREFIX_INC: {
            // ++x: increment then return new value
            Expr *operand = expr->as.prefix_inc.operand;

            if (operand->type == EXPR_IDENT) {
                // Simple variable: ++x
                Value old_val = env_get(env, operand->as.ident, ctx);
                Value new_val = value_add_one(old_val, ctx);
                env_set(env, operand->as.ident, new_val, ctx);
                return new_val;
            } else if (operand->type == EXPR_INDEX) {
                // Array/buffer/string index: ++arr[i]
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    runtime_error(ctx, "Index must be an integer");
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index);
                    Value new_val = value_add_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val);
                    return new_val;
                } else {
                    runtime_error(ctx, "Can only use ++ on array elements");
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                // Object property: ++obj.field
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    runtime_error(ctx, "Can only increment object properties");
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_add_one(old_val, ctx);
                        obj->field_values[i] = new_val;
                        return new_val;
                    }
                }
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
                Value old_val = env_get(env, operand->as.ident, ctx);
                Value new_val = value_sub_one(old_val, ctx);
                env_set(env, operand->as.ident, new_val, ctx);
                return new_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    runtime_error(ctx, "Index must be an integer");
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index);
                    Value new_val = value_sub_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val);
                    return new_val;
                } else {
                    runtime_error(ctx, "Can only use -- on array elements");
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    runtime_error(ctx, "Can only decrement object properties");
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_sub_one(old_val, ctx);
                        obj->field_values[i] = new_val;
                        return new_val;
                    }
                }
                runtime_error(ctx, "Property '%s' not found", property);
            } else {
                runtime_error(ctx, "Invalid operand for --");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_POSTFIX_INC: {
            // x++: return old value then increment
            Expr *operand = expr->as.postfix_inc.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident, ctx);
                Value new_val = value_add_one(old_val, ctx);
                env_set(env, operand->as.ident, new_val, ctx);
                return old_val;  // Return old value!
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    runtime_error(ctx, "Index must be an integer");
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index);
                    Value new_val = value_add_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val);
                    return old_val;
                } else {
                    runtime_error(ctx, "Can only use ++ on array elements");
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    runtime_error(ctx, "Can only increment object properties");
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_add_one(old_val, ctx);
                        obj->field_values[i] = new_val;
                        return old_val;
                    }
                }
                runtime_error(ctx, "Property '%s' not found", property);
            } else {
                runtime_error(ctx, "Invalid operand for ++");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_POSTFIX_DEC: {
            // x--: return old value then decrement
            Expr *operand = expr->as.postfix_dec.operand;

            if (operand->type == EXPR_IDENT) {
                Value old_val = env_get(env, operand->as.ident, ctx);
                Value new_val = value_sub_one(old_val, ctx);
                env_set(env, operand->as.ident, new_val, ctx);
                return old_val;
            } else if (operand->type == EXPR_INDEX) {
                Value object = eval_expr(operand->as.index.object, env, ctx);
                Value index_val = eval_expr(operand->as.index.index, env, ctx);

                if (!is_integer(index_val)) {
                    runtime_error(ctx, "Index must be an integer");
                }
                int32_t index = value_to_int(index_val);

                if (object.type == VAL_ARRAY) {
                    Value old_val = array_get(object.as.as_array, index);
                    Value new_val = value_sub_one(old_val, ctx);
                    array_set(object.as.as_array, index, new_val);
                    return old_val;
                } else {
                    runtime_error(ctx, "Can only use -- on array elements");
                }
            } else if (operand->type == EXPR_GET_PROPERTY) {
                Value object = eval_expr(operand->as.get_property.object, env, ctx);
                const char *property = operand->as.get_property.property;
                if (object.type != VAL_OBJECT) {
                    runtime_error(ctx, "Can only decrement object properties");
                }
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        Value old_val = obj->field_values[i];
                        Value new_val = value_sub_one(old_val, ctx);
                        obj->field_values[i] = new_val;
                        return old_val;
                    }
                }
                runtime_error(ctx, "Property '%s' not found", property);
            } else {
                runtime_error(ctx, "Invalid operand for --");
            }
            return val_null();  // Unreachable, but silences fallthrough warning
        }

        case EXPR_AWAIT: {
            // For MVP: await just evaluates the expression synchronously
            // TODO: Implement proper async/await scheduling
            Value awaited = eval_expr(expr->as.await_expr.awaited_expr, env, ctx);

            // If it's a task, we should join it
            // For now, just return the value as-is
            return awaited;
        }
    }

    return val_null();
}

