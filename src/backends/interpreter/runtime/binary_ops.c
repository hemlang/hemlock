/*
 * Hemlock Interpreter - Binary Operations
 *
 * Handles evaluation of binary expressions: arithmetic, comparison,
 * logical, and bitwise operations with type promotion.
 *
 * Extracted from expressions.c to reduce file size.
 */

#include "internal.h"

// Forward declaration for recursive expression evaluation
Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx);

/*
 * Evaluate a binary expression.
 * Handles:
 * - Short-circuit evaluation for && and ||
 * - Fast paths for i32, i64, f64 operations
 * - String concatenation
 * - Type promotion between numeric types
 * - Comparison and equality operators
 * - Bitwise operations
 */
Value eval_binary_expr(Expr *expr, Environment *env, ExecutionContext *ctx) {
    if (expr->as.binary.op == OP_AND) {
        Value left = eval_expr(expr->as.binary.left, env, ctx);
        if (!value_is_truthy(left)) {
            VALUE_RELEASE(left);  // Release left before returning
            return val_bool(0);
        }

        VALUE_RELEASE(left);  // Release left after checking
        Value right = eval_expr(expr->as.binary.right, env, ctx);
        int result = value_is_truthy(right);
        VALUE_RELEASE(right);  // Release right before returning
        return val_bool(result);
    }

    if (expr->as.binary.op == OP_OR) {
        Value left = eval_expr(expr->as.binary.left, env, ctx);
        if (value_is_truthy(left)) {
            VALUE_RELEASE(left);  // Release left before returning
            return val_bool(1);
        }

        VALUE_RELEASE(left);  // Release left after checking
        Value right = eval_expr(expr->as.binary.right, env, ctx);
        int result = value_is_truthy(right);
        VALUE_RELEASE(right);  // Release right before returning
        return val_bool(result);
    }

    // Evaluate both operands
    Value left = eval_expr(expr->as.binary.left, env, ctx);
    Value right = eval_expr(expr->as.binary.right, env, ctx);
    Value binary_result = val_null();  // Initialize to avoid undefined behavior

    // FAST PATH: i32 operations (most common case in benchmarks)
    // No refcounting needed for primitives, skip type promotion
    if (left.type == VAL_I32 && right.type == VAL_I32) {
        int32_t l = left.as.as_i32;
        int32_t r = right.as.as_i32;
        switch (expr->as.binary.op) {
            case OP_ADD: return val_i32(l + r);
            case OP_SUB: return val_i32(l - r);
            case OP_MUL: return val_i32(l * r);
            case OP_DIV:
                if (r == 0) { runtime_error(ctx, "Division by zero"); return val_null(); }
                return val_f64((double)l / (double)r);  // Always float division
            case OP_MOD:
                if (r == 0) { runtime_error(ctx, "Division by zero"); return val_null(); }
                return val_i32(l % r);
            case OP_LESS: return val_bool(l < r);
            case OP_LESS_EQUAL: return val_bool(l <= r);
            case OP_GREATER: return val_bool(l > r);
            case OP_GREATER_EQUAL: return val_bool(l >= r);
            case OP_EQUAL: return val_bool(l == r);
            case OP_NOT_EQUAL: return val_bool(l != r);
            case OP_BIT_AND: return val_i32(l & r);
            case OP_BIT_OR: return val_i32(l | r);
            case OP_BIT_XOR: return val_i32(l ^ r);
            case OP_BIT_LSHIFT: return val_i32(l << r);
            case OP_BIT_RSHIFT: return val_i32(l >> r);
            default: break;  // Fall through to generic path
        }
    }

    // FAST PATH: i64 operations
    if (left.type == VAL_I64 && right.type == VAL_I64) {
        int64_t l = left.as.as_i64;
        int64_t r = right.as.as_i64;
        switch (expr->as.binary.op) {
            case OP_ADD: return val_i64(l + r);
            case OP_SUB: return val_i64(l - r);
            case OP_MUL: return val_i64(l * r);
            case OP_DIV:
                if (r == 0) { runtime_error(ctx, "Division by zero"); return val_null(); }
                return val_f64((double)l / (double)r);  // Always float division
            case OP_MOD:
                if (r == 0) { runtime_error(ctx, "Division by zero"); return val_null(); }
                return val_i64(l % r);
            case OP_LESS: return val_bool(l < r);
            case OP_LESS_EQUAL: return val_bool(l <= r);
            case OP_GREATER: return val_bool(l > r);
            case OP_GREATER_EQUAL: return val_bool(l >= r);
            case OP_EQUAL: return val_bool(l == r);
            case OP_NOT_EQUAL: return val_bool(l != r);
            case OP_BIT_AND: return val_i64(l & r);
            case OP_BIT_OR: return val_i64(l | r);
            case OP_BIT_XOR: return val_i64(l ^ r);
            case OP_BIT_LSHIFT: return val_i64(l << r);
            case OP_BIT_RSHIFT: return val_i64(l >> r);
            default: break;
        }
    }

    // FAST PATH: f64 operations
    if (left.type == VAL_F64 && right.type == VAL_F64) {
        double l = left.as.as_f64;
        double r = right.as.as_f64;
        switch (expr->as.binary.op) {
            case OP_ADD: return val_f64(l + r);
            case OP_SUB: return val_f64(l - r);
            case OP_MUL: return val_f64(l * r);
            case OP_DIV:
                // IEEE 754: float division by zero returns Infinity or NaN
                return val_f64(l / r);
            case OP_LESS: return val_bool(l < r);
            case OP_LESS_EQUAL: return val_bool(l <= r);
            case OP_GREATER: return val_bool(l > r);
            case OP_GREATER_EQUAL: return val_bool(l >= r);
            case OP_EQUAL: return val_bool(l == r);
            case OP_NOT_EQUAL: return val_bool(l != r);
            default: break;
        }
    }

    // FAST PATH: Mixed i32/i64 - promote to i64
    if ((left.type == VAL_I32 && right.type == VAL_I64) ||
        (left.type == VAL_I64 && right.type == VAL_I32)) {
        int64_t l = (left.type == VAL_I64) ? left.as.as_i64 : (int64_t)left.as.as_i32;
        int64_t r = (right.type == VAL_I64) ? right.as.as_i64 : (int64_t)right.as.as_i32;
        switch (expr->as.binary.op) {
            case OP_ADD: return val_i64(l + r);
            case OP_SUB: return val_i64(l - r);
            case OP_MUL: return val_i64(l * r);
            case OP_DIV:
                if (r == 0) { runtime_error(ctx, "Division by zero"); return val_null(); }
                return val_f64((double)l / (double)r);  // Always float division
            case OP_MOD:
                if (r == 0) { runtime_error(ctx, "Division by zero"); return val_null(); }
                return val_i64(l % r);
            case OP_LESS: return val_bool(l < r);
            case OP_LESS_EQUAL: return val_bool(l <= r);
            case OP_GREATER: return val_bool(l > r);
            case OP_GREATER_EQUAL: return val_bool(l >= r);
            case OP_EQUAL: return val_bool(l == r);
            case OP_NOT_EQUAL: return val_bool(l != r);
            case OP_BIT_AND: return val_i64(l & r);
            case OP_BIT_OR: return val_i64(l | r);
            case OP_BIT_XOR: return val_i64(l ^ r);
            case OP_BIT_LSHIFT: return val_i64(l << r);
            case OP_BIT_RSHIFT: return val_i64(l >> r);
            default: break;
        }
    }

    // String concatenation
    if (expr->as.binary.op == OP_ADD && left.type == VAL_STRING && right.type == VAL_STRING) {
        String *result = string_concat(left.as.as_string, right.as.as_string);
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
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
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
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
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
    }

    // String + number concatenation (auto-convert number to string)
    if (expr->as.binary.op == OP_ADD && left.type == VAL_STRING && (is_numeric(right) || right.type == VAL_BOOL)) {
        char *right_str = value_to_string(right);
        String *right_string = string_new(right_str);
        free(right_str);
        String *result = string_concat(left.as.as_string, right_string);
        free(right_string);
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
    }

    // Number + string concatenation (auto-convert number to string)
    if (expr->as.binary.op == OP_ADD && (is_numeric(left) || left.type == VAL_BOOL) && right.type == VAL_STRING) {
        char *left_str = value_to_string(left);
        String *left_string = string_new(left_str);
        free(left_str);
        String *result = string_concat(left_string, right.as.as_string);
        free(left_string);
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
    }

    // String + object/array concatenation (auto-serialize to JSON)
    if (expr->as.binary.op == OP_ADD && left.type == VAL_STRING && (right.type == VAL_OBJECT || right.type == VAL_ARRAY)) {
        VisitedSet visited;
        visited_init(&visited);
        char *right_json = serialize_value(right, &visited, ctx);
        visited_free(&visited);
        if (right_json == NULL) {
            // Serialization failed (exception already thrown)
            goto binary_cleanup;
        }
        String *right_string = string_new(right_json);
        free(right_json);
        String *result = string_concat(left.as.as_string, right_string);
        free(right_string);
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
    }

    // Object/array + string concatenation (auto-serialize to JSON)
    if (expr->as.binary.op == OP_ADD && (left.type == VAL_OBJECT || left.type == VAL_ARRAY) && right.type == VAL_STRING) {
        VisitedSet visited;
        visited_init(&visited);
        char *left_json = serialize_value(left, &visited, ctx);
        visited_free(&visited);
        if (left_json == NULL) {
            // Serialization failed (exception already thrown)
            goto binary_cleanup;
        }
        String *left_string = string_new(left_json);
        free(left_json);
        String *result = string_concat(left_string, right.as.as_string);
        free(left_string);
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
    }

    // String + null concatenation (coerce null to "null")
    if (expr->as.binary.op == OP_ADD && left.type == VAL_STRING && right.type == VAL_NULL) {
        String *null_str = string_new("null");
        String *result = string_concat(left.as.as_string, null_str);
        free(null_str);
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
    }

    // Null + string concatenation (coerce null to "null")
    if (expr->as.binary.op == OP_ADD && left.type == VAL_NULL && right.type == VAL_STRING) {
        String *null_str = string_new("null");
        String *result = string_concat(null_str, right.as.as_string);
        free(null_str);
        binary_result = (Value){ .type = VAL_STRING, .as.as_string = result };
        goto binary_cleanup;
    }

    // Pointer arithmetic
    if (left.type == VAL_PTR && is_integer(right)) {
        if (expr->as.binary.op == OP_ADD) {
            void *ptr = left.as.as_ptr;
            int32_t offset = value_to_int(right);
            binary_result = val_ptr((char *)ptr + offset);
            goto binary_cleanup;
        } else if (expr->as.binary.op == OP_SUB) {
            void *ptr = left.as.as_ptr;
            int32_t offset = value_to_int(right);
            binary_result = val_ptr((char *)ptr - offset);
            goto binary_cleanup;
        }
    }

    if (is_integer(left) && right.type == VAL_PTR) {
        if (expr->as.binary.op == OP_ADD) {
            int32_t offset = value_to_int(left);
            void *ptr = right.as.as_ptr;
            binary_result = val_ptr((char *)ptr + offset);
            goto binary_cleanup;
        }
    }

    // Boolean comparisons
    if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
        if (expr->as.binary.op == OP_EQUAL) {
            binary_result = val_bool(left.as.as_bool == right.as.as_bool);
            goto binary_cleanup;
        } else if (expr->as.binary.op == OP_NOT_EQUAL) {
            binary_result = val_bool(left.as.as_bool != right.as.as_bool);
            goto binary_cleanup;
        }
    }

    // String comparisons (including ordering comparisons)
    if (left.type == VAL_STRING && right.type == VAL_STRING) {
        String *left_str = left.as.as_string;
        String *right_str = right.as.as_string;
        // Use memcmp for lexicographic comparison, comparing up to the shorter length
        size_t min_len = left_str->length < right_str->length ? left_str->length : right_str->length;
        int cmp = memcmp(left_str->data, right_str->data, min_len);
        // If equal up to min_len, the shorter string is "less"
        if (cmp == 0) {
            if (left_str->length < right_str->length) {
                cmp = -1;
            } else if (left_str->length > right_str->length) {
                cmp = 1;
            }
        }
        switch (expr->as.binary.op) {
            case OP_EQUAL:
                binary_result = val_bool(cmp == 0);
                goto binary_cleanup;
            case OP_NOT_EQUAL:
                binary_result = val_bool(cmp != 0);
                goto binary_cleanup;
            case OP_LESS:
                binary_result = val_bool(cmp < 0);
                goto binary_cleanup;
            case OP_LESS_EQUAL:
                binary_result = val_bool(cmp <= 0);
                goto binary_cleanup;
            case OP_GREATER:
                binary_result = val_bool(cmp > 0);
                goto binary_cleanup;
            case OP_GREATER_EQUAL:
                binary_result = val_bool(cmp >= 0);
                goto binary_cleanup;
            default:
                break;  // Fall through for other operations
        }
    }

    // Rune comparisons (including ordering comparisons)
    if (left.type == VAL_RUNE && right.type == VAL_RUNE) {
        switch (expr->as.binary.op) {
            case OP_EQUAL:
                binary_result = val_bool(left.as.as_rune == right.as.as_rune);
                goto binary_cleanup;
            case OP_NOT_EQUAL:
                binary_result = val_bool(left.as.as_rune != right.as.as_rune);
                goto binary_cleanup;
            case OP_LESS:
                binary_result = val_bool(left.as.as_rune < right.as.as_rune);
                goto binary_cleanup;
            case OP_LESS_EQUAL:
                binary_result = val_bool(left.as.as_rune <= right.as.as_rune);
                goto binary_cleanup;
            case OP_GREATER:
                binary_result = val_bool(left.as.as_rune > right.as.as_rune);
                goto binary_cleanup;
            case OP_GREATER_EQUAL:
                binary_result = val_bool(left.as.as_rune >= right.as.as_rune);
                goto binary_cleanup;
            default:
                break;  // Fall through for other operations
        }
    }

    // Pointer comparisons (both null and non-null)
    if (left.type == VAL_PTR && right.type == VAL_PTR) {
        void *lp = left.as.as_ptr;
        void *rp = right.as.as_ptr;
        switch (expr->as.binary.op) {
            case OP_EQUAL:
                binary_result = val_bool(lp == rp);
                goto binary_cleanup;
            case OP_NOT_EQUAL:
                binary_result = val_bool(lp != rp);
                goto binary_cleanup;
            case OP_LESS:
                binary_result = val_bool(lp < rp);
                goto binary_cleanup;
            case OP_LESS_EQUAL:
                binary_result = val_bool(lp <= rp);
                goto binary_cleanup;
            case OP_GREATER:
                binary_result = val_bool(lp > rp);
                goto binary_cleanup;
            case OP_GREATER_EQUAL:
                binary_result = val_bool(lp >= rp);
                goto binary_cleanup;
            default:
                break;
        }
    }

    // Null comparisons (including NULL pointers)
    if (left.type == VAL_NULL || right.type == VAL_NULL ||
        (left.type == VAL_PTR && left.as.as_ptr == NULL) ||
        (right.type == VAL_PTR && right.as.as_ptr == NULL)) {
        if (expr->as.binary.op == OP_EQUAL) {
            // Check if both are null (either VAL_NULL or VAL_PTR with NULL)
            int left_is_null = (left.type == VAL_NULL) || (left.type == VAL_PTR && left.as.as_ptr == NULL);
            int right_is_null = (right.type == VAL_NULL) || (right.type == VAL_PTR && right.as.as_ptr == NULL);
            binary_result = val_bool(left_is_null && right_is_null);
            goto binary_cleanup;
        } else if (expr->as.binary.op == OP_NOT_EQUAL) {
            // Check if both are null (either VAL_NULL or VAL_PTR with NULL)
            int left_is_null = (left.type == VAL_NULL) || (left.type == VAL_PTR && left.as.as_ptr == NULL);
            int right_is_null = (right.type == VAL_NULL) || (right.type == VAL_PTR && right.as.as_ptr == NULL);
            binary_result = val_bool(!(left_is_null && right_is_null));
            goto binary_cleanup;
        }
    }

    // Object comparisons (reference equality)
    if (left.type == VAL_OBJECT && right.type == VAL_OBJECT) {
        if (expr->as.binary.op == OP_EQUAL) {
            binary_result = val_bool(left.as.as_object == right.as.as_object);
            goto binary_cleanup;
        } else if (expr->as.binary.op == OP_NOT_EQUAL) {
            binary_result = val_bool(left.as.as_object != right.as.as_object);
            goto binary_cleanup;
        }
    }

    // Cross-type equality comparisons (before numeric type check)
    // If types are different and not both numeric, == returns false, != returns true
    if (expr->as.binary.op == OP_EQUAL || expr->as.binary.op == OP_NOT_EQUAL) {
        int left_is_numeric = is_numeric(left);
        int right_is_numeric = is_numeric(right);

        // If one is numeric and the other is not, types don't match
        if (left_is_numeric != right_is_numeric) {
            binary_result = val_bool(expr->as.binary.op == OP_NOT_EQUAL);
            goto binary_cleanup;
        }

        // If both are non-numeric but types are different (handled above for strings/bools/runes)
        // this handles any remaining cases
        if (!left_is_numeric && !right_is_numeric && left.type != right.type) {
            binary_result = val_bool(expr->as.binary.op == OP_NOT_EQUAL);
            goto binary_cleanup;
        }
    }

    // Numeric operations
    if (!is_numeric(left) || !is_numeric(right)) {
        runtime_error(ctx, "Binary operation requires numeric operands");
        goto binary_cleanup;
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
                binary_result = (result_type == VAL_F32) ? val_f32((float)(l + r)) : val_f64(l + r);
                goto binary_cleanup;
            case OP_SUB:
                binary_result = (result_type == VAL_F32) ? val_f32((float)(l - r)) : val_f64(l - r);
                goto binary_cleanup;
            case OP_MUL:
                binary_result = (result_type == VAL_F32) ? val_f32((float)(l * r)) : val_f64(l * r);
                goto binary_cleanup;
            case OP_DIV:
                // IEEE 754: float division by zero returns Infinity or NaN
                binary_result = (result_type == VAL_F32) ? val_f32((float)(l / r)) : val_f64(l / r);
                goto binary_cleanup;
            case OP_MOD:
                // IEEE 754: fmod with zero returns NaN
                binary_result = (result_type == VAL_F32) ? val_f32((float)fmod(l, r)) : val_f64(fmod(l, r));
                goto binary_cleanup;
            case OP_EQUAL:
                binary_result = val_bool(l == r);
                goto binary_cleanup;
            case OP_NOT_EQUAL:
                binary_result = val_bool(l != r);
                goto binary_cleanup;
            case OP_LESS:
                binary_result = val_bool(l < r);
                goto binary_cleanup;
            case OP_LESS_EQUAL:
                binary_result = val_bool(l <= r);
                goto binary_cleanup;
            case OP_GREATER:
                binary_result = val_bool(l > r);
                goto binary_cleanup;
            case OP_GREATER_EQUAL:
                binary_result = val_bool(l >= r);
                goto binary_cleanup;
            case OP_BIT_AND:
            case OP_BIT_OR:
            case OP_BIT_XOR:
            case OP_BIT_LSHIFT:
            case OP_BIT_RSHIFT:
                runtime_error(ctx, "Invalid operation for floats");
                goto binary_cleanup;
            default: break;
        }
    } else {
        // Division always uses float regardless of operand types
        if (expr->as.binary.op == OP_DIV) {
            double l = value_to_float(left);
            double r = value_to_float(right);
            if (r == 0.0) {
                runtime_error(ctx, "Division by zero");
                goto binary_cleanup;
            }
            binary_result = val_f64(l / r);
            goto binary_cleanup;
        }

        // Integer operation - handle each result type properly to avoid truncation
        switch (expr->as.binary.op) {
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_MOD: {
                // Extract values according to the promoted type
                switch (result_type) {
                    case VAL_I8: {
                        int8_t l = left.as.as_i8;
                        int8_t r = right.as.as_i8;
                        if ((expr->as.binary.op == OP_DIV || expr->as.binary.op == OP_MOD) && r == 0) {
                            runtime_error(ctx, "Division by zero");
                            goto binary_cleanup;
                        }
                        int8_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                       (expr->as.binary.op == OP_SUB) ? (l - r) :
                                       (expr->as.binary.op == OP_MUL) ? (l * r) :
                                       (expr->as.binary.op == OP_DIV) ? (l / r) : (l % r);
                        binary_result = val_i8(result);
                        goto binary_cleanup;
                    }
                    case VAL_I16: {
                        int16_t l = left.as.as_i16;
                        int16_t r = right.as.as_i16;
                        if ((expr->as.binary.op == OP_DIV || expr->as.binary.op == OP_MOD) && r == 0) {
                            runtime_error(ctx, "Division by zero");
                            goto binary_cleanup;
                        }
                        int16_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                        (expr->as.binary.op == OP_SUB) ? (l - r) :
                                        (expr->as.binary.op == OP_MUL) ? (l * r) :
                                        (expr->as.binary.op == OP_DIV) ? (l / r) : (l % r);
                        binary_result = val_i16(result);
                        goto binary_cleanup;
                    }
                    case VAL_I32: {
                        int32_t l = left.as.as_i32;
                        int32_t r = right.as.as_i32;
                        if ((expr->as.binary.op == OP_DIV || expr->as.binary.op == OP_MOD) && r == 0) {
                            runtime_error(ctx, "Division by zero");
                            goto binary_cleanup;
                        }
                        int32_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                        (expr->as.binary.op == OP_SUB) ? (l - r) :
                                        (expr->as.binary.op == OP_MUL) ? (l * r) :
                                        (expr->as.binary.op == OP_DIV) ? (l / r) : (l % r);
                        binary_result = val_i32(result);
                        goto binary_cleanup;
                    }
                    case VAL_I64: {
                        int64_t l = left.as.as_i64;
                        int64_t r = right.as.as_i64;
                        if ((expr->as.binary.op == OP_DIV || expr->as.binary.op == OP_MOD) && r == 0) {
                            runtime_error(ctx, "Division by zero");
                            goto binary_cleanup;
                        }
                        int64_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                        (expr->as.binary.op == OP_SUB) ? (l - r) :
                                        (expr->as.binary.op == OP_MUL) ? (l * r) :
                                        (expr->as.binary.op == OP_DIV) ? (l / r) : (l % r);
                        binary_result = val_i64(result);
                        goto binary_cleanup;
                    }
                    case VAL_U8: {
                        uint8_t l = left.as.as_u8;
                        uint8_t r = right.as.as_u8;
                        if ((expr->as.binary.op == OP_DIV || expr->as.binary.op == OP_MOD) && r == 0) {
                            runtime_error(ctx, "Division by zero");
                            goto binary_cleanup;
                        }
                        uint8_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                        (expr->as.binary.op == OP_SUB) ? (l - r) :
                                        (expr->as.binary.op == OP_MUL) ? (l * r) :
                                        (expr->as.binary.op == OP_DIV) ? (l / r) : (l % r);
                        binary_result = val_u8(result);
                        goto binary_cleanup;
                    }
                    case VAL_U16: {
                        uint16_t l = left.as.as_u16;
                        uint16_t r = right.as.as_u16;
                        if ((expr->as.binary.op == OP_DIV || expr->as.binary.op == OP_MOD) && r == 0) {
                            runtime_error(ctx, "Division by zero");
                            goto binary_cleanup;
                        }
                        uint16_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                         (expr->as.binary.op == OP_SUB) ? (l - r) :
                                         (expr->as.binary.op == OP_MUL) ? (l * r) :
                                         (expr->as.binary.op == OP_DIV) ? (l / r) : (l % r);
                        binary_result = val_u16(result);
                        goto binary_cleanup;
                    }
                    case VAL_U32: {
                        uint32_t l = left.as.as_u32;
                        uint32_t r = right.as.as_u32;
                        if ((expr->as.binary.op == OP_DIV || expr->as.binary.op == OP_MOD) && r == 0) {
                            runtime_error(ctx, "Division by zero");
                            goto binary_cleanup;
                        }
                        uint32_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                         (expr->as.binary.op == OP_SUB) ? (l - r) :
                                         (expr->as.binary.op == OP_MUL) ? (l * r) :
                                         (expr->as.binary.op == OP_DIV) ? (l / r) : (l % r);
                        binary_result = val_u32(result);
                        goto binary_cleanup;
                    }
                    case VAL_U64: {
                        uint64_t l = left.as.as_u64;
                        uint64_t r = right.as.as_u64;
                        if ((expr->as.binary.op == OP_DIV || expr->as.binary.op == OP_MOD) && r == 0) {
                            runtime_error(ctx, "Division by zero");
                            goto binary_cleanup;
                        }
                        uint64_t result = (expr->as.binary.op == OP_ADD) ? (l + r) :
                                         (expr->as.binary.op == OP_SUB) ? (l - r) :
                                         (expr->as.binary.op == OP_MUL) ? (l * r) :
                                         (expr->as.binary.op == OP_DIV) ? (l / r) : (l % r);
                        binary_result = val_u64(result);
                        goto binary_cleanup;
                    }
                    default:
                        runtime_error(ctx, "Invalid integer type for arithmetic");
                        return val_null();
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
                        case OP_EQUAL:
                            binary_result = val_bool(l == r);
                            goto binary_cleanup;
                        case OP_NOT_EQUAL:
                            binary_result = val_bool(l != r);
                            goto binary_cleanup;
                        case OP_LESS:
                            binary_result = val_bool(l < r);
                            goto binary_cleanup;
                        case OP_LESS_EQUAL:
                            binary_result = val_bool(l <= r);
                            goto binary_cleanup;
                        case OP_GREATER:
                            binary_result = val_bool(l > r);
                            goto binary_cleanup;
                        case OP_GREATER_EQUAL:
                            binary_result = val_bool(l >= r);
                            goto binary_cleanup;
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
                        case OP_EQUAL:
                            binary_result = val_bool(l == r);
                            goto binary_cleanup;
                        case OP_NOT_EQUAL:
                            binary_result = val_bool(l != r);
                            goto binary_cleanup;
                        case OP_LESS:
                            binary_result = val_bool(l < r);
                            goto binary_cleanup;
                        case OP_LESS_EQUAL:
                            binary_result = val_bool(l <= r);
                            goto binary_cleanup;
                        case OP_GREATER:
                            binary_result = val_bool(l > r);
                            goto binary_cleanup;
                        case OP_GREATER_EQUAL:
                            binary_result = val_bool(l >= r);
                            goto binary_cleanup;
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
                if (result_type == VAL_F32 || result_type == VAL_F64) {
                    runtime_error(ctx, "Invalid operation for floats");
                    goto binary_cleanup;
                }
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
                        case VAL_I8:
                            binary_result = val_i8((int8_t)result);
                            goto binary_cleanup;
                        case VAL_I16:
                            binary_result = val_i16((int16_t)result);
                            goto binary_cleanup;
                        case VAL_I32:
                            binary_result = val_i32((int32_t)result);
                            goto binary_cleanup;
                        case VAL_I64:
                            binary_result = val_i64(result);
                            goto binary_cleanup;
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
                        case VAL_U8:
                            binary_result = val_u8((uint8_t)result);
                            goto binary_cleanup;
                        case VAL_U16:
                            binary_result = val_u16((uint16_t)result);
                            goto binary_cleanup;
                        case VAL_U32:
                            binary_result = val_u32((uint32_t)result);
                            goto binary_cleanup;
                        case VAL_U64:
                            binary_result = val_u64(result);
                            goto binary_cleanup;
                        default: break;
                    }
                }
                break;
            }
            default: break;
        }
    }

binary_cleanup:
    // Release operands after binary operation completes
    VALUE_RELEASE(left);
    VALUE_RELEASE(right);
    return binary_result;
}
