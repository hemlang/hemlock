/*
 * Hemlock Runtime Library - FFI (Foreign Function Interface)
 *
 * Dynamic loading, function calls, struct marshaling, and callbacks.
 */

#include "builtins_internal.h"

// ========== FFI HELPERS ==========

// Helper to check if a file exists (used on macOS for library path translation)
#ifdef __APPLE__
static int ffi_file_exists(const char *path) {
    return access(path, F_OK) == 0;
}
#endif

// Translate Linux library names to macOS equivalents (on macOS only)
static const char* translate_library_path(const char *path) {
#ifdef __APPLE__
    // libc.so.6 -> libSystem.B.dylib (macOS system C library)
    if (strcmp(path, "libc.so.6") == 0) {
        return "libSystem.B.dylib";
    }
    // libm.so.6 -> libSystem.B.dylib (math is part of libSystem on macOS)
    if (strcmp(path, "libm.so.6") == 0) {
        return "libSystem.B.dylib";
    }
    // libcrypto.so.3 -> Homebrew OpenSSL
    if (strcmp(path, "libcrypto.so.3") == 0 || strcmp(path, "libcrypto.dylib") == 0) {
        if (ffi_file_exists("/opt/homebrew/opt/openssl@3/lib/libcrypto.dylib")) {
            return "/opt/homebrew/opt/openssl@3/lib/libcrypto.dylib";
        }
        if (ffi_file_exists("/usr/local/opt/openssl@3/lib/libcrypto.dylib")) {
            return "/usr/local/opt/openssl@3/lib/libcrypto.dylib";
        }
        return "libcrypto.dylib";
    }
    // Generic .so to .dylib translation
    static char translated[512];
    size_t len = strlen(path);
    // Handle .so.N pattern (e.g., libfoo.so.6)
    const char *so_pos = strstr(path, ".so.");
    if (so_pos) {
        size_t base_len = so_pos - path;
        if (base_len < sizeof(translated) - 7) {
            strncpy(translated, path, base_len);
            strcpy(translated + base_len, ".dylib");
            return translated;
        }
    }
    // Handle plain .so (e.g., libfoo.so)
    if (len > 3 && strcmp(path + len - 3, ".so") == 0) {
        if (len < sizeof(translated) - 4) {
            strncpy(translated, path, len - 3);
            strcpy(translated + len - 3, ".dylib");
            return translated;
        }
    }
#endif
    return path;  // No translation on Linux or if no pattern matched
}

// SECURITY: Validate FFI library path for obvious security issues
static const char* validate_ffi_path(const char *path) {
    if (!path || path[0] == '\0') {
        return "Empty library path";
    }

    // Check for directory traversal
    if (strstr(path, "..")) {
        return "Library path contains directory traversal (..)";
    }

    // Warn about world-writable locations
    if (strncmp(path, "/tmp/", 5) == 0 ||
        strncmp(path, "/var/tmp/", 9) == 0 ||
        strncmp(path, "/dev/shm/", 9) == 0) {
        fprintf(stderr, "Warning: Loading FFI library from world-writable location: %s\n", path);
        fprintf(stderr, "         This is a security risk - libraries in /tmp could be malicious\n");
    }

    // Check for suspicious patterns
    if (strstr(path, "/../") || strstr(path, "/./")) {
        return "Library path contains suspicious directory references";
    }

    return NULL;  // Path is acceptable
}

// ========== FFI CORE FUNCTIONS ==========

HmlValue hml_ffi_load(const char *path) {
    // Translate library path for cross-platform compatibility (e.g., .so -> .dylib on macOS)
    const char *actual_path = translate_library_path(path);

    // SECURITY: Validate library path before loading
    const char *validation_error = validate_ffi_path(actual_path);
    if (validation_error) {
        hml_runtime_error("FFI security error: %s (path: %s)", validation_error, actual_path);
    }

    void *handle = dlopen(actual_path, RTLD_LAZY);
    if (!handle) {
        hml_runtime_error("Failed to load library '%s': %s", path, dlerror());
    }
    return hml_val_ptr(handle);
}

void hml_ffi_close(HmlValue lib) {
    if (lib.type == HML_VAL_PTR && lib.as.as_ptr) {
        dlclose(lib.as.as_ptr);
    }
}

void* hml_ffi_sym(HmlValue lib, const char *name) {
    if (lib.type != HML_VAL_PTR || !lib.as.as_ptr) {
        // Return NULL for lazy resolution - error will be thrown when function is called
        return NULL;
    }
    dlerror(); // Clear errors
    void *sym = dlsym(lib.as.as_ptr, name);
    // Don't throw here - let the caller handle NULL for lazy resolution
    // This allows modules to export many extern functions without requiring
    // all symbols to exist in the library
    return sym;
}

// ========== FFI TYPE CONVERSION ==========

