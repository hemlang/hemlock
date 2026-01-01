#include "internal.h"
#include <ffi.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

// Stack allocation threshold for FFI calls
#define FFI_MAX_STACK_ARGS 8

// ========== FFI DATA STRUCTURES ==========

// Loaded library structure
struct FFILibrary {
    char *path;              // Library file path
    void *handle;            // dlopen() handle
};

// FFI struct field descriptor
typedef struct {
    char *name;              // Field name
    ffi_type *ffi_type;      // libffi type for this field
    TypeKind hemlock_type;   // Hemlock type kind
    size_t offset;           // Byte offset within struct
    size_t size;             // Field size in bytes
} FFIStructField;

// FFI struct type descriptor
typedef struct FFIStructType {
    char *name;                  // Struct type name (e.g., "Point")
    FFIStructField *fields;      // Array of field descriptors
    int num_fields;              // Number of fields
    size_t size;                 // Total struct size (with padding)
    size_t alignment;            // Struct alignment requirement
    ffi_type *ffi_type;          // Cached libffi ffi_type struct
    ffi_type **ffi_field_types;  // Array of field types for ffi_type
    struct FFIStructType *next;  // Next in registry linked list
} FFIStructType;

// FFI struct registry
typedef struct {
    FFIStructType *types;    // Linked list of registered struct types
    int count;               // Number of registered types
} FFIStructRegistry;

static FFIStructRegistry g_struct_registry = {NULL, 0};

// Global FFI state
typedef struct {
    FFILibrary **libraries;  // All loaded libraries
    int num_libraries;
    int libraries_capacity;
    FFILibrary *current_lib; // Most recently imported library
} FFIState;

static FFIState g_ffi_state = {NULL, 0, 0, NULL};

// Thread-safety: Mutex for FFI library cache access
static pthread_mutex_t ffi_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// Callback tracking for cleanup
typedef struct {
    FFICallback **callbacks;
    int num_callbacks;
    int callbacks_capacity;
} CallbackState;

static CallbackState g_callback_state = {NULL, 0, 0};
static int g_next_callback_id = 1;

// Thread-safety: Mutex for callback invocations
// Note: Hemlock interpreter is not fully thread-safe, so callbacks must be serialized
static pthread_mutex_t ffi_callback_mutex = PTHREAD_MUTEX_INITIALIZER;

// ========== PLATFORM-SPECIFIC LIBRARY PATH TRANSLATION ==========

#ifdef __APPLE__
#include <sys/stat.h>

// Check if a file exists
static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Translate Linux library names to macOS equivalents
static const char* translate_library_path(const char *path) {
    // libc.so.6 -> libSystem.B.dylib (macOS system C library)
    if (strcmp(path, "libc.so.6") == 0) {
        return "libSystem.B.dylib";
    }
    // libcrypto.so.3 -> Homebrew OpenSSL (avoid macOS SIP restrictions)
    // macOS system libcrypto triggers security warnings and may abort
    if (strcmp(path, "libcrypto.so.3") == 0 || strcmp(path, "libcrypto.dylib") == 0) {
        // Try Homebrew paths (Apple Silicon first, then Intel)
        if (file_exists("/opt/homebrew/opt/openssl@3/lib/libcrypto.dylib")) {
            return "/opt/homebrew/opt/openssl@3/lib/libcrypto.dylib";
        }
        if (file_exists("/usr/local/opt/openssl@3/lib/libcrypto.dylib")) {
            return "/usr/local/opt/openssl@3/lib/libcrypto.dylib";
        }
        // Fallback to generic name (may trigger warning on macOS)
        return "libcrypto.dylib";
    }
    // Generic .so to .dylib translation (e.g., libfoo.so -> libfoo.dylib)
    // Only translate if it ends with .so or .so.N
    static char translated[512];
    size_t len = strlen(path);
    
    // Check for .so.N pattern (e.g., libfoo.so.6)
    const char *so_pos = strstr(path, ".so.");
    if (so_pos != NULL) {
        size_t base_len = so_pos - path;
        if (base_len < sizeof(translated) - 7) {
            strncpy(translated, path, base_len);
            strcpy(translated + base_len, ".dylib");
            return translated;
        }
    }
    
    // Check for .so at end (e.g., libfoo.so)
    if (len > 3 && strcmp(path + len - 3, ".so") == 0) {
        if (len < sizeof(translated) - 4) {
            strncpy(translated, path, len - 3);
            strcpy(translated + len - 3, ".dylib");
            return translated;
        }
    }
    
    return path;  // No translation needed
}
#endif

// ========== LIBRARY PATH SECURITY ==========

