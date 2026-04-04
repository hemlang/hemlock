/*
 * Hemlock Compiler - Type Constructors and Utilities
 *
 * Functions for creating, cloning, freeing, and naming checked types.
 */

#include "type_check_internal.h"

// ========== TYPE CONSTRUCTORS ==========

CheckedType* checked_type_primitive(CheckedTypeKind kind) {
    CheckedType *type = calloc(1, sizeof(CheckedType));
    type->kind = kind;
    return type;
}

CheckedType* checked_type_array(CheckedType *element_type) {
    CheckedType *type = calloc(1, sizeof(CheckedType));
    type->kind = CHECKED_ARRAY;
    type->element_type = element_type;
    return type;
}

CheckedType* checked_type_custom(const char *name) {
    CheckedType *type = calloc(1, sizeof(CheckedType));
    type->kind = CHECKED_CUSTOM;
    type->type_name = strdup(name);
    return type;
}

CheckedType* checked_type_function(CheckedType **param_types, int num_params,
                                   CheckedType *return_type, int has_rest_param) {
    CheckedType *type = calloc(1, sizeof(CheckedType));
    type->kind = CHECKED_FUNCTION;
    type->num_params = num_params;
    type->has_rest_param = has_rest_param;
    type->return_type = return_type;
    if (num_params > 0 && param_types) {
        type->param_types = calloc(num_params, sizeof(CheckedType*));
        for (int i = 0; i < num_params; i++) {
            type->param_types[i] = param_types[i];
        }
    }
    return type;
}

CheckedType* checked_type_nullable(CheckedType *base) {
    CheckedType *type = checked_type_clone(base);
    type->nullable = 1;
    return type;
}

CheckedType* checked_type_clone(const CheckedType *type) {
    if (!type) return NULL;

    CheckedType *clone = calloc(1, sizeof(CheckedType));
    clone->kind = type->kind;
    clone->nullable = type->nullable;

    if (type->type_name) {
        clone->type_name = strdup(type->type_name);
    }
    if (type->element_type) {
        clone->element_type = checked_type_clone(type->element_type);
    }
    if (type->return_type) {
        clone->return_type = checked_type_clone(type->return_type);
    }
    clone->num_params = type->num_params;
    clone->has_rest_param = type->has_rest_param;
    if (type->num_params > 0 && type->param_types) {
        clone->param_types = calloc(type->num_params, sizeof(CheckedType*));
        for (int i = 0; i < type->num_params; i++) {
            clone->param_types[i] = checked_type_clone(type->param_types[i]);
        }
    }
    // Clone type arguments (for generic types like Stack<i32>)
    clone->num_type_args = type->num_type_args;
    if (type->num_type_args > 0 && type->type_args) {
        clone->type_args = calloc(type->num_type_args, sizeof(CheckedType*));
        for (int i = 0; i < type->num_type_args; i++) {
            clone->type_args[i] = checked_type_clone(type->type_args[i]);
        }
    }

    // Clone compound types
    clone->num_compound_types = type->num_compound_types;
    if (type->num_compound_types > 0 && type->compound_types) {
        clone->compound_types = calloc(type->num_compound_types, sizeof(CheckedType*));
        for (int i = 0; i < type->num_compound_types; i++) {
            clone->compound_types[i] = checked_type_clone(type->compound_types[i]);
        }
    }

    return clone;
}

void checked_type_free(CheckedType *type) {
    if (!type) return;
    free(type->type_name);
    checked_type_free(type->element_type);
    checked_type_free(type->return_type);
    if (type->param_types) {
        for (int i = 0; i < type->num_params; i++) {
            checked_type_free(type->param_types[i]);
        }
        free(type->param_types);
    }
    // Free compound types
    if (type->compound_types) {
        for (int i = 0; i < type->num_compound_types; i++) {
            checked_type_free(type->compound_types[i]);
        }
        free(type->compound_types);
    }
    // Free type arguments (for generic types)
    if (type->type_args) {
        for (int i = 0; i < type->num_type_args; i++) {
            checked_type_free(type->type_args[i]);
        }
        free(type->type_args);
    }
    free(type);
}