// Convert HmlFFIType to libffi type
static ffi_type* hml_ffi_type_to_ffi(HmlFFIType type) {
    switch (type) {
        case HML_FFI_VOID:   return &ffi_type_void;
        case HML_FFI_I8:     return &ffi_type_sint8;
        case HML_FFI_I16:    return &ffi_type_sint16;
        case HML_FFI_I32:    return &ffi_type_sint32;
        case HML_FFI_I64:    return &ffi_type_sint64;
        case HML_FFI_U8:     return &ffi_type_uint8;
        case HML_FFI_U16:    return &ffi_type_uint16;
        case HML_FFI_U32:    return &ffi_type_uint32;
        case HML_FFI_U64:    return &ffi_type_uint64;
        case HML_FFI_F32:    return &ffi_type_float;
        case HML_FFI_F64:    return &ffi_type_double;
        case HML_FFI_PTR:    return &ffi_type_pointer;
        case HML_FFI_STRING: return &ffi_type_pointer;
        default:
            hml_runtime_error("Unknown FFI type: %d", type);
    }
}

// Get the size of an FFI type for proper argument storage allocation
// This is critical for ARM64 where floats must use 4-byte storage
static size_t hml_ffi_type_size(HmlFFIType type) {
    switch (type) {
        case HML_FFI_I8:     return sizeof(int8_t);
        case HML_FFI_I16:    return sizeof(int16_t);
        case HML_FFI_I32:    return sizeof(int32_t);
        case HML_FFI_I64:    return sizeof(int64_t);
        case HML_FFI_U8:     return sizeof(uint8_t);
        case HML_FFI_U16:    return sizeof(uint16_t);
        case HML_FFI_U32:    return sizeof(uint32_t);
        case HML_FFI_U64:    return sizeof(uint64_t);
        case HML_FFI_F32:    return sizeof(float);
        case HML_FFI_F64:    return sizeof(double);
        case HML_FFI_PTR:    return sizeof(void*);
        case HML_FFI_STRING: return sizeof(char*);
        default:             return sizeof(void*);
    }
}

// Convert HmlValue to C value for FFI call
static void hml_value_to_ffi(HmlValue val, HmlFFIType type, void *out) {
    switch (type) {
        case HML_FFI_I8:     *(int8_t*)out = (int8_t)hml_to_i32(val); break;
        case HML_FFI_I16:    *(int16_t*)out = (int16_t)hml_to_i32(val); break;
        case HML_FFI_I32:    *(int32_t*)out = hml_to_i32(val); break;
        case HML_FFI_I64:    *(int64_t*)out = hml_to_i64(val); break;
        case HML_FFI_U8:     *(uint8_t*)out = (uint8_t)hml_to_i32(val); break;
        case HML_FFI_U16:    *(uint16_t*)out = (uint16_t)hml_to_i32(val); break;
        case HML_FFI_U32:    *(uint32_t*)out = (uint32_t)hml_to_i32(val); break;
        case HML_FFI_U64:    *(uint64_t*)out = (uint64_t)hml_to_i64(val); break;
        case HML_FFI_F32:    *(float*)out = (float)hml_to_f64(val); break;
        case HML_FFI_F64:    *(double*)out = hml_to_f64(val); break;
        case HML_FFI_PTR:
            if (val.type == HML_VAL_PTR) *(void**)out = val.as.as_ptr;
            else if (val.type == HML_VAL_BUFFER) *(void**)out = val.as.as_buffer->data;
            else *(void**)out = NULL;
            break;
        case HML_FFI_STRING:
            if (val.type == HML_VAL_STRING && val.as.as_string)
                *(char**)out = val.as.as_string->data;
            else
                *(char**)out = NULL;
            break;
        default:
            hml_runtime_error("Cannot convert to FFI type: %d", type);
    }
}

// Convert C value to HmlValue after FFI call
static HmlValue hml_ffi_to_value(void *result, HmlFFIType type) {
    switch (type) {
        case HML_FFI_VOID:   return hml_val_null();
        case HML_FFI_I8:     return hml_val_i32(*(int8_t*)result);
        case HML_FFI_I16:    return hml_val_i32(*(int16_t*)result);
        case HML_FFI_I32:    return hml_val_i32(*(int32_t*)result);
        case HML_FFI_I64:    return hml_val_i64(*(int64_t*)result);
        case HML_FFI_U8:     return hml_val_u8(*(uint8_t*)result);
        case HML_FFI_U16:    return hml_val_u16(*(uint16_t*)result);
        case HML_FFI_U32:    return hml_val_u32(*(uint32_t*)result);
        case HML_FFI_U64:    return hml_val_u64(*(uint64_t*)result);
        case HML_FFI_F32:    return hml_val_f32(*(float*)result);
        case HML_FFI_F64:    return hml_val_f64(*(double*)result);
        case HML_FFI_PTR:    return hml_val_ptr(*(void**)result);
        case HML_FFI_STRING: {
            char *s = *(char**)result;
            if (s) return hml_val_string(s);
            return hml_val_null();
        }
        default:
            hml_runtime_error("Cannot convert from FFI type: %d", type);
    }
}

// ========== FFI CALL ==========

