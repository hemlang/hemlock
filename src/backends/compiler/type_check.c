/*
 * Hemlock Compiler - Compile-Time Type Checking
 *
 * Implementation of static type analysis for the Hemlock compiler.
 */

#include "type_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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
        case TYPE_INFER:
        default:
            type->kind = CHECKED_ANY;
            break;
    }

    return type;
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
        default:              return "unknown";
    }
}

const char* checked_type_name(CheckedType *type) {
    // Use rotating buffers to allow multiple calls in one expression (e.g., error messages)
    #define TYPE_NAME_BUFFERS 4
    #define TYPE_NAME_BUFSIZE 256
    static char buffers[TYPE_NAME_BUFFERS][TYPE_NAME_BUFSIZE];
    static int buf_index = 0;

    char *buffer = buffers[buf_index];
    buf_index = (buf_index + 1) % TYPE_NAME_BUFFERS;

    if (!type) return "unknown";

    if (type->kind == CHECKED_CUSTOM && type->type_name) {
        snprintf(buffer, TYPE_NAME_BUFSIZE, "%s%s",
                 type->type_name, type->nullable ? "?" : "");
        return buffer;
    }

    if (type->kind == CHECKED_ARRAY) {
        if (type->element_type) {
            // Get element type name first (uses its own buffer slot)
            const char *elem_name = checked_type_name(type->element_type);
            snprintf(buffer, TYPE_NAME_BUFSIZE, "array<%s>%s",
                     elem_name, type->nullable ? "?" : "");
        } else {
            snprintf(buffer, TYPE_NAME_BUFSIZE, "array%s",
                     type->nullable ? "?" : "");
        }
        return buffer;
    }

    if (type->kind == CHECKED_ENUM && type->type_name) {
        snprintf(buffer, TYPE_NAME_BUFSIZE, "%s%s",
                 type->type_name, type->nullable ? "?" : "");
        return buffer;
    }

    const char *base = checked_type_kind_name(type->kind);
    if (type->nullable) {
        snprintf(buffer, TYPE_NAME_BUFSIZE, "%s?", base);
        return buffer;
    }
    return base;
}

// ========== CONTEXT MANAGEMENT ==========

TypeCheckContext* type_check_new(const char *filename) {
    TypeCheckContext *ctx = calloc(1, sizeof(TypeCheckContext));
    ctx->filename = filename;
    ctx->current_env = calloc(1, sizeof(TypeCheckEnv));
    return ctx;
}

void type_check_free(TypeCheckContext *ctx) {
    if (!ctx) return;

    // Free environment stack
    while (ctx->current_env) {
        TypeCheckEnv *env = ctx->current_env;
        ctx->current_env = env->parent;

        TypeCheckBinding *b = env->bindings;
        while (b) {
            TypeCheckBinding *next = b->next;
            free(b->name);
            checked_type_free(b->type);
            free(b);
            b = next;
        }
        free(env);
    }

    // Free function signatures
    FunctionSig *f = ctx->functions;
    while (f) {
        FunctionSig *next = f->next;
        free(f->name);
        if (f->param_types) {
            for (int i = 0; i < f->num_params; i++) {
                checked_type_free(f->param_types[i]);
            }
            free(f->param_types);
        }
        if (f->param_names) {
            for (int i = 0; i < f->num_params; i++) {
                free(f->param_names[i]);
            }
            free(f->param_names);
        }
        free(f->param_optional);
        checked_type_free(f->return_type);
        free(f);
        f = next;
    }

    // Free object definitions
    ObjectDef *o = ctx->object_defs;
    while (o) {
        ObjectDef *next = o->next;
        free(o->name);
        if (o->field_names) {
            for (int i = 0; i < o->num_fields; i++) {
                free(o->field_names[i]);
            }
            free(o->field_names);
        }
        if (o->field_types) {
            for (int i = 0; i < o->num_fields; i++) {
                checked_type_free(o->field_types[i]);
            }
            free(o->field_types);
        }
        free(o->field_optional);
        free(o);
        o = next;
    }

    // Free enum definitions
    EnumDef *e = ctx->enum_defs;
    while (e) {
        EnumDef *next = e->next;
        free(e->name);
        if (e->variant_names) {
            for (int i = 0; i < e->num_variants; i++) {
                free(e->variant_names[i]);
            }
            free(e->variant_names);
        }
        free(e);
        e = next;
    }

    // Free unboxable variable list
    UnboxableVar *u = ctx->unboxable_vars;
    while (u) {
        UnboxableVar *next = u->next;
        free(u->name);
        free(u);
        u = next;
    }

    free(ctx->current_function_name);
    checked_type_free(ctx->current_return_type);
    free(ctx);
}

// ========== ENVIRONMENT OPERATIONS ==========

void type_check_push_scope(TypeCheckContext *ctx) {
    TypeCheckEnv *env = calloc(1, sizeof(TypeCheckEnv));
    env->parent = ctx->current_env;
    ctx->current_env = env;
}

void type_check_pop_scope(TypeCheckContext *ctx) {
    if (!ctx->current_env) return;

    TypeCheckEnv *env = ctx->current_env;
    ctx->current_env = env->parent;

    TypeCheckBinding *b = env->bindings;
    while (b) {
        TypeCheckBinding *next = b->next;
        free(b->name);
        checked_type_free(b->type);
        free(b);
        b = next;
    }
    free(env);
}

void type_check_bind(TypeCheckContext *ctx, const char *name, CheckedType *type,
                     int is_const, int line) {
    TypeCheckBinding *binding = calloc(1, sizeof(TypeCheckBinding));
    binding->name = strdup(name);
    binding->type = type;
    binding->is_const = is_const;
    binding->line = line;
    binding->next = ctx->current_env->bindings;
    ctx->current_env->bindings = binding;
}

CheckedType* type_check_lookup(TypeCheckContext *ctx, const char *name) {
    for (TypeCheckEnv *env = ctx->current_env; env; env = env->parent) {
        for (TypeCheckBinding *b = env->bindings; b; b = b->next) {
            if (strcmp(b->name, name) == 0) {
                return b->type;
            }
        }
    }
    return NULL;
}

int type_check_is_const(TypeCheckContext *ctx, const char *name) {
    for (TypeCheckEnv *env = ctx->current_env; env; env = env->parent) {
        for (TypeCheckBinding *b = env->bindings; b; b = b->next) {
            if (strcmp(b->name, name) == 0) {
                return b->is_const;
            }
        }
    }
    return 0;
}

// ========== FUNCTION REGISTRATION ==========

void type_check_register_function(TypeCheckContext *ctx, const char *name,
                                  CheckedType **param_types, char **param_names,
                                  int *param_optional, int num_params,
                                  CheckedType *return_type,
                                  int has_rest_param, int is_async) {
    FunctionSig *sig = calloc(1, sizeof(FunctionSig));
    sig->name = strdup(name);
    sig->num_params = num_params;
    sig->has_rest_param = has_rest_param;
    sig->is_async = is_async;
    sig->return_type = return_type;

    // Count required parameters
    sig->num_required = 0;
    for (int i = 0; i < num_params; i++) {
        if (!param_optional || !param_optional[i]) {
            sig->num_required = i + 1;  // Last required param index + 1
        }
    }

    if (num_params > 0) {
        sig->param_types = calloc(num_params, sizeof(CheckedType*));
        sig->param_names = calloc(num_params, sizeof(char*));
        sig->param_optional = calloc(num_params, sizeof(int));
        for (int i = 0; i < num_params; i++) {
            sig->param_types[i] = param_types ? param_types[i] : NULL;
            sig->param_names[i] = param_names ? strdup(param_names[i]) : NULL;
            sig->param_optional[i] = param_optional ? param_optional[i] : 0;
        }
    }

    sig->next = ctx->functions;
    ctx->functions = sig;
}

FunctionSig* type_check_lookup_function(TypeCheckContext *ctx, const char *name) {
    for (FunctionSig *f = ctx->functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            return f;
        }
    }
    return NULL;
}

// ========== OBJECT DEFINITION REGISTRATION ==========

void type_check_register_object(TypeCheckContext *ctx, const char *name,
                                char **field_names, CheckedType **field_types,
                                int *field_optional, int num_fields) {
    ObjectDef *def = calloc(1, sizeof(ObjectDef));
    def->name = strdup(name);
    def->num_fields = num_fields;

    if (num_fields > 0) {
        def->field_names = calloc(num_fields, sizeof(char*));
        def->field_types = calloc(num_fields, sizeof(CheckedType*));
        def->field_optional = calloc(num_fields, sizeof(int));
        for (int i = 0; i < num_fields; i++) {
            def->field_names[i] = strdup(field_names[i]);
            def->field_types[i] = field_types[i];
            def->field_optional[i] = field_optional ? field_optional[i] : 0;
        }
    }

    def->next = ctx->object_defs;
    ctx->object_defs = def;
}

ObjectDef* type_check_lookup_object(TypeCheckContext *ctx, const char *name) {
    for (ObjectDef *o = ctx->object_defs; o; o = o->next) {
        if (strcmp(o->name, name) == 0) {
            return o;
        }
    }
    return NULL;
}

// ========== ENUM REGISTRATION ==========

void type_check_register_enum(TypeCheckContext *ctx, const char *name,
                              char **variant_names, int num_variants) {
    EnumDef *def = calloc(1, sizeof(EnumDef));
    def->name = strdup(name);
    def->num_variants = num_variants;

    if (num_variants > 0) {
        def->variant_names = calloc(num_variants, sizeof(char*));
        for (int i = 0; i < num_variants; i++) {
            def->variant_names[i] = strdup(variant_names[i]);
        }
    }

    def->next = ctx->enum_defs;
    ctx->enum_defs = def;
}

EnumDef* type_check_lookup_enum(TypeCheckContext *ctx, const char *name) {
    for (EnumDef *e = ctx->enum_defs; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return NULL;
}

// ========== TYPE COMPATIBILITY ==========

int type_is_numeric(CheckedType *type) {
    if (!type) return 0;
    switch (type->kind) {
        case CHECKED_I8: case CHECKED_I16: case CHECKED_I32: case CHECKED_I64:
        case CHECKED_U8: case CHECKED_U16: case CHECKED_U32: case CHECKED_U64:
        case CHECKED_F32: case CHECKED_F64:
        case CHECKED_NUMERIC: case CHECKED_INTEGER:
            return 1;
        default:
            return 0;
    }
}

int type_is_integer(CheckedType *type) {
    if (!type) return 0;
    switch (type->kind) {
        case CHECKED_I8: case CHECKED_I16: case CHECKED_I32: case CHECKED_I64:
        case CHECKED_U8: case CHECKED_U16: case CHECKED_U32: case CHECKED_U64:
        case CHECKED_INTEGER:
            return 1;
        default:
            return 0;
    }
}

int type_is_float(CheckedType *type) {
    if (!type) return 0;
    return type->kind == CHECKED_F32 || type->kind == CHECKED_F64;
}

int type_equals(CheckedType *a, CheckedType *b) {
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->nullable != b->nullable) return 0;

    if (a->kind == CHECKED_CUSTOM || a->kind == CHECKED_ENUM) {
        if (!a->type_name || !b->type_name) return 0;
        return strcmp(a->type_name, b->type_name) == 0;
    }

    if (a->kind == CHECKED_ARRAY) {
        if (!a->element_type && !b->element_type) return 1;
        if (!a->element_type || !b->element_type) return 1; // Untyped array matches any
        return type_equals(a->element_type, b->element_type);
    }

    return 1;
}