// Validate FFI library path for obvious security issues
// Returns error message if invalid, NULL if path is acceptable
static const char* validate_ffi_library_path(const char *path) {
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

// ========== LIBRARY LOADING ==========

FFILibrary* ffi_load_library(const char *path, ExecutionContext *ctx) {
    // SECURITY: Validate library path before loading
    const char *validation_error = validate_ffi_library_path(path);
    if (validation_error) {
        ctx->exception_state.is_throwing = 1;
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "FFI security error: %s (path: %s)", validation_error, path);
        ctx->exception_state.exception_value = val_string(error_msg);
        return NULL;
    }

    pthread_mutex_lock(&ffi_cache_mutex);

#ifdef __APPLE__
    // Translate Linux library names to macOS equivalents
    const char *actual_path = translate_library_path(path);
#else
    const char *actual_path = path;
#endif

    // Check if library is already loaded (check both original and translated paths)
    for (int i = 0; i < g_ffi_state.num_libraries; i++) {
        if (strcmp(g_ffi_state.libraries[i]->path, path) == 0 ||
            strcmp(g_ffi_state.libraries[i]->path, actual_path) == 0) {
            FFILibrary *lib = g_ffi_state.libraries[i];
            pthread_mutex_unlock(&ffi_cache_mutex);
            return lib;
        }
    }

    // Open library with RTLD_LAZY (resolve symbols on first call)
    void *handle = dlopen(actual_path, RTLD_LAZY);
    if (handle == NULL) {
        pthread_mutex_unlock(&ffi_cache_mutex);
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
        // SECURITY: Check for integer overflow before doubling capacity
        int new_capacity = g_ffi_state.libraries_capacity == 0 ? 4 : g_ffi_state.libraries_capacity * 2;
        if (new_capacity < g_ffi_state.libraries_capacity) {
            // Overflow detected - free allocated resources before returning
            free(lib->path);
            free(lib);
            pthread_mutex_unlock(&ffi_cache_mutex);
            ctx->exception_state.is_throwing = 1;
            ctx->exception_state.exception_value = val_string("FFI library cache capacity overflow");
            return NULL;
        }
        g_ffi_state.libraries_capacity = new_capacity;
        g_ffi_state.libraries = realloc(g_ffi_state.libraries, sizeof(FFILibrary*) * g_ffi_state.libraries_capacity);
    }
    g_ffi_state.libraries[g_ffi_state.num_libraries++] = lib;

    pthread_mutex_unlock(&ffi_cache_mutex);
    return lib;
}

void ffi_close_library(FFILibrary *lib) {
    if (lib->handle != NULL) {
        dlclose(lib->handle);
    }
    free(lib->path);
    free(lib);
}

// ========== FFI STRUCT SUPPORT ==========

// Get ffi_type for a TypeKind (helper for struct field types)
static ffi_type* type_kind_to_ffi_type(TypeKind kind) {
    switch (kind) {
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
        case TYPE_STRING: return &ffi_type_pointer;
        case TYPE_BOOL:   return &ffi_type_sint;
        case TYPE_VOID:   return &ffi_type_void;
        default:          return NULL;  // Unsupported for struct fields
    }
}

// Get size of a TypeKind in bytes
static size_t type_kind_size(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:     return sizeof(int8_t);
        case TYPE_I16:    return sizeof(int16_t);
        case TYPE_I32:    return sizeof(int32_t);
        case TYPE_I64:    return sizeof(int64_t);
        case TYPE_U8:     return sizeof(uint8_t);
        case TYPE_U16:    return sizeof(uint16_t);
        case TYPE_U32:    return sizeof(uint32_t);
        case TYPE_U64:    return sizeof(uint64_t);
        case TYPE_F32:    return sizeof(float);
        case TYPE_F64:    return sizeof(double);
        case TYPE_PTR:    return sizeof(void*);
        case TYPE_STRING: return sizeof(char*);
        case TYPE_BOOL:   return sizeof(int);
        default:          return 0;
    }
}

// Lookup a registered FFI struct type by name
FFIStructType* ffi_lookup_struct(const char *name) {
    if (!name) return NULL;
    FFIStructType *st = g_struct_registry.types;
    while (st) {
        if (strcmp(st->name, name) == 0) {
            return st;
        }
        st = st->next;
    }
    return NULL;
}

// Register a struct type for FFI use
// This creates the libffi struct type with proper layout
FFIStructType* ffi_register_struct(const char *name, char **field_names,
                                    Type **field_types, int num_fields) {
    // Check if already registered
    FFIStructType *existing = ffi_lookup_struct(name);
    if (existing) {
        return existing;
    }

    // Validate all field types are FFI-compatible
    for (int i = 0; i < num_fields; i++) {
        if (!field_types[i]) {
            fprintf(stderr, "Error: Struct '%s' field '%s' has no type annotation (required for FFI)\n",
                    name, field_names[i]);
            return NULL;
        }
        TypeKind kind = field_types[i]->kind;
        if (kind == TYPE_CUSTOM_OBJECT) {
            // Nested structs - check if already registered
            if (!ffi_lookup_struct(field_types[i]->type_name)) {
                fprintf(stderr, "Error: Struct '%s' field '%s' uses unregistered struct type '%s'\n",
                        name, field_names[i], field_types[i]->type_name);
                return NULL;
            }
        } else if (!type_kind_to_ffi_type(kind)) {
            fprintf(stderr, "Error: Struct '%s' field '%s' has unsupported FFI type\n",
                    name, field_names[i]);
            return NULL;
        }
    }

    // Create the struct type
    FFIStructType *st = malloc(sizeof(FFIStructType));
    st->name = strdup(name);
    st->num_fields = num_fields;
    st->fields = malloc(sizeof(FFIStructField) * num_fields);
    st->ffi_field_types = malloc(sizeof(ffi_type*) * (num_fields + 1));  // NULL-terminated

    // Build field descriptors and libffi type array
    for (int i = 0; i < num_fields; i++) {
        st->fields[i].name = strdup(field_names[i]);
        st->fields[i].hemlock_type = field_types[i]->kind;

        if (field_types[i]->kind == TYPE_CUSTOM_OBJECT) {
            // Nested struct
            FFIStructType *nested = ffi_lookup_struct(field_types[i]->type_name);
            st->fields[i].ffi_type = nested->ffi_type;
            st->fields[i].size = nested->size;
        } else {
            st->fields[i].ffi_type = type_kind_to_ffi_type(field_types[i]->kind);
            st->fields[i].size = type_kind_size(field_types[i]->kind);
        }
        st->ffi_field_types[i] = st->fields[i].ffi_type;
    }
    st->ffi_field_types[num_fields] = NULL;  // NULL-terminate

    // Create the libffi struct type
    st->ffi_type = malloc(sizeof(ffi_type));
    st->ffi_type->size = 0;  // Let libffi compute
    st->ffi_type->alignment = 0;  // Let libffi compute
    st->ffi_type->type = FFI_TYPE_STRUCT;
    st->ffi_type->elements = st->ffi_field_types;

    // Compute offsets using libffi's layout
    // We need to prepare a dummy CIF to get libffi to compute the struct layout
    ffi_cif dummy_cif;
    ffi_type *dummy_args[1] = { st->ffi_type };
    ffi_prep_cif(&dummy_cif, FFI_DEFAULT_ABI, 1, &ffi_type_void, dummy_args);

    // Now st->ffi_type has computed size and alignment
    st->size = st->ffi_type->size;
    st->alignment = st->ffi_type->alignment;

    // Compute field offsets
    size_t offset = 0;
    for (int i = 0; i < num_fields; i++) {
        size_t field_align = st->fields[i].ffi_type->alignment;
        // Align offset
        if (field_align > 0) {
            offset = (offset + field_align - 1) & ~(field_align - 1);
        }
        st->fields[i].offset = offset;
        offset += st->fields[i].size;
    }

    // Add to registry
    st->next = g_struct_registry.types;
    g_struct_registry.types = st;
    g_struct_registry.count++;

    return st;
}