HmlValue hml_ffi_call(void *func_ptr, HmlValue *args, int num_args, HmlFFIType *types) {
    if (!func_ptr) {
        hml_runtime_error("FFI call with null function pointer");
    }

    // types[0] is return type, types[1..] are arg types
    HmlFFIType return_type = types[0];

    // Prepare libffi call interface
    ffi_cif cif;
    ffi_type **arg_types = NULL;
    void **arg_values = NULL;
    void **arg_storage = NULL;

    if (num_args > 0) {
        arg_types = malloc(num_args * sizeof(ffi_type*));
        arg_values = malloc(num_args * sizeof(void*));
        arg_storage = malloc(num_args * sizeof(void*));

        for (int i = 0; i < num_args; i++) {
            arg_types[i] = hml_ffi_type_to_ffi(types[i + 1]);
            arg_storage[i] = malloc(hml_ffi_type_size(types[i + 1]));
            hml_value_to_ffi(args[i], types[i + 1], arg_storage[i]);
            arg_values[i] = arg_storage[i];
        }
    }

    ffi_type *ret_type = hml_ffi_type_to_ffi(return_type);
    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, num_args, ret_type, arg_types);

    if (status != FFI_OK) {
        hml_runtime_error("Failed to prepare FFI call");
    }

    // Call the function
    union {
        int8_t i8; int16_t i16; int32_t i32; int64_t i64;
        uint8_t u8; uint16_t u16; uint32_t u32; uint64_t u64;
        float f32; double f64;
        void *ptr;
    } result;

    ffi_call(&cif, func_ptr, &result, arg_values);

    // Convert result
    HmlValue ret = hml_ffi_to_value(&result, return_type);

    // Cleanup
    if (num_args > 0) {
        for (int i = 0; i < num_args; i++) {
            free(arg_storage[i]);
        }
        free(arg_types);
        free(arg_values);
        free(arg_storage);
    }

    return ret;
}

// ========== FFI STRUCT SUPPORT ==========

// Global struct registry
static HmlFFIStructType *g_ffi_struct_registry = NULL;

// Get ffi_type for an HmlFFIType (helper for struct field types)
static ffi_type* hml_ffi_type_to_ffi_internal(HmlFFIType type) {
    switch (type) {
        case HML_FFI_VOID:   return &ffi_type_void;
        case HML_FFI_I8:     return &ffi_type_sint8;
        case HML_FFI_I16:    return &ffi_type_sint16;
        case HML_FFI_I32:    return &ffi_type_sint32;
        case HML_FFI_I64:    return &ffi_type_sint64;
        case HML_FFI_U8:     return &ffi_type_uint8;
        case HML_FFI_U16:    return &ffi_type_uint16;
        case HML_FFI_U32:    return &ffi_type_uint32;
        case HML_FFI_U64:    return &ffi_type_uint64;
        case HML_FFI_F32:    return &ffi_type_float;
        case HML_FFI_F64:    return &ffi_type_double;
        case HML_FFI_PTR:    return &ffi_type_pointer;
        case HML_FFI_STRING: return &ffi_type_pointer;
        default:             return &ffi_type_void;
    }
}

// Get size of an HmlFFIType in bytes
static size_t hml_ffi_type_size_internal(HmlFFIType type) {
    switch (type) {
        case HML_FFI_I8:     return sizeof(int8_t);
        case HML_FFI_I16:    return sizeof(int16_t);
        case HML_FFI_I32:    return sizeof(int32_t);
        case HML_FFI_I64:    return sizeof(int64_t);
        case HML_FFI_U8:     return sizeof(uint8_t);
        case HML_FFI_U16:    return sizeof(uint16_t);
        case HML_FFI_U32:    return sizeof(uint32_t);
        case HML_FFI_U64:    return sizeof(uint64_t);
        case HML_FFI_F32:    return sizeof(float);
        case HML_FFI_F64:    return sizeof(double);
        case HML_FFI_PTR:    return sizeof(void*);
        case HML_FFI_STRING: return sizeof(char*);
        default:             return 0;
    }
}

// Lookup a registered struct type by name
HmlFFIStructType* hml_ffi_lookup_struct(const char *name) {
    if (!name) return NULL;
    HmlFFIStructType *st = g_ffi_struct_registry;
    while (st) {
        if (strcmp(st->name, name) == 0) {
            return st;
        }
        st = st->next;
    }
    return NULL;
}