int type_is_assignable(CheckedType *to, CheckedType *from) {
    if (!to || !from) return 1; // Unknown types are permissive

    // ANY type accepts anything
    if (to->kind == CHECKED_ANY || from->kind == CHECKED_ANY) return 1;
    if (to->kind == CHECKED_UNKNOWN || from->kind == CHECKED_UNKNOWN) return 1;

    // Null can be assigned to nullable types
    if (from->kind == CHECKED_NULL) {
        return to->nullable || to->kind == CHECKED_NULL;
    }

    // Exact match
    if (type_equals(to, from)) return 1;

    // Nullable target accepts non-nullable source
    if (to->nullable && !from->nullable) {
        CheckedType temp = *to;
        temp.nullable = 0;
        if (type_equals(&temp, from)) return 1;
    }

    // Numeric conversions - Hemlock allows all numeric conversions at compile time
    // and validates ranges at runtime. This is consistent with Hemlock's dynamic nature.
    if (type_is_numeric(to) && type_is_numeric(from)) {
        return 1;  // All numeric conversions allowed, runtime validates range
    }

    // Rune to integer - rune is a Unicode codepoint (essentially an integer)
    if (type_is_integer(to) && from->kind == CHECKED_RUNE) {
        return 1;  // Rune codepoint to integer is valid
    }

    // Numeric/rune to bool - truthy conversion (0/0.0 = false, non-zero = true)
    if (to->kind == CHECKED_BOOL && (type_is_numeric(from) || from->kind == CHECKED_RUNE)) {
        return 1;  // Truthy conversion is valid
    }

    // Any value to string - Hemlock supports string coercion for all types
    if (to->kind == CHECKED_STRING) {
        // All basic types can be converted to string
        if (type_is_numeric(from) || from->kind == CHECKED_BOOL ||
            from->kind == CHECKED_RUNE || from->kind == CHECKED_NULL) {
            return 1;  // Value to string coercion is valid
        }
    }

    // Array compatibility
    if (to->kind == CHECKED_ARRAY && from->kind == CHECKED_ARRAY) {
        // Untyped array accepts any array
        if (!to->element_type) return 1;
        if (!from->element_type) return 1; // Untyped source to typed target - runtime check
        return type_is_assignable(to->element_type, from->element_type);
    }

    // Object to custom object (duck typing)
    if (to->kind == CHECKED_CUSTOM && from->kind == CHECKED_OBJECT) {
        return 1; // Will be checked at runtime
    }

    // Custom objects with same name
    if (to->kind == CHECKED_CUSTOM && from->kind == CHECKED_CUSTOM) {
        if (to->type_name && from->type_name) {
            return strcmp(to->type_name, from->type_name) == 0;
        }
    }

    return 0;
}

CheckedType* type_common(CheckedType *a, CheckedType *b) {
    if (!a) return b ? checked_type_clone(b) : NULL;
    if (!b) return checked_type_clone(a);

    // Same type
    if (type_equals(a, b)) return checked_type_clone(a);

    // ANY absorbs
    if (a->kind == CHECKED_ANY) return checked_type_clone(b);
    if (b->kind == CHECKED_ANY) return checked_type_clone(a);

    // Numeric promotion
    if (type_is_numeric(a) && type_is_numeric(b)) {
        // Float wins over integer
        if (type_is_float(a) || type_is_float(b)) {
            if (a->kind == CHECKED_F64 || b->kind == CHECKED_F64) {
                return checked_type_primitive(CHECKED_F64);
            }
            return checked_type_primitive(CHECKED_F32);
        }

        // Larger integer wins
        int sizes[] = {
            [CHECKED_I8] = 1, [CHECKED_U8] = 1,
            [CHECKED_I16] = 2, [CHECKED_U16] = 2,
            [CHECKED_I32] = 4, [CHECKED_U32] = 4,
            [CHECKED_I64] = 8, [CHECKED_U64] = 8,
        };
        int size_a = (a->kind <= CHECKED_U64) ? sizes[a->kind] : 4;
        int size_b = (b->kind <= CHECKED_U64) ? sizes[b->kind] : 4;

        if (size_a >= size_b) return checked_type_clone(a);
        return checked_type_clone(b);
    }

    // String concatenation
    if (a->kind == CHECKED_STRING || b->kind == CHECKED_STRING) {
        return checked_type_primitive(CHECKED_STRING);
    }

    // Fallback to ANY
    return checked_type_primitive(CHECKED_ANY);
}

// ========== ERROR REPORTING ==========