// Marshal a Hemlock object to C struct memory
// Returns pointer to allocated struct memory (caller must free)
void* ffi_object_to_struct(Value obj, FFIStructType *struct_type, ExecutionContext *ctx) {
    if (obj.type != VAL_OBJECT || !obj.as.as_object) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("FFI struct conversion requires an object");
        return NULL;
    }

    Object *o = obj.as.as_object;
    void *struct_mem = calloc(1, struct_type->size);

    for (int i = 0; i < struct_type->num_fields; i++) {
        FFIStructField *field = &struct_type->fields[i];
        void *field_ptr = (char*)struct_mem + field->offset;

        // Look up field in object
        int field_idx = object_lookup_field(o, field->name);
        if (field_idx < 0) {
            ctx->exception_state.is_throwing = 1;
            char err[256];
            snprintf(err, sizeof(err), "FFI struct '%s' missing required field '%s'",
                     struct_type->name, field->name);
            ctx->exception_state.exception_value = val_string(err);
            free(struct_mem);
            return NULL;
        }

        Value field_val = o->field_values[field_idx];

        // Convert and write field value
        switch (field->hemlock_type) {
            case TYPE_I8:
                *(int8_t*)field_ptr = (int8_t)value_to_int(field_val);
                break;
            case TYPE_I16:
                *(int16_t*)field_ptr = (int16_t)value_to_int(field_val);
                break;
            case TYPE_I32:
                *(int32_t*)field_ptr = value_to_int(field_val);
                break;
            case TYPE_I64:
                *(int64_t*)field_ptr = value_to_int64(field_val);
                break;
            case TYPE_U8:
                *(uint8_t*)field_ptr = (uint8_t)value_to_int(field_val);
                break;
            case TYPE_U16:
                *(uint16_t*)field_ptr = (uint16_t)value_to_int(field_val);
                break;
            case TYPE_U32:
                *(uint32_t*)field_ptr = (uint32_t)value_to_int(field_val);
                break;
            case TYPE_U64:
                *(uint64_t*)field_ptr = (uint64_t)value_to_int64(field_val);
                break;
            case TYPE_F32:
                *(float*)field_ptr = (float)value_to_float(field_val);
                break;
            case TYPE_F64:
                *(double*)field_ptr = value_to_float(field_val);
                break;
            case TYPE_PTR:
                if (field_val.type == VAL_PTR) {
                    *(void**)field_ptr = field_val.as.as_ptr;
                } else if (field_val.type == VAL_BUFFER) {
                    *(void**)field_ptr = field_val.as.as_buffer->data;
                } else {
                    *(void**)field_ptr = NULL;
                }
                break;
            case TYPE_STRING:
                if (field_val.type == VAL_STRING && field_val.as.as_string) {
                    *(char**)field_ptr = field_val.as.as_string->data;
                } else {
                    *(char**)field_ptr = NULL;
                }
                break;
            case TYPE_BOOL:
                *(int*)field_ptr = value_is_truthy(field_val) ? 1 : 0;
                break;
            case TYPE_CUSTOM_OBJECT: {
                // Nested struct - recursively convert
                FFIStructType *nested = ffi_lookup_struct(field->name);
                if (nested && field_val.type == VAL_OBJECT) {
                    void *nested_mem = ffi_object_to_struct(field_val, nested, ctx);
                    if (!nested_mem) {
                        free(struct_mem);
                        return NULL;
                    }
                    memcpy(field_ptr, nested_mem, nested->size);
                    free(nested_mem);
                }
                break;
            }
            default:
                break;
        }
    }

    return struct_mem;
}