// Register a struct type for FFI use
HmlFFIStructType* hml_ffi_register_struct(const char *name, const char **field_names,
                                           HmlFFIType *field_types, int num_fields) {
    // Check if already registered
    HmlFFIStructType *existing = hml_ffi_lookup_struct(name);
    if (existing) {
        return existing;
    }

    // Create the struct type
    HmlFFIStructType *st = malloc(sizeof(HmlFFIStructType));
    st->name = strdup(name);
    st->num_fields = num_fields;
    st->fields = malloc(sizeof(HmlFFIStructField) * num_fields);

    // Build ffi_type array for struct
    ffi_type **ffi_field_types = malloc(sizeof(ffi_type*) * (num_fields + 1));

    for (int i = 0; i < num_fields; i++) {
        st->fields[i].name = strdup(field_names[i]);
        st->fields[i].type = field_types[i];
        st->fields[i].size = hml_ffi_type_size_internal(field_types[i]);
        ffi_field_types[i] = hml_ffi_type_to_ffi_internal(field_types[i]);
    }
    ffi_field_types[num_fields] = NULL;  // NULL-terminate

    // Create the libffi struct type
    ffi_type *struct_ffi_type = malloc(sizeof(ffi_type));
    struct_ffi_type->size = 0;  // Let libffi compute
    struct_ffi_type->alignment = 0;  // Let libffi compute
    struct_ffi_type->type = FFI_TYPE_STRUCT;
    struct_ffi_type->elements = ffi_field_types;
    st->ffi_type = struct_ffi_type;

    // Compute offsets using libffi's layout
    ffi_cif dummy_cif;
    ffi_type *dummy_args[1] = { struct_ffi_type };
    ffi_prep_cif(&dummy_cif, FFI_DEFAULT_ABI, 1, &ffi_type_void, dummy_args);

    // Store computed size and alignment
    st->size = struct_ffi_type->size;
    st->alignment = struct_ffi_type->alignment;

    // Compute field offsets
    size_t offset = 0;
    for (int i = 0; i < num_fields; i++) {
        size_t field_align = ffi_field_types[i]->alignment;
        if (field_align > 0) {
            offset = (offset + field_align - 1) & ~(field_align - 1);
        }
        st->fields[i].offset = offset;
        offset += st->fields[i].size;
    }

    // Add to registry
    st->next = g_ffi_struct_registry;
    g_ffi_struct_registry = st;

    return st;
}

// Marshal a Hemlock object to C struct memory
void* hml_ffi_object_to_struct(HmlValue obj, HmlFFIStructType *struct_type) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("FFI struct conversion requires an object");
    }

    HmlObject *o = obj.as.as_object;
    void *struct_mem = calloc(1, struct_type->size);

    for (int i = 0; i < struct_type->num_fields; i++) {
        HmlFFIStructField *field = &struct_type->fields[i];
        void *field_ptr = (char*)struct_mem + field->offset;

        // Look up field in object
        HmlValue field_val = hml_val_null();
        for (int j = 0; j < o->num_fields; j++) {
            if (strcmp(o->field_names[j], field->name) == 0) {
                field_val = o->field_values[j];
                break;
            }
        }

        // Convert and write field value
        switch (field->type) {
            case HML_FFI_I8:
                *(int8_t*)field_ptr = (int8_t)hml_to_i32(field_val);
                break;
            case HML_FFI_I16:
                *(int16_t*)field_ptr = (int16_t)hml_to_i32(field_val);
                break;
            case HML_FFI_I32:
                *(int32_t*)field_ptr = hml_to_i32(field_val);
                break;
            case HML_FFI_I64:
                *(int64_t*)field_ptr = hml_to_i64(field_val);
                break;
            case HML_FFI_U8:
                *(uint8_t*)field_ptr = (uint8_t)hml_to_i32(field_val);
                break;
            case HML_FFI_U16:
                *(uint16_t*)field_ptr = (uint16_t)hml_to_i32(field_val);
                break;
            case HML_FFI_U32:
                *(uint32_t*)field_ptr = (uint32_t)hml_to_i32(field_val);
                break;
            case HML_FFI_U64:
                *(uint64_t*)field_ptr = (uint64_t)hml_to_i64(field_val);
                break;
            case HML_FFI_F32:
                *(float*)field_ptr = (float)hml_to_f64(field_val);
                break;
            case HML_FFI_F64:
                *(double*)field_ptr = hml_to_f64(field_val);
                break;
            case HML_FFI_PTR:
                if (field_val.type == HML_VAL_PTR) {
                    *(void**)field_ptr = field_val.as.as_ptr;
                } else if (field_val.type == HML_VAL_BUFFER) {
                    *(void**)field_ptr = field_val.as.as_buffer->data;
                } else {
                    *(void**)field_ptr = NULL;
                }
                break;
            case HML_FFI_STRING:
                if (field_val.type == HML_VAL_STRING && field_val.as.as_string) {
                    *(char**)field_ptr = field_val.as.as_string->data;
                } else {
                    *(char**)field_ptr = NULL;
                }
                break;
            default:
                break;
        }
    }

    return struct_mem;
}

// Marshal C struct memory to a Hemlock object
HmlValue hml_ffi_struct_to_object(void *struct_ptr, HmlFFIStructType *struct_type) {
    HmlValue obj = hml_val_object();

    for (int i = 0; i < struct_type->num_fields; i++) {
        HmlFFIStructField *field = &struct_type->fields[i];
        void *field_ptr = (char*)struct_ptr + field->offset;
        HmlValue field_val;

        switch (field->type) {
            case HML_FFI_I8:
                field_val = hml_val_i8(*(int8_t*)field_ptr);
                break;
            case HML_FFI_I16:
                field_val = hml_val_i16(*(int16_t*)field_ptr);
                break;
            case HML_FFI_I32:
                field_val = hml_val_i32(*(int32_t*)field_ptr);
                break;
            case HML_FFI_I64:
                field_val = hml_val_i64(*(int64_t*)field_ptr);
                break;
            case HML_FFI_U8:
                field_val = hml_val_u8(*(uint8_t*)field_ptr);
                break;
            case HML_FFI_U16:
                field_val = hml_val_u16(*(uint16_t*)field_ptr);
                break;
            case HML_FFI_U32:
                field_val = hml_val_u32(*(uint32_t*)field_ptr);
                break;
            case HML_FFI_U64:
                field_val = hml_val_u64(*(uint64_t*)field_ptr);
                break;
            case HML_FFI_F32:
                field_val = hml_val_f32(*(float*)field_ptr);
                break;
            case HML_FFI_F64:
                field_val = hml_val_f64(*(double*)field_ptr);
                break;
            case HML_FFI_PTR:
                field_val = hml_val_ptr(*(void**)field_ptr);
                break;
            case HML_FFI_STRING: {
                char *str = *(char**)field_ptr;
                if (str) {
                    field_val = hml_val_string(str);
                } else {
                    field_val = hml_val_null();
                }
                break;
            }
            default:
                field_val = hml_val_null();
                break;
        }

        // Add field to object
        hml_object_set_field(obj, field->name, field_val);
    }

    return obj;
}

