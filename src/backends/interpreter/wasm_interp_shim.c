/*
 * Hemlock Interpreter - WASM Shim Layer
 *
 * Provides stub implementations for functions that are excluded from the
 * WASM build due to unavailable system libraries (libffi, OpenSSL).
 *
 * This file is ONLY compiled when targeting Emscripten (__EMSCRIPTEN__).
 * On native builds, the real implementations in ffi.c and crypto.c are used.
 */

#ifdef __EMSCRIPTEN__

#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * FFI STUBS (replaces ffi.c)
 *
 * libffi and dlopen are not available in WASM. All FFI operations return
 * errors explaining the limitation. Import statements for shared libraries
 * are silently ignored (with a warning) rather than crashing.
 * ======================================================================== */

void ffi_init(void) {
    /* No-op in WASM - no shared libraries to initialize */
}

void ffi_cleanup(void) {
    /* No-op in WASM */
}

void execute_import_ffi(Stmt *stmt, ExecutionContext *ctx) {
    (void)stmt;
    /* In WASM, FFI imports are not available - warn but don't crash */
    fprintf(stderr, "Warning: FFI import ignored in WebAssembly (no shared library support)\n");
    (void)ctx;
}

void execute_extern_fn(Stmt *stmt, Environment *env, ExecutionContext *ctx) {
    (void)stmt;
    (void)env;
    runtime_error(ctx, "extern fn is not available in WebAssembly (no FFI support)");
}

Value ffi_call_function(FFIFunction *func, Value *args, int num_args, ExecutionContext *ctx) {
    (void)func;
    (void)args;
    (void)num_args;
    runtime_error(ctx, "FFI calls are not available in WebAssembly");
    return val_null();
}

void ffi_free_function(FFIFunction *func) {
    if (func) {
        free(func->name);
        free(func->lib_path);
        free(func->hemlock_params);
        free(func);
    }
}

FFICallback* ffi_create_callback(Function *fn, Type **param_types, int num_params,
                                  Type *return_type, ExecutionContext *ctx) {
    (void)fn;
    (void)param_types;
    (void)num_params;
    (void)return_type;
    runtime_error(ctx, "FFI callbacks are not available in WebAssembly");
    return NULL;
}

void ffi_free_callback(FFICallback *cb) {
    (void)cb;
}

void* ffi_callback_get_ptr(FFICallback *cb) {
    (void)cb;
    return NULL;
}

int ffi_free_callback_by_ptr(void *code_ptr) {
    (void)code_ptr;
    return 0;
}

FFIStructType* ffi_register_struct(const char *name, char **field_names,
                                    Type **field_types, int num_fields) {
    (void)name;
    (void)field_names;
    (void)field_types;
    (void)num_fields;
    fprintf(stderr, "Warning: FFI struct registration ignored in WebAssembly\n");
    return NULL;
}

FFIStructType* ffi_lookup_struct(const char *name) {
    (void)name;
    return NULL;
}

void ffi_struct_cleanup(void) {
    /* No-op */
}

Type* type_from_string(const char *name) {
    if (!name) return NULL;

    Type *t = malloc(sizeof(Type));
    memset(t, 0, sizeof(Type));

    if (strcmp(name, "i8") == 0) { t->kind = TYPE_I8; }
    else if (strcmp(name, "i16") == 0) { t->kind = TYPE_I16; }
    else if (strcmp(name, "i32") == 0 || strcmp(name, "integer") == 0) { t->kind = TYPE_I32; }
    else if (strcmp(name, "i64") == 0) { t->kind = TYPE_I64; }
    else if (strcmp(name, "u8") == 0 || strcmp(name, "byte") == 0) { t->kind = TYPE_U8; }
    else if (strcmp(name, "u16") == 0) { t->kind = TYPE_U16; }
    else if (strcmp(name, "u32") == 0) { t->kind = TYPE_U32; }
    else if (strcmp(name, "u64") == 0) { t->kind = TYPE_U64; }
    else if (strcmp(name, "f32") == 0) { t->kind = TYPE_F32; }
    else if (strcmp(name, "f64") == 0 || strcmp(name, "number") == 0) { t->kind = TYPE_F64; }
    else if (strcmp(name, "bool") == 0) { t->kind = TYPE_BOOL; }
    else if (strcmp(name, "string") == 0) { t->kind = TYPE_STRING; }
    else if (strcmp(name, "ptr") == 0) { t->kind = TYPE_PTR; }
    else if (strcmp(name, "void") == 0) { t->kind = TYPE_VOID; }
    else { t->kind = TYPE_INFER; }

    return t;
}

/* ========================================================================
 * CRYPTO STUBS (replaces builtins/crypto.c)
 *
 * OpenSSL is not available in WASM builds. Crypto builtins return errors.
 * The @stdlib/hash module provides pure-Hemlock hash alternatives.
 * ======================================================================== */

Value builtin_sha256(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)num_args;
    runtime_error(ctx, "__sha256() is not available in WebAssembly (no OpenSSL)");
    return val_null();
}

Value builtin_sha512(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)num_args;
    runtime_error(ctx, "__sha512() is not available in WebAssembly (no OpenSSL)");
    return val_null();
}

Value builtin_md5(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)num_args;
    runtime_error(ctx, "__md5() is not available in WebAssembly (no OpenSSL)");
    return val_null();
}

Value builtin_ecdsa_generate_key(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)num_args;
    runtime_error(ctx, "ecdsa_generate_key() is not available in WebAssembly (no OpenSSL)");
    return val_null();
}

Value builtin_ecdsa_free_key(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)num_args;
    runtime_error(ctx, "ecdsa_free_key() is not available in WebAssembly (no OpenSSL)");
    return val_null();
}

Value builtin_ecdsa_sign(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)num_args;
    runtime_error(ctx, "ecdsa_sign() is not available in WebAssembly (no OpenSSL)");
    return val_null();
}

Value builtin_ecdsa_verify(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)num_args;
    runtime_error(ctx, "ecdsa_verify() is not available in WebAssembly (no OpenSSL)");
    return val_null();
}

#endif /* __EMSCRIPTEN__ */