CheckedType* checked_type_from_ast(Type *ast_type) {
    if (!ast_type) return checked_type_primitive(CHECKED_ANY);

    CheckedType *type = calloc(1, sizeof(CheckedType));
    type->nullable = ast_type->nullable;

    switch (ast_type->kind) {
        case TYPE_I8:    type->kind = CHECKED_I8; break;
        case TYPE_I16:   type->kind = CHECKED_I16; break;
        case TYPE_I32:   type->kind = CHECKED_I32; break;
        case TYPE_I64:   type->kind = CHECKED_I64; break;
        case TYPE_U8:    type->kind = CHECKED_U8; break;
        case TYPE_U16:   type->kind = CHECKED_U16; break;
        case TYPE_U32:   type->kind = CHECKED_U32; break;
        case TYPE_U64:   type->kind = CHECKED_U64; break;
        case TYPE_F32:   type->kind = CHECKED_F32; break;
        case TYPE_F64:   type->kind = CHECKED_F64; break;
        case TYPE_BOOL:  type->kind = CHECKED_BOOL; break;
        case TYPE_STRING: type->kind = CHECKED_STRING; break;
        case TYPE_RUNE:  type->kind = CHECKED_RUNE; break;
        case TYPE_PTR:   type->kind = CHECKED_PTR; break;
        case TYPE_BUFFER: type->kind = CHECKED_BUFFER; break;
        case TYPE_NULL:  type->kind = CHECKED_NULL; break;
        case TYPE_VOID:  type->kind = CHECKED_VOID; break;
        case TYPE_ARRAY:
            type->kind = CHECKED_ARRAY;
            if (ast_type->element_type) {
                type->element_type = checked_type_from_ast(ast_type->element_type);
            }
            break;
        case TYPE_CUSTOM_OBJECT:
            type->kind = CHECKED_CUSTOM;
            if (ast_type->type_name) {
                type->type_name = strdup(ast_type->type_name);
            }
            // Handle type arguments for generic types (e.g., Stack<i32>)
            if (ast_type->num_type_args > 0) {
                type->num_type_args = ast_type->num_type_args;
                type->type_args = calloc(ast_type->num_type_args, sizeof(CheckedType*));
                for (int i = 0; i < ast_type->num_type_args; i++) {
                    type->type_args[i] = checked_type_from_ast(ast_type->type_args[i]);
                }
            }
            break;
        case TYPE_GENERIC_OBJECT:
            type->kind = CHECKED_OBJECT;
            break;
        case TYPE_ENUM:
            type->kind = CHECKED_ENUM;
            if (ast_type->type_name) {
                type->type_name = strdup(ast_type->type_name);
            }
            break;
        case TYPE_COMPOUND:
            type->kind = CHECKED_COMPOUND;
            type->num_compound_types = ast_type->num_compound_types;
            if (ast_type->num_compound_types > 0 && ast_type->compound_types) {
                type->compound_types = calloc(ast_type->num_compound_types, sizeof(CheckedType*));
                for (int i = 0; i < ast_type->num_compound_types; i++) {
                    type->compound_types[i] = checked_type_from_ast(ast_type->compound_types[i]);
                }
            }
            break;
        case TYPE_PARAM:
            type->kind = CHECKED_PARAM;
            if (ast_type->type_name) {
                type->type_name = strdup(ast_type->type_name);
            }
            break;
        case TYPE_INFER:
        default:
            type->kind = CHECKED_ANY;
            break;
    }

    return type;
}

CheckedType* checked_type_from_ast_ctx(TypeCheckContext *ctx, Type *ast_type) {
    if (!ast_type) return checked_type_primitive(CHECKED_ANY);

    // Check if this is a custom type that's actually a type alias
    if (ast_type->kind == TYPE_CUSTOM_OBJECT && ast_type->type_name && ctx) {
        TypeAliasDef *alias = type_check_lookup_type_alias(ctx, ast_type->type_name);
        if (alias && alias->aliased_type) {
            // Return a clone of the aliased type
            CheckedType *resolved = checked_type_clone(alias->aliased_type);
            // Preserve nullable from the original
            if (ast_type->nullable) {
                resolved->nullable = 1;
            }
            return resolved;
        }
    }

    // Handle arrays with context for element type resolution
    if (ast_type->kind == TYPE_ARRAY) {
        CheckedType *type = calloc(1, sizeof(CheckedType));
        type->kind = CHECKED_ARRAY;
        type->nullable = ast_type->nullable;
        if (ast_type->element_type) {
            type->element_type = checked_type_from_ast_ctx(ctx, ast_type->element_type);
        }
        return type;
    }

    // Handle compound types with context
    if (ast_type->kind == TYPE_COMPOUND) {
        CheckedType *type = calloc(1, sizeof(CheckedType));
        type->kind = CHECKED_COMPOUND;
        type->nullable = ast_type->nullable;
        type->num_compound_types = ast_type->num_compound_types;
        if (ast_type->num_compound_types > 0 && ast_type->compound_types) {
            type->compound_types = calloc(ast_type->num_compound_types, sizeof(CheckedType*));
            for (int i = 0; i < ast_type->num_compound_types; i++) {
                type->compound_types[i] = checked_type_from_ast_ctx(ctx, ast_type->compound_types[i]);
            }
        }
        return type;
    }

    // For other types, use the base function
    return checked_type_from_ast(ast_type);
}