// Free FFI struct registry on cleanup
void hml_ffi_struct_cleanup(void) {
    HmlFFIStructType *st = g_ffi_struct_registry;
    while (st) {
        HmlFFIStructType *next = st->next;
        free((void*)st->name);
        for (int i = 0; i < st->num_fields; i++) {
            free((void*)st->fields[i].name);
        }
        free(st->fields);
        if (st->ffi_type) {
            ffi_type *ft = (ffi_type*)st->ffi_type;
            free(ft->elements);
            free(ft);
        }
        free(st);
        st = next;
    }
    g_ffi_struct_registry = NULL;
}

// FFI call with struct support
// struct_names: array of struct type names, one for each type that is HML_FFI_STRUCT (or NULL for non-struct types)
//               struct_names[0] is for return type, struct_names[1..] for args
HmlValue hml_ffi_call_with_structs(void *func_ptr, HmlValue *args, int num_args,
                                    HmlFFIType *types, const char **struct_names) {
    if (!func_ptr) {
        hml_runtime_error("FFI call with null function pointer");
    }

    // types[0] is return type, types[1..] are arg types
    HmlFFIType return_type = types[0];
    HmlFFIStructType *return_struct = NULL;

    if (return_type == HML_FFI_STRUCT && struct_names && struct_names[0]) {
        return_struct = hml_ffi_lookup_struct(struct_names[0]);
        if (!return_struct) {
            hml_runtime_error("FFI struct type '%s' not registered", struct_names[0]);
        }
    }

    // Prepare libffi call interface
    ffi_cif cif;
    ffi_type **arg_types = NULL;
    void **arg_values = NULL;
    void **arg_storage = NULL;
    HmlFFIStructType **arg_structs = NULL;

    if (num_args > 0) {
        arg_types = malloc(num_args * sizeof(ffi_type*));
        arg_values = malloc(num_args * sizeof(void*));
        arg_storage = malloc(num_args * sizeof(void*));
        arg_structs = malloc(num_args * sizeof(HmlFFIStructType*));

        for (int i = 0; i < num_args; i++) {
            HmlFFIType arg_type = types[i + 1];
            arg_structs[i] = NULL;

            if (arg_type == HML_FFI_STRUCT && struct_names && struct_names[i + 1]) {
                // Struct argument
                arg_structs[i] = hml_ffi_lookup_struct(struct_names[i + 1]);
                if (!arg_structs[i]) {
                    hml_runtime_error("FFI struct type '%s' not registered", struct_names[i + 1]);
                }
                arg_types[i] = (ffi_type*)arg_structs[i]->ffi_type;
                arg_storage[i] = hml_ffi_object_to_struct(args[i], arg_structs[i]);
                arg_values[i] = arg_storage[i];
            } else {
                // Non-struct argument
                arg_types[i] = hml_ffi_type_to_ffi(arg_type);
                arg_storage[i] = malloc(hml_ffi_type_size(arg_type));
                hml_value_to_ffi(args[i], arg_type, arg_storage[i]);
                arg_values[i] = arg_storage[i];
            }
        }
    }

    ffi_type *ret_type;
    if (return_struct) {
        ret_type = (ffi_type*)return_struct->ffi_type;
    } else {
        ret_type = hml_ffi_type_to_ffi(return_type);
    }

    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, num_args, ret_type, arg_types);

    if (status != FFI_OK) {
        hml_runtime_error("Failed to prepare FFI call");
    }

    // Allocate space for return value
    void *result_ptr;
    if (return_struct) {
        result_ptr = calloc(1, return_struct->size);
    } else {
        result_ptr = malloc(sizeof(double) > sizeof(void*) ? sizeof(double) : sizeof(void*));
    }

    ffi_call(&cif, func_ptr, result_ptr, arg_values);

    // Convert result
    HmlValue ret;
    if (return_struct) {
        ret = hml_ffi_struct_to_object(result_ptr, return_struct);
    } else {
        ret = hml_ffi_to_value(result_ptr, return_type);
    }

    // Cleanup
    free(result_ptr);
    if (num_args > 0) {
        for (int i = 0; i < num_args; i++) {
            free(arg_storage[i]);
        }
        free(arg_types);
        free(arg_values);
        free(arg_storage);
        free(arg_structs);
    }

    return ret;
}