// Marshal C struct memory to a Hemlock object
Value ffi_struct_to_object(void *struct_ptr, FFIStructType *struct_type) {
    Object *obj = object_new(strdup(struct_type->name), struct_type->num_fields);

    for (int i = 0; i < struct_type->num_fields; i++) {
        FFIStructField *field = &struct_type->fields[i];
        void *field_ptr = (char*)struct_ptr + field->offset;
        Value field_val;

        switch (field->hemlock_type) {
            case TYPE_I8:
                field_val = val_i8(*(int8_t*)field_ptr);
                break;
            case TYPE_I16:
                field_val = val_i16(*(int16_t*)field_ptr);
                break;
            case TYPE_I32:
                field_val = val_i32(*(int32_t*)field_ptr);
                break;
            case TYPE_I64:
                field_val = val_i64(*(int64_t*)field_ptr);
                break;
            case TYPE_U8:
                field_val = val_u8(*(uint8_t*)field_ptr);
                break;
            case TYPE_U16:
                field_val = val_u16(*(uint16_t*)field_ptr);
                break;
            case TYPE_U32:
                field_val = val_u32(*(uint32_t*)field_ptr);
                break;
            case TYPE_U64:
                field_val = val_u64(*(uint64_t*)field_ptr);
                break;
            case TYPE_F32:
                field_val = val_f32(*(float*)field_ptr);
                break;
            case TYPE_F64:
                field_val = val_f64(*(double*)field_ptr);
                break;
            case TYPE_PTR:
                field_val = val_ptr(*(void**)field_ptr);
                break;
            case TYPE_STRING: {
                char *str = *(char**)field_ptr;
                if (str) {
                    field_val = val_string(str);
                } else {
                    field_val = val_null();
                }
                break;
            }
            case TYPE_BOOL:
                field_val = val_bool(*(int*)field_ptr != 0);
                break;
            case TYPE_CUSTOM_OBJECT: {
                // Nested struct
                FFIStructType *nested = ffi_lookup_struct(field->name);
                if (nested) {
                    field_val = ffi_struct_to_object(field_ptr, nested);
                } else {
                    field_val = val_null();
                }
                break;
            }
            default:
                field_val = val_null();
                break;
        }

        // Add field to object
        obj->field_names[obj->num_fields] = strdup(field->name);
        obj->field_values[obj->num_fields] = field_val;
        obj->num_fields++;
    }

    return val_object(obj);
}

// Free the struct registry on cleanup
void ffi_struct_cleanup(void) {
    FFIStructType *st = g_struct_registry.types;
    while (st) {
        FFIStructType *next = st->next;
        free(st->name);
        for (int i = 0; i < st->num_fields; i++) {
            free(st->fields[i].name);
        }
        free(st->fields);
        free(st->ffi_field_types);
        free(st->ffi_type);
        free(st);
        st = next;
    }
    g_struct_registry.types = NULL;
    g_struct_registry.count = 0;
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
        case TYPE_CUSTOM_OBJECT: {
            // Look up registered struct type
            if (type->type_name) {
                FFIStructType *st = ffi_lookup_struct(type->type_name);
                if (st) {
                    return st->ffi_type;
                }
            }
            fprintf(stderr, "Error: Struct type '%s' not registered for FFI\n",
                    type->type_name ? type->type_name : "(unknown)");
            exit(1);
        }
        default:
            fprintf(stderr, "Error: Unsupported FFI type: %d\n", type->kind);
            exit(1);
    }
}

// ========== VALUE CONVERSION ==========

// Fast path: write primitive value to caller-provided 8-byte storage
// Returns 1 if handled (primitive), 0 if struct (needs heap allocation)
static int hemlock_to_c_value_fast(Value val, Type *type, void *storage) {
    switch (type->kind) {
        case TYPE_I8:
            *(int8_t*)storage = val.as.as_i8;
            return 1;
        case TYPE_I16:
            *(int16_t*)storage = val.as.as_i16;
            return 1;
        case TYPE_I32:
            *(int32_t*)storage = val.as.as_i32;
            return 1;
        case TYPE_I64:
            if (val.type == VAL_PTR) {
                *(int64_t*)storage = (int64_t)(intptr_t)val.as.as_ptr;
            } else {
                *(int64_t*)storage = val.as.as_i64;
            }
            return 1;
        case TYPE_U8:
            *(uint8_t*)storage = val.as.as_u8;
            return 1;
        case TYPE_U16:
            *(uint16_t*)storage = val.as.as_u16;
            return 1;
        case TYPE_U32:
            *(uint32_t*)storage = val.as.as_u32;
            return 1;
        case TYPE_U64:
            if (val.type == VAL_PTR) {
                *(uint64_t*)storage = (uint64_t)(uintptr_t)val.as.as_ptr;
            } else {
                *(uint64_t*)storage = val.as.as_u64;
            }
            return 1;
        case TYPE_F32:
            *(float*)storage = val.as.as_f32;
            return 1;
        case TYPE_F64:
            *(double*)storage = val.as.as_f64;
            return 1;
        case TYPE_PTR:
            *(void**)storage = val.as.as_ptr;
            return 1;
        case TYPE_STRING:
            *(char**)storage = val.as.as_string ? val.as.as_string->data : NULL;
            return 1;
        case TYPE_BOOL:
            *(int*)storage = val.as.as_bool ? 1 : 0;
            return 1;
        case TYPE_CUSTOM_OBJECT:
            return 0;  // Struct - needs heap allocation
        default:
            return 0;
    }
}

