#include "internal.h"
#include "type_promotion.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

/* ========== TYPE CONVERSION HELPERS ==========
 *
 * Convert between ValueType (interpreter) and HmlTypeKind (shared module).
 * This allows us to use the shared type promotion logic.
 */

static inline HmlTypeKind val_to_tk(ValueType vt) {
    switch (vt) {
        case VAL_NULL:   return HML_TK_NULL;
        case VAL_BOOL:   return HML_TK_BOOL;
        case VAL_I8:     return HML_TK_I8;
        case VAL_I16:    return HML_TK_I16;
        case VAL_I32:    return HML_TK_I32;
        case VAL_I64:    return HML_TK_I64;
        case VAL_U8:     return HML_TK_U8;
        case VAL_U16:    return HML_TK_U16;
        case VAL_U32:    return HML_TK_U32;
        case VAL_U64:    return HML_TK_U64;
        case VAL_F32:    return HML_TK_F32;
        case VAL_F64:    return HML_TK_F64;
        case VAL_RUNE:   return HML_TK_RUNE;
        case VAL_STRING: return HML_TK_STRING;
        case VAL_PTR:    return HML_TK_PTR;
        default:         return HML_TK_OTHER;
    }
}

static inline ValueType tk_to_val(HmlTypeKind tk) {
    switch (tk) {
        case HML_TK_NULL:   return VAL_NULL;
        case HML_TK_BOOL:   return VAL_BOOL;
        case HML_TK_I8:     return VAL_I8;
        case HML_TK_I16:    return VAL_I16;
        case HML_TK_I32:    return VAL_I32;
        case HML_TK_I64:    return VAL_I64;
        case HML_TK_U8:     return VAL_U8;
        case HML_TK_U16:    return VAL_U16;
        case HML_TK_U32:    return VAL_U32;
        case HML_TK_U64:    return VAL_U64;
        case HML_TK_F32:    return VAL_F32;
        case HML_TK_F64:    return VAL_F64;
        case HML_TK_RUNE:   return VAL_RUNE;
        case HML_TK_STRING: return VAL_STRING;
        case HML_TK_PTR:    return VAL_PTR;
        default:            return VAL_NULL;
    }
}

// ========== VALUE TYPE NAMES (for error messages) ==========

const char* value_type_name(ValueType type) {
    switch (type) {
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
        case VAL_STRING: return "string";
        case VAL_RUNE: return "rune";
        case VAL_ARRAY: return "array";
        case VAL_OBJECT: return "object";
        case VAL_FUNCTION: return "function";
        case VAL_BUILTIN_FN: return "builtin function";
        case VAL_FILE: return "file";
        case VAL_PTR: return "ptr";
        case VAL_BUFFER: return "buffer";
        case VAL_TASK: return "task";
        case VAL_CHANNEL: return "channel";
        case VAL_FFI_FUNCTION: return "ffi function";
        case VAL_SOCKET: return "socket";
        case VAL_WEBSOCKET: return "websocket";
        case VAL_TYPE: return "type";
        case VAL_REF: return "ref";
        default: return "unknown";
    }
}

// ========== TYPE REGISTRY HASH TABLE ==========

