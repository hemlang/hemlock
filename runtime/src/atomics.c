/*
 * Hemlock Runtime Library - Atomic Operations
 *
 * Implements atomic operations for lock-free concurrent programming.
 * All operations use sequential consistency (memory_order_seq_cst).
 */

#include "../include/hemlock_runtime.h"
#include <stdatomic.h>

// Helper to convert HmlValue to int32_t
static int32_t value_to_i32(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:  return (int32_t)val.as.as_i8;
        case HML_VAL_I16: return (int32_t)val.as.as_i16;
        case HML_VAL_I32: return val.as.as_i32;
        case HML_VAL_I64: return (int32_t)val.as.as_i64;
        case HML_VAL_U8:  return (int32_t)val.as.as_u8;
        case HML_VAL_U16: return (int32_t)val.as.as_u16;
        case HML_VAL_U32: return (int32_t)val.as.as_u32;
        case HML_VAL_U64: return (int32_t)val.as.as_u64;
        default:
            return 0;
    }
}

// Helper to convert HmlValue to int64_t
static int64_t value_to_i64(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:  return (int64_t)val.as.as_i8;
        case HML_VAL_I16: return (int64_t)val.as.as_i16;
        case HML_VAL_I32: return (int64_t)val.as.as_i32;
        case HML_VAL_I64: return val.as.as_i64;
        case HML_VAL_U8:  return (int64_t)val.as.as_u8;
        case HML_VAL_U16: return (int64_t)val.as.as_u16;
        case HML_VAL_U32: return (int64_t)val.as.as_u32;
        case HML_VAL_U64: return (int64_t)val.as.as_u64;
        default:
            return 0;
    }
}

// ========== i32 ATOMIC OPERATIONS ==========

HmlValue hml_atomic_load_i32(HmlValue ptr) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_load_i32() expects a pointer argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t value = atomic_load(p);
    return hml_val_i32(value);
}

HmlValue hml_atomic_store_i32(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_store_i32() expects a pointer as first argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t val = value_to_i32(value);
    atomic_store(p, val);
    return hml_val_null();
}

HmlValue hml_atomic_add_i32(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_add_i32() expects a pointer as first argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t val = value_to_i32(value);
    int32_t old = atomic_fetch_add(p, val);
    return hml_val_i32(old);
}

HmlValue hml_atomic_sub_i32(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_sub_i32() expects a pointer as first argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t val = value_to_i32(value);
    int32_t old = atomic_fetch_sub(p, val);
    return hml_val_i32(old);
}

HmlValue hml_atomic_and_i32(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_and_i32() expects a pointer as first argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t val = value_to_i32(value);
    int32_t old = atomic_fetch_and(p, val);
    return hml_val_i32(old);
}

HmlValue hml_atomic_or_i32(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_or_i32() expects a pointer as first argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t val = value_to_i32(value);
    int32_t old = atomic_fetch_or(p, val);
    return hml_val_i32(old);
}

HmlValue hml_atomic_xor_i32(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_xor_i32() expects a pointer as first argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t val = value_to_i32(value);
    int32_t old = atomic_fetch_xor(p, val);
    return hml_val_i32(old);
}

HmlValue hml_atomic_cas_i32(HmlValue ptr, HmlValue expected, HmlValue desired) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_cas_i32() expects a pointer as first argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t exp = value_to_i32(expected);
    int32_t des = value_to_i32(desired);
    _Bool success = atomic_compare_exchange_strong(p, &exp, des);
    return hml_val_bool(success);
}

HmlValue hml_atomic_exchange_i32(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_exchange_i32() expects a pointer as first argument");
    }
    _Atomic int32_t *p = (_Atomic int32_t *)ptr.as.as_ptr;
    int32_t val = value_to_i32(value);
    int32_t old = atomic_exchange(p, val);
    return hml_val_i32(old);
}

// ========== i64 ATOMIC OPERATIONS ==========

HmlValue hml_atomic_load_i64(HmlValue ptr) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_load_i64() expects a pointer argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t value = atomic_load(p);
    return hml_val_i64(value);
}