// ========== FFI CALLBACKS ==========

// Structure for FFI callback handle
struct HmlFFICallback {
    void *closure;           // ffi_closure (opaque)
    void *code_ptr;          // C-callable function pointer
    ffi_cif cif;             // Call interface
    ffi_type **arg_types;    // libffi argument types
    ffi_type *return_type;   // libffi return type
    HmlValue hemlock_fn;     // The Hemlock function to invoke
    HmlFFIType *param_types; // Hemlock parameter types
    HmlFFIType ret_type;     // Hemlock return type
    int num_params;
    int id;                  // Unique callback ID
};

// Global callback tracking
static HmlFFICallback **g_callbacks = NULL;
static int g_num_callbacks = 0;
static int g_callbacks_capacity = 0;
static int g_next_callback_id = 1;
static pthread_mutex_t g_callback_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declaration
HmlValue hml_call_function(HmlValue fn, HmlValue *args, int num_args);

// Convert C value to HmlValue for callback arguments
static HmlValue hml_ffi_ptr_to_value(void *ptr, HmlFFIType type) {
    switch (type) {
        case HML_FFI_I8:     return hml_val_i32(*(int8_t*)ptr);
        case HML_FFI_I16:    return hml_val_i32(*(int16_t*)ptr);
        case HML_FFI_I32:    return hml_val_i32(*(int32_t*)ptr);
        case HML_FFI_I64:    return hml_val_i64(*(int64_t*)ptr);
        case HML_FFI_U8:     return hml_val_u32(*(uint8_t*)ptr);
        case HML_FFI_U16:    return hml_val_u32(*(uint16_t*)ptr);
        case HML_FFI_U32:    return hml_val_u32(*(uint32_t*)ptr);
        case HML_FFI_U64:    return hml_val_u64(*(uint64_t*)ptr);
        case HML_FFI_F32:    return hml_val_f64(*(float*)ptr);
        case HML_FFI_F64:    return hml_val_f64(*(double*)ptr);
        case HML_FFI_PTR:    return hml_val_ptr(*(void**)ptr);
        case HML_FFI_STRING: return hml_val_string(*(char**)ptr);
        default:             return hml_val_null();
    }
}

// Convert HmlValue to C storage for callback return
static void hml_value_to_ffi_storage(HmlValue val, HmlFFIType type, void *storage) {
    switch (type) {
        case HML_FFI_VOID:
            break;
        case HML_FFI_I8:
            *(int8_t*)storage = (int8_t)hml_to_i32(val);
            break;
        case HML_FFI_I16:
            *(int16_t*)storage = (int16_t)hml_to_i32(val);
            break;
        case HML_FFI_I32:
            *(int32_t*)storage = hml_to_i32(val);
            break;
        case HML_FFI_I64:
            *(int64_t*)storage = hml_to_i64(val);
            break;
        case HML_FFI_U8:
            *(uint8_t*)storage = (uint8_t)hml_to_i32(val);
            break;
        case HML_FFI_U16:
            *(uint16_t*)storage = (uint16_t)hml_to_i32(val);
            break;
        case HML_FFI_U32:
            *(uint32_t*)storage = (uint32_t)hml_to_i64(val);
            break;
        case HML_FFI_U64:
            *(uint64_t*)storage = (uint64_t)hml_to_i64(val);
            break;
        case HML_FFI_F32:
            *(float*)storage = (float)hml_to_f64(val);
            break;
        case HML_FFI_F64:
            *(double*)storage = hml_to_f64(val);
            break;
        case HML_FFI_PTR:
            *(void**)storage = (val.type == HML_VAL_PTR) ? val.as.as_ptr : NULL;
            break;
        case HML_FFI_STRING:
            *(char**)storage = (val.type == HML_VAL_STRING && val.as.as_string)
                              ? val.as.as_string->data : NULL;
            break;
        case HML_FFI_STRUCT:
            // Struct types are not supported as callback return values
            break;
    }
}

// Universal callback handler - this is called by libffi when C code invokes the callback
static void hml_ffi_callback_handler(ffi_cif *cif, void *ret, void **args, void *user_data) {
    (void)cif;  // Unused parameter
    HmlFFICallback *cb = (HmlFFICallback *)user_data;

    // Lock to ensure thread-safety
    pthread_mutex_lock(&g_callback_mutex);

    // Convert C arguments to Hemlock values
    HmlValue *hemlock_args = NULL;
    if (cb->num_params > 0) {
        hemlock_args = malloc(cb->num_params * sizeof(HmlValue));
        for (int i = 0; i < cb->num_params; i++) {
            hemlock_args[i] = hml_ffi_ptr_to_value(args[i], cb->param_types[i]);
        }
    }

    // Call the Hemlock function
    HmlValue result = hml_call_function(cb->hemlock_fn, hemlock_args, cb->num_params);

    // Handle return value
    if (cb->ret_type != HML_FFI_VOID) {
        hml_value_to_ffi_storage(result, cb->ret_type, ret);
    }

    // Cleanup
    hml_release(&result);
    if (hemlock_args) {
        for (int i = 0; i < cb->num_params; i++) {
            hml_release(&hemlock_args[i]);
        }
        free(hemlock_args);
    }

    pthread_mutex_unlock(&g_callback_mutex);
}