// DJB2 hash function for type names
static uint32_t type_hash(const char *str) {
    uint32_t hash = HML_DJB2_HASH_SEED;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// ========== OBJECT TYPE REGISTRY ==========

ObjectTypeRegistry object_types = {0};

// Rebuild object type hash table
static void object_types_hash_rebuild(void) {
    int new_capacity = object_types.count < 8 ? 16 : object_types.count * 2;

    free(object_types.hash_table);
    object_types.hash_capacity = new_capacity;
    object_types.hash_table = malloc(sizeof(int) * new_capacity);
    if (!object_types.hash_table) {
        fprintf(stderr, "Fatal: Failed to allocate type registry hash table\n");
        exit(1);
    }

    // Initialize all slots to -1 (empty)
    for (int i = 0; i < new_capacity; i++) {
        object_types.hash_table[i] = -1;
    }

    // Rehash all types
    for (int i = 0; i < object_types.count; i++) {
        uint32_t hash = type_hash(object_types.types[i]->name);
        int slot = hash % new_capacity;
        while (object_types.hash_table[slot] != -1) {
            slot = (slot + 1) % new_capacity;
        }
        object_types.hash_table[slot] = i;
    }
}

void init_object_types(void) {
    if (object_types.types == NULL) {
        object_types.capacity = 16;
        object_types.types = malloc(sizeof(ObjectType*) * object_types.capacity);
        object_types.count = 0;
        object_types.hash_table = NULL;
        object_types.hash_capacity = 0;
    }
}

void register_object_type(ObjectType *type) {
    init_object_types();
    if (object_types.count >= object_types.capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (object_types.capacity > INT_MAX / 2) {
            fprintf(stderr, "Fatal: Object type registry capacity overflow\n");
            exit(1);
        }
        int new_capacity = object_types.capacity * 2;
        ObjectType **new_types = realloc(object_types.types, sizeof(ObjectType*) * new_capacity);
        if (!new_types) {
            fprintf(stderr, "Fatal: Failed to expand object type registry\n");
            exit(1);
        }
        object_types.types = new_types;
        object_types.capacity = new_capacity;
    }
    int idx = object_types.count;
    object_types.types[idx] = type;
    object_types.count++;

    // Insert into hash table (rebuild if load factor > 0.7)
    if (object_types.hash_table == NULL ||
        object_types.count * 10 > object_types.hash_capacity * 7) {
        object_types_hash_rebuild();
    } else {
        uint32_t hash = type_hash(type->name);
        int slot = hash % object_types.hash_capacity;
        while (object_types.hash_table[slot] != -1) {
            slot = (slot + 1) % object_types.hash_capacity;
        }
        object_types.hash_table[slot] = idx;
    }
}

ObjectType* lookup_object_type(const char *name) {
    if (name == NULL || object_types.hash_table == NULL || object_types.hash_capacity == 0) {
        return NULL;
    }

    uint32_t hash = type_hash(name);
    int slot = hash % object_types.hash_capacity;
    int start_slot = slot;

    while (object_types.hash_table[slot] != -1) {
        int idx = object_types.hash_table[slot];
        if (strcmp(object_types.types[idx]->name, name) == 0) {
            return object_types.types[idx];
        }
        slot = (slot + 1) % object_types.hash_capacity;
        if (slot == start_slot) break;
    }
    return NULL;
}

void cleanup_object_types(void) {
    if (object_types.types == NULL) {
        return;  // Nothing to clean up
    }

    // Free each ObjectType
    for (int i = 0; i < object_types.count; i++) {
        ObjectType *type = object_types.types[i];
        if (type) {
            // Free type name
            free(type->name);

            // Free type parameters (for generic types)
            if (type->type_params) {
                for (int j = 0; j < type->num_type_params; j++) {
                    free(type->type_params[j]);
                }
                free(type->type_params);
            }

            // Free field names
            for (int j = 0; j < type->num_fields; j++) {
                free(type->field_names[j]);
            }
            free(type->field_names);

            // Free arrays (but not contents - they're AST pointers)
            free(type->field_types);
            free(type->field_optional);
            free(type->field_defaults);

            // Free method names
            if (type->method_names) {
                for (int j = 0; j < type->num_methods; j++) {
                    free(type->method_names[j]);
                }
                free(type->method_names);
            }

            // Free method arrays (but not contents - they're AST pointers)
            free(type->method_types);
            free(type->method_optional);
            free(type->method_defaults);

            // Free the ObjectType struct itself
            free(type);
        }
    }

    // Free the types array and hash table
    free(object_types.types);
    free(object_types.hash_table);

    // Reset the registry
    object_types.types = NULL;
    object_types.count = 0;
    object_types.capacity = 0;
    object_types.hash_table = NULL;
    object_types.hash_capacity = 0;
}

// ========== ENUM TYPE REGISTRY ==========

EnumTypeRegistry enum_types = {0};

// Rebuild enum type hash table
static void enum_types_hash_rebuild(void) {
    int new_capacity = enum_types.count < 8 ? 16 : enum_types.count * 2;

    free(enum_types.hash_table);
    enum_types.hash_capacity = new_capacity;
    enum_types.hash_table = malloc(sizeof(int) * new_capacity);
    if (!enum_types.hash_table) {
        fprintf(stderr, "Fatal: Failed to allocate enum type hash table\n");
        exit(1);
    }

    for (int i = 0; i < new_capacity; i++) {
        enum_types.hash_table[i] = -1;
    }

    for (int i = 0; i < enum_types.count; i++) {
        uint32_t hash = type_hash(enum_types.types[i]->name);
        int slot = hash % new_capacity;
        while (enum_types.hash_table[slot] != -1) {
            slot = (slot + 1) % new_capacity;
        }
        enum_types.hash_table[slot] = i;
    }
}

void init_enum_types(void) {
    if (enum_types.types == NULL) {
        enum_types.capacity = 16;
        enum_types.types = malloc(sizeof(EnumType*) * enum_types.capacity);
        enum_types.count = 0;
        enum_types.hash_table = NULL;
        enum_types.hash_capacity = 0;
    }
}

void register_enum_type(EnumType *type) {
    init_enum_types();
    if (enum_types.count >= enum_types.capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (enum_types.capacity > INT_MAX / 2) {
            fprintf(stderr, "Fatal: Enum type registry capacity overflow\n");
            exit(1);
        }
        int new_capacity = enum_types.capacity * 2;
        EnumType **new_types = realloc(enum_types.types, sizeof(EnumType*) * new_capacity);
        if (!new_types) {
            fprintf(stderr, "Fatal: Failed to expand enum type registry\n");
            exit(1);
        }
        enum_types.types = new_types;
        enum_types.capacity = new_capacity;
    }
    int idx = enum_types.count;
    enum_types.types[idx] = type;
    enum_types.count++;

    // Insert into hash table
    if (enum_types.hash_table == NULL ||
        enum_types.count * 10 > enum_types.hash_capacity * 7) {
        enum_types_hash_rebuild();
    } else {
        uint32_t hash = type_hash(type->name);
        int slot = hash % enum_types.hash_capacity;
        while (enum_types.hash_table[slot] != -1) {
            slot = (slot + 1) % enum_types.hash_capacity;
        }
        enum_types.hash_table[slot] = idx;
    }
}

EnumType* lookup_enum_type(const char *name) {
    if (name == NULL || enum_types.hash_table == NULL || enum_types.hash_capacity == 0) {
        return NULL;
    }

    uint32_t hash = type_hash(name);
    int slot = hash % enum_types.hash_capacity;
    int start_slot = slot;

    while (enum_types.hash_table[slot] != -1) {
        int idx = enum_types.hash_table[slot];
        if (strcmp(enum_types.types[idx]->name, name) == 0) {
            return enum_types.types[idx];
        }
        slot = (slot + 1) % enum_types.hash_capacity;
        if (slot == start_slot) break;
    }
    return NULL;
}

void cleanup_enum_types(void) {
    if (enum_types.types == NULL) {
        return;  // Nothing to clean up
    }

    // Free each EnumType
    for (int i = 0; i < enum_types.count; i++) {
        EnumType *type = enum_types.types[i];
        if (type) {
            // Free type name
            free(type->name);

            // Free variant names
            for (int j = 0; j < type->num_variants; j++) {
                free(type->variant_names[j]);
            }
            free(type->variant_names);

            // Free variant values
            free(type->variant_values);

            // Free the EnumType struct itself
            free(type);
        }
    }

    // Free the types array and hash table
    free(enum_types.types);
    free(enum_types.hash_table);

    // Reset the registry
    enum_types.types = NULL;
    enum_types.count = 0;
    enum_types.capacity = 0;
    enum_types.hash_table = NULL;
    enum_types.hash_capacity = 0;
}

// ========== TYPE ALIAS REGISTRY ==========

TypeAliasRegistry type_aliases = {0};

// Rebuild type alias hash table
static void type_aliases_hash_rebuild(void) {
    int new_capacity = type_aliases.count < 8 ? 16 : type_aliases.count * 2;

    free(type_aliases.hash_table);
    type_aliases.hash_capacity = new_capacity;
    type_aliases.hash_table = malloc(sizeof(int) * new_capacity);
    if (!type_aliases.hash_table) {
        fprintf(stderr, "Fatal: Failed to allocate type alias hash table\n");
        exit(1);
    }

    for (int i = 0; i < new_capacity; i++) {
        type_aliases.hash_table[i] = -1;
    }

    for (int i = 0; i < type_aliases.count; i++) {
        uint32_t hash = type_hash(type_aliases.aliases[i]->name);
        int slot = hash % new_capacity;
        while (type_aliases.hash_table[slot] != -1) {
            slot = (slot + 1) % new_capacity;
        }
        type_aliases.hash_table[slot] = i;
    }
}

void init_type_aliases(void) {
    if (type_aliases.aliases == NULL) {
        type_aliases.capacity = 16;
        type_aliases.aliases = malloc(sizeof(TypeAlias*) * type_aliases.capacity);
        type_aliases.count = 0;
        type_aliases.hash_table = NULL;
        type_aliases.hash_capacity = 0;
    }
}

void register_type_alias(TypeAlias *alias) {
    init_type_aliases();
    if (type_aliases.count >= type_aliases.capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (type_aliases.capacity > INT_MAX / 2) {
            fprintf(stderr, "Fatal: Type alias registry capacity overflow\n");
            exit(1);
        }
        int new_capacity = type_aliases.capacity * 2;
        TypeAlias **new_aliases = realloc(type_aliases.aliases, sizeof(TypeAlias*) * new_capacity);
        if (!new_aliases) {
            fprintf(stderr, "Fatal: Failed to expand type alias registry\n");
            exit(1);
        }
        type_aliases.aliases = new_aliases;
        type_aliases.capacity = new_capacity;
    }
    int idx = type_aliases.count;
    type_aliases.aliases[idx] = alias;
    type_aliases.count++;

    // Insert into hash table
    if (type_aliases.hash_table == NULL ||
        type_aliases.count * 10 > type_aliases.hash_capacity * 7) {
        type_aliases_hash_rebuild();
    } else {
        uint32_t hash = type_hash(alias->name);
        int slot = hash % type_aliases.hash_capacity;
        while (type_aliases.hash_table[slot] != -1) {
            slot = (slot + 1) % type_aliases.hash_capacity;
        }
        type_aliases.hash_table[slot] = idx;
    }
}

TypeAlias* lookup_type_alias(const char *name) {
    if (name == NULL || type_aliases.hash_table == NULL || type_aliases.hash_capacity == 0) {
        return NULL;
    }

    uint32_t hash = type_hash(name);
    int slot = hash % type_aliases.hash_capacity;
    int start_slot = slot;

    while (type_aliases.hash_table[slot] != -1) {
        int idx = type_aliases.hash_table[slot];
        if (strcmp(type_aliases.aliases[idx]->name, name) == 0) {
            return type_aliases.aliases[idx];
        }
        slot = (slot + 1) % type_aliases.hash_capacity;
        if (slot == start_slot) break;
    }
    return NULL;
}

void cleanup_type_aliases(void) {
    if (type_aliases.aliases == NULL) {
        return;  // Nothing to clean up
    }

    // Free each TypeAlias
    for (int i = 0; i < type_aliases.count; i++) {
        TypeAlias *alias = type_aliases.aliases[i];
        if (alias) {
            // Free alias name
            free(alias->name);

            // Free type parameters
            if (alias->type_params) {
                for (int j = 0; j < alias->num_type_params; j++) {
                    free(alias->type_params[j]);
                }
                free(alias->type_params);
            }

            // Note: aliased_type is an AST pointer, don't free it

            // Free the TypeAlias struct itself
            free(alias);
        }
    }

    // Free the aliases array and hash table
    free(type_aliases.aliases);
    free(type_aliases.hash_table);

    // Reset the registry
    type_aliases.aliases = NULL;
    type_aliases.count = 0;
    type_aliases.capacity = 0;
    type_aliases.hash_table = NULL;
    type_aliases.hash_capacity = 0;
}

// ========== TYPE PARAMETER SUBSTITUTION ==========

// Substitute type parameters in a Type with concrete type arguments
// Returns a new Type that must be freed by caller, or the original type if no substitution needed
// type_params: array of type parameter names (e.g., ["T", "U"])
// type_args: array of Type* to substitute for each parameter
// num_params: number of type parameters/arguments
static Type* substitute_type_params(Type *type, char **type_params, Type **type_args, int num_params) {
    if (!type) return NULL;

    // Check if this type is a type parameter reference
    if (type->kind == TYPE_PARAM && type->type_name) {
        for (int i = 0; i < num_params; i++) {
            if (type_params[i] && strcmp(type->type_name, type_params[i]) == 0) {
                // Found matching type parameter - return a copy of the type argument
                // Use calloc to zero-initialize all fields
                Type *result = calloc(1, sizeof(Type));
                result->kind = type_args[i]->kind;
                result->type_name = type_args[i]->type_name ? strdup(type_args[i]->type_name) : NULL;
                result->element_type = type_args[i]->element_type ?
                    substitute_type_params(type_args[i]->element_type, type_params, type_args, num_params) : NULL;
                result->nullable = type_args[i]->nullable;
                // Copy type_args if present
                if (type_args[i]->num_type_args > 0) {
                    result->type_args = malloc(sizeof(Type*) * type_args[i]->num_type_args);
                    result->num_type_args = type_args[i]->num_type_args;
                    for (int j = 0; j < result->num_type_args; j++) {
                        result->type_args[j] = substitute_type_params(type_args[i]->type_args[j],
                                                                       type_params, type_args, num_params);
                    }
                }
                return result;
            }
        }
    }

    // For array types, substitute element type
    if (type->kind == TYPE_ARRAY && type->element_type) {
        Type *new_element = substitute_type_params(type->element_type, type_params, type_args, num_params);
        if (new_element != type->element_type) {
            // Use calloc to zero-initialize all fields
            Type *result = calloc(1, sizeof(Type));
            result->kind = TYPE_ARRAY;
            result->element_type = new_element;
            result->nullable = type->nullable;
            return result;
        }
    }

    // For custom object types with type arguments, substitute recursively
    if (type->kind == TYPE_CUSTOM_OBJECT && type->num_type_args > 0) {
        Type **new_args = malloc(sizeof(Type*) * type->num_type_args);
        int any_changed = 0;
        for (int i = 0; i < type->num_type_args; i++) {
            new_args[i] = substitute_type_params(type->type_args[i], type_params, type_args, num_params);
            if (new_args[i] != type->type_args[i]) {
                any_changed = 1;
            }
        }
        if (any_changed) {
            // Use calloc to zero-initialize all fields
            Type *result = calloc(1, sizeof(Type));
            result->kind = TYPE_CUSTOM_OBJECT;
            result->type_name = type->type_name ? strdup(type->type_name) : NULL;
            result->nullable = type->nullable;
            result->type_args = new_args;
            result->num_type_args = type->num_type_args;
            return result;
        }
        // No changes, free the copies
        for (int i = 0; i < type->num_type_args; i++) {
            if (new_args[i] != type->type_args[i]) {
                type_free(new_args[i]);
            }
        }
        free(new_args);
    }

    // No substitution needed, return original
    return type;
}

// Free a substituted type (deep free)
// Only frees memory that was newly allocated during substitution
static void free_substituted_type(Type *type, Type *original) {
    if (type && type != original) {
        // This is a newly allocated type, free it
        if (type->type_name) free(type->type_name);
        if (type->element_type) {
            // Recursively free if it was also substituted
            free_substituted_type(type->element_type, original ? original->element_type : NULL);
        }
        if (type->type_args) {
            for (int i = 0; i < type->num_type_args; i++) {
                // Only free type_args[i] if it differs from the original's type_args[i]
                // This handles the case where some type_args were substituted and some weren't
                Type *original_arg = (original && original->type_args && i < original->num_type_args)
                                     ? original->type_args[i] : NULL;
                free_substituted_type(type->type_args[i], original_arg);
            }
            free(type->type_args);
        }
        free(type);
    }
}

// Forward declaration for convert_to_type (needed for recursive type checking)
Value convert_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx);