// Thread-safe: context is now passed directly instead of using a global
// NOTE: This allocates memory that must be freed by caller
void* hemlock_to_c_value(Value val, Type *type, ExecutionContext *ctx) {
    size_t size = 0;
    ffi_type *ffi_t = hemlock_type_to_ffi_type(type);

    // Handle struct types specially
    if (type->kind == TYPE_CUSTOM_OBJECT) {
        FFIStructType *st = ffi_lookup_struct(type->type_name);
        if (st) {
            // Convert object to struct, return the struct memory directly
            return ffi_object_to_struct(val, st, ctx);
        }
        fprintf(stderr, "Error: Struct type '%s' not registered for FFI\n", type->type_name);
        exit(1);
    }

    // Determine size for primitive types
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
    hemlock_to_c_value_fast(val, type, storage);
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
        case TYPE_CUSTOM_OBJECT: {
            // Convert struct to object
            FFIStructType *st = ffi_lookup_struct(type->type_name);
            if (st) {
                return ffi_struct_to_object(c_value, st);
            }
            fprintf(stderr, "Error: Struct type '%s' not registered for FFI\n", type->type_name);
            exit(1);
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
    // Create FFI function with lazy symbol resolution
    // Symbol will be resolved on first call, not at declaration time
    FFIFunction *func = malloc(sizeof(FFIFunction));
    func->name = strdup(name);
    func->func_ptr = NULL;  // Lazy resolution - will be set on first call
    func->lib_handle = lib->handle;  // Store for lazy resolution
    func->lib_path = strdup(lib->path);  // Store for error messages
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
    if (func->lib_path) free(func->lib_path);
    if (func->cif) free(func->cif);
    if (func->arg_types) free(func->arg_types);
    // Note: hemlock_params and hemlock_return are managed by AST
    free(func);
}

// ========== FUNCTION INVOCATION ==========

Value ffi_call_function(FFIFunction *func, Value *args, int num_args, ExecutionContext *ctx) {
    // Lazy symbol resolution: resolve on first call
    if (func->func_ptr == NULL) {
        dlerror();  // Clear any existing error
        func->func_ptr = dlsym(func->lib_handle, func->name);
        char *error_msg = dlerror();
        if (error_msg != NULL || func->func_ptr == NULL) {
            ctx->exception_state.is_throwing = 1;
            char err[512];
            snprintf(err, sizeof(err), "FFI function '%s' not found in '%s'%s%s",
                     func->name, func->lib_path,
                     error_msg ? ": " : "", error_msg ? error_msg : "");
            ctx->exception_state.exception_value = val_string(err);
            return val_null();
        }
    }

    // Validate argument count
    if (num_args != func->num_params) {
        ctx->exception_state.is_throwing = 1;
        char err[256];
        snprintf(err, sizeof(err), "FFI function '%s' expects %d arguments, got %d",
                 func->name, func->num_params, num_args);
        ctx->exception_state.exception_value = val_string(err);
        return val_null();
    }

    // Stack-based allocation for common case (<=8 args)
    // bulk_storage: 8 bytes per arg, enough for any primitive type
    void *stack_arg_values[FFI_MAX_STACK_ARGS];
    uint64_t stack_bulk_storage[FFI_MAX_STACK_ARGS];
    void *stack_struct_storage[FFI_MAX_STACK_ARGS];  // Track heap-allocated structs
    int use_stack = (num_args <= FFI_MAX_STACK_ARGS);

    void **arg_values;
    uint64_t *bulk_storage;
    void **struct_storage;
    int num_structs = 0;

    if (num_args > 0) {
        if (use_stack) {
            arg_values = stack_arg_values;
            bulk_storage = stack_bulk_storage;
            struct_storage = stack_struct_storage;
        } else {
            arg_values = malloc(sizeof(void*) * num_args);
            bulk_storage = malloc(sizeof(uint64_t) * num_args);
            struct_storage = malloc(sizeof(void*) * num_args);
        }

        for (int i = 0; i < num_args; i++) {
            Type *param_type = func->hemlock_params[i];

            // Fast path: primitives use bulk storage (no malloc)
            if (hemlock_to_c_value_fast(args[i], param_type, &bulk_storage[i])) {
                arg_values[i] = &bulk_storage[i];
                struct_storage[i] = NULL;
            } else {
                // Slow path: struct needs heap allocation
                void *heap_storage = hemlock_to_c_value(args[i], param_type, ctx);
                if (ctx->exception_state.is_throwing) {
                    // Cleanup already-allocated structs
                    for (int j = 0; j < i; j++) {
                        if (struct_storage[j]) free(struct_storage[j]);
                    }
                    if (!use_stack) {
                        free(arg_values);
                        free(bulk_storage);
                        free(struct_storage);
                    }
                    return val_null();
                }
                arg_values[i] = heap_storage;
                struct_storage[i] = heap_storage;
                num_structs++;
            }
        }
    } else {
        arg_values = NULL;
        bulk_storage = NULL;
        struct_storage = NULL;
    }

    // Stack-based return value storage for non-struct returns
    union {
        int8_t i8; int16_t i16; int32_t i32; int64_t i64;
        uint8_t u8; uint16_t u16; uint32_t u32; uint64_t u64;
        float f32; double f64;
        void *ptr;
    } stack_return;

    void *return_storage = NULL;
    int free_return = 0;

    if (func->hemlock_return != NULL && func->hemlock_return->kind != TYPE_VOID) {
        ffi_type *ret_type = (ffi_type*)func->return_type;
        if (ret_type->type == FFI_TYPE_STRUCT) {
            // Struct returns need heap allocation (variable size)
            return_storage = malloc(ret_type->size);
            free_return = 1;
        } else {
            // Primitive returns use stack
            return_storage = &stack_return;
        }
    }

    // Call via libffi (CIF already cached at declaration time)
    ffi_cif *cif = (ffi_cif*)func->cif;
    ffi_call(cif, FFI_FN(func->func_ptr), return_storage, arg_values);

    // Convert return value
    Value result;
    if (func->hemlock_return == NULL || func->hemlock_return->kind == TYPE_VOID) {
        result = val_null();
    } else {
        result = c_to_hemlock_value(return_storage, func->hemlock_return);
    }

    // Cleanup - only free heap-allocated struct storage
    if (num_structs > 0) {
        for (int i = 0; i < num_args; i++) {
            if (struct_storage[i]) free(struct_storage[i]);
        }
    }
    if (!use_stack && num_args > 0) {
        free(arg_values);
        free(bulk_storage);
        free(struct_storage);
    }
    if (free_return) {
        free(return_storage);
    }

    return result;
}

// ========== FFI CALLBACKS ==========

// Helper: Convert C value at address to Hemlock Value (for callback arguments)
static Value c_ptr_to_hemlock_value(void *c_ptr, Type *type) {
    if (type == NULL || type->kind == TYPE_VOID) {
        return val_null();
    }

    switch (type->kind) {
        case TYPE_I8:
            return val_i8(*(int8_t*)c_ptr);
        case TYPE_I16:
            return val_i16(*(int16_t*)c_ptr);
        case TYPE_I32:
            return val_i32(*(int32_t*)c_ptr);
        case TYPE_I64:
            return val_i64(*(int64_t*)c_ptr);
        case TYPE_U8:
            return val_u8(*(uint8_t*)c_ptr);
        case TYPE_U16:
            return val_u16(*(uint16_t*)c_ptr);
        case TYPE_U32:
            return val_u32(*(uint32_t*)c_ptr);
        case TYPE_U64:
            return val_u64(*(uint64_t*)c_ptr);
        case TYPE_F32:
            return val_f32(*(float*)c_ptr);
        case TYPE_F64:
            return val_f64(*(double*)c_ptr);
        case TYPE_PTR:
            return val_ptr(*(void**)c_ptr);
        case TYPE_BOOL:
            return val_bool(*(int*)c_ptr != 0);
        case TYPE_STRING: {
            char *str = *(char**)c_ptr;
            if (str == NULL) {
                return val_null();
            }
            return val_string(str);
        }
        default:
            fprintf(stderr, "Error: Unsupported callback argument type: %d\n", type->kind);
            return val_null();
    }
}

// Helper: Write Hemlock Value to C storage (for callback return values)
static void hemlock_to_c_storage(Value val, Type *type, void *storage) {
    if (type == NULL || type->kind == TYPE_VOID) {
        return;
    }

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
        case TYPE_BOOL:
            *(int*)storage = val.as.as_bool ? 1 : 0;
            break;
        case TYPE_STRING:
            *(char**)storage = val.as.as_string ? val.as.as_string->data : NULL;
            break;
        default:
            fprintf(stderr, "Error: Unsupported callback return type: %d\n", type->kind);
            break;
    }
}

