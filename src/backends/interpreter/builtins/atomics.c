/*
 * Hemlock Interpreter - Atomic Operations Builtins
 *
 * Provides atomic operations for lock-free concurrent programming:
 * - atomic_load_i32/i64: Load value atomically
 * - atomic_store_i32/i64: Store value atomically
 * - atomic_add_i32/i64: Atomic fetch-and-add
 * - atomic_sub_i32/i64: Atomic fetch-and-subtract
 * - atomic_and_i32/i64: Atomic fetch-and-bitwise-and
 * - atomic_or_i32/i64: Atomic fetch-and-bitwise-or
 * - atomic_xor_i32/i64: Atomic fetch-and-bitwise-xor
 * - atomic_cas_i32/i64: Compare-and-swap
 * - atomic_exchange_i32/i64: Atomic exchange
 *
 * All operations use sequential consistency (memory_order_seq_cst) by default.
 */

#include "internal.h"
#include <stdatomic.h>

// ========== i32 ATOMIC OPERATIONS ==========

// atomic_load_i32(ptr: ptr): i32
// Atomically loads an i32 from the pointer
Value builtin_atomic_load_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "atomic_load_i32() expects 1 argument (pointer)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_load_i32() expects a pointer argument");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t value = atomic_load(ptr);
    return val_i32(value);
}

// atomic_store_i32(ptr: ptr, value: i32): null
// Atomically stores an i32 to the pointer
Value builtin_atomic_store_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_store_i32() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_store_i32() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_store_i32() expects an integer as second argument");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t value = value_to_int(args[1]);
    atomic_store(ptr, value);
    return val_null();
}

// atomic_add_i32(ptr: ptr, value: i32): i32
// Atomically adds value to *ptr and returns the OLD value
Value builtin_atomic_add_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_add_i32() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_add_i32() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_add_i32() expects an integer as second argument");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t value = value_to_int(args[1]);
    int32_t old = atomic_fetch_add(ptr, value);
    return val_i32(old);
}

// atomic_sub_i32(ptr: ptr, value: i32): i32
// Atomically subtracts value from *ptr and returns the OLD value
Value builtin_atomic_sub_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_sub_i32() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_sub_i32() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_sub_i32() expects an integer as second argument");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t value = value_to_int(args[1]);
    int32_t old = atomic_fetch_sub(ptr, value);
    return val_i32(old);
}

// atomic_and_i32(ptr: ptr, value: i32): i32
// Atomically performs *ptr &= value and returns the OLD value
Value builtin_atomic_and_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_and_i32() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_and_i32() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_and_i32() expects an integer as second argument");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t value = value_to_int(args[1]);
    int32_t old = atomic_fetch_and(ptr, value);
    return val_i32(old);
}

// atomic_or_i32(ptr: ptr, value: i32): i32
// Atomically performs *ptr |= value and returns the OLD value
Value builtin_atomic_or_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_or_i32() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_or_i32() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_or_i32() expects an integer as second argument");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t value = value_to_int(args[1]);
    int32_t old = atomic_fetch_or(ptr, value);
    return val_i32(old);
}

// atomic_xor_i32(ptr: ptr, value: i32): i32
// Atomically performs *ptr ^= value and returns the OLD value
Value builtin_atomic_xor_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_xor_i32() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_xor_i32() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_xor_i32() expects an integer as second argument");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t value = value_to_int(args[1]);
    int32_t old = atomic_fetch_xor(ptr, value);
    return val_i32(old);
}

// atomic_cas_i32(ptr: ptr, expected: i32, desired: i32): bool
// Compare-and-swap: if *ptr == expected, set *ptr = desired and return true
// Otherwise, return false (and *ptr is unchanged)
Value builtin_atomic_cas_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "atomic_cas_i32() expects 3 arguments (pointer, expected, desired)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_cas_i32() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_cas_i32() expects an integer as second argument (expected)");
        return val_null();
    }

    if (!is_integer(args[2])) {
        runtime_error(ctx, "atomic_cas_i32() expects an integer as third argument (desired)");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t expected = value_to_int(args[1]);
    int32_t desired = value_to_int(args[2]);

    // atomic_compare_exchange_strong returns true if swap succeeded
    _Bool success = atomic_compare_exchange_strong(ptr, &expected, desired);
    return val_bool(success);
}