// ========== TYPE STRING FORMATTING ==========

// Format a Type to a human-readable string (uses rotating buffers for multiple calls)
// Uses HML_TYPE_NAME_BUFSIZE from hemlock_limits.h
static const char* type_to_string(Type *type) {
    #define TYPE_STR_BUFFERS 4
    static char buffers[TYPE_STR_BUFFERS][HML_TYPE_NAME_BUFSIZE];
    static int buf_index = 0;

    char *buffer = buffers[buf_index];
    buf_index = (buf_index + 1) % TYPE_STR_BUFFERS;

    if (!type) return "any";

    switch (type->kind) {
        case TYPE_I8: return "i8";
        case TYPE_I16: return "i16";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_U8: return "u8";
        case TYPE_U16: return "u16";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_RUNE: return "rune";
        case TYPE_PTR: return "ptr";
        case TYPE_BUFFER: return "buffer";
        case TYPE_NULL: return "null";
        case TYPE_VOID: return "void";
        case TYPE_INFER: return "any";
        case TYPE_SELF: return "Self";
        case TYPE_ARRAY:
            if (type->element_type) {
                snprintf(buffer, HML_TYPE_NAME_BUFSIZE, "array<%s>", type_to_string(type->element_type));
            } else {
                return "array";
            }
            return buffer;
        case TYPE_CUSTOM_OBJECT:
        case TYPE_GENERIC_OBJECT:
            if (type->type_name) {
                return type->type_name;
            }
            return "object";
        case TYPE_FUNCTION: {
            char *pos = buffer;
            int remaining = HML_TYPE_NAME_BUFSIZE;
            int written;
            if (type->fn_is_async) {
                written = snprintf(pos, remaining, "async fn(");
            } else {
                written = snprintf(pos, remaining, "fn(");
            }
            pos += written;
            remaining -= written;
            for (int i = 0; i < type->fn_num_params && remaining > 0; i++) {
                if (i > 0) {
                    written = snprintf(pos, remaining, ", ");
                    pos += written;
                    remaining -= written;
                }
                const char *param_type_str = type->fn_param_types && type->fn_param_types[i]
                    ? type_to_string(type->fn_param_types[i])
                    : "any";
                written = snprintf(pos, remaining, "%s", param_type_str);
                pos += written;
                remaining -= written;
            }
            if (remaining > 0) {
                if (type->fn_return_type) {
                    snprintf(pos, remaining, "): %s", type_to_string(type->fn_return_type));
                } else {
                    snprintf(pos, remaining, "): void");
                }
            }
            return buffer;
        }
        case TYPE_PARAM:
            return type->type_name ? type->type_name : "T";
        case TYPE_COMPOUND:
            if (type->num_compound_types > 0) {
                char *pos = buffer;
                int remaining = HML_TYPE_NAME_BUFSIZE;
                for (int i = 0; i < type->num_compound_types && remaining > 0; i++) {
                    int written;
                    if (i > 0) {
                        written = snprintf(pos, remaining, " & ");
                        pos += written;
                        remaining -= written;
                    }
                    written = snprintf(pos, remaining, "%s", type_to_string(type->compound_types[i]));
                    pos += written;
                    remaining -= written;
                }
                return buffer;
            }
            return "unknown";
        case TYPE_ENUM:
            return type->type_name ? type->type_name : "enum";
        default:
            return "unknown";
    }
}

// ========== TYPE COMPATIBILITY CHECKING ==========

// Check if a TypeKind represents a numeric type
static int is_numeric_type_kind(TypeKind kind) {
    switch (kind) {
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        case TYPE_F32: case TYPE_F64:
            return 1;
        default:
            return 0;
    }
}

// Check if two types are compatible for method signature validation
// This is permissive: allows numeric coercion and treats NULL/INFER as "any"
static int types_compatible(Type *expected, Type *actual) {
    if (!expected || !actual) return 1;  // Missing type = any
    if (expected->kind == TYPE_INFER || actual->kind == TYPE_INFER) return 1;

    // Self type is compatible with any object (resolved at runtime)
    if (expected->kind == TYPE_SELF || actual->kind == TYPE_SELF) return 1;

    // Self type represented as TYPE_CUSTOM_OBJECT with type_name "Self"
    if (expected->kind == TYPE_CUSTOM_OBJECT && expected->type_name &&
        strcmp(expected->type_name, "Self") == 0) return 1;
    if (actual->kind == TYPE_CUSTOM_OBJECT && actual->type_name &&
        strcmp(actual->type_name, "Self") == 0) return 1;

    // Same kind is always compatible
    if (expected->kind == actual->kind) {
        // For custom objects, check name matches
        if (expected->kind == TYPE_CUSTOM_OBJECT) {
            if (expected->type_name && actual->type_name) {
                return strcmp(expected->type_name, actual->type_name) == 0;
            }
            return 1;  // No name = any object
        }
        // For arrays, check element types
        if (expected->kind == TYPE_ARRAY) {
            return types_compatible(expected->element_type, actual->element_type);
        }
        // For functions, check signatures
        if (expected->kind == TYPE_FUNCTION) {
            // Check async flag
            if (expected->fn_is_async != actual->fn_is_async) return 0;
            // Check parameter count
            if (expected->fn_num_params != actual->fn_num_params) return 0;
            // Check parameter types
            for (int i = 0; i < expected->fn_num_params; i++) {
                if (!types_compatible(expected->fn_param_types[i], actual->fn_param_types[i])) {
                    return 0;
                }
            }
            // Check return type
            return types_compatible(expected->fn_return_type, actual->fn_return_type);
        }
        return 1;
    }

    // Numeric types are compatible with each other (coercion allowed)
    if (is_numeric_type_kind(expected->kind) && is_numeric_type_kind(actual->kind)) {
        return 1;
    }

    // Generic object accepts any object
    if (expected->kind == TYPE_GENERIC_OBJECT && actual->kind == TYPE_CUSTOM_OBJECT) {
        return 1;
    }
    if (expected->kind == TYPE_CUSTOM_OBJECT && actual->kind == TYPE_GENERIC_OBJECT) {
        return 1;
    }

    return 0;
}

