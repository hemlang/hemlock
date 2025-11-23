#include "internal.h"
#include <ffi.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ========== FFI DATA STRUCTURES ==========

// Loaded library structure
struct FFILibrary {
    char *path;              // Library file path
    void *handle;            // dlopen() handle
};

// Global FFI state
typedef struct {
    FFILibrary **libraries;  // All loaded libraries
    int num_libraries;
    int libraries_capacity;
    FFILibrary *current_lib; // Most recently imported library
} FFIState;

static FFIState g_ffi_state = {NULL, 0, 0, NULL};

// ========== LIBRARY LOADING ==========

FFILibrary* ffi_load_library(const char *path, ExecutionContext *ctx) {
    // Check if library is already loaded
    for (int i = 0; i < g_ffi_state.num_libraries; i++) {
        if (strcmp(g_ffi_state.libraries[i]->path, path) == 0) {
            return g_ffi_state.libraries[i];
        }
    }

    // Open library with RTLD_LAZY (resolve symbols on first call)
    void *handle = dlopen(path, RTLD_LAZY);
    if (handle == NULL) {
        ctx->exception_state.is_throwing = 1;
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to load library '%s': %s", path, dlerror());
        ctx->exception_state.exception_value = val_string(error_msg);
        return NULL;
    }

    FFILibrary *lib = malloc(sizeof(FFILibrary));
    lib->path = strdup(path);
    lib->handle = handle;

    // Add to library list
    if (g_ffi_state.num_libraries >= g_ffi_state.libraries_capacity) {
        g_ffi_state.libraries_capacity = g_ffi_state.libraries_capacity == 0 ? 4 : g_ffi_state.libraries_capacity * 2;
        g_ffi_state.libraries = realloc(g_ffi_state.libraries, sizeof(FFILibrary*) * g_ffi_state.libraries_capacity);
    }
    g_ffi_state.libraries[g_ffi_state.num_libraries++] = lib;

    return lib;
}

void ffi_close_library(FFILibrary *lib) {
    if (lib->handle != NULL) {
        dlclose(lib->handle);
    }
    free(lib->path);
    free(lib);
}

// ========== TYPE MAPPING ==========

ffi_type* hemlock_type_to_ffi_type(Type *type) {
    if (type == NULL) {
        // NULL type means void
        return &ffi_type_void;
    }

    switch (type->kind) {
        case TYPE_I8:     return &ffi_type_sint8;
        case TYPE_I16:    return &ffi_type_sint16;
        case TYPE_I32:    return &ffi_type_sint32;
        case TYPE_I64:    return &ffi_type_sint64;
        case TYPE_U8:     return &ffi_type_uint8;
        case TYPE_U16:    return &ffi_type_uint16;
        case TYPE_U32:    return &ffi_type_uint32;
        case TYPE_U64:    return &ffi_type_uint64;
        case TYPE_F32:    return &ffi_type_float;
        case TYPE_F64:    return &ffi_type_double;
        case TYPE_PTR:    return &ffi_type_pointer;
        case TYPE_STRING: return &ffi_type_pointer;  // string → char*
        case TYPE_BOOL:   return &ffi_type_sint;
        case TYPE_VOID:   return &ffi_type_void;
        default:
            fprintf(stderr, "Error: Unsupported FFI type: %d\n", type->kind);
            exit(1);
    }
}

// ========== VALUE CONVERSION ==========

void* hemlock_to_c_value(Value val, Type *type) {
    size_t size = 0;
    ffi_type *ffi_t = hemlock_type_to_ffi_type(type);

    // Determine size
    if (ffi_t == &ffi_type_sint8 || ffi_t == &ffi_type_uint8) size = 1;
    else if (ffi_t == &ffi_type_sint16 || ffi_t == &ffi_type_uint16) size = 2;
    else if (ffi_t == &ffi_type_sint32 || ffi_t == &ffi_type_uint32) size = 4;
    else if (ffi_t == &ffi_type_sint64 || ffi_t == &ffi_type_uint64) size = 8;
    else if (ffi_t == &ffi_type_sint || ffi_t == &ffi_type_uint) size = sizeof(int);
    else if (ffi_t == &ffi_type_float) size = sizeof(float);
    else if (ffi_t == &ffi_type_double) size = sizeof(double);
    else if (ffi_t == &ffi_type_pointer) size = sizeof(void*);
    else {
        fprintf(stderr, "Error: Cannot determine size for FFI type\n");
        exit(1);
    }

    void *storage = malloc(size);

    switch (type->kind) {
        case TYPE_I8:
            *(int8_t*)storage = val.as.as_i8;
            break;
        case TYPE_I16:
            *(int16_t*)storage = val.as.as_i16;
            break;
        case TYPE_I32:
            *(int32_t*)storage = val.as.as_i32;
            break;
        case TYPE_I64:
            *(int64_t*)storage = val.as.as_i64;
            break;
        case TYPE_U8:
            *(uint8_t*)storage = val.as.as_u8;
            break;
        case TYPE_U16:
            *(uint16_t*)storage = val.as.as_u16;
            break;
        case TYPE_U32:
            *(uint32_t*)storage = val.as.as_u32;
            break;
        case TYPE_U64:
            *(uint64_t*)storage = val.as.as_u64;
            break;
        case TYPE_F32:
            *(float*)storage = val.as.as_f32;
            break;
        case TYPE_F64:
            *(double*)storage = val.as.as_f64;
            break;
        case TYPE_PTR:
            *(void**)storage = val.as.as_ptr;
            break;
        case TYPE_STRING:
            *(char**)storage = val.as.as_string->data;
            break;
        case TYPE_BOOL:
            *(int*)storage = val.as.as_bool ? 1 : 0;
            break;
        default:
            fprintf(stderr, "Error: Cannot convert Hemlock type to C: %d\n", type->kind);
            exit(1);
    }

    return storage;
}