// Universal callback handler - this is called by libffi when C code invokes the callback
static void ffi_callback_handler(ffi_cif *cif, void *ret, void **args, void *user_data) {
    (void)cif;  // Unused parameter
    FFICallback *cb = (FFICallback *)user_data;
    Function *fn = cb->hemlock_fn;

    // Lock to ensure thread-safety (Hemlock interpreter is not fully thread-safe)
    pthread_mutex_lock(&ffi_callback_mutex);

    // Create a fresh execution context for this callback invocation
    ExecutionContext *ctx = exec_context_new();

    // Create a new environment with the function's closure as parent
    Environment *func_env = env_new(fn->closure_env);

    // Convert C arguments to Hemlock values and bind parameters
    for (int i = 0; i < cb->num_params && i < fn->num_params; i++) {
        Value arg = c_ptr_to_hemlock_value(args[i], cb->hemlock_params[i]);
        env_define(func_env, fn->param_names[i], arg, 0, ctx);
    }

    // Execute the Hemlock function body
    eval_stmt(fn->body, func_env, ctx);

    // Handle return value
    if (ctx->return_state.is_returning && cb->hemlock_return != NULL && cb->hemlock_return->kind != TYPE_VOID) {
        Value result = ctx->return_state.return_value;
        hemlock_to_c_storage(result, cb->hemlock_return, ret);
    }

    // Handle exceptions - we can't propagate them to C, so just print a warning
    if (ctx->exception_state.is_throwing) {
        fprintf(stderr, "Warning: Exception in FFI callback (cannot propagate to C): ");
        print_value(ctx->exception_state.exception_value);
        fprintf(stderr, "\n");
    }

    // Cleanup
    env_release(func_env);
    exec_context_free(ctx);

    pthread_mutex_unlock(&ffi_callback_mutex);
}