// ========== METHOD SIGNATURE VALIDATION ==========

// Validate a function value against an expected method signature type
// Returns 1 if valid, 0 if invalid
// Populates error_msg with details on mismatch (caller should not free - static buffer)
static int validate_method_signature(Function *func, Type *expected_type,
                                      const char *method_name, const char *type_name,
                                      const char **error_msg) {
    static char error_buffer[512];
    *error_msg = NULL;

    if (!expected_type || expected_type->kind != TYPE_FUNCTION) {
        return 1;  // No signature constraint or not a function type
    }

    // Check async flag
    if (expected_type->fn_is_async && !func->is_async) {
        snprintf(error_buffer, sizeof(error_buffer),
            "Method '%s' in type '%s' must be async", method_name, type_name);
        *error_msg = error_buffer;
        return 0;
    }

    // Check parameter count
    // Required parameters in signature must be present in function
    int expected_required = expected_type->fn_num_params;
    for (int i = 0; i < expected_type->fn_num_params; i++) {
        if (expected_type->fn_param_optional && expected_type->fn_param_optional[i]) {
            expected_required--;
        }
    }

    // Function must accept at least as many parameters as required by signature
    if (func->num_params < expected_required) {
        snprintf(error_buffer, sizeof(error_buffer),
            "Method '%s' in type '%s' requires at least %d parameter(s), got %d",
            method_name, type_name, expected_required, func->num_params);
        *error_msg = error_buffer;
        return 0;
    }

    // Check parameter types for matching positions
    int params_to_check = expected_type->fn_num_params < func->num_params
        ? expected_type->fn_num_params : func->num_params;
    for (int i = 0; i < params_to_check; i++) {
        Type *expected_param = expected_type->fn_param_types ? expected_type->fn_param_types[i] : NULL;
        Type *actual_param = func->param_types ? func->param_types[i] : NULL;

        if (!types_compatible(expected_param, actual_param)) {
            snprintf(error_buffer, sizeof(error_buffer),
                "Method '%s' in type '%s': parameter %d type mismatch (expected %s, got %s)",
                method_name, type_name, i + 1,
                type_to_string(expected_param), type_to_string(actual_param));
            *error_msg = error_buffer;
            return 0;
        }
    }

    // Check return type
    Type *expected_return = expected_type->fn_return_type;
    Type *actual_return = func->return_type;
    if (!types_compatible(expected_return, actual_return)) {
        snprintf(error_buffer, sizeof(error_buffer),
            "Method '%s' in type '%s': return type mismatch (expected %s, got %s)",
            method_name, type_name,
            type_to_string(expected_return), type_to_string(actual_return));
        *error_msg = error_buffer;
        return 0;
    }

    return 1;
}

// Check if an object matches a type definition with type arguments (for generics)
// type_args: array of Type* to substitute for type parameters (NULL for non-generic types)
// num_type_args: number of type arguments
static Value check_object_type_generic(Value value, ObjectType *object_type,
                                 Type **type_args, int num_type_args,
                                 Environment *env, ExecutionContext *ctx) {
    if (value.type != VAL_OBJECT) {
        fprintf(stderr, "Runtime error: Expected object for type '%s', got non-object\n",
                object_type->name);
        exit(1);
    }

    // Validate type argument count for generic types
    if (object_type->num_type_params > 0) {
        if (num_type_args != object_type->num_type_params) {
            fprintf(stderr, "Runtime error: Type '%s' expects %d type argument(s), got %d\n",
                    object_type->name, object_type->num_type_params, num_type_args);
            exit(1);
        }
    }

    Object *obj = value.as.as_object;

    // Check all required fields
    for (int i = 0; i < object_type->num_fields; i++) {
        const char *field_name = object_type->field_names[i];
        int field_optional = object_type->field_optional[i];
        Type *field_type = object_type->field_types[i];

        // Substitute type parameters if this is a generic type
        Type *substituted_field_type = field_type;
        if (field_type && object_type->num_type_params > 0 && type_args) {
            substituted_field_type = substitute_type_params(
                field_type, object_type->type_params, type_args, num_type_args);
        }

        // Look for field in object
        int found = 0;
        Value field_value;
        int field_index = -1;
        for (int j = 0; j < obj->num_fields; j++) {
            if (strcmp(obj->fields[j].name, field_name) == 0) {
                found = 1;
                field_value = obj->fields[j].value;
                field_index = j;
                break;
            }
        }

        if (!found) {
            // Field not present - check if it's optional
            if (field_optional) {
                // Add field with default value or null
                if (obj->num_fields >= obj->capacity) {
                    // SECURITY: Check for integer overflow before doubling capacity
                    if (obj->capacity > INT_MAX / 2) {
                        free_substituted_type(substituted_field_type, field_type);
                        fprintf(stderr, "Runtime error: Object field capacity overflow\n");
                        exit(1);
                    }
                    int new_capacity = obj->capacity * 2;
                    FieldEntry *new_fields = object_grow_fields(obj, new_capacity);
                    if (!new_fields) {
                        free_substituted_type(substituted_field_type, field_type);
                        fprintf(stderr, "Runtime error: Failed to expand object fields\n");
                        exit(1);
                    }
                }

                char *name_copy = strdup(field_name);
                if (!name_copy) {
                    free_substituted_type(substituted_field_type, field_type);
                    fprintf(stderr, "Runtime error: Failed to allocate field name\n");
                    exit(1);
                }
                obj->fields[obj->num_fields].name = name_copy;
                if (object_type->field_defaults[i]) {
                    obj->fields[obj->num_fields].value = eval_expr(object_type->field_defaults[i], env, ctx);
                } else {
                    obj->fields[obj->num_fields].value = val_null();
                }
                obj->num_fields++;
            } else {
                // Free substituted type if allocated
                free_substituted_type(substituted_field_type, field_type);
                fprintf(stderr, "Runtime error: Object missing required field '%s' for type '%s'\n",
                        field_name, object_type->name);
                exit(1);
            }
        } else if (substituted_field_type && substituted_field_type->kind != TYPE_INFER) {
            // Type check the field using the (possibly substituted) type
            // Use convert_to_type for recursive type checking (handles arrays, objects, etc.)
            Value converted = convert_to_type(field_value, substituted_field_type, env, ctx);
            obj->fields[field_index].value = converted;
        }

        // Free substituted type if it was newly allocated
        free_substituted_type(substituted_field_type, field_type);
    }

    // Check all required method signatures
    for (int i = 0; i < object_type->num_methods; i++) {
        const char *method_name = object_type->method_names[i];
        int method_optional = object_type->method_optional[i];

        // Look for method in object (stored as a field with function value)
        int found = 0;
        Value method_value;
        for (int j = 0; j < obj->num_fields; j++) {
            if (strcmp(obj->fields[j].name, method_name) == 0) {
                found = 1;
                method_value = obj->fields[j].value;
                break;
            }
        }

        if (!found) {
            // Method not present
            if (method_optional) {
                // Optional method - provide default implementation if available
                if (object_type->method_defaults[i]) {
                    // Add method with default implementation
                    if (obj->num_fields >= obj->capacity) {
                        // SECURITY: Check for integer overflow before doubling capacity
                        if (obj->capacity > INT_MAX / 2) {
                            fprintf(stderr, "Runtime error: Object field capacity overflow for method\n");
                            exit(1);
                        }
                        int new_capacity = obj->capacity * 2;
                        FieldEntry *new_fields = object_grow_fields(obj, new_capacity);
                        if (!new_fields) {
                            fprintf(stderr, "Runtime error: Failed to expand object fields for method\n");
                            exit(1);
                        }
                    }

                    char *name_copy = strdup(method_name);
                    if (!name_copy) {
                        fprintf(stderr, "Runtime error: Failed to allocate method name\n");
                        exit(1);
                    }
                    obj->fields[obj->num_fields].name = name_copy;
                    obj->fields[obj->num_fields].value = eval_expr(object_type->method_defaults[i], env, ctx);
                    obj->num_fields++;
                }
                // If no default, optional methods are simply not required
            } else {
                // Required method missing
                fprintf(stderr, "Runtime error: Object missing required method '%s' for type '%s'\n",
                        method_name, object_type->name);
                exit(1);
            }
        } else {
            // Method found - verify it's a function
            if (method_value.type != VAL_FUNCTION) {
                fprintf(stderr, "Runtime error: Property '%s' must be a function for type '%s'\n",
                        method_name, object_type->name);
                exit(1);
            }

            // Validate method signature against expected type
            Type *method_type = object_type->method_types[i];

            // Handle type parameter substitution for generic types
            Type *substituted_method_type = method_type;
            if (method_type && object_type->num_type_params > 0 && type_args) {
                substituted_method_type = substitute_type_params(
                    method_type, object_type->type_params, type_args, num_type_args);
            }

            if (substituted_method_type && substituted_method_type->kind == TYPE_FUNCTION) {
                Function *func = method_value.as.as_function;
                const char *error_msg;
                if (!validate_method_signature(func, substituted_method_type,
                                                method_name, object_type->name, &error_msg)) {
                    // Free substituted type before error exit
                    free_substituted_type(substituted_method_type, method_type);
                    fprintf(stderr, "Runtime error: %s\n", error_msg);
                    exit(1);
                }
            }

            // Free substituted type if it was newly allocated
            free_substituted_type(substituted_method_type, method_type);
        }
    }

    // Set the type name on the object (include type args for display)
    if (obj->type_name) free(obj->type_name);
    obj->type_name = strdup(object_type->name);

    return value;
}