void type_error(TypeCheckContext *ctx, int line, const char *fmt, ...) {
    ctx->error_count++;
    fprintf(stderr, "%s:%d: error: ", ctx->filename ? ctx->filename : "<unknown>", line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void type_warning(TypeCheckContext *ctx, int line, const char *fmt, ...) {
    ctx->warning_count++;
    fprintf(stderr, "%s:%d: warning: ", ctx->filename ? ctx->filename : "<unknown>", line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

// ========== TYPE INFERENCE ==========

// Forward declarations
static void collect_function_signatures(TypeCheckContext *ctx, Stmt **stmts, int count);
static void type_check_function_body(TypeCheckContext *ctx, Expr *func, const char *name);

CheckedType* type_check_infer_expr(TypeCheckContext *ctx, Expr *expr) {
    if (!expr) return checked_type_primitive(CHECKED_ANY);

    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return checked_type_primitive(CHECKED_F64);
            }
            // Check if it fits in i32
            if (expr->as.number.int_value >= -2147483648LL &&
                expr->as.number.int_value <= 2147483647LL) {
                return checked_type_primitive(CHECKED_I32);
            }
            return checked_type_primitive(CHECKED_I64);

        case EXPR_BOOL:
            return checked_type_primitive(CHECKED_BOOL);

        case EXPR_STRING:
            return checked_type_primitive(CHECKED_STRING);

        case EXPR_RUNE:
            return checked_type_primitive(CHECKED_RUNE);

        case EXPR_NULL:
            return checked_type_primitive(CHECKED_NULL);

        case EXPR_IDENT: {
            const char *name = expr->as.ident.name;
            CheckedType *type = type_check_lookup(ctx, name);
            if (type) {
                return checked_type_clone(type);
            }

            // Check for built-in functions (don't warn for these)
            static const char *builtins[] = {
                "print", "eprint", "typeof", "len", "alloc", "free", "memset",
                "memcpy", "buffer", "ptr_read_i8", "ptr_read_i16", "ptr_read_i32",
                "ptr_read_i64", "ptr_read_f32", "ptr_read_f64", "ptr_read_u8",
                "ptr_read_u16", "ptr_read_u32", "ptr_read_u64", "ptr_write_i8",
                "ptr_write_i16", "ptr_write_i32", "ptr_write_i64", "ptr_write_f32",
                "ptr_write_f64", "ptr_write_u8", "ptr_write_u16", "ptr_write_u32",
                "ptr_write_u64", "ptr_null", "sizeof", "talloc", "open", "read_line",
                "panic", "throw", "spawn", "join", "detach", "channel", "signal",
                "raise", "apply", "exec", "wait", "kill", "fork", "sleep", "exit",
                "atomic_load_i32", "atomic_store_i32", "atomic_add_i32", "atomic_sub_i32",
                "atomic_cas_i32", "atomic_exchange_i32", "atomic_fence",
                "atomic_load_i64", "atomic_store_i64", "atomic_add_i64", "atomic_sub_i64",
                "atomic_cas_i64", "atomic_and_i32", "atomic_or_i32", "atomic_xor_i32",
                "ffi_open", "ffi_bind", "ffi_close",
                // Type constructors
                "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                "f32", "f64", "bool", "string", "integer", "number", "byte",
                NULL
            };

            for (int i = 0; builtins[i]; i++) {
                if (strcmp(name, builtins[i]) == 0) {
                    return checked_type_primitive(CHECKED_ANY);
                }
            }

            // Check if it's a registered function
            if (type_check_lookup_function(ctx, name)) {
                return checked_type_primitive(CHECKED_ANY);
            }

            // Check if it's an enum name
            if (type_check_lookup_enum(ctx, name)) {
                return checked_type_primitive(CHECKED_ENUM);
            }

            // Unknown identifier - warn if configured
            if (ctx->warn_implicit_any) {
                type_warning(ctx, expr->line,
                    "identifier '%s' has unknown type", name);
            }

            return checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_BINARY: {
            CheckedType *left = type_check_infer_expr(ctx, expr->as.binary.left);
            CheckedType *right = type_check_infer_expr(ctx, expr->as.binary.right);
            CheckedType *result = NULL;

            switch (expr->as.binary.op) {
                // Comparison operators return bool
                case OP_EQUAL:
                case OP_NOT_EQUAL:
                case OP_LESS:
                case OP_LESS_EQUAL:
                case OP_GREATER:
                case OP_GREATER_EQUAL:
                    result = checked_type_primitive(CHECKED_BOOL);
                    break;

                // Logical operators return bool
                case OP_AND:
                case OP_OR:
                    result = checked_type_primitive(CHECKED_BOOL);
                    break;

                // Arithmetic operators
                case OP_ADD:
                    // String concatenation
                    if (left->kind == CHECKED_STRING || right->kind == CHECKED_STRING) {
                        result = checked_type_primitive(CHECKED_STRING);
                    } else {
                        result = type_common(left, right);
                    }
                    break;

                case OP_SUB:
                case OP_MUL:
                case OP_MOD:
                    result = type_common(left, right);
                    break;

                case OP_DIV:
                    // Division always returns float in Hemlock
                    result = checked_type_primitive(CHECKED_F64);
                    break;

                // Bitwise operators require integers, return integer
                case OP_BIT_AND:
                case OP_BIT_OR:
                case OP_BIT_XOR:
                case OP_BIT_LSHIFT:
                case OP_BIT_RSHIFT:
                    if (type_is_integer(left)) {
                        result = checked_type_clone(left);
                    } else if (type_is_integer(right)) {
                        result = checked_type_clone(right);
                    } else {
                        result = checked_type_primitive(CHECKED_I32);
                    }
                    break;

                default:
                    result = checked_type_primitive(CHECKED_ANY);
            }

            checked_type_free(left);
            checked_type_free(right);
            return result;
        }

        case EXPR_UNARY: {
            CheckedType *operand = type_check_infer_expr(ctx, expr->as.unary.operand);
            CheckedType *result = NULL;

            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    result = checked_type_primitive(CHECKED_BOOL);
                    break;
                case UNARY_NEGATE:
                    result = checked_type_clone(operand);
                    break;
                case UNARY_BIT_NOT:
                    result = checked_type_clone(operand);
                    break;
                default:
                    result = checked_type_primitive(CHECKED_ANY);
            }

            checked_type_free(operand);
            return result;
        }

        case EXPR_TERNARY: {
            CheckedType *true_type = type_check_infer_expr(ctx, expr->as.ternary.true_expr);
            CheckedType *false_type = type_check_infer_expr(ctx, expr->as.ternary.false_expr);
            CheckedType *result = type_common(true_type, false_type);
            checked_type_free(true_type);
            checked_type_free(false_type);
            return result;
        }

        case EXPR_CALL: {
            // Check if calling a known function
            if (expr->as.call.func->type == EXPR_IDENT) {
                const char *name = expr->as.call.func->as.ident.name;

                // Check for type constructor functions
                if (strcmp(name, "i8") == 0) return checked_type_primitive(CHECKED_I8);
                if (strcmp(name, "i16") == 0) return checked_type_primitive(CHECKED_I16);
                if (strcmp(name, "i32") == 0) return checked_type_primitive(CHECKED_I32);
                if (strcmp(name, "i64") == 0) return checked_type_primitive(CHECKED_I64);
                if (strcmp(name, "u8") == 0) return checked_type_primitive(CHECKED_U8);
                if (strcmp(name, "u16") == 0) return checked_type_primitive(CHECKED_U16);
                if (strcmp(name, "u32") == 0) return checked_type_primitive(CHECKED_U32);
                if (strcmp(name, "u64") == 0) return checked_type_primitive(CHECKED_U64);
                if (strcmp(name, "f32") == 0) return checked_type_primitive(CHECKED_F32);
                if (strcmp(name, "f64") == 0) return checked_type_primitive(CHECKED_F64);
                if (strcmp(name, "bool") == 0) return checked_type_primitive(CHECKED_BOOL);
                if (strcmp(name, "string") == 0) return checked_type_primitive(CHECKED_STRING);
                if (strcmp(name, "integer") == 0) return checked_type_primitive(CHECKED_I32);
                if (strcmp(name, "number") == 0) return checked_type_primitive(CHECKED_F64);
                if (strcmp(name, "byte") == 0) return checked_type_primitive(CHECKED_U8);

                // Built-in functions
                if (strcmp(name, "typeof") == 0) return checked_type_primitive(CHECKED_STRING);
                if (strcmp(name, "len") == 0) return checked_type_primitive(CHECKED_I32);
                if (strcmp(name, "alloc") == 0) return checked_type_primitive(CHECKED_PTR);
                if (strcmp(name, "buffer") == 0) return checked_type_primitive(CHECKED_BUFFER);
                if (strcmp(name, "open") == 0) return checked_type_primitive(CHECKED_FILE);
                if (strcmp(name, "channel") == 0) return checked_type_primitive(CHECKED_CHANNEL);
                if (strcmp(name, "spawn") == 0) return checked_type_primitive(CHECKED_TASK);
                if (strcmp(name, "read_line") == 0) {
                    CheckedType *t = checked_type_primitive(CHECKED_STRING);
                    t->nullable = 1;
                    return t;
                }

                // Check registered functions
                FunctionSig *sig = type_check_lookup_function(ctx, name);
                if (sig && sig->return_type) {
                    return checked_type_clone(sig->return_type);
                }

                // Check if it's a variable holding a function
                CheckedType *var_type = type_check_lookup(ctx, name);
                if (var_type && var_type->kind == CHECKED_FUNCTION && var_type->return_type) {
                    return checked_type_clone(var_type->return_type);
                }
            }

            return checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_ARRAY_LITERAL: {
            if (expr->as.array_literal.num_elements == 0) {
                return checked_type_array(NULL);
            }
            // Infer element type from first element
            CheckedType *elem = type_check_infer_expr(ctx, expr->as.array_literal.elements[0]);
            return checked_type_array(elem);
        }

        case EXPR_OBJECT_LITERAL:
            return checked_type_primitive(CHECKED_OBJECT);

        case EXPR_FUNCTION: {
            Expr *func = expr;
            CheckedType **param_types = NULL;
            if (func->as.function.num_params > 0) {
                param_types = calloc(func->as.function.num_params, sizeof(CheckedType*));
                for (int i = 0; i < func->as.function.num_params; i++) {
                    if (func->as.function.param_types[i]) {
                        param_types[i] = checked_type_from_ast(func->as.function.param_types[i]);
                    } else {
                        param_types[i] = checked_type_primitive(CHECKED_ANY);
                    }
                }
            }
            CheckedType *ret = func->as.function.return_type
                ? checked_type_from_ast(func->as.function.return_type)
                : checked_type_primitive(CHECKED_ANY);
            return checked_type_function(param_types, func->as.function.num_params,
                                         ret, func->as.function.rest_param != NULL);
        }

        case EXPR_INDEX: {
            CheckedType *obj = type_check_infer_expr(ctx, expr->as.index.object);
            CheckedType *result = NULL;

            if (obj->kind == CHECKED_ARRAY && obj->element_type) {
                result = checked_type_clone(obj->element_type);
            } else if (obj->kind == CHECKED_STRING) {
                result = checked_type_primitive(CHECKED_RUNE);
            } else {
                result = checked_type_primitive(CHECKED_ANY);
            }

            checked_type_free(obj);
            return result;
        }

        case EXPR_GET_PROPERTY:
            // Could be improved with object type tracking
            return checked_type_primitive(CHECKED_ANY);

        case EXPR_AWAIT: {
            // await on a task returns the task's result type
            // For now, return ANY
            return checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_STRING_INTERPOLATION:
            return checked_type_primitive(CHECKED_STRING);

        case EXPR_NULL_COALESCE: {
            CheckedType *left = type_check_infer_expr(ctx, expr->as.null_coalesce.left);
            CheckedType *right = type_check_infer_expr(ctx, expr->as.null_coalesce.right);
            // Result is non-nullable version of left, or right's type
            CheckedType *result = type_common(left, right);
            if (result) result->nullable = 0;
            checked_type_free(left);
            checked_type_free(right);
            return result ? result : checked_type_primitive(CHECKED_ANY);
        }

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return type_check_infer_expr(ctx, expr->as.prefix_inc.operand);

        default:
            return checked_type_primitive(CHECKED_ANY);
    }
}

// ========== METHOD TYPE CHECKING ==========

// Check method call arguments for built-in types (array, string)
// Returns 1 if method was found and checked, 0 if unknown method
static int type_check_method_call(TypeCheckContext *ctx, CheckedType *receiver_type,
                                  const char *method_name, Expr **args, int num_args, int line) {
    if (!receiver_type || !method_name) return 0;

    // Array methods
    if (receiver_type->kind == CHECKED_ARRAY) {
        CheckedType *elem_type = receiver_type->element_type;

        // Methods that take an element: push, unshift, insert, contains, find
        if (strcmp(method_name, "push") == 0 || strcmp(method_name, "unshift") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "array.%s() requires at least 1 argument", method_name);
                return 1;
            }
            if (elem_type && elem_type->kind != CHECKED_ANY) {
                for (int i = 0; i < num_args; i++) {
                    CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                    if (!type_is_assignable(elem_type, arg_type)) {
                        type_error(ctx, line,
                            "array.%s(): cannot add '%s' to array<%s>",
                            method_name, checked_type_name(arg_type),
                            checked_type_name(elem_type));
                    }
                    checked_type_free(arg_type);
                }
            }
            return 1;
        }

        if (strcmp(method_name, "insert") == 0) {
            if (num_args < 2) {
                type_error(ctx, line, "array.insert() requires 2 arguments (index, element)");
                return 1;
            }
            // First arg should be integer (index)
            CheckedType *idx_type = type_check_infer_expr(ctx, args[0]);
            if (!type_is_integer(idx_type) && idx_type->kind != CHECKED_ANY) {
                type_error(ctx, line, "array.insert(): index must be integer, got '%s'",
                    checked_type_name(idx_type));
            }
            checked_type_free(idx_type);

            // Second arg should be element type
            if (elem_type && elem_type->kind != CHECKED_ANY) {
                CheckedType *val_type = type_check_infer_expr(ctx, args[1]);
                if (!type_is_assignable(elem_type, val_type)) {
                    type_error(ctx, line,
                        "array.insert(): cannot insert '%s' into array<%s>",
                        checked_type_name(val_type), checked_type_name(elem_type));
                }
                checked_type_free(val_type);
            }
            return 1;
        }

        // Methods with no required args: pop, shift, first, last, clear, reverse
        if (strcmp(method_name, "pop") == 0 || strcmp(method_name, "shift") == 0 ||
            strcmp(method_name, "first") == 0 || strcmp(method_name, "last") == 0 ||
            strcmp(method_name, "clear") == 0 || strcmp(method_name, "reverse") == 0) {
            return 1;  // No type checking needed for arguments
        }

        // Methods that take index: remove, slice
        if (strcmp(method_name, "remove") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "array.remove() requires 1 argument (index)");
                return 1;
            }
            CheckedType *idx_type = type_check_infer_expr(ctx, args[0]);
            if (!type_is_integer(idx_type) && idx_type->kind != CHECKED_ANY) {
                type_error(ctx, line, "array.remove(): index must be integer, got '%s'",
                    checked_type_name(idx_type));
            }
            checked_type_free(idx_type);
            return 1;
        }

        if (strcmp(method_name, "slice") == 0) {
            for (int i = 0; i < num_args && i < 2; i++) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                if (!type_is_integer(arg_type) && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "array.slice(): argument %d must be integer, got '%s'",
                        i + 1, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // join takes a string separator
        if (strcmp(method_name, "join") == 0) {
            if (num_args > 0) {
                CheckedType *sep_type = type_check_infer_expr(ctx, args[0]);
                if (sep_type->kind != CHECKED_STRING && sep_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "array.join(): separator must be string, got '%s'",
                        checked_type_name(sep_type));
                }
                checked_type_free(sep_type);
            }
            return 1;
        }

        // map, filter, reduce take functions
        if (strcmp(method_name, "map") == 0 || strcmp(method_name, "filter") == 0 ||
            strcmp(method_name, "reduce") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "array.%s() requires a function argument", method_name);
            }
            return 1;
        }

        // contains, find take an element
        if (strcmp(method_name, "contains") == 0 || strcmp(method_name, "find") == 0) {
            return 1;  // Permissive for now
        }

        // concat takes another array
        if (strcmp(method_name, "concat") == 0) {
            if (num_args > 0) {
                CheckedType *other_type = type_check_infer_expr(ctx, args[0]);
                if (other_type->kind != CHECKED_ARRAY && other_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "array.concat(): argument must be array, got '%s'",
                        checked_type_name(other_type));
                }
                checked_type_free(other_type);
            }
            return 1;
        }
    }

    // String methods
    if (receiver_type->kind == CHECKED_STRING) {
        // Methods that take integer indices
        if (strcmp(method_name, "substr") == 0 || strcmp(method_name, "slice") == 0 ||
            strcmp(method_name, "char_at") == 0 || strcmp(method_name, "byte_at") == 0) {
            for (int i = 0; i < num_args && i < 2; i++) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                if (!type_is_integer(arg_type) && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "string.%s(): argument %d must be integer, got '%s'",
                        method_name, i + 1, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // Methods that take a string argument
        if (strcmp(method_name, "find") == 0 || strcmp(method_name, "contains") == 0 ||
            strcmp(method_name, "starts_with") == 0 || strcmp(method_name, "ends_with") == 0 ||
            strcmp(method_name, "split") == 0) {
            if (num_args > 0) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[0]);
                if (arg_type->kind != CHECKED_STRING && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "string.%s(): argument must be string, got '%s'",
                        method_name, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // replace/replace_all take two strings
        if (strcmp(method_name, "replace") == 0 || strcmp(method_name, "replace_all") == 0) {
            if (num_args < 2) {
                type_error(ctx, line, "string.%s() requires 2 arguments (pattern, replacement)",
                    method_name);
                return 1;
            }
            for (int i = 0; i < 2; i++) {
                CheckedType *arg_type = type_check_infer_expr(ctx, args[i]);
                if (arg_type->kind != CHECKED_STRING && arg_type->kind != CHECKED_ANY) {
                    type_error(ctx, line, "string.%s(): argument %d must be string, got '%s'",
                        method_name, i + 1, checked_type_name(arg_type));
                }
                checked_type_free(arg_type);
            }
            return 1;
        }

        // repeat takes an integer
        if (strcmp(method_name, "repeat") == 0) {
            if (num_args < 1) {
                type_error(ctx, line, "string.repeat() requires 1 argument (count)");
                return 1;
            }
            CheckedType *arg_type = type_check_infer_expr(ctx, args[0]);
            if (!type_is_integer(arg_type) && arg_type->kind != CHECKED_ANY) {
                type_error(ctx, line, "string.repeat(): count must be integer, got '%s'",
                    checked_type_name(arg_type));
            }
            checked_type_free(arg_type);
            return 1;
        }

        // No-arg string methods
        if (strcmp(method_name, "trim") == 0 || strcmp(method_name, "to_upper") == 0 ||
            strcmp(method_name, "to_lower") == 0 || strcmp(method_name, "chars") == 0 ||
            strcmp(method_name, "bytes") == 0 || strcmp(method_name, "to_bytes") == 0 ||
            strcmp(method_name, "deserialize") == 0) {
            return 1;
        }
    }

    return 0;  // Unknown method, not checked
}

// ========== EXPRESSION TYPE CHECKING ==========

void type_check_expr(TypeCheckContext *ctx, Expr *expr) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_BINARY: {
            type_check_expr(ctx, expr->as.binary.left);
            type_check_expr(ctx, expr->as.binary.right);

            CheckedType *left = type_check_infer_expr(ctx, expr->as.binary.left);
            CheckedType *right = type_check_infer_expr(ctx, expr->as.binary.right);

            BinaryOp op = expr->as.binary.op;

            // Check operator compatibility
            switch (op) {
                case OP_ADD:
                    // Allow string + anything (concatenation)
                    // Allow numeric + numeric
                    // Allow pointer + integer (pointer arithmetic)
                    if (left->kind != CHECKED_STRING && right->kind != CHECKED_STRING) {
                        // Pointer arithmetic: ptr + int or int + ptr
                        int is_ptr_arith = (left->kind == CHECKED_PTR && type_is_integer(right)) ||
                                           (type_is_integer(left) && right->kind == CHECKED_PTR);
                        int is_numeric_op = type_is_numeric(left) && type_is_numeric(right);
                        int is_any = left->kind == CHECKED_ANY || right->kind == CHECKED_ANY;
                        if (!is_ptr_arith && !is_numeric_op && !is_any) {
                            type_error(ctx, expr->line,
                                "cannot add '%s' and '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_SUB:
                case OP_MUL:
                case OP_MOD:
                    if (!type_is_numeric(left) || !type_is_numeric(right)) {
                        if (left->kind != CHECKED_ANY && right->kind != CHECKED_ANY) {
                            const char *op_name = op == OP_SUB ? "subtract" :
                                                  op == OP_MUL ? "multiply" : "modulo";
                            type_error(ctx, expr->line,
                                "cannot %s '%s' and '%s'",
                                op_name, checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_DIV:
                    if (!type_is_numeric(left) || !type_is_numeric(right)) {
                        if (left->kind != CHECKED_ANY && right->kind != CHECKED_ANY) {
                            type_error(ctx, expr->line,
                                "cannot divide '%s' by '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_BIT_AND:
                case OP_BIT_OR:
                case OP_BIT_XOR:
                case OP_BIT_LSHIFT:
                case OP_BIT_RSHIFT:
                    if (!type_is_integer(left) || !type_is_integer(right)) {
                        if (left->kind != CHECKED_ANY && right->kind != CHECKED_ANY) {
                            type_error(ctx, expr->line,
                                "bitwise operation requires integer operands, got '%s' and '%s'",
                                checked_type_name(left), checked_type_name(right));
                        }
                    }
                    break;

                case OP_AND:
                case OP_OR:
                    // These are more permissive - any truthy/falsy value works
                    break;

                default:
                    break;
            }

            checked_type_free(left);
            checked_type_free(right);
            break;
        }

        case EXPR_UNARY: {
            type_check_expr(ctx, expr->as.unary.operand);
            CheckedType *operand = type_check_infer_expr(ctx, expr->as.unary.operand);

            switch (expr->as.unary.op) {
                case UNARY_NEGATE:
                    if (!type_is_numeric(operand) && operand->kind != CHECKED_ANY) {
                        type_error(ctx, expr->line,
                            "cannot negate '%s'", checked_type_name(operand));
                    }
                    break;
                case UNARY_BIT_NOT:
                    if (!type_is_integer(operand) && operand->kind != CHECKED_ANY) {
                        type_error(ctx, expr->line,
                            "bitwise NOT requires integer operand, got '%s'",
                            checked_type_name(operand));
                    }
                    break;
                default:
                    break;
            }

            checked_type_free(operand);
            break;
        }

        case EXPR_CALL: {
            type_check_expr(ctx, expr->as.call.func);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                type_check_expr(ctx, expr->as.call.args[i]);
            }

            // Check function call argument types
            if (expr->as.call.func->type == EXPR_IDENT) {
                const char *name = expr->as.call.func->as.ident.name;
                FunctionSig *sig = type_check_lookup_function(ctx, name);

                if (sig) {
                    // Check argument count
                    int provided = expr->as.call.num_args;

                    if (!sig->has_rest_param && provided > sig->num_params) {
                        type_error(ctx, expr->line,
                            "too many arguments to '%s': expected %d, got %d",
                            name, sig->num_params, provided);
                    }

                    // Check for too few arguments (must have at least num_required)
                    if (provided < sig->num_required) {
                        if (sig->num_required == sig->num_params) {
                            type_error(ctx, expr->line,
                                "too few arguments to '%s': expected %d, got %d",
                                name, sig->num_required, provided);
                        } else {
                            type_error(ctx, expr->line,
                                "too few arguments to '%s': expected at least %d, got %d",
                                name, sig->num_required, provided);
                        }
                    }

                    // Check argument types
                    for (int i = 0; i < provided && i < sig->num_params; i++) {
                        if (!sig->param_types[i]) continue;

                        CheckedType *arg_type = type_check_infer_expr(ctx, expr->as.call.args[i]);
                        if (!type_is_assignable(sig->param_types[i], arg_type)) {
                            type_error(ctx, expr->line,
                                "argument %d to '%s': expected '%s', got '%s'",
                                i + 1, name,
                                checked_type_name(sig->param_types[i]),
                                checked_type_name(arg_type));
                        }
                        checked_type_free(arg_type);
                    }
                }
            }
            // Check method calls (e.g., arr.push(x), str.split(","))
            else if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                Expr *prop_expr = expr->as.call.func;
                CheckedType *receiver_type = type_check_infer_expr(ctx, prop_expr->as.get_property.object);
                const char *method_name = prop_expr->as.get_property.property;

                type_check_method_call(ctx, receiver_type, method_name,
                    expr->as.call.args, expr->as.call.num_args, expr->line);

                checked_type_free(receiver_type);
            }
            break;
        }

        case EXPR_ASSIGN: {
            type_check_expr(ctx, expr->as.assign.value);

            // Check if variable is const
            if (type_check_is_const(ctx, expr->as.assign.name)) {
                type_error(ctx, expr->line,
                    "cannot reassign const variable '%s'", expr->as.assign.name);
            }

            // Check type compatibility
            CheckedType *var_type = type_check_lookup(ctx, expr->as.assign.name);
            if (var_type && var_type->kind != CHECKED_ANY) {
                CheckedType *val_type = type_check_infer_expr(ctx, expr->as.assign.value);
                if (!type_is_assignable(var_type, val_type)) {
                    type_error(ctx, expr->line,
                        "cannot assign '%s' to variable '%s' of type '%s'",
                        checked_type_name(val_type), expr->as.assign.name,
                        checked_type_name(var_type));
                }
                checked_type_free(val_type);
            }
            break;
        }

        case EXPR_INDEX:
            type_check_expr(ctx, expr->as.index.object);
            type_check_expr(ctx, expr->as.index.index);
            break;

        case EXPR_INDEX_ASSIGN:
            type_check_expr(ctx, expr->as.index_assign.object);
            type_check_expr(ctx, expr->as.index_assign.index);
            type_check_expr(ctx, expr->as.index_assign.value);
            break;

        case EXPR_GET_PROPERTY: {
            type_check_expr(ctx, expr->as.get_property.object);

            // Validate property access against object definition
            CheckedType *obj_type = type_check_infer_expr(ctx, expr->as.get_property.object);
            if (obj_type && obj_type->kind == CHECKED_CUSTOM && obj_type->type_name) {
                ObjectDef *def = type_check_lookup_object(ctx, obj_type->type_name);
                if (def) {
                    const char *prop = expr->as.get_property.property;
                    int found = 0;
                    for (int i = 0; i < def->num_fields; i++) {
                        if (strcmp(def->field_names[i], prop) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        type_warning(ctx, expr->line,
                            "property '%s' not defined in type '%s'",
                            prop, obj_type->type_name);
                    }
                }
            }
            checked_type_free(obj_type);
            break;
        }

        case EXPR_SET_PROPERTY: {
            type_check_expr(ctx, expr->as.set_property.object);
            type_check_expr(ctx, expr->as.set_property.value);

            // Validate property and type against object definition
            CheckedType *obj_type = type_check_infer_expr(ctx, expr->as.set_property.object);
            if (obj_type && obj_type->kind == CHECKED_CUSTOM && obj_type->type_name) {
                ObjectDef *def = type_check_lookup_object(ctx, obj_type->type_name);
                if (def) {
                    const char *prop = expr->as.set_property.property;
                    int found = 0;
                    CheckedType *field_type = NULL;
                    for (int i = 0; i < def->num_fields; i++) {
                        if (strcmp(def->field_names[i], prop) == 0) {
                            found = 1;
                            field_type = def->field_types[i];
                            break;
                        }
                    }
                    if (!found) {
                        type_warning(ctx, expr->line,
                            "property '%s' not defined in type '%s'",
                            prop, obj_type->type_name);
                    } else if (field_type && field_type->kind != CHECKED_ANY) {
                        // Check that assigned value matches field type
                        CheckedType *val_type = type_check_infer_expr(ctx, expr->as.set_property.value);
                        if (!type_is_assignable(field_type, val_type)) {
                            type_error(ctx, expr->line,
                                "cannot assign '%s' to property '%s' of type '%s'",
                                checked_type_name(val_type), prop,
                                checked_type_name(field_type));
                        }
                        checked_type_free(val_type);
                    }
                }
            }
            checked_type_free(obj_type);
            break;
        }

        case EXPR_TERNARY:
            type_check_expr(ctx, expr->as.ternary.condition);
            type_check_expr(ctx, expr->as.ternary.true_expr);
            type_check_expr(ctx, expr->as.ternary.false_expr);
            break;

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                type_check_expr(ctx, expr->as.array_literal.elements[i]);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                type_check_expr(ctx, expr->as.object_literal.field_values[i]);
            }
            break;

        case EXPR_FUNCTION:
            // Check function body in its own scope
            type_check_function_body(ctx, expr, NULL);
            break;

        case EXPR_AWAIT:
            type_check_expr(ctx, expr->as.await_expr.awaited_expr);
            break;

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                type_check_expr(ctx, expr->as.string_interpolation.expr_parts[i]);
            }
            break;

        case EXPR_OPTIONAL_CHAIN:
            type_check_expr(ctx, expr->as.optional_chain.object);
            if (expr->as.optional_chain.index) {
                type_check_expr(ctx, expr->as.optional_chain.index);
            }
            if (expr->as.optional_chain.args) {
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    type_check_expr(ctx, expr->as.optional_chain.args[i]);
                }
            }
            break;

        case EXPR_NULL_COALESCE:
            type_check_expr(ctx, expr->as.null_coalesce.left);
            type_check_expr(ctx, expr->as.null_coalesce.right);
            break;

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            type_check_expr(ctx, expr->as.prefix_inc.operand);
            break;

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            type_check_expr(ctx, expr->as.postfix_inc.operand);
            break;

        default:
            break;
    }
}

// ========== STATEMENT TYPE CHECKING ==========

static void type_check_function_body(TypeCheckContext *ctx, Expr *func, const char *name) {
    type_check_push_scope(ctx);

    // Save current function context
    CheckedType *saved_return_type = ctx->current_return_type;
    char *saved_func_name = ctx->current_function_name;
    int saved_async = ctx->in_async_function;

    // Set up function context
    ctx->current_return_type = func->as.function.return_type
        ? checked_type_from_ast(func->as.function.return_type)
        : NULL;
    ctx->current_function_name = name ? strdup(name) : NULL;
    ctx->in_async_function = func->as.function.is_async;

    // Bind parameters
    for (int i = 0; i < func->as.function.num_params; i++) {
        CheckedType *param_type;
        if (func->as.function.param_types[i]) {
            param_type = checked_type_from_ast(func->as.function.param_types[i]);
        } else {
            param_type = checked_type_primitive(CHECKED_ANY);
        }
        type_check_bind(ctx, func->as.function.param_names[i], param_type, 0, 0);
    }

    // Bind rest parameter if present
    if (func->as.function.rest_param) {
        CheckedType *rest_type = checked_type_array(
            func->as.function.rest_param_type
                ? checked_type_from_ast(func->as.function.rest_param_type)
                : checked_type_primitive(CHECKED_ANY)
        );
        type_check_bind(ctx, func->as.function.rest_param, rest_type, 0, 0);
    }

    // Check body
    if (func->as.function.body) {
        type_check_stmt(ctx, func->as.function.body);
    }

    // Restore context
    checked_type_free(ctx->current_return_type);
    free(ctx->current_function_name);
    ctx->current_return_type = saved_return_type;
    ctx->current_function_name = saved_func_name;
    ctx->in_async_function = saved_async;

    type_check_pop_scope(ctx);
}

void type_check_stmt(TypeCheckContext *ctx, Stmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_LET: {
            if (stmt->as.let.value) {
                type_check_expr(ctx, stmt->as.let.value);
            }

            CheckedType *declared_type = NULL;
            if (stmt->as.let.type_annotation) {
                declared_type = checked_type_from_ast(stmt->as.let.type_annotation);

                // Check that initializer matches declared type
                if (stmt->as.let.value) {
                    CheckedType *init_type = type_check_infer_expr(ctx, stmt->as.let.value);
                    if (!type_is_assignable(declared_type, init_type)) {
                        type_error(ctx, stmt->line,
                            "cannot initialize '%s' of type '%s' with '%s'",
                            stmt->as.let.name, checked_type_name(declared_type),
                            checked_type_name(init_type));
                    }
                    checked_type_free(init_type);
                }
            } else if (stmt->as.let.value) {
                declared_type = type_check_infer_expr(ctx, stmt->as.let.value);
            } else {
                declared_type = checked_type_primitive(CHECKED_ANY);
            }

            type_check_bind(ctx, stmt->as.let.name, declared_type, 0, stmt->line);
            break;
        }

        case STMT_CONST: {
            if (stmt->as.const_stmt.value) {
                type_check_expr(ctx, stmt->as.const_stmt.value);
            }

            CheckedType *declared_type = NULL;
            if (stmt->as.const_stmt.type_annotation) {
                declared_type = checked_type_from_ast(stmt->as.const_stmt.type_annotation);

                if (stmt->as.const_stmt.value) {
                    CheckedType *init_type = type_check_infer_expr(ctx, stmt->as.const_stmt.value);
                    if (!type_is_assignable(declared_type, init_type)) {
                        type_error(ctx, stmt->line,
                            "cannot initialize const '%s' of type '%s' with '%s'",
                            stmt->as.const_stmt.name, checked_type_name(declared_type),
                            checked_type_name(init_type));
                    }
                    checked_type_free(init_type);
                }
            } else if (stmt->as.const_stmt.value) {
                declared_type = type_check_infer_expr(ctx, stmt->as.const_stmt.value);
            } else {
                declared_type = checked_type_primitive(CHECKED_ANY);
            }

            type_check_bind(ctx, stmt->as.const_stmt.name, declared_type, 1, stmt->line);
            break;
        }

        case STMT_EXPR:
            type_check_expr(ctx, stmt->as.expr);
            break;

        case STMT_IF:
            type_check_expr(ctx, stmt->as.if_stmt.condition);
            type_check_stmt(ctx, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                type_check_stmt(ctx, stmt->as.if_stmt.else_branch);
            }
            break;

        case STMT_WHILE:
            type_check_expr(ctx, stmt->as.while_stmt.condition);
            type_check_stmt(ctx, stmt->as.while_stmt.body);
            break;

        case STMT_FOR:
            type_check_push_scope(ctx);
            if (stmt->as.for_loop.initializer) {
                type_check_stmt(ctx, stmt->as.for_loop.initializer);
            }
            if (stmt->as.for_loop.condition) {
                type_check_expr(ctx, stmt->as.for_loop.condition);
            }
            if (stmt->as.for_loop.increment) {
                type_check_expr(ctx, stmt->as.for_loop.increment);
            }
            type_check_stmt(ctx, stmt->as.for_loop.body);
            type_check_pop_scope(ctx);
            break;

        case STMT_FOR_IN: {
            type_check_push_scope(ctx);
            type_check_expr(ctx, stmt->as.for_in.iterable);

            // Infer types for loop variables
            CheckedType *iter_type = type_check_infer_expr(ctx, stmt->as.for_in.iterable);
            CheckedType *value_type = checked_type_primitive(CHECKED_ANY);

            if (iter_type->kind == CHECKED_ARRAY && iter_type->element_type) {
                checked_type_free(value_type);
                value_type = checked_type_clone(iter_type->element_type);
            } else if (iter_type->kind == CHECKED_STRING) {
                checked_type_free(value_type);
                value_type = checked_type_primitive(CHECKED_RUNE);
            }

            if (stmt->as.for_in.key_var) {
                type_check_bind(ctx, stmt->as.for_in.key_var,
                    checked_type_primitive(CHECKED_I32), 0, stmt->line);
            }
            type_check_bind(ctx, stmt->as.for_in.value_var, value_type, 0, stmt->line);

            checked_type_free(iter_type);
            type_check_stmt(ctx, stmt->as.for_in.body);
            type_check_pop_scope(ctx);
            break;
        }

        case STMT_BLOCK:
            type_check_push_scope(ctx);
            for (int i = 0; i < stmt->as.block.count; i++) {
                type_check_stmt(ctx, stmt->as.block.statements[i]);
            }
            type_check_pop_scope(ctx);
            break;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                type_check_expr(ctx, stmt->as.return_stmt.value);

                if (ctx->current_return_type) {
                    CheckedType *ret_type = type_check_infer_expr(ctx, stmt->as.return_stmt.value);
                    if (!type_is_assignable(ctx->current_return_type, ret_type)) {
                        type_error(ctx, stmt->line,
                            "return type mismatch: expected '%s', got '%s'",
                            checked_type_name(ctx->current_return_type),
                            checked_type_name(ret_type));
                    }
                    checked_type_free(ret_type);
                }
            } else if (ctx->current_return_type &&
                       ctx->current_return_type->kind != CHECKED_VOID &&
                       ctx->current_return_type->kind != CHECKED_ANY) {
                type_warning(ctx, stmt->line,
                    "missing return value, expected '%s'",
                    checked_type_name(ctx->current_return_type));
            }
            break;

        case STMT_DEFINE_OBJECT: {
            // Register the object type
            CheckedType **field_types = NULL;
            if (stmt->as.define_object.num_fields > 0) {
                field_types = calloc(stmt->as.define_object.num_fields, sizeof(CheckedType*));
                for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                    if (stmt->as.define_object.field_types[i]) {
                        field_types[i] = checked_type_from_ast(stmt->as.define_object.field_types[i]);
                    } else {
                        field_types[i] = checked_type_primitive(CHECKED_ANY);
                    }
                }
            }
            type_check_register_object(ctx, stmt->as.define_object.name,
                stmt->as.define_object.field_names, field_types,
                stmt->as.define_object.field_optional,
                stmt->as.define_object.num_fields);
            break;
        }

        case STMT_ENUM: {
            type_check_register_enum(ctx, stmt->as.enum_decl.name,
                stmt->as.enum_decl.variant_names, stmt->as.enum_decl.num_variants);

            // Also bind enum to scope as a type
            CheckedType *enum_type = checked_type_primitive(CHECKED_ENUM);
            enum_type->type_name = strdup(stmt->as.enum_decl.name);
            type_check_bind(ctx, stmt->as.enum_decl.name, enum_type, 1, stmt->line);
            break;
        }

        case STMT_TRY:
            type_check_stmt(ctx, stmt->as.try_stmt.try_block);
            if (stmt->as.try_stmt.catch_block) {
                type_check_push_scope(ctx);
                if (stmt->as.try_stmt.catch_param) {
                    type_check_bind(ctx, stmt->as.try_stmt.catch_param,
                        checked_type_primitive(CHECKED_ANY), 0, stmt->line);
                }
                type_check_stmt(ctx, stmt->as.try_stmt.catch_block);
                type_check_pop_scope(ctx);
            }
            if (stmt->as.try_stmt.finally_block) {
                type_check_stmt(ctx, stmt->as.try_stmt.finally_block);
            }
            break;

        case STMT_THROW:
            type_check_expr(ctx, stmt->as.throw_stmt.value);
            break;

        case STMT_SWITCH:
            type_check_expr(ctx, stmt->as.switch_stmt.expr);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                if (stmt->as.switch_stmt.case_values[i]) {
                    type_check_expr(ctx, stmt->as.switch_stmt.case_values[i]);
                }
                type_check_stmt(ctx, stmt->as.switch_stmt.case_bodies[i]);
            }
            break;

        case STMT_DEFER:
            type_check_expr(ctx, stmt->as.defer_stmt.call);
            break;

        case STMT_EXPORT:
            if (stmt->as.export_stmt.is_declaration && stmt->as.export_stmt.declaration) {
                type_check_stmt(ctx, stmt->as.export_stmt.declaration);
            }
            break;

        default:
            break;
    }
}

// ========== FIRST PASS: COLLECT SIGNATURES ==========

static void collect_function_signatures(TypeCheckContext *ctx, Stmt **stmts, int count) {
    for (int i = 0; i < count; i++) {
        Stmt *stmt = stmts[i];
        if (!stmt) continue;

        // Handle top-level function definitions (let/const with function value)
        if ((stmt->type == STMT_LET || stmt->type == STMT_CONST)) {
            const char *name = stmt->type == STMT_LET
                ? stmt->as.let.name
                : stmt->as.const_stmt.name;
            Expr *value = stmt->type == STMT_LET
                ? stmt->as.let.value
                : stmt->as.const_stmt.value;

            if (value && value->type == EXPR_FUNCTION) {
                Expr *func = value;

                // Collect parameter types and optional info
                CheckedType **param_types = NULL;
                char **param_names = NULL;
                int *param_optional = NULL;
                if (func->as.function.num_params > 0) {
                    param_types = calloc(func->as.function.num_params, sizeof(CheckedType*));
                    param_names = calloc(func->as.function.num_params, sizeof(char*));
                    param_optional = calloc(func->as.function.num_params, sizeof(int));
                    for (int j = 0; j < func->as.function.num_params; j++) {
                        if (func->as.function.param_types[j]) {
                            param_types[j] = checked_type_from_ast(func->as.function.param_types[j]);
                        }
                        param_names[j] = strdup(func->as.function.param_names[j]);
                        // Parameter is optional if it has a default value
                        param_optional[j] = (func->as.function.param_defaults &&
                                            func->as.function.param_defaults[j]) ? 1 : 0;
                    }
                }

                CheckedType *return_type = func->as.function.return_type
                    ? checked_type_from_ast(func->as.function.return_type)
                    : NULL;

                type_check_register_function(ctx, name, param_types, param_names,
                    param_optional, func->as.function.num_params, return_type,
                    func->as.function.rest_param != NULL,
                    func->as.function.is_async);
            }
        }

        // Handle export statements
        if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration) {
            Stmt *decl = stmt->as.export_stmt.declaration;
            if (decl && (decl->type == STMT_LET || decl->type == STMT_CONST)) {
                const char *name = decl->type == STMT_LET
                    ? decl->as.let.name
                    : decl->as.const_stmt.name;
                Expr *value = decl->type == STMT_LET
                    ? decl->as.let.value
                    : decl->as.const_stmt.value;

                if (value && value->type == EXPR_FUNCTION) {
                    Expr *func = value;

                    CheckedType **param_types = NULL;
                    char **param_names = NULL;
                    int *param_optional = NULL;
                    if (func->as.function.num_params > 0) {
                        param_types = calloc(func->as.function.num_params, sizeof(CheckedType*));
                        param_names = calloc(func->as.function.num_params, sizeof(char*));
                        param_optional = calloc(func->as.function.num_params, sizeof(int));
                        for (int j = 0; j < func->as.function.num_params; j++) {
                            if (func->as.function.param_types[j]) {
                                param_types[j] = checked_type_from_ast(func->as.function.param_types[j]);
                            }
                            param_names[j] = strdup(func->as.function.param_names[j]);
                            param_optional[j] = (func->as.function.param_defaults &&
                                                func->as.function.param_defaults[j]) ? 1 : 0;
                        }
                    }

                    CheckedType *return_type = func->as.function.return_type
                        ? checked_type_from_ast(func->as.function.return_type)
                        : NULL;

                    type_check_register_function(ctx, name, param_types, param_names,
                        param_optional, func->as.function.num_params, return_type,
                        func->as.function.rest_param != NULL,
                        func->as.function.is_async);
                }
            }
        }

        // Handle object definitions
        if (stmt->type == STMT_DEFINE_OBJECT) {
            CheckedType **field_types = NULL;
            if (stmt->as.define_object.num_fields > 0) {
                field_types = calloc(stmt->as.define_object.num_fields, sizeof(CheckedType*));
                for (int j = 0; j < stmt->as.define_object.num_fields; j++) {
                    if (stmt->as.define_object.field_types[j]) {
                        field_types[j] = checked_type_from_ast(stmt->as.define_object.field_types[j]);
                    } else {
                        field_types[j] = checked_type_primitive(CHECKED_ANY);
                    }
                }
            }
            type_check_register_object(ctx, stmt->as.define_object.name,
                stmt->as.define_object.field_names, field_types,
                stmt->as.define_object.field_optional,
                stmt->as.define_object.num_fields);
        }

        // Handle enum definitions
        if (stmt->type == STMT_ENUM) {
            type_check_register_enum(ctx, stmt->as.enum_decl.name,
                stmt->as.enum_decl.variant_names, stmt->as.enum_decl.num_variants);
        }
    }
}