// Create a C-callable function pointer from a Hemlock function
FFICallback* ffi_create_callback(Function *fn, Type **param_types, int num_params, Type *return_type, ExecutionContext *ctx) {
    FFICallback *cb = malloc(sizeof(FFICallback));
    if (!cb) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate FFI callback");
        return NULL;
    }

    cb->hemlock_fn = fn;
    function_retain(fn);  // Keep the function alive
    cb->num_params = num_params;
    cb->id = g_next_callback_id++;

    // Copy parameter types
    cb->hemlock_params = malloc(sizeof(Type*) * num_params);
    for (int i = 0; i < num_params; i++) {
        cb->hemlock_params[i] = param_types[i];
    }
    cb->hemlock_return = return_type;

    // Build libffi type arrays
    ffi_type **arg_types = malloc(sizeof(ffi_type*) * num_params);
    for (int i = 0; i < num_params; i++) {
        arg_types[i] = hemlock_type_to_ffi_type(param_types[i]);
    }
    cb->arg_types = (void**)arg_types;

    ffi_type *ret_type = hemlock_type_to_ffi_type(return_type);
    cb->return_type = ret_type;

    // Allocate and prepare the CIF (Call Interface)
    ffi_cif *cif = malloc(sizeof(ffi_cif));
    cb->cif = cif;

    ffi_status status = ffi_prep_cif(cif, FFI_DEFAULT_ABI, num_params, ret_type, arg_types);
    if (status != FFI_OK) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to prepare FFI callback interface");
        function_release(fn);
        free(cb->hemlock_params);
        free(arg_types);
        free(cif);
        free(cb);
        return NULL;
    }

    // Allocate the closure
    void *code_ptr;
    ffi_closure *closure = ffi_closure_alloc(sizeof(ffi_closure), &code_ptr);
    if (!closure) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to allocate FFI closure");
        function_release(fn);
        free(cb->hemlock_params);
        free(arg_types);
        free(cif);
        free(cb);
        return NULL;
    }

    cb->closure = closure;
    cb->code_ptr = code_ptr;

    // Prepare the closure with our handler
    status = ffi_prep_closure_loc(closure, cif, ffi_callback_handler, cb, code_ptr);
    if (status != FFI_OK) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("Failed to prepare FFI closure");
        ffi_closure_free(closure);
        function_release(fn);
        free(cb->hemlock_params);
        free(arg_types);
        free(cif);
        free(cb);
        return NULL;
    }

    // Track the callback for cleanup
    pthread_mutex_lock(&ffi_cache_mutex);
    if (g_callback_state.num_callbacks >= g_callback_state.callbacks_capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        int new_capacity = g_callback_state.callbacks_capacity == 0 ? 8 : g_callback_state.callbacks_capacity * 2;
        if (new_capacity < g_callback_state.callbacks_capacity) {
            // Overflow detected
            pthread_mutex_unlock(&ffi_cache_mutex);
            ffi_closure_free(closure);
            function_release(fn);
            free(cb->hemlock_params);
            free(arg_types);
            free(cif);
            free(cb);
            ctx->exception_state.is_throwing = 1;
            ctx->exception_state.exception_value = val_string("FFI callback cache capacity overflow");
            return NULL;
        }
        g_callback_state.callbacks_capacity = new_capacity;
        g_callback_state.callbacks = realloc(g_callback_state.callbacks, sizeof(FFICallback*) * g_callback_state.callbacks_capacity);
    }
    g_callback_state.callbacks[g_callback_state.num_callbacks++] = cb;
    pthread_mutex_unlock(&ffi_cache_mutex);

    return cb;
}

// Free a callback
void ffi_free_callback(FFICallback *cb) {
    if (cb == NULL) return;

    // Remove from tracking list
    pthread_mutex_lock(&ffi_cache_mutex);
    for (int i = 0; i < g_callback_state.num_callbacks; i++) {
        if (g_callback_state.callbacks[i] == cb) {
            // Shift remaining elements
            for (int j = i; j < g_callback_state.num_callbacks - 1; j++) {
                g_callback_state.callbacks[j] = g_callback_state.callbacks[j + 1];
            }
            g_callback_state.num_callbacks--;
            break;
        }
    }
    pthread_mutex_unlock(&ffi_cache_mutex);

    // Free the closure
    if (cb->closure) {
        ffi_closure_free(cb->closure);
    }

    // Release the Hemlock function
    if (cb->hemlock_fn) {
        function_release(cb->hemlock_fn);
    }

    // Free type arrays
    if (cb->hemlock_params) {
        free(cb->hemlock_params);
    }
    if (cb->arg_types) {
        free(cb->arg_types);
    }
    if (cb->cif) {
        free(cb->cif);
    }

    free(cb);
}

// Get the C-callable function pointer from a callback
void* ffi_callback_get_ptr(FFICallback *cb) {
    return cb ? cb->code_ptr : NULL;
}

// Free a callback by its code pointer (for use by callback_free builtin)
int ffi_free_callback_by_ptr(void *code_ptr) {
    if (code_ptr == NULL) return 0;

    pthread_mutex_lock(&ffi_cache_mutex);
    for (int i = 0; i < g_callback_state.num_callbacks; i++) {
        FFICallback *cb = g_callback_state.callbacks[i];
        if (cb && cb->code_ptr == code_ptr) {
            // Remove from list
            for (int j = i; j < g_callback_state.num_callbacks - 1; j++) {
                g_callback_state.callbacks[j] = g_callback_state.callbacks[j + 1];
            }
            g_callback_state.num_callbacks--;
            pthread_mutex_unlock(&ffi_cache_mutex);

            // Free the callback (now outside the mutex)
            if (cb->closure) {
                ffi_closure_free(cb->closure);
            }
            if (cb->hemlock_fn) {
                function_release(cb->hemlock_fn);
            }
            if (cb->hemlock_params) {
                free(cb->hemlock_params);
            }
            if (cb->arg_types) {
                free(cb->arg_types);
            }
            if (cb->cif) {
                free(cb->cif);
            }
            free(cb);
            return 1;  // Success
        }
    }
    pthread_mutex_unlock(&ffi_cache_mutex);
    return 0;  // Not found
}