// Check if an object matches a type definition (duck typing) - non-generic version
Value check_object_type(Value value, ObjectType *object_type, Environment *env, ExecutionContext *ctx) {
    return check_object_type_generic(value, object_type, NULL, 0, env, ctx);
}

// ========== TYPE CHECKING HELPERS ==========

// Helper: Check if a value is any integer type
int is_integer(Value val) {
    return val.type == VAL_I8 || val.type == VAL_I16 || val.type == VAL_I32 || val.type == VAL_I64 ||
           val.type == VAL_U8 || val.type == VAL_U16 || val.type == VAL_U32 || val.type == VAL_U64;
}

// Helper: Check if a value is any float type
int is_float(Value val) {
    return val.type == VAL_F32 || val.type == VAL_F64;
}

// Helper: Check if a value is any numeric type
int is_numeric(Value val) {
    return is_integer(val) || is_float(val);
}

// Helper: Convert a value to a string key for object property access.
// Returns true if the value can be coerced (integers, floats, bools, runes).
bool value_coerce_to_key(Value val, char *buf, size_t buf_size) {
    // Check rune and bool before integer (runtime hml_is_integer includes runes)
    if (val.type == VAL_RUNE) {
        int len = utf8_encode(val.as.as_rune, buf);
        buf[len] = '\0';
        return true;
    }
    if (val.type == VAL_BOOL) {
        snprintf(buf, buf_size, "%s", val.as.as_bool ? "true" : "false");
        return true;
    }
    if (is_integer(val)) {
        snprintf(buf, buf_size, "%" PRId64, value_to_int64(val));
        return true;
    }
    if (is_float(val)) {
        snprintf(buf, buf_size, "%.17g", value_to_float(val));
        return true;
    }
    return false;
}

// Helper: Convert any integer to i32 (for operations)
int32_t value_to_int(Value val) {
    switch (val.type) {
        case VAL_I8: return val.as.as_i8;
        case VAL_I16: return val.as.as_i16;
        case VAL_I32: return val.as.as_i32;
        case VAL_I64: return (int32_t)val.as.as_i64;  // potential overflow
        case VAL_U8: return val.as.as_u8;
        case VAL_U16: return val.as.as_u16;
        case VAL_U32: return (int32_t)val.as.as_u32;  // potential overflow
        case VAL_U64: return (int32_t)val.as.as_u64;  // potential overflow
        case VAL_RUNE: return (int32_t)val.as.as_rune;  // rune is a uint32_t codepoint
        case VAL_BOOL: return val.as.as_bool;
        default:
            fprintf(stderr, "Runtime error: Cannot convert to int\n");
            exit(1);
    }
}

// Helper: Convert any integer to i64 (preserves full range)
int64_t value_to_int64(Value val) {
    switch (val.type) {
        case VAL_I8: return (int64_t)val.as.as_i8;
        case VAL_I16: return (int64_t)val.as.as_i16;
        case VAL_I32: return (int64_t)val.as.as_i32;
        case VAL_I64: return val.as.as_i64;
        case VAL_U8: return (int64_t)val.as.as_u8;
        case VAL_U16: return (int64_t)val.as.as_u16;
        case VAL_U32: return (int64_t)val.as.as_u32;
        case VAL_U64: return (int64_t)val.as.as_u64;  // May lose high bit
        case VAL_BOOL: return val.as.as_bool;
        default:
            fprintf(stderr, "Runtime error: Cannot convert to int64\n");
            exit(1);
    }
}

// Helper: Convert any numeric to f64 (for operations)
double value_to_float(Value val) {
    switch (val.type) {
        case VAL_I8: return (double)val.as.as_i8;
        case VAL_I16: return (double)val.as.as_i16;
        case VAL_I32: return (double)val.as.as_i32;
        case VAL_I64: return (double)val.as.as_i64;
        case VAL_U8: return (double)val.as.as_u8;
        case VAL_U16: return (double)val.as.as_u16;
        case VAL_U32: return (double)val.as.as_u32;
        case VAL_U64: return (double)val.as.as_u64;
        case VAL_F32: return (double)val.as.as_f32;
        case VAL_F64: return val.as.as_f64;
        default:
            fprintf(stderr, "Runtime error: Cannot convert to float\n");
            exit(1);
    }
}

// Helper: Check if value is truthy
int value_is_truthy(Value val) {
    if (val.type == VAL_BOOL) {
        return val.as.as_bool;
    } else if (is_integer(val)) {
        return value_to_int(val) != 0;
    } else if (is_float(val)) {
        return value_to_float(val) != 0.0;
    } else if (val.type == VAL_NULL) {
        return 0;
    } else if (val.type == VAL_STRING) {
        // Empty string is falsy, non-empty string is truthy
        return val.as.as_string != NULL && val.as.as_string->length > 0;
    } else if (val.type == VAL_ARRAY) {
        // Empty array is falsy, non-empty array is truthy
        return val.as.as_array != NULL && val.as.as_array->length > 0;
    }
    return 1;  // objects, functions, etc. are truthy
}

// ========== TYPE PROMOTION ==========