// ========== MAIN ENTRY POINT ==========

int type_check_program(TypeCheckContext *ctx, Stmt **stmts, int stmt_count) {
    // First pass: collect all function signatures, object definitions, enums
    collect_function_signatures(ctx, stmts, stmt_count);

    // Second pass: type check all statements
    for (int i = 0; i < stmt_count; i++) {
        type_check_stmt(ctx, stmts[i]);
    }

    return ctx->error_count;
}

// ========== UNBOXING OPTIMIZATION ==========

void type_check_mark_unboxable(TypeCheckContext *ctx, const char *name,
                               CheckedTypeKind native_type, int is_loop_counter,
                               int is_accumulator, int is_typed_var) {
    if (!ctx) return;

    // Check if already marked
    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            // Update if more specific
            if (native_type != CHECKED_UNKNOWN) {
                u->native_type = native_type;
            }
            u->is_loop_counter |= is_loop_counter;
            u->is_accumulator |= is_accumulator;
            u->is_typed_var |= is_typed_var;
            return;
        }
    }
    // Add new entry
    UnboxableVar *u = malloc(sizeof(UnboxableVar));
    u->name = strdup(name);
    u->native_type = native_type;
    u->is_loop_counter = is_loop_counter;
    u->is_accumulator = is_accumulator;
    u->is_typed_var = is_typed_var;
    u->next = ctx->unboxable_vars;
    ctx->unboxable_vars = u;
}