// Type helper: Create a Type from a string name
Type* type_from_string(const char *name) {
    Type *type = malloc(sizeof(Type));
    type->type_name = NULL;
    type->element_type = NULL;

    if (strcmp(name, "i8") == 0) {
        type->kind = TYPE_I8;
    } else if (strcmp(name, "i16") == 0) {
        type->kind = TYPE_I16;
    } else if (strcmp(name, "i32") == 0 || strcmp(name, "integer") == 0 || strcmp(name, "int") == 0) {
        type->kind = TYPE_I32;
    } else if (strcmp(name, "i64") == 0 || strcmp(name, "long") == 0) {
        type->kind = TYPE_I64;
    } else if (strcmp(name, "u8") == 0 || strcmp(name, "byte") == 0) {
        type->kind = TYPE_U8;
    } else if (strcmp(name, "u16") == 0) {
        type->kind = TYPE_U16;
    } else if (strcmp(name, "u32") == 0) {
        type->kind = TYPE_U32;
    } else if (strcmp(name, "u64") == 0) {
        type->kind = TYPE_U64;
    } else if (strcmp(name, "f32") == 0 || strcmp(name, "float") == 0) {
        type->kind = TYPE_F32;
    } else if (strcmp(name, "f64") == 0 || strcmp(name, "double") == 0 || strcmp(name, "number") == 0) {
        type->kind = TYPE_F64;
    } else if (strcmp(name, "bool") == 0) {
        type->kind = TYPE_BOOL;
    } else if (strcmp(name, "string") == 0) {
        type->kind = TYPE_STRING;
    } else if (strcmp(name, "ptr") == 0) {
        type->kind = TYPE_PTR;
    } else if (strcmp(name, "void") == 0) {
        type->kind = TYPE_VOID;
    } else if (strcmp(name, "null") == 0) {
        type->kind = TYPE_NULL;
    } else if (strcmp(name, "size_t") == 0 || strcmp(name, "usize") == 0) {
        // size_t is platform-dependent: u64 on 64-bit, u32 on 32-bit
        type->kind = (sizeof(size_t) == 8) ? TYPE_U64 : TYPE_U32;
    } else if (strcmp(name, "intptr_t") == 0 || strcmp(name, "isize") == 0 || strcmp(name, "ssize_t") == 0) {
        // intptr_t is platform-dependent: i64 on 64-bit, i32 on 32-bit
        type->kind = (sizeof(intptr_t) == 8) ? TYPE_I64 : TYPE_I32;
    } else if (strcmp(name, "uintptr_t") == 0) {
        // uintptr_t is platform-dependent: u64 on 64-bit, u32 on 32-bit
        type->kind = (sizeof(uintptr_t) == 8) ? TYPE_U64 : TYPE_U32;
    } else {
        // Check if it's a registered struct type
        FFIStructType *st = ffi_lookup_struct(name);
        if (st) {
            type->kind = TYPE_CUSTOM_OBJECT;
            type->type_name = strdup(name);
        } else {
            // Unknown type - default to void
            type->kind = TYPE_VOID;
        }
    }

    return type;
}

// ========== PUBLIC API ==========

void ffi_init() {
    g_ffi_state.libraries = NULL;
    g_ffi_state.num_libraries = 0;
    g_ffi_state.libraries_capacity = 0;
    g_ffi_state.current_lib = NULL;
}

void ffi_cleanup() {
    // Clean up all callbacks
    for (int i = 0; i < g_callback_state.num_callbacks; i++) {
        FFICallback *cb = g_callback_state.callbacks[i];
        if (cb) {
            // Free the closure
            if (cb->closure) {
                ffi_closure_free(cb->closure);
            }
            // Release the Hemlock function
            if (cb->hemlock_fn) {
                function_release(cb->hemlock_fn);
            }
            // Free type arrays
            if (cb->hemlock_params) {
                free(cb->hemlock_params);
            }
            if (cb->arg_types) {
                free(cb->arg_types);
            }
            if (cb->cif) {
                free(cb->cif);
            }
            free(cb);
        }
    }
    free(g_callback_state.callbacks);
    g_callback_state.callbacks = NULL;
    g_callback_state.num_callbacks = 0;
    g_callback_state.callbacks_capacity = 0;

    // Clean up libraries
    for (int i = 0; i < g_ffi_state.num_libraries; i++) {
        ffi_close_library(g_ffi_state.libraries[i]);
    }
    free(g_ffi_state.libraries);
    g_ffi_state.libraries = NULL;
    g_ffi_state.num_libraries = 0;
    g_ffi_state.libraries_capacity = 0;
    g_ffi_state.current_lib = NULL;

    // Clean up struct registry
    ffi_struct_cleanup();
}

void execute_import_ffi(Stmt *stmt, ExecutionContext *ctx) {
    const char *library_path = stmt->as.import_ffi.library_path;
    FFILibrary *lib = ffi_load_library(library_path, ctx);
    if (lib != NULL) {
        pthread_mutex_lock(&ffi_cache_mutex);
        g_ffi_state.current_lib = lib;
        pthread_mutex_unlock(&ffi_cache_mutex);
    }
}

void execute_extern_fn(Stmt *stmt, Environment *env, ExecutionContext *ctx) {
    pthread_mutex_lock(&ffi_cache_mutex);
    FFILibrary *current_lib = g_ffi_state.current_lib;
    pthread_mutex_unlock(&ffi_cache_mutex);

    if (current_lib == NULL) {
        ctx->exception_state.is_throwing = 1;
        ctx->exception_state.exception_value = val_string("No library imported before extern declaration");
        return;
    }

    const char *function_name = stmt->as.extern_fn.function_name;
    Type **param_types = stmt->as.extern_fn.param_types;
    int num_params = stmt->as.extern_fn.num_params;
    Type *return_type = stmt->as.extern_fn.return_type;

    FFIFunction *func = ffi_declare_function(
        current_lib,
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