// Helper to convert ValueType to descriptive string for error messages
static const char* value_type_to_string(ValueType type) {
    switch (type) {
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

/*
 * Type promotion using shared module.
 * These wrapper functions convert between ValueType and HmlTypeKind,
 * use the shared implementation, then convert back.
 */

// Get the "rank" of a type for promotion rules (using shared module)
int type_rank(ValueType type) {
    return hml_tk_priority(val_to_tk(type));
}

// Determine the result type for binary operations (using shared module)
ValueType promote_types(ValueType left, ValueType right) {
    HmlTypeKind result = hml_tk_promote(val_to_tk(left), val_to_tk(right));
    return tk_to_val(result);
}

// Convert a value to a specific ValueType (for promotions during operations)
Value promote_value(Value val, ValueType target_type) {
    if (val.type == target_type) return val;

    switch (target_type) {
        case VAL_I8: return val_i8((int8_t)value_to_int(val));
        case VAL_I16: return val_i16((int16_t)value_to_int(val));
        case VAL_I32: return val_i32(value_to_int(val));
        case VAL_I64:
            // For i64, need to handle full range
            if (is_float(val)) {
                return val_i64((int64_t)value_to_float(val));
            } else if (val.type == VAL_I64) {
                return val;
            } else if (val.type == VAL_U64) {
                return val_i64((int64_t)val.as.as_u64);
            } else {
                return val_i64((int64_t)value_to_int(val));
            }
        case VAL_U8: return val_u8((uint8_t)value_to_int(val));
        case VAL_U16: return val_u16((uint16_t)value_to_int(val));
        case VAL_U32: return val_u32((uint32_t)value_to_int(val));
        case VAL_U64:
            // For u64, need to handle full range
            if (is_float(val)) {
                return val_u64((uint64_t)value_to_float(val));
            } else if (val.type == VAL_U64) {
                return val;
            } else if (val.type == VAL_I64) {
                return val_u64((uint64_t)val.as.as_i64);
            } else {
                return val_u64((uint64_t)value_to_int(val));
            }
        case VAL_F32:
            // Use value_to_float to preserve precision for i64/u64 values
            return val_f32((float)value_to_float(val));
        case VAL_F64:
            // Use value_to_float to preserve precision for i64/u64 values
            return val_f64(value_to_float(val));
        case VAL_RUNE:
            // Rune is a Unicode codepoint (u32)
            if (val.type == VAL_RUNE) {
                return val;
            } else {
                return val_rune((uint32_t)value_to_int(val));
            }
        default:
            fprintf(stderr, "Runtime error: Cannot promote value of type '%s' to target type '%s'\n",
                    value_type_to_string(val.type), value_type_to_string(target_type));
            exit(1);
    }
}

// ========== TYPE CONVERSION ==========

// Helper to check if a TypeKind is a numeric type
static int is_integer_target(TypeKind kind) {
    switch (kind) {
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
            return 1;
        default:
            return 0;
    }
}

static int is_numeric_type(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:
        case TYPE_I16:
        case TYPE_I32:
        case TYPE_I64:
        case TYPE_U8:
        case TYPE_U16:
        case TYPE_U32:
        case TYPE_U64:
        case TYPE_F32:
        case TYPE_F64:
            return 1;
        default:
            return 0;
    }
}

// Helper to convert TypeKind to string name
static const char* type_kind_to_string(TypeKind kind) {
    switch (kind) {
        case TYPE_I8: return "i8";
        case TYPE_I16: return "i16";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_U8: return "u8";
        case TYPE_U16: return "u16";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_RUNE: return "rune";
        case TYPE_PTR: return "ptr";
        case TYPE_BUFFER: return "buffer";
        case TYPE_ARRAY: return "array";
        case TYPE_NULL: return "null";
        default: return "unknown";
    }
}

// Helper to convert a value to a target type
Value convert_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx) {
    if (!target_type) {
        return value;  // No type annotation
    }

    // If target type is nullable and value is null, allow it
    if (target_type->nullable && value.type == VAL_NULL) {
        return value;
    }

    TypeKind kind = target_type->kind;

    // Handle function types early - validate function signature matches
    if (kind == TYPE_FUNCTION) {
        if (value.type == VAL_FUNCTION) {
            Function *func = value.as.as_function;

            // Validate async flag
            if (target_type->fn_is_async && !func->is_async) {
                fprintf(stderr, "Runtime error: Expected async function, got non-async function\n");
                exit(1);
            }

            // Calculate required parameters (non-optional) for both sides
            int expected_required = target_type->fn_num_params;
            for (int i = 0; i < target_type->fn_num_params; i++) {
                if (target_type->fn_param_optional && target_type->fn_param_optional[i]) {
                    expected_required--;
                }
            }

            int actual_required = func->num_params;
            for (int i = 0; i < func->num_params; i++) {
                if (func->param_defaults && func->param_defaults[i]) {
                    actual_required--;
                }
            }

            // Function must accept at least as many required parameters as the type specifies
            // and not require more parameters than the type provides
            if (actual_required > target_type->fn_num_params) {
                fprintf(stderr, "Runtime error: Function requires %d parameter(s), but type expects at most %d\n",
                        actual_required, target_type->fn_num_params);
                exit(1);
            }

            if (func->num_params < expected_required) {
                fprintf(stderr, "Runtime error: Function accepts %d parameter(s), but type requires at least %d\n",
                        func->num_params, expected_required);
                exit(1);
            }

            // Check parameter types for matching positions
            int params_to_check = target_type->fn_num_params < func->num_params
                ? target_type->fn_num_params : func->num_params;
            for (int i = 0; i < params_to_check; i++) {
                Type *expected_param = target_type->fn_param_types ? target_type->fn_param_types[i] : NULL;
                Type *actual_param = func->param_types ? func->param_types[i] : NULL;

                if (!types_compatible(expected_param, actual_param)) {
                    fprintf(stderr, "Runtime error: Function parameter %d type mismatch (expected %s, got %s)\n",
                            i + 1, type_to_string(expected_param), type_to_string(actual_param));
                    exit(1);
                }
            }

            // Check return type
            if (!types_compatible(target_type->fn_return_type, func->return_type)) {
                fprintf(stderr, "Runtime error: Function return type mismatch (expected %s, got %s)\n",
                        type_to_string(target_type->fn_return_type), type_to_string(func->return_type));
                exit(1);
            }

            return value;
        }

        // Also accept builtin functions (no signature validation possible)
        if (value.type == VAL_BUILTIN_FN) {
            return value;
        }

        fprintf(stderr, "Runtime error: Expected function value\n");
        exit(1);
    }

    // Handle object and enum types (both use TYPE_CUSTOM_OBJECT at parse time)
    if (kind == TYPE_CUSTOM_OBJECT) {
        // Check if it's an enum type first
        EnumType *enum_type = lookup_enum_type(target_type->type_name);
        if (enum_type) {
            // Enum values are i32
            if (value.type != VAL_I32) {
                fprintf(stderr, "Runtime error: Expected enum value (i32) for type '%s'\n",
                        target_type->type_name);
                exit(1);
            }
            // Validate that the value is one of the valid enum variants
            int32_t val = value.as.as_i32;
            int is_valid_variant = 0;
            for (int i = 0; i < enum_type->num_variants; i++) {
                if (enum_type->variant_values[i] == val) {
                    is_valid_variant = 1;
                    break;
                }
            }
            if (!is_valid_variant) {
                fprintf(stderr, "Runtime error: Value %d is not a valid variant of enum '%s'\n",
                        val, enum_type->name);
                exit(1);
            }
            return value;
        }

        // Not an enum, try type alias
        TypeAlias *alias = lookup_type_alias(target_type->type_name);
        if (alias) {
            // Handle generic type aliases with type argument substitution
            if (alias->num_type_params > 0) {
                // Generic type alias - verify type arguments and substitute
                if (target_type->num_type_args != alias->num_type_params) {
                    fprintf(stderr, "Runtime error: Type alias '%s' expects %d type argument(s), got %d\n",
                            alias->name, alias->num_type_params, target_type->num_type_args);
                    exit(1);
                }

                // Substitute type parameters in the aliased type with actual type arguments
                Type *substituted = substitute_type_params(alias->aliased_type,
                                                          alias->type_params,
                                                          target_type->type_args,
                                                          alias->num_type_params);

                // Recursively check against the substituted type
                Value result = convert_to_type(value, substituted, env, ctx);

                // Free the substituted type if it was newly allocated
                free_substituted_type(substituted, alias->aliased_type);

                return result;
            }
            // Non-generic alias - just resolve directly
            return convert_to_type(value, alias->aliased_type, env, ctx);
        }

        // Not a type alias, try object type
        ObjectType *object_type = lookup_object_type(target_type->type_name);
        if (!object_type) {
            fprintf(stderr, "Runtime error: Unknown type '%s'\n", target_type->type_name);
            exit(1);
        }

        // For generic types, pass type arguments for substitution
        if (target_type->num_type_args > 0) {
            return check_object_type_generic(value, object_type,
                                             target_type->type_args, target_type->num_type_args,
                                             env, ctx);
        }
        return check_object_type(value, object_type, env, ctx);
    }

    if (kind == TYPE_GENERIC_OBJECT) {
        if (value.type != VAL_OBJECT) {
            fprintf(stderr, "Runtime error: Expected object, got non-object\n");
            exit(1);
        }
        return value;
    }

    // Handle compound types (A & B & C) - value must satisfy ALL constituent types
    if (kind == TYPE_COMPOUND) {
        if (value.type != VAL_OBJECT) {
            fprintf(stderr, "Runtime error: Compound type requires an object\n");
            exit(1);
        }

        // Check against each constituent type
        for (int i = 0; i < target_type->num_compound_types; i++) {
            Type *constituent = target_type->compound_types[i];
            value = convert_to_type(value, constituent, env, ctx);
        }
        return value;
    }

    // Handle typed arrays
    if (kind == TYPE_ARRAY) {
        if (value.type != VAL_ARRAY) {
            fprintf(stderr, "Runtime error: Expected array, got non-array\n");
            exit(1);
        }

        Array *arr = value.as.as_array;

        // If target_type has no element_type, it's an untyped array (let arr: array = ...)
        if (target_type->element_type == NULL) {
            // No type constraint, just verify it's an array (already done above)
            return value;
        }

        // Target has element type constraint - apply it to the array
        // If array already has element_type, validate compatibility
        if (arr->element_type != NULL) {
            // Check if existing type matches target type
            if (arr->element_type->kind != target_type->element_type->kind) {
                fprintf(stderr, "Runtime error: Array element type mismatch\n");
                exit(1);
            }
        } else {
            // Set the element type constraint on the array
            // Clone the type to avoid lifetime issues
            arr->element_type = malloc(sizeof(Type));
            arr->element_type->kind = target_type->element_type->kind;
            arr->element_type->type_name = target_type->element_type->type_name ? strdup(target_type->element_type->type_name) : NULL;
            arr->element_type->element_type = NULL;  // Don't support nested typed arrays yet
            arr->element_type->nullable = 0;
            arr->element_type->compound_types = NULL;
            arr->element_type->num_compound_types = 0;
            arr->element_type->type_args = NULL;
            arr->element_type->num_type_args = 0;
            // Initialize function type fields to NULL
            arr->element_type->fn_param_types = NULL;
            arr->element_type->fn_param_names = NULL;
            arr->element_type->fn_param_optional = NULL;
            arr->element_type->fn_param_is_const = NULL;
            arr->element_type->fn_num_params = 0;
            arr->element_type->fn_rest_param_name = NULL;
            arr->element_type->fn_rest_param_type = NULL;
            arr->element_type->fn_return_type = NULL;
            arr->element_type->fn_is_async = 0;
        }

        // Validate all existing elements match the type constraint
        for (int i = 0; i < arr->length; i++) {
            Value elem = arr->elements[i];
            // Convert/validate each element against the element type
            arr->elements[i] = convert_to_type(elem, target_type->element_type, env, ctx);
        }

        return value;
    }

    // Original function continues with TypeKind
    TypeKind target_kind = kind;
    // Get the source value as the widest type for range checking
    int64_t int_val = 0;
    double float_val = 0.0;
    int is_source_float = 0;

    // Extract source value (use int64 to preserve full range)
    if (is_integer(value)) {
        int_val = value_to_int64(value);
    } else if (is_float(value)) {
        float_val = value_to_float(value);
        is_source_float = 1;
    } else if (value.type == VAL_BOOL) {
        // Allow bool -> int conversions
        int_val = value.as.as_bool;
    } else if (value.type == VAL_RUNE) {
        // Allow rune -> int conversions (get codepoint value)
        int_val = value.as.as_rune;
    } else if (value.type == VAL_STRING && target_kind == TYPE_STRING) {
        return value;  // String to string, ok
    } else if (value.type == VAL_STRING && target_kind == TYPE_BOOL) {
        // String to bool via type annotation is not allowed
        // Use explicit conversion: bool("true") or bool("false")
        runtime_error(ctx, "Cannot convert string to bool via type annotation. Use bool(\"...\") instead.");
        return val_null();
    } else if (value.type == VAL_STRING && is_numeric_type(target_kind)) {
        // String to numeric via type annotation is not allowed
        // Use explicit conversion: i32("42"), f64("3.14"), etc.
        const char *type_name = type_kind_to_string(target_kind);
        runtime_error(ctx, "Cannot convert string to %s via type annotation. Use %s(\"...\") instead.",
                type_name, type_name);
        return val_null();
    } else if (value.type == VAL_BOOL && target_kind == TYPE_BOOL) {
        return value;  // Bool to bool, ok
    } else if (value.type == VAL_NULL && target_kind == TYPE_NULL) {
        return value;  // Null to null, ok
    } else if (value.type == VAL_RUNE && target_kind == TYPE_RUNE) {
        return value;  // Rune to rune, ok
    } else if (value.type == VAL_PTR && target_kind == TYPE_PTR) {
        return value;  // Ptr to ptr, ok
    } else if (value.type == VAL_BUFFER && target_kind == TYPE_BUFFER) {
        return value;  // Buffer to buffer, ok
    } else if (value.type == VAL_OBJECT && target_kind == TYPE_CUSTOM_OBJECT) {
        // Object to custom object - already handled above, but be permissive
        return value;
    } else {
        runtime_error(ctx, "Cannot convert type to target type");
        return val_null();
    }

    // Check for NaN/Inf/out-of-range before float-to-int cast (which is UB in C)
    if (is_source_float && is_integer_target(target_kind)) {
        if (isnan(float_val)) {
            runtime_error(ctx, "Cannot convert NaN to %s", type_kind_to_string(target_kind));
            return val_null();
        }
        if (isinf(float_val)) {
            runtime_error(ctx, "Cannot convert %sInfinity to %s",
                    float_val < 0 ? "-" : "", type_kind_to_string(target_kind));
            return val_null();
        }
        if (float_val > 9.223372036854775e+18 || float_val < -9.223372036854776e+18) {
            runtime_error(ctx, "Float value out of range for integer conversion to %s", type_kind_to_string(target_kind));
            return val_null();
        }
    }

    switch (target_kind) {
        case TYPE_I8:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < HML_I8_MIN || int_val > HML_I8_MAX) {
                runtime_error(ctx, "Value %" PRId64 " out of range for i8 [%d, %d]", int_val, HML_I8_MIN, HML_I8_MAX);
                return val_null();
            }
            return val_i8((int8_t)int_val);

        case TYPE_I16:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < HML_I16_MIN || int_val > HML_I16_MAX) {
                runtime_error(ctx, "Value %" PRId64 " out of range for i16 [%d, %d]", int_val, HML_I16_MIN, HML_I16_MAX);
                return val_null();
            }
            return val_i16((int16_t)int_val);

        case TYPE_I32:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < HML_I32_MIN || int_val > HML_I32_MAX) {
                runtime_error(ctx, "Value %" PRId64 " out of range for i32 [%lld, %lld]", int_val, (long long)HML_I32_MIN, (long long)HML_I32_MAX);
                return val_null();
            }
            return val_i32((int32_t)int_val);

        case TYPE_I64:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            // i64 can hold full int64_t range, no additional check needed for int_val
            // For float values, precision loss may occur but that's expected
            return val_i64(int_val);

        case TYPE_U8:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > HML_U8_MAX) {
                runtime_error(ctx, "Value %" PRId64 " out of range for u8 [0, %d]", int_val, HML_U8_MAX);
                return val_null();
            }
            return val_u8((uint8_t)int_val);

        case TYPE_U16:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > HML_U16_MAX) {
                runtime_error(ctx, "Value %" PRId64 " out of range for u16 [0, %d]", int_val, HML_U16_MAX);
                return val_null();
            }
            return val_u16((uint16_t)int_val);

        case TYPE_U32:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > HML_U32_MAX) {
                runtime_error(ctx, "Value %" PRId64 " out of range for u32 [0, %lld]", int_val, (long long)HML_U32_MAX);
                return val_null();
            }
            return val_u32((uint32_t)int_val);

        case TYPE_U64:
            if (is_source_float) {
                if (float_val < 0.0 || float_val > (double)HML_U64_MAX) {
                    fprintf(stderr, "Runtime error: Value %g out of range for u64 [0, 18446744073709551615]\n", float_val);
                    exit(1);
                }
                return val_u64((uint64_t)float_val);
            }
            // If source is already u64, pass through directly
            if (value.type == VAL_U64) {
                return value;
            }
            if (int_val < 0) {
                runtime_error(ctx, "Value %" PRId64 " out of range for u64 [0, 18446744073709551615]", int_val);
                return val_null();
            }
            return val_u64((uint64_t)int_val);

        case TYPE_F32:
            if (is_source_float) {
                return val_f32((float)float_val);
            } else {
                return val_f32((float)int_val);
            }

        case TYPE_F64:
            if (is_source_float) {
                return val_f64(float_val);
            } else {
                return val_f64((double)int_val);
            }

        case TYPE_BOOL:
            if (value.type == VAL_BOOL) {
                return value;
            }
            // String -> bool is handled above with early return
            // Allow conversion from numeric types to bool (0 = false, non-zero = true)
            if (is_source_float) {
                return val_bool(float_val != 0.0);
            }
            return val_bool(int_val != 0);

        case TYPE_STRING:
            if (value.type == VAL_STRING) {
                return value;
            }
            // Allow conversion from rune to string
            if (value.type == VAL_RUNE) {
                char rune_bytes[5];  // Max 4 bytes + null terminator
                int rune_len = utf8_encode(value.as.as_rune, rune_bytes);
                rune_bytes[rune_len] = '\0';
                return val_string(rune_bytes);
            }
            // Allow conversion from bool to string
            if (value.type == VAL_BOOL) {
                return val_string(value.as.as_bool ? "true" : "false");
            }
            // Allow conversion from numeric types to string
            if (is_integer(value)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRId64, value_to_int64(value));
                return val_string(buf);
            }
            if (is_float(value)) {
                char buf[64];
                double fval = value_to_float(value);
                // Use %g to avoid trailing zeros, but ensure we get enough precision
                snprintf(buf, sizeof(buf), "%.17g", fval);
                return val_string(buf);
            }
            runtime_error(ctx, "Cannot convert to string");
            return val_null();

        case TYPE_RUNE:
            if (value.type == VAL_RUNE) {
                return value;
            }
            // Allow conversion from integers to rune
            if (is_integer(value)) {
                int64_t codepoint = value_to_int(value);
                if (codepoint < 0 || codepoint > 0x10FFFF) {
                    runtime_error(ctx, "Value %" PRId64 " out of range for rune [0, 0x10FFFF]", codepoint);
                    return val_null();
                }
                return val_rune((uint32_t)codepoint);
            }
            runtime_error(ctx, "Cannot convert to rune");
            return val_null();

        case TYPE_PTR:
            if (value.type == VAL_PTR) {
                return value;
            }
            runtime_error(ctx, "Cannot convert to ptr");
            return val_null();

        case TYPE_BUFFER:
            if (value.type == VAL_BUFFER) {
                return value;
            }
            runtime_error(ctx, "Cannot convert to buffer");
            return val_null();

        case TYPE_ARRAY:
            if (value.type == VAL_ARRAY) {
                return value;
            }
            runtime_error(ctx, "Cannot convert to array");
            return val_null();

        case TYPE_NULL:
            return val_null();

        case TYPE_INFER:
            return value;  // No conversion needed

        case TYPE_ENUM:
            // Enum type should be handled earlier in the function
            // If we reach here, something went wrong
            runtime_error(ctx, "Enum type should be handled earlier");
            return val_null();

        case TYPE_VOID:
            // Void type is only used for FFI function return types
            // Should not be converted to in normal code
            runtime_error(ctx, "Cannot convert to void type");
            return val_null();

        case TYPE_CUSTOM_OBJECT:
        case TYPE_GENERIC_OBJECT:
            // These should have been handled above in the early return
            runtime_error(ctx, "Internal error - object type not handled properly");
            return val_null();

        case TYPE_PARAM:
            // Type parameters should be substituted before reaching this point
            // If we get here, it means we're using a generic type without type arguments
            runtime_error(ctx, "Unresolved type parameter - generic type requires type arguments");
            return val_null();

        case TYPE_COMPOUND:
            // Compound types are handled earlier in this function
            runtime_error(ctx, "Internal error - compound type not handled properly");
            return val_null();

        case TYPE_FUNCTION:
            // Function types are used for type annotations on callbacks
            // At runtime, we just verify it's a function
            if (value.type == VAL_FUNCTION) {
                return value;
            }
            runtime_error(ctx, "Expected function value");
            return val_null();

        case TYPE_SELF:
            // Self type should be substituted with the concrete type before runtime
            runtime_error(ctx, "Unresolved Self type - should be substituted before runtime");
            return val_null();
    }

    runtime_error(ctx, "Unknown type conversion");
    return val_null();
}