// Create a C-callable function pointer from a Hemlock function
HmlFFICallback* hml_ffi_callback_create(HmlValue fn, HmlFFIType *param_types, int num_params, HmlFFIType return_type) {
    if (fn.type != HML_VAL_FUNCTION || !fn.as.as_function) {
        hml_runtime_error("callback() requires a function");
    }

    HmlFFICallback *cb = malloc(sizeof(HmlFFICallback));
    if (!cb) {
        hml_runtime_error("Failed to allocate FFI callback");
    }

    cb->hemlock_fn = fn;
    hml_retain(&cb->hemlock_fn);
    cb->num_params = num_params;
    cb->ret_type = return_type;
    cb->id = g_next_callback_id++;

    // Copy parameter types
    cb->param_types = NULL;
    if (num_params > 0) {
        cb->param_types = malloc(sizeof(HmlFFIType) * num_params);
        for (int i = 0; i < num_params; i++) {
            cb->param_types[i] = param_types[i];
        }
    }

    // Build libffi type arrays
    cb->arg_types = NULL;
    if (num_params > 0) {
        cb->arg_types = malloc(sizeof(ffi_type*) * num_params);
        for (int i = 0; i < num_params; i++) {
            cb->arg_types[i] = hml_ffi_type_to_ffi(param_types[i]);
        }
    }

    cb->return_type = hml_ffi_type_to_ffi(return_type);

    // Prepare the CIF (Call Interface)
    ffi_status status = ffi_prep_cif(&cb->cif, FFI_DEFAULT_ABI, num_params, cb->return_type, cb->arg_types);
    if (status != FFI_OK) {
        hml_release(&cb->hemlock_fn);
        free(cb->param_types);
        free(cb->arg_types);
        free(cb);
        hml_runtime_error("Failed to prepare FFI callback interface");
    }

    // Allocate the closure
    void *code_ptr;
    ffi_closure *closure = ffi_closure_alloc(sizeof(ffi_closure), &code_ptr);
    if (!closure) {
        hml_release(&cb->hemlock_fn);
        free(cb->param_types);
        free(cb->arg_types);
        free(cb);
        hml_runtime_error("Failed to allocate FFI closure");
    }

    cb->closure = closure;
    cb->code_ptr = code_ptr;

    // Prepare the closure with our handler
    status = ffi_prep_closure_loc(closure, &cb->cif, hml_ffi_callback_handler, cb, code_ptr);
    if (status != FFI_OK) {
        ffi_closure_free(closure);
        hml_release(&cb->hemlock_fn);
        free(cb->param_types);
        free(cb->arg_types);
        free(cb);
        hml_runtime_error("Failed to prepare FFI closure");
    }

    // Track the callback
    pthread_mutex_lock(&g_callback_mutex);
    if (g_num_callbacks >= g_callbacks_capacity) {
        g_callbacks_capacity = g_callbacks_capacity == 0 ? 8 : g_callbacks_capacity * 2;
        g_callbacks = realloc(g_callbacks, sizeof(HmlFFICallback*) * g_callbacks_capacity);
    }
    g_callbacks[g_num_callbacks++] = cb;
    pthread_mutex_unlock(&g_callback_mutex);

    return cb;
}

// Get the C-callable function pointer from a callback handle
void* hml_ffi_callback_ptr(HmlFFICallback *cb) {
    return cb ? cb->code_ptr : NULL;
}

// Free a callback handle
void hml_ffi_callback_free(HmlFFICallback *cb) {
    if (!cb) return;

    // Remove from tracking list
    pthread_mutex_lock(&g_callback_mutex);
    for (int i = 0; i < g_num_callbacks; i++) {
        if (g_callbacks[i] == cb) {
            for (int j = i; j < g_num_callbacks - 1; j++) {
                g_callbacks[j] = g_callbacks[j + 1];
            }
            g_num_callbacks--;
            break;
        }
    }
    pthread_mutex_unlock(&g_callback_mutex);

    // Free the closure
    if (cb->closure) {
        ffi_closure_free(cb->closure);
    }

    // Release the Hemlock function
    hml_release(&cb->hemlock_fn);

    // Free type arrays
    free(cb->param_types);
    free(cb->arg_types);
    free(cb);
}