Value c_to_hemlock_value(void *c_value, Type *type) {
    if (type == NULL || type->kind == TYPE_VOID) {
        return val_null();
    }

    switch (type->kind) {
        case TYPE_I8:
            return val_i8(*(int8_t*)c_value);
        case TYPE_I16:
            return val_i16(*(int16_t*)c_value);
        case TYPE_I32:
            return val_i32(*(int32_t*)c_value);
        case TYPE_I64:
            return val_i64(*(int64_t*)c_value);
        case TYPE_U8:
            return val_u8(*(uint8_t*)c_value);
        case TYPE_U16:
            return val_u16(*(uint16_t*)c_value);
        case TYPE_U32:
            return val_u32(*(uint32_t*)c_value);
        case TYPE_U64:
            return val_u64(*(uint64_t*)c_value);
        case TYPE_F32:
            return val_f32(*(float*)c_value);
        case TYPE_F64:
            return val_f64(*(double*)c_value);
        case TYPE_PTR:
            return val_ptr(*(void**)c_value);
        case TYPE_BOOL:
            return val_bool(*(int*)c_value != 0);
        case TYPE_STRING: {
            char *str = *(char**)c_value;
            if (str == NULL) {
                return val_null();
            }
            // Create a Hemlock string from the C string
            return val_string(str);
        }
        default:
            fprintf(stderr, "Error: Cannot convert C type to Hemlock: %d\n", type->kind);
            exit(1);
    }
}

// ========== FUNCTION DECLARATION ==========

FFIFunction* ffi_declare_function(
    FFILibrary *lib,
    const char *name,
    Type **param_types,
    int num_params,
    Type *return_type,
    ExecutionContext *ctx
) {
    // Look up symbol
    dlerror();  // Clear any existing error
    void *func_ptr = dlsym(lib->handle, name);
    char *error_msg = dlerror();
    if (error_msg != NULL) {
        ctx->exception_state.is_throwing = 1;
        char err[512];
        snprintf(err, sizeof(err), "Function '%s' not found in '%s': %s", name, lib->path, error_msg);
        ctx->exception_state.exception_value = val_string(err);
        return NULL;
    }

    // Create FFI function
    FFIFunction *func = malloc(sizeof(FFIFunction));
    func->name = strdup(name);
    func->func_ptr = func_ptr;
    func->hemlock_params = param_types;
    func->num_params = num_params;
    func->hemlock_return = return_type;

    // Allocate and store ffi_cif
    ffi_cif *cif = malloc(sizeof(ffi_cif));
    func->cif = cif;

    // Convert types to libffi
    ffi_type **arg_types = malloc(sizeof(ffi_type*) * num_params);
    for (int i = 0; i < num_params; i++) {
        arg_types[i] = hemlock_type_to_ffi_type(param_types[i]);
    }
    func->arg_types = (void**)arg_types;

    ffi_type *return_type_ffi = hemlock_type_to_ffi_type(return_type);
    func->return_type = return_type_ffi;

    // Prepare libffi call interface
    ffi_status status = ffi_prep_cif(
        cif,
        FFI_DEFAULT_ABI,
        num_params,
        return_type_ffi,
        arg_types
    );

    if (status != FFI_OK) {
        ctx->exception_state.is_throwing = 1;
        char err[512];
        snprintf(err, sizeof(err), "Failed to prepare FFI call interface for '%s'", name);
        ctx->exception_state.exception_value = val_string(err);
        free(func->arg_types);
        free(func->name);
        free(func);
        return NULL;
    }

    return func;
}