CheckedTypeKind type_check_get_unboxable(TypeCheckContext *ctx, const char *name) {
    if (!ctx) return CHECKED_UNKNOWN;

    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->native_type;
        }
    }
    return CHECKED_UNKNOWN;
}

void type_check_clear_unboxable(TypeCheckContext *ctx, const char *name) {
    if (!ctx || !name) return;

    UnboxableVar **prev = &ctx->unboxable_vars;
    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            // Remove from list
            *prev = u->next;
            free(u->name);
            free(u);
            return;
        }
        prev = &u->next;
    }
}

int type_check_is_loop_counter(TypeCheckContext *ctx, const char *name) {
    if (!ctx) return 0;

    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->is_loop_counter;
        }
    }
    return 0;
}

int type_check_is_accumulator(TypeCheckContext *ctx, const char *name) {
    if (!ctx) return 0;

    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->is_accumulator;
        }
    }
    return 0;
}

int type_check_is_typed_var(TypeCheckContext *ctx, const char *name) {
    if (!ctx) return 0;

    for (UnboxableVar *u = ctx->unboxable_vars; u; u = u->next) {
        if (strcmp(u->name, name) == 0) {
            return u->is_typed_var;
        }
    }
    return 0;
}

CheckedTypeKind type_check_can_unbox_annotation(Type *type_annotation) {
    if (!type_annotation) return CHECKED_UNKNOWN;

    switch (type_annotation->kind) {
        case TYPE_I8: return CHECKED_I8;
        case TYPE_I16: return CHECKED_I16;
        case TYPE_I32: return CHECKED_I32;
        case TYPE_I64: return CHECKED_I64;
        case TYPE_U8: return CHECKED_U8;
        case TYPE_U16: return CHECKED_U16;
        case TYPE_U32: return CHECKED_U32;
        case TYPE_U64: return CHECKED_U64;
        case TYPE_F32: return CHECKED_F32;
        case TYPE_F64: return CHECKED_F64;
        case TYPE_BOOL: return CHECKED_BOOL;
        default:
            // Non-primitive types cannot be unboxed
            return CHECKED_UNKNOWN;
    }
}