// Parse a value to a target type (for type constructors like i32("42"))
// This function ALLOWS string parsing, unlike convert_to_type
Value parse_string_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx) {
    if (!target_type) {
        return value;
    }

    TypeKind target_kind = target_type->kind;

    // Handle string parsing for type constructors
    if (value.type == VAL_STRING && target_kind == TYPE_BOOL) {
        // String to bool: check for "true" or "false"
        String *str = value.as.as_string;
        if (str && str->length == 4 &&
            str->data[0] == 't' && str->data[1] == 'r' &&
            str->data[2] == 'u' && str->data[3] == 'e') {
            return val_bool(1);
        } else if (str && str->length == 5 &&
            str->data[0] == 'f' && str->data[1] == 'a' &&
            str->data[2] == 'l' && str->data[3] == 's' && str->data[4] == 'e') {
            return val_bool(0);
        }
        runtime_error(ctx, "Cannot parse string as bool (expected 'true' or 'false')");
        return val_null();
    } else if (value.type == VAL_STRING && is_numeric_type(target_kind)) {
        // String to numeric conversion - parse the string
        String *str = value.as.as_string;
        int64_t int_val = 0;
        double float_val = 0.0;
        int is_float = 0;

        if (str && str->length > 0) {
            // Create null-terminated copy for parsing
            char *cstr = malloc(str->length + 1);
            memcpy(cstr, str->data, str->length);
            cstr[str->length] = '\0';

            // Try to parse as number
            char *endptr;
            errno = 0;

            // Check for float (contains '.' or 'e'/'E')
            int has_decimal = 0;
            for (int i = 0; i < str->length; i++) {
                if (cstr[i] == '.' || cstr[i] == 'e' || cstr[i] == 'E') {
                    has_decimal = 1;
                    break;
                }
            }

            if (has_decimal) {
                float_val = strtod(cstr, &endptr);
                if (endptr == cstr || *endptr != '\0') {
                    runtime_error(ctx, "Cannot parse '%s' as number", cstr);
                    free(cstr);
                    return val_null();
                }
                is_float = 1;
            } else {
                int_val = strtoll(cstr, &endptr, 0);  // base 0 supports hex, octal
                if (endptr == cstr || *endptr != '\0') {
                    runtime_error(ctx, "Cannot parse '%s' as integer", cstr);
                    free(cstr);
                    return val_null();
                }
            }
            free(cstr);
        } else {
            runtime_error(ctx, "Cannot convert empty string to number");
            return val_null();
        }

        // Check for NaN/Inf/out-of-range before float-to-int cast (which is UB in C)
        if (is_float && is_integer_target(target_kind)) {
            if (isnan(float_val)) {
                runtime_error(ctx, "Cannot convert NaN to %s", type_kind_to_string(target_kind));
                return val_null();
            }
            if (isinf(float_val)) {
                runtime_error(ctx, "Cannot convert %sInfinity to %s",
                        float_val < 0 ? "-" : "", type_kind_to_string(target_kind));
                return val_null();
            }
            if (float_val > 9.223372036854775e+18 || float_val < -9.223372036854776e+18) {
                runtime_error(ctx, "Float value out of range for integer conversion to %s", type_kind_to_string(target_kind));
                return val_null();
            }
        }

        // Now convert to target type with range checking
        switch (target_kind) {
            case TYPE_I8:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -128 || int_val > 127) {
                    runtime_error(ctx, "Value %" PRId64 " out of range for i8 [-128, 127]", int_val);
                    return val_null();
                }
                return val_i8((int8_t)int_val);

            case TYPE_I16:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -32768 || int_val > 32767) {
                    runtime_error(ctx, "Value %" PRId64 " out of range for i16 [-32768, 32767]", int_val);
                    return val_null();
                }
                return val_i16((int16_t)int_val);

            case TYPE_I32:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < HML_I32_MIN || int_val > HML_I32_MAX) {
                    runtime_error(ctx, "Value %" PRId64 " out of range for i32", int_val);
                    return val_null();
                }
                return val_i32((int32_t)int_val);

            case TYPE_I64:
                if (is_float) int_val = (int64_t)float_val;
                return val_i64(int_val);

            case TYPE_U8:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > HML_U8_MAX) {
                    runtime_error(ctx, "Value %" PRId64 " out of range for u8 [0, %d]", int_val, HML_U8_MAX);
                    return val_null();
                }
                return val_u8((uint8_t)int_val);

            case TYPE_U16:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > HML_U16_MAX) {
                    runtime_error(ctx, "Value %" PRId64 " out of range for u16 [0, %d]", int_val, HML_U16_MAX);
                    return val_null();
                }
                return val_u16((uint16_t)int_val);

            case TYPE_U32:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > HML_U32_MAX) {
                    runtime_error(ctx, "Value %" PRId64 " out of range for u32", int_val);
                    return val_null();
                }
                return val_u32((uint32_t)int_val);

            case TYPE_U64:
                if (is_float) {
                    if (float_val < 0.0 || float_val > (double)HML_U64_MAX) {
                        fprintf(stderr, "Runtime error: Value %g out of range for u64\n", float_val);
                        exit(1);
                    }
                    return val_u64((uint64_t)float_val);
                }
                if (int_val < 0) {
                    runtime_error(ctx, "Value %" PRId64 " out of range for u64", int_val);
                    return val_null();
                }
                return val_u64((uint64_t)int_val);

            case TYPE_F32:
                if (is_float) {
                    return val_f32((float)float_val);
                } else {
                    return val_f32((float)int_val);
                }

            case TYPE_F64:
                if (is_float) {
                    return val_f64(float_val);
                } else {
                    return val_f64((double)int_val);
                }

            default:
                break;
        }
    }

    // For non-string values, fall back to regular convert_to_type
    return convert_to_type(value, target_type, env, ctx);
}