// ========== TYPE NAME HELPERS ==========

const char* checked_type_kind_name(CheckedTypeKind kind) {
    switch (kind) {
        case CHECKED_UNKNOWN: return "unknown";
        case CHECKED_I8:      return "i8";
        case CHECKED_I16:     return "i16";
        case CHECKED_I32:     return "i32";
        case CHECKED_I64:     return "i64";
        case CHECKED_U8:      return "u8";
        case CHECKED_U16:     return "u16";
        case CHECKED_U32:     return "u32";
        case CHECKED_U64:     return "u64";
        case CHECKED_F32:     return "f32";
        case CHECKED_F64:     return "f64";
        case CHECKED_BOOL:    return "bool";
        case CHECKED_STRING:  return "string";
        case CHECKED_RUNE:    return "rune";
        case CHECKED_NULL:    return "null";
        case CHECKED_PTR:     return "ptr";
        case CHECKED_BUFFER:  return "buffer";
        case CHECKED_ARRAY:   return "array";
        case CHECKED_OBJECT:  return "object";
        case CHECKED_CUSTOM:  return "object";
        case CHECKED_FUNCTION: return "function";
        case CHECKED_TASK:    return "task";
        case CHECKED_CHANNEL: return "channel";
        case CHECKED_FILE:    return "file";
        case CHECKED_ENUM:    return "enum";
        case CHECKED_VOID:    return "void";
        case CHECKED_ANY:     return "any";
        case CHECKED_NUMERIC: return "numeric";
        case CHECKED_INTEGER: return "integer";
        case CHECKED_COMPOUND: return "compound";
        case CHECKED_PARAM:   return "type parameter";
        default:              return "unknown";
    }
}

const char* checked_type_name(CheckedType *type) {
    // Use rotating buffers to allow multiple calls in one expression (e.g., error messages)
    // Uses HML_TYPE_NAME_BUFSIZE from hemlock_limits.h
    #define TYPE_NAME_BUFFERS 4
    static char buffers[TYPE_NAME_BUFFERS][HML_TYPE_NAME_BUFSIZE];
    static int buf_index = 0;

    char *buffer = buffers[buf_index];
    buf_index = (buf_index + 1) % TYPE_NAME_BUFFERS;

    if (!type) return "unknown";

    if (type->kind == CHECKED_CUSTOM && type->type_name) {
        snprintf(buffer, HML_TYPE_NAME_BUFSIZE, "%s%s",
                 type->type_name, type->nullable ? "?" : "");
        return buffer;
    }

    if (type->kind == CHECKED_ARRAY) {
        if (type->element_type) {
            // Get element type name first (uses its own buffer slot)
            const char *elem_name = checked_type_name(type->element_type);
            snprintf(buffer, HML_TYPE_NAME_BUFSIZE, "array<%s>%s",
                     elem_name, type->nullable ? "?" : "");
        } else {
            snprintf(buffer, HML_TYPE_NAME_BUFSIZE, "array%s",
                     type->nullable ? "?" : "");
        }
        return buffer;
    }

    if (type->kind == CHECKED_ENUM && type->type_name) {
        snprintf(buffer, HML_TYPE_NAME_BUFSIZE, "%s%s",
                 type->type_name, type->nullable ? "?" : "");
        return buffer;
    }

    if (type->kind == CHECKED_COMPOUND && type->compound_types && type->num_compound_types > 0) {
        // Build "A & B & C" format
        char *pos = buffer;
        int remaining = HML_TYPE_NAME_BUFSIZE;
        for (int i = 0; i < type->num_compound_types; i++) {
            const char *name = checked_type_name(type->compound_types[i]);
            int written;
            if (i == 0) {
                written = snprintf(pos, remaining, "%s", name);
            } else {
                written = snprintf(pos, remaining, " & %s", name);
            }
            if (written >= remaining) break;
            pos += written;
            remaining -= written;
        }
        if (type->nullable && remaining > 1) {
            snprintf(pos, remaining, "?");
        }
        return buffer;
    }

    const char *base = checked_type_kind_name(type->kind);
    if (type->nullable) {
        snprintf(buffer, HML_TYPE_NAME_BUFSIZE, "%s?", base);
        return buffer;
    }
    return base;
}