HmlValue hml_atomic_store_i64(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_store_i64() expects a pointer as first argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t val = value_to_i64(value);
    atomic_store(p, val);
    return hml_val_null();
}

HmlValue hml_atomic_add_i64(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_add_i64() expects a pointer as first argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t val = value_to_i64(value);
    int64_t old = atomic_fetch_add(p, val);
    return hml_val_i64(old);
}

HmlValue hml_atomic_sub_i64(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_sub_i64() expects a pointer as first argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t val = value_to_i64(value);
    int64_t old = atomic_fetch_sub(p, val);
    return hml_val_i64(old);
}

HmlValue hml_atomic_and_i64(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_and_i64() expects a pointer as first argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t val = value_to_i64(value);
    int64_t old = atomic_fetch_and(p, val);
    return hml_val_i64(old);
}

HmlValue hml_atomic_or_i64(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_or_i64() expects a pointer as first argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t val = value_to_i64(value);
    int64_t old = atomic_fetch_or(p, val);
    return hml_val_i64(old);
}

HmlValue hml_atomic_xor_i64(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_xor_i64() expects a pointer as first argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t val = value_to_i64(value);
    int64_t old = atomic_fetch_xor(p, val);
    return hml_val_i64(old);
}

HmlValue hml_atomic_cas_i64(HmlValue ptr, HmlValue expected, HmlValue desired) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_cas_i64() expects a pointer as first argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t exp = value_to_i64(expected);
    int64_t des = value_to_i64(desired);
    _Bool success = atomic_compare_exchange_strong(p, &exp, des);
    return hml_val_bool(success);
}

HmlValue hml_atomic_exchange_i64(HmlValue ptr, HmlValue value) {
    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("atomic_exchange_i64() expects a pointer as first argument");
    }
    _Atomic int64_t *p = (_Atomic int64_t *)ptr.as.as_ptr;
    int64_t val = value_to_i64(value);
    int64_t old = atomic_exchange(p, val);
    return hml_val_i64(old);
}

// ========== MEMORY FENCE ==========

void hml_atomic_fence(void) {
    atomic_thread_fence(memory_order_seq_cst);
}

// ========== BUILTIN WRAPPERS ==========

HmlValue hml_builtin_atomic_load_i32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_atomic_load_i32(ptr);
}

HmlValue hml_builtin_atomic_store_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_store_i32(ptr, value);
}

HmlValue hml_builtin_atomic_add_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_add_i32(ptr, value);
}

HmlValue hml_builtin_atomic_sub_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_sub_i32(ptr, value);
}

HmlValue hml_builtin_atomic_and_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_and_i32(ptr, value);
}

HmlValue hml_builtin_atomic_or_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_or_i32(ptr, value);
}

HmlValue hml_builtin_atomic_xor_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_xor_i32(ptr, value);
}

HmlValue hml_builtin_atomic_cas_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue expected, HmlValue desired) {
    (void)env;
    return hml_atomic_cas_i32(ptr, expected, desired);
}

HmlValue hml_builtin_atomic_exchange_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_exchange_i32(ptr, value);
}

HmlValue hml_builtin_atomic_load_i64(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;
    return hml_atomic_load_i64(ptr);
}

HmlValue hml_builtin_atomic_store_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_store_i64(ptr, value);
}

HmlValue hml_builtin_atomic_add_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_add_i64(ptr, value);
}

HmlValue hml_builtin_atomic_sub_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_sub_i64(ptr, value);
}

HmlValue hml_builtin_atomic_and_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_and_i64(ptr, value);
}

HmlValue hml_builtin_atomic_or_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_or_i64(ptr, value);
}

HmlValue hml_builtin_atomic_xor_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_xor_i64(ptr, value);
}

HmlValue hml_builtin_atomic_cas_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue expected, HmlValue desired) {
    (void)env;
    return hml_atomic_cas_i64(ptr, expected, desired);
}

HmlValue hml_builtin_atomic_exchange_i64(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;
    return hml_atomic_exchange_i64(ptr, value);
}

HmlValue hml_builtin_atomic_fence(HmlClosureEnv *env) {
    (void)env;
    hml_atomic_fence();
    return hml_val_null();
}