// ========== ESCAPE ANALYSIS ==========

// Forward declarations for mutual recursion
static int variable_escapes_in_expr_internal(Expr *expr, const char *var_name);
static int variable_escapes_in_stmt_internal(Stmt *stmt, const char *var_name);

static int variable_escapes_in_expr_internal(Expr *expr, const char *var_name) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_IDENT:
            // Variable usage itself doesn't escape
            return 0;

        case EXPR_CALL:
            // If variable is passed to a function, it escapes
            for (int i = 0; i < expr->as.call.num_args; i++) {
                Expr *arg = expr->as.call.args[i];
                if (arg->type == EXPR_IDENT && strcmp(arg->as.ident.name, var_name) == 0) {
                    return 1;  // Variable passed to function - escapes
                }
                if (variable_escapes_in_expr_internal(arg, var_name)) return 1;
            }
            return variable_escapes_in_expr_internal(expr->as.call.func, var_name);

        case EXPR_BINARY:
            return variable_escapes_in_expr_internal(expr->as.binary.left, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.binary.right, var_name);

        case EXPR_UNARY:
            return variable_escapes_in_expr_internal(expr->as.unary.operand, var_name);

        case EXPR_ASSIGN:
            return variable_escapes_in_expr_internal(expr->as.assign.value, var_name);

        case EXPR_INDEX:
            if (expr->as.index.object->type == EXPR_IDENT &&
                strcmp(expr->as.index.object->as.ident.name, var_name) == 0) {
                return 1;  // Using variable as array - escapes
            }
            return variable_escapes_in_expr_internal(expr->as.index.index, var_name);

        case EXPR_INDEX_ASSIGN:
            if (expr->as.index_assign.value->type == EXPR_IDENT &&
                strcmp(expr->as.index_assign.value->as.ident.name, var_name) == 0) {
                return 1;  // Storing variable in array - escapes
            }
            return variable_escapes_in_expr_internal(expr->as.index_assign.object, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.index_assign.index, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.index_assign.value, var_name);

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                Expr *elem = expr->as.array_literal.elements[i];
                if (elem->type == EXPR_IDENT && strcmp(elem->as.ident.name, var_name) == 0) {
                    return 1;
                }
                if (variable_escapes_in_expr_internal(elem, var_name)) return 1;
            }
            return 0;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                Expr *val = expr->as.object_literal.field_values[i];
                if (val->type == EXPR_IDENT && strcmp(val->as.ident.name, var_name) == 0) {
                    return 1;
                }
                if (variable_escapes_in_expr_internal(val, var_name)) return 1;
            }
            return 0;

        case EXPR_TERNARY:
            return variable_escapes_in_expr_internal(expr->as.ternary.condition, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.ternary.true_expr, var_name) ||
                   variable_escapes_in_expr_internal(expr->as.ternary.false_expr, var_name);

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            return variable_escapes_in_expr_internal(expr->as.prefix_inc.operand, var_name);

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return variable_escapes_in_expr_internal(expr->as.postfix_inc.operand, var_name);

        case EXPR_FUNCTION:
            // Conservative: assume function captures variable
            return 1;

        default:
            return 0;
    }
}