void ffi_free_function(FFIFunction *func) {
    if (func == NULL) return;
    free(func->name);
    if (func->cif) free(func->cif);
    if (func->arg_types) free(func->arg_types);
    // Note: hemlock_params and hemlock_return are managed by AST
    free(func);
}

// ========== FUNCTION INVOCATION ==========

Value ffi_call_function(FFIFunction *func, Value *args, int num_args, ExecutionContext *ctx) {
    // Validate argument count
    if (num_args != func->num_params) {
        ctx->exception_state.is_throwing = 1;
        char err[256];
        snprintf(err, sizeof(err), "FFI function '%s' expects %d arguments, got %d",
                 func->name, func->num_params, num_args);
        ctx->exception_state.exception_value = val_string(err);
        return val_null();
    }

    // Convert Hemlock values to C values
    void **arg_values = malloc(sizeof(void*) * num_args);
    void **arg_storage = malloc(sizeof(void*) * num_args);

    for (int i = 0; i < num_args; i++) {
        arg_storage[i] = hemlock_to_c_value(args[i], func->hemlock_params[i]);
        arg_values[i] = arg_storage[i];
    }

    // Prepare return value storage
    void *return_storage = NULL;
    if (func->hemlock_return != NULL && func->hemlock_return->kind != TYPE_VOID) {
        // Allocate enough space for the return value
        size_t return_size = 8;  // Default to pointer size
        ffi_type *ret_type = (ffi_type*)func->return_type;
        if (ret_type == &ffi_type_sint8 || ret_type == &ffi_type_uint8) return_size = 1;
        else if (ret_type == &ffi_type_sint16 || ret_type == &ffi_type_uint16) return_size = 2;
        else if (ret_type == &ffi_type_sint32 || ret_type == &ffi_type_uint32) return_size = 4;
        else if (ret_type == &ffi_type_float) return_size = sizeof(float);
        else if (ret_type == &ffi_type_double) return_size = sizeof(double);

        return_storage = malloc(return_size);
    }

    // Call via libffi
    ffi_cif *cif = (ffi_cif*)func->cif;
    ffi_call(cif, FFI_FN(func->func_ptr), return_storage, arg_values);

    // Convert return value
    Value result;
    if (func->hemlock_return == NULL || func->hemlock_return->kind == TYPE_VOID) {
        result = val_null();
    } else {
        result = c_to_hemlock_value(return_storage, func->hemlock_return);
    }

    // Cleanup
    for (int i = 0; i < num_args; i++) {
        free(arg_storage[i]);
    }
    free(arg_values);
    free(arg_storage);
    if (return_storage != NULL) {
        free(return_storage);
    }

    return result;
}

// ========== PUBLIC API ==========

void ffi_init() {
    g_ffi_state.libraries = NULL;
    g_ffi_state.num_libraries = 0;
    g_ffi_state.libraries_capacity = 0;
    g_ffi_state.current_lib = NULL;
}

void ffi_cleanup() {
    for (int i = 0; i < g_ffi_state.num_libraries; i++) {
        ffi_close_library(g_ffi_state.libraries[i]);
    }
    free(g_ffi_state.libraries);
    g_ffi_state.libraries = NULL;
    g_ffi_state.num_libraries = 0;
    g_ffi_state.libraries_capacity = 0;
    g_ffi_state.current_lib = NULL;
}

void execute_import_ffi(Stmt *stmt, ExecutionContext *ctx) {
    const char *library_path = stmt->as.import_ffi.library_path;
    FFILibrary *lib = ffi_load_library(library_path, ctx);
    if (lib != NULL) {
        g_ffi_state.current_lib = lib;
    }
}

void execute_extern_fn(Stmt *stmt, Environment *env, ExecutionContext *ctx) {
    if (g_ffi_state.current_lib == NULL) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("No library imported before extern declaration");
        return;
    }

    const char *function_name = stmt->as.extern_fn.function_name;
    Type **param_types = stmt->as.extern_fn.param_types;
    int num_params = stmt->as.extern_fn.num_params;
    Type *return_type = stmt->as.extern_fn.return_type;

    FFIFunction *func = ffi_declare_function(
        g_ffi_state.current_lib,
        function_name,
        param_types,
        num_params,
        return_type,
        ctx
    );

    if (func != NULL) {
        // Create a value to store the FFI function
        Value ffi_val = {0};  // Zero-initialize entire struct
        ffi_val.type = VAL_FFI_FUNCTION;
        ffi_val.as.as_ffi_function = func;
        env_define(env, function_name, ffi_val, 0, ctx);
    }
}