// Free a callback by its code pointer
int hml_ffi_callback_free_by_ptr(void *ptr) {
    if (!ptr) return 0;

    pthread_mutex_lock(&g_callback_mutex);
    for (int i = 0; i < g_num_callbacks; i++) {
        HmlFFICallback *cb = g_callbacks[i];
        if (cb && cb->code_ptr == ptr) {
            // Remove from list
            for (int j = i; j < g_num_callbacks - 1; j++) {
                g_callbacks[j] = g_callbacks[j + 1];
            }
            g_num_callbacks--;
            pthread_mutex_unlock(&g_callback_mutex);

            // Free the callback
            if (cb->closure) {
                ffi_closure_free(cb->closure);
            }
            hml_release(&cb->hemlock_fn);
            free(cb->param_types);
            free(cb->arg_types);
            free(cb);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_callback_mutex);
    return 0;
}

// Helper: convert type name string to HmlFFIType
static HmlFFIType hml_string_to_ffi_type(const char *name) {
    if (!name) return HML_FFI_VOID;
    if (strcmp(name, "void") == 0) return HML_FFI_VOID;
    if (strcmp(name, "i8") == 0) return HML_FFI_I8;
    if (strcmp(name, "i16") == 0) return HML_FFI_I16;
    if (strcmp(name, "i32") == 0 || strcmp(name, "integer") == 0) return HML_FFI_I32;
    if (strcmp(name, "i64") == 0) return HML_FFI_I64;
    if (strcmp(name, "u8") == 0 || strcmp(name, "byte") == 0) return HML_FFI_U8;
    if (strcmp(name, "u16") == 0) return HML_FFI_U16;
    if (strcmp(name, "u32") == 0) return HML_FFI_U32;
    if (strcmp(name, "u64") == 0) return HML_FFI_U64;
    if (strcmp(name, "f32") == 0) return HML_FFI_F32;
    if (strcmp(name, "f64") == 0 || strcmp(name, "number") == 0) return HML_FFI_F64;
    if (strcmp(name, "ptr") == 0) return HML_FFI_PTR;
    if (strcmp(name, "string") == 0) return HML_FFI_STRING;
    return HML_FFI_I32; // Default
}

// ========== BUILTIN WRAPPERS ==========

// Builtin wrapper: callback(fn, param_types, return_type) -> ptr
HmlValue hml_builtin_callback(HmlClosureEnv *env, HmlValue fn, HmlValue param_types, HmlValue return_type) {
    (void)env;

    if (fn.type != HML_VAL_FUNCTION) {
        hml_runtime_error("callback() first argument must be a function");
    }

    if (param_types.type != HML_VAL_ARRAY || !param_types.as.as_array) {
        hml_runtime_error("callback() second argument must be an array of type names");
    }

    HmlArray *params_arr = param_types.as.as_array;
    int num_params = params_arr->length;

    // Build parameter types
    HmlFFIType *types = NULL;
    if (num_params > 0) {
        types = malloc(sizeof(HmlFFIType) * num_params);
        for (int i = 0; i < num_params; i++) {
            HmlValue type_val = params_arr->elements[i];
            if (type_val.type != HML_VAL_STRING || !type_val.as.as_string) {
                free(types);
                hml_runtime_error("callback() param_types must contain type name strings");
            }
            types[i] = hml_string_to_ffi_type(type_val.as.as_string->data);
        }
    }

    // Get return type
    HmlFFIType ret_type = HML_FFI_VOID;
    if (return_type.type == HML_VAL_STRING && return_type.as.as_string) {
        ret_type = hml_string_to_ffi_type(return_type.as.as_string->data);
    }

    // Create the callback
    HmlFFICallback *cb = hml_ffi_callback_create(fn, types, num_params, ret_type);
    free(types);

    // Return the C-callable function pointer
    return hml_val_ptr(hml_ffi_callback_ptr(cb));
}

// Builtin wrapper: callback_free(ptr)
HmlValue hml_builtin_callback_free(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("callback_free() argument must be a ptr");
    }

    int success = hml_ffi_callback_free_by_ptr(ptr.as.as_ptr);
    if (!success) {
        hml_runtime_error("callback_free(): pointer is not a valid callback");
    }

    return hml_val_null();
}

// Builtin: ptr_deref_i32(ptr) -> i32
HmlValue hml_builtin_ptr_deref_i32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_deref_i32() argument must be a ptr");
    }

    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_deref_i32() cannot dereference null pointer");
    }

    return hml_val_i32(*(int32_t*)p);
}

// Builtin: ptr_write_i32(ptr, value)
HmlValue hml_builtin_ptr_write_i32(HmlClosureEnv *env, HmlValue ptr, HmlValue value) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_write_i32() first argument must be a ptr");
    }

    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_write_i32() cannot write to null pointer");
    }

    *(int32_t*)p = hml_to_i32(value);
    return hml_val_null();
}

// Builtin: ptr_offset(ptr, offset, element_size) -> ptr
HmlValue hml_builtin_ptr_offset(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue element_size) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_offset() first argument must be a ptr");
    }

    void *p = ptr.as.as_ptr;
    int64_t off = hml_to_i64(offset);
    int64_t elem_size = hml_to_i64(element_size);

    char *base = (char*)p;
    return hml_val_ptr(base + (off * elem_size));
}

// Builtin: ptr_read_i32(ptr) -> i32  (dereference pointer-to-pointer, for qsort)
HmlValue hml_builtin_ptr_read_i32(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("ptr_read_i32() argument must be a ptr");
    }

    void *p = ptr.as.as_ptr;
    if (!p) {
        hml_runtime_error("ptr_read_i32() cannot read from null pointer");
    }

    // Read through pointer-to-pointer (qsort passes ptr to element)
    int32_t *actual_ptr = *(int32_t**)p;
    return hml_val_i32(*actual_ptr);
}