// atomic_exchange_i32(ptr: ptr, value: i32): i32
// Atomically exchanges *ptr with value and returns the OLD value
Value builtin_atomic_exchange_i32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_exchange_i32() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_exchange_i32() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_exchange_i32() expects an integer as second argument");
        return val_null();
    }

    _Atomic int32_t *ptr = (_Atomic int32_t *)args[0].as.as_ptr;
    int32_t value = value_to_int(args[1]);
    int32_t old = atomic_exchange(ptr, value);
    return val_i32(old);
}

// ========== i64 ATOMIC OPERATIONS ==========

// atomic_load_i64(ptr: ptr): i64
Value builtin_atomic_load_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "atomic_load_i64() expects 1 argument (pointer)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_load_i64() expects a pointer argument");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t value = atomic_load(ptr);
    return val_i64(value);
}

// atomic_store_i64(ptr: ptr, value: i64): null
Value builtin_atomic_store_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_store_i64() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_store_i64() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_store_i64() expects an integer as second argument");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t value = value_to_int64(args[1]);
    atomic_store(ptr, value);
    return val_null();
}

// atomic_add_i64(ptr: ptr, value: i64): i64
Value builtin_atomic_add_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_add_i64() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_add_i64() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_add_i64() expects an integer as second argument");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t value = value_to_int64(args[1]);
    int64_t old = atomic_fetch_add(ptr, value);
    return val_i64(old);
}

// atomic_sub_i64(ptr: ptr, value: i64): i64
Value builtin_atomic_sub_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_sub_i64() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_sub_i64() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_sub_i64() expects an integer as second argument");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t value = value_to_int64(args[1]);
    int64_t old = atomic_fetch_sub(ptr, value);
    return val_i64(old);
}

// atomic_and_i64(ptr: ptr, value: i64): i64
Value builtin_atomic_and_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_and_i64() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_and_i64() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_and_i64() expects an integer as second argument");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t value = value_to_int64(args[1]);
    int64_t old = atomic_fetch_and(ptr, value);
    return val_i64(old);
}

// atomic_or_i64(ptr: ptr, value: i64): i64
Value builtin_atomic_or_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_or_i64() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_or_i64() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_or_i64() expects an integer as second argument");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t value = value_to_int64(args[1]);
    int64_t old = atomic_fetch_or(ptr, value);
    return val_i64(old);
}

// atomic_xor_i64(ptr: ptr, value: i64): i64
Value builtin_atomic_xor_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_xor_i64() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_xor_i64() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_xor_i64() expects an integer as second argument");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t value = value_to_int64(args[1]);
    int64_t old = atomic_fetch_xor(ptr, value);
    return val_i64(old);
}

// atomic_cas_i64(ptr: ptr, expected: i64, desired: i64): bool
Value builtin_atomic_cas_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "atomic_cas_i64() expects 3 arguments (pointer, expected, desired)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_cas_i64() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_cas_i64() expects an integer as second argument (expected)");
        return val_null();
    }

    if (!is_integer(args[2])) {
        runtime_error(ctx, "atomic_cas_i64() expects an integer as third argument (desired)");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t expected = value_to_int64(args[1]);
    int64_t desired = value_to_int64(args[2]);

    _Bool success = atomic_compare_exchange_strong(ptr, &expected, desired);
    return val_bool(success);
}

// atomic_exchange_i64(ptr: ptr, value: i64): i64
Value builtin_atomic_exchange_i64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "atomic_exchange_i64() expects 2 arguments (pointer, value)");
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "atomic_exchange_i64() expects a pointer as first argument");
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "atomic_exchange_i64() expects an integer as second argument");
        return val_null();
    }

    _Atomic int64_t *ptr = (_Atomic int64_t *)args[0].as.as_ptr;
    int64_t value = value_to_int64(args[1]);
    int64_t old = atomic_exchange(ptr, value);
    return val_i64(old);
}

// ========== MEMORY FENCE ==========

// atomic_fence(): null
// Full memory barrier (sequential consistency)
Value builtin_atomic_fence(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)num_args;
    (void)ctx;

    atomic_thread_fence(memory_order_seq_cst);
    return val_null();
}