static int variable_escapes_in_stmt_internal(Stmt *stmt, const char *var_name) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case STMT_EXPR:
            return variable_escapes_in_expr_internal(stmt->as.expr, var_name);

        case STMT_LET:
        case STMT_CONST:
            if (stmt->as.let.value) {
                return variable_escapes_in_expr_internal(stmt->as.let.value, var_name);
            }
            return 0;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                if (stmt->as.return_stmt.value->type == EXPR_IDENT &&
                    strcmp(stmt->as.return_stmt.value->as.ident.name, var_name) == 0) {
                    return 1;  // Returning the variable - escapes
                }
                return variable_escapes_in_expr_internal(stmt->as.return_stmt.value, var_name);
            }
            return 0;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (variable_escapes_in_stmt_internal(stmt->as.block.statements[i], var_name)) {
                    return 1;
                }
            }
            return 0;

        case STMT_IF:
            return variable_escapes_in_expr_internal(stmt->as.if_stmt.condition, var_name) ||
                   variable_escapes_in_stmt_internal(stmt->as.if_stmt.then_branch, var_name) ||
                   (stmt->as.if_stmt.else_branch && variable_escapes_in_stmt_internal(stmt->as.if_stmt.else_branch, var_name));

        case STMT_WHILE:
            return variable_escapes_in_expr_internal(stmt->as.while_stmt.condition, var_name) ||
                   variable_escapes_in_stmt_internal(stmt->as.while_stmt.body, var_name);

        case STMT_FOR:
            return (stmt->as.for_loop.initializer && variable_escapes_in_stmt_internal(stmt->as.for_loop.initializer, var_name)) ||
                   (stmt->as.for_loop.condition && variable_escapes_in_expr_internal(stmt->as.for_loop.condition, var_name)) ||
                   (stmt->as.for_loop.increment && variable_escapes_in_expr_internal(stmt->as.for_loop.increment, var_name)) ||
                   variable_escapes_in_stmt_internal(stmt->as.for_loop.body, var_name);

        default:
            return 0;
    }
}

int type_check_variable_escapes(const char *var_name, Stmt *stmt) {
    return variable_escapes_in_stmt_internal(stmt, var_name);
}

int type_check_variable_escapes_in_expr(const char *var_name, Expr *expr) {
    return variable_escapes_in_expr_internal(expr, var_name);
}

// ========== LOOP ANALYSIS ==========

// Check if an expression is a simple increment pattern: i = i + 1, i++, etc.
static int is_simple_increment(Expr *expr, const char *var_name) {
    if (!expr) return 0;

    if (expr->type == EXPR_ASSIGN && strcmp(expr->as.assign.name, var_name) == 0) {
        Expr *val = expr->as.assign.value;
        if (val->type == EXPR_BINARY) {
            if (val->as.binary.left->type == EXPR_IDENT &&
                strcmp(val->as.binary.left->as.ident.name, var_name) == 0) {
                if (val->as.binary.right->type == EXPR_NUMBER &&
                    !val->as.binary.right->as.number.is_float) {
                    BinaryOp op = val->as.binary.op;
                    return op == OP_ADD || op == OP_SUB;
                }
            }
        }
    }

    if (expr->type == EXPR_PREFIX_INC || expr->type == EXPR_PREFIX_DEC) {
        if (expr->as.prefix_inc.operand->type == EXPR_IDENT &&
            strcmp(expr->as.prefix_inc.operand->as.ident.name, var_name) == 0) {
            return 1;
        }
    }
    if (expr->type == EXPR_POSTFIX_INC || expr->type == EXPR_POSTFIX_DEC) {
        if (expr->as.postfix_inc.operand->type == EXPR_IDENT &&
            strcmp(expr->as.postfix_inc.operand->as.ident.name, var_name) == 0) {
            return 1;
        }
    }

    return 0;
}

// Check if a condition is a simple comparison on the variable
static int is_simple_comparison(Expr *expr, const char *var_name) {
    if (!expr || expr->type != EXPR_BINARY) return 0;

    BinaryOp op = expr->as.binary.op;
    if (op != OP_LESS && op != OP_LESS_EQUAL &&
        op != OP_GREATER && op != OP_GREATER_EQUAL &&
        op != OP_EQUAL && op != OP_NOT_EQUAL) {
        return 0;
    }

    Expr *left = expr->as.binary.left;
    Expr *right = expr->as.binary.right;

    int left_is_var = (left->type == EXPR_IDENT && strcmp(left->as.ident.name, var_name) == 0);
    int right_is_var = (right->type == EXPR_IDENT && strcmp(right->as.ident.name, var_name) == 0);

    if (left_is_var) {
        return right->type == EXPR_NUMBER ||
               right->type == EXPR_IDENT ||
               right->type == EXPR_GET_PROPERTY;
    }
    if (right_is_var) {
        return left->type == EXPR_NUMBER ||
               left->type == EXPR_IDENT ||
               left->type == EXPR_GET_PROPERTY;
    }

    return 0;
}

void type_check_analyze_for_loop(TypeCheckContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt || stmt->type != STMT_FOR) return;

    Stmt *init = stmt->as.for_loop.initializer;
    Expr *cond = stmt->as.for_loop.condition;
    Expr *inc = stmt->as.for_loop.increment;
    Stmt *body = stmt->as.for_loop.body;

    if (!init || init->type != STMT_LET) return;

    const char *var_name = init->as.let.name;
    Expr *init_value = init->as.let.value;

    // Check initializer is an integer literal
    if (!init_value || init_value->type != EXPR_NUMBER || init_value->as.number.is_float) {
        return;
    }

    // Check condition is a simple comparison
    if (!is_simple_comparison(cond, var_name)) {
        return;
    }

    // Check increment is a simple increment
    if (!is_simple_increment(inc, var_name)) {
        return;
    }

    // Check if variable escapes in the loop body
    if (variable_escapes_in_stmt_internal(body, var_name)) {
        return;
    }

    // All checks passed - this loop counter can be unboxed!
    CheckedTypeKind native_type = CHECKED_I32;
    if (init_value->as.number.int_value > 2147483647LL ||
        init_value->as.number.int_value < -2147483648LL) {
        native_type = CHECKED_I64;
    }

    type_check_mark_unboxable(ctx, var_name, native_type, 1, 0, 0);
}

// Helper: Check if a statement modifies a variable as an accumulator
static int is_accumulator_update(Stmt *stmt, const char *var_name) {
    if (!stmt || stmt->type != STMT_EXPR) return 0;
    Expr *expr = stmt->as.expr;
    if (!expr || expr->type != EXPR_ASSIGN) return 0;

    if (strcmp(expr->as.assign.name, var_name) != 0) return 0;

    Expr *val = expr->as.assign.value;
    if (!val || val->type != EXPR_BINARY) return 0;

    if (val->as.binary.left->type != EXPR_IDENT) return 0;
    if (strcmp(val->as.binary.left->as.ident.name, var_name) != 0) return 0;

    BinaryOp op = val->as.binary.op;
    return op == OP_ADD || op == OP_SUB || op == OP_MUL ||
           op == OP_BIT_OR || op == OP_BIT_XOR || op == OP_BIT_AND;
}

static int find_accumulator_in_block(Stmt *body, const char *var_name) {
    if (!body) return 0;

    if (body->type == STMT_BLOCK) {
        for (int i = 0; i < body->as.block.count; i++) {
            if (is_accumulator_update(body->as.block.statements[i], var_name)) {
                return 1;
            }
        }
    } else if (is_accumulator_update(body, var_name)) {
        return 1;
    }
    return 0;
}

void type_check_analyze_while_loop(TypeCheckContext *ctx, Stmt *stmt) {
    if (!ctx || !stmt || stmt->type != STMT_WHILE) return;

    Stmt *body = stmt->as.while_stmt.body;
    if (!body) return;

    // Look for accumulator patterns in the type environment
    for (TypeCheckBinding *b = ctx->current_env->bindings; b; b = b->next) {
        CheckedTypeKind kind = b->type ? b->type->kind : CHECKED_UNKNOWN;
        if ((kind == CHECKED_I32 || kind == CHECKED_I64) &&
            find_accumulator_in_block(body, b->name)) {
            if (!variable_escapes_in_stmt_internal(body, b->name)) {
                type_check_mark_unboxable(ctx, b->name, kind, 0, 1, 0);
            }
        }
    }
}

// ========== TYPED VARIABLE UNBOXING ==========

// Helper: Check if expression is unboxable (primitive literal, arithmetic, etc.)
static int is_unboxable_expr(Expr *expr) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_IDENT:
        case EXPR_RUNE:
            return 1;

        case EXPR_BINARY:
            return is_unboxable_expr(expr->as.binary.left) &&
                   is_unboxable_expr(expr->as.binary.right);

        case EXPR_UNARY:
            return is_unboxable_expr(expr->as.unary.operand);

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            return is_unboxable_expr(expr->as.prefix_inc.operand);

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            return is_unboxable_expr(expr->as.postfix_inc.operand);

        case EXPR_TERNARY:
            return is_unboxable_expr(expr->as.ternary.true_expr) &&
                   is_unboxable_expr(expr->as.ternary.false_expr);

        default:
            return 0;
    }
}

// Helper: Check for incompatible assignments
static int has_incompatible_assignment_expr(Expr *expr, const char *var_name);
static int has_incompatible_assignment_stmt(Stmt *stmt, const char *var_name);

static int has_incompatible_assignment_expr(Expr *expr, const char *var_name) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_ASSIGN:
            if (strcmp(expr->as.assign.name, var_name) == 0) {
                if (!is_unboxable_expr(expr->as.assign.value)) {
                    return 1;
                }
            }
            return has_incompatible_assignment_expr(expr->as.assign.value, var_name);

        case EXPR_BINARY:
            return has_incompatible_assignment_expr(expr->as.binary.left, var_name) ||
                   has_incompatible_assignment_expr(expr->as.binary.right, var_name);

        case EXPR_UNARY:
            return has_incompatible_assignment_expr(expr->as.unary.operand, var_name);

        case EXPR_CALL:
            if (has_incompatible_assignment_expr(expr->as.call.func, var_name)) return 1;
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (has_incompatible_assignment_expr(expr->as.call.args[i], var_name)) return 1;
            }
            return 0;

        case EXPR_TERNARY:
            return has_incompatible_assignment_expr(expr->as.ternary.condition, var_name) ||
                   has_incompatible_assignment_expr(expr->as.ternary.true_expr, var_name) ||
                   has_incompatible_assignment_expr(expr->as.ternary.false_expr, var_name);

        default:
            return 0;
    }
}

static int has_incompatible_assignment_stmt(Stmt *stmt, const char *var_name) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case STMT_EXPR:
            return has_incompatible_assignment_expr(stmt->as.expr, var_name);

        case STMT_LET:
        case STMT_CONST:
            if (stmt->as.let.value) {
                return has_incompatible_assignment_expr(stmt->as.let.value, var_name);
            }
            return 0;

        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                return has_incompatible_assignment_expr(stmt->as.return_stmt.value, var_name);
            }
            return 0;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (has_incompatible_assignment_stmt(stmt->as.block.statements[i], var_name)) {
                    return 1;
                }
            }
            return 0;

        case STMT_IF:
            return has_incompatible_assignment_expr(stmt->as.if_stmt.condition, var_name) ||
                   has_incompatible_assignment_stmt(stmt->as.if_stmt.then_branch, var_name) ||
                   (stmt->as.if_stmt.else_branch && has_incompatible_assignment_stmt(stmt->as.if_stmt.else_branch, var_name));

        case STMT_WHILE:
            return has_incompatible_assignment_expr(stmt->as.while_stmt.condition, var_name) ||
                   has_incompatible_assignment_stmt(stmt->as.while_stmt.body, var_name);

        case STMT_FOR:
            return (stmt->as.for_loop.initializer && has_incompatible_assignment_stmt(stmt->as.for_loop.initializer, var_name)) ||
                   (stmt->as.for_loop.condition && has_incompatible_assignment_expr(stmt->as.for_loop.condition, var_name)) ||
                   (stmt->as.for_loop.increment && has_incompatible_assignment_expr(stmt->as.for_loop.increment, var_name)) ||
                   has_incompatible_assignment_stmt(stmt->as.for_loop.body, var_name);

        default:
            return 0;
    }
}

void type_check_analyze_typed_let(TypeCheckContext *ctx, Stmt *stmt,
                                  Stmt *containing_block, int stmt_index) {
    if (!ctx || !stmt || stmt->type != STMT_LET) return;
    if (!stmt->as.let.type_annotation) return;

    const char *var_name = stmt->as.let.name;

    CheckedTypeKind native_type = type_check_can_unbox_annotation(stmt->as.let.type_annotation);
    if (native_type == CHECKED_UNKNOWN) return;

    // Check if the initializer is unboxable
    if (stmt->as.let.value && !is_unboxable_expr(stmt->as.let.value)) {
        return;
    }

    // Check if variable escapes or has incompatible assignments in subsequent statements
    if (containing_block && containing_block->type == STMT_BLOCK) {
        for (int i = stmt_index + 1; i < containing_block->as.block.count; i++) {
            if (variable_escapes_in_stmt_internal(containing_block->as.block.statements[i], var_name)) {
                return;
            }
            if (has_incompatible_assignment_stmt(containing_block->as.block.statements[i], var_name)) {
                return;
            }
        }
    }

    // Variable can be unboxed!
    type_check_mark_unboxable(ctx, var_name, native_type, 0, 0, 1);
}

void type_check_analyze_block_for_unboxing(TypeCheckContext *ctx, Stmt *block) {
    if (!ctx || !block) return;

    if (block->type == STMT_BLOCK) {
        for (int i = 0; i < block->as.block.count; i++) {
            Stmt *stmt = block->as.block.statements[i];

            // Analyze typed let statements
            if (stmt->type == STMT_LET && stmt->as.let.type_annotation) {
                type_check_analyze_typed_let(ctx, stmt, block, i);
            }

            // Analyze for loops for loop counters
            if (stmt->type == STMT_FOR) {
                type_check_analyze_for_loop(ctx, stmt);
            }

            // Analyze while loops for accumulators
            if (stmt->type == STMT_WHILE) {
                type_check_analyze_while_loop(ctx, stmt);
            }

            // Recursively analyze nested blocks
            if (stmt->type == STMT_IF) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.if_stmt.then_branch);
                if (stmt->as.if_stmt.else_branch) {
                    type_check_analyze_block_for_unboxing(ctx, stmt->as.if_stmt.else_branch);
                }
            } else if (stmt->type == STMT_WHILE) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.while_stmt.body);
            } else if (stmt->type == STMT_FOR) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.for_loop.body);
            } else if (stmt->type == STMT_FOR_IN) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.for_in.body);
            } else if (stmt->type == STMT_BLOCK) {
                type_check_analyze_block_for_unboxing(ctx, stmt);
            } else if (stmt->type == STMT_TRY) {
                type_check_analyze_block_for_unboxing(ctx, stmt->as.try_stmt.try_block);
                if (stmt->as.try_stmt.catch_block) {
                    type_check_analyze_block_for_unboxing(ctx, stmt->as.try_stmt.catch_block);
                }
                if (stmt->as.try_stmt.finally_block) {
                    type_check_analyze_block_for_unboxing(ctx, stmt->as.try_stmt.finally_block);
                }
            }
        }
    } else if (block->type == STMT_LET && block->as.let.type_annotation) {
        type_check_analyze_typed_let(ctx, block, NULL, 0);
    }
}

// ========== TAIL CALL OPTIMIZATION ==========

// Helper: Check if expression contains a call to the given function (non-tail position)
static int contains_recursive_call(Expr *expr, const char *func_name) {
    if (!expr) return 0;

    switch (expr->type) {
        case EXPR_CALL:
            // Check if this is a call to the function
            if (expr->as.call.func->type == EXPR_IDENT &&
                strcmp(expr->as.call.func->as.ident.name, func_name) == 0) {
                return 1;
            }
            // Check callee and arguments
            if (contains_recursive_call(expr->as.call.func, func_name)) return 1;
            for (int i = 0; i < expr->as.call.num_args; i++) {
                if (contains_recursive_call(expr->as.call.args[i], func_name)) return 1;
            }
            return 0;

        case EXPR_BINARY:
            return contains_recursive_call(expr->as.binary.left, func_name) ||
                   contains_recursive_call(expr->as.binary.right, func_name);

        case EXPR_UNARY:
            return contains_recursive_call(expr->as.unary.operand, func_name);

        case EXPR_TERNARY:
            return contains_recursive_call(expr->as.ternary.condition, func_name) ||
                   contains_recursive_call(expr->as.ternary.true_expr, func_name) ||
                   contains_recursive_call(expr->as.ternary.false_expr, func_name);

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                if (contains_recursive_call(expr->as.array_literal.elements[i], func_name)) return 1;
            }
            return 0;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                if (contains_recursive_call(expr->as.object_literal.field_values[i], func_name)) return 1;
            }
            return 0;

        case EXPR_INDEX:
            return contains_recursive_call(expr->as.index.object, func_name) ||
                   contains_recursive_call(expr->as.index.index, func_name);

        case EXPR_INDEX_ASSIGN:
            return contains_recursive_call(expr->as.index_assign.object, func_name) ||
                   contains_recursive_call(expr->as.index_assign.index, func_name) ||
                   contains_recursive_call(expr->as.index_assign.value, func_name);

        case EXPR_ASSIGN:
            return contains_recursive_call(expr->as.assign.value, func_name);

        default:
            return 0;
    }
}

int is_tail_call_expr(Expr *expr, const char *func_name) {
    if (!expr || expr->type != EXPR_CALL) return 0;

    // Check if the callee is the function we're looking for
    if (expr->as.call.func->type != EXPR_IDENT) return 0;
    if (strcmp(expr->as.call.func->as.ident.name, func_name) != 0) return 0;

    // Check that arguments don't contain recursive calls (that would make it non-tail)
    for (int i = 0; i < expr->as.call.num_args; i++) {
        if (contains_recursive_call(expr->as.call.args[i], func_name)) return 0;
    }

    return 1;
}

static int stmt_is_tail_recursive(Stmt *stmt, const char *func_name) {
    if (!stmt) return 1;  // Empty statement is fine

    switch (stmt->type) {
        case STMT_RETURN:
            if (!stmt->as.return_stmt.value) return 1;  // return; is fine
            // Either it's a tail call, or it doesn't contain recursive calls
            if (is_tail_call_expr(stmt->as.return_stmt.value, func_name)) return 1;
            return !contains_recursive_call(stmt->as.return_stmt.value, func_name);

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                if (!stmt_is_tail_recursive(stmt->as.block.statements[i], func_name)) {
                    return 0;
                }
            }
            return 1;

        case STMT_IF:
            // Both branches must be tail recursive
            if (!stmt_is_tail_recursive(stmt->as.if_stmt.then_branch, func_name)) return 0;
            if (stmt->as.if_stmt.else_branch) {
                if (!stmt_is_tail_recursive(stmt->as.if_stmt.else_branch, func_name)) return 0;
            }
            // Condition must not contain recursive calls
            return !contains_recursive_call(stmt->as.if_stmt.condition, func_name);

        case STMT_EXPR:
            // Expression statements can't contain recursive calls in tail position
            return !contains_recursive_call(stmt->as.expr, func_name);

        case STMT_LET:
        case STMT_CONST:
            // Variable declarations can't have recursive calls in value
            if (stmt->as.let.value) {
                return !contains_recursive_call(stmt->as.let.value, func_name);
            }
            return 1;

        case STMT_WHILE:
        case STMT_FOR:
        case STMT_FOR_IN:
            // Loops are not compatible with tail call optimization
            // (they could contain recursive calls in non-tail position)
            return 0;

        case STMT_TRY:
            // Try-catch is not compatible with simple tail call optimization
            return 0;

        case STMT_DEFER:
            // Defer is not compatible with tail call optimization
            return 0;

        default:
            return 1;
    }
}

int is_tail_recursive_function(Stmt *body, const char *func_name) {
    if (!body || !func_name) return 0;

    // The body must contain at least one tail call to be worth optimizing
    // and all returns must be either base cases or tail calls
    return stmt_is_tail_recursive(body, func_name);
}
