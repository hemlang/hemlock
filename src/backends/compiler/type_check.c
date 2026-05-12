/*
 * Hemlock Compiler - Compile-Time Type Checking
 *
 * Context management and environment operations for the type checker.
 *
 * The type checker is split across multiple files:
 *   type_check.c           - Context management, environment ops, registries (this file)
 *   type_construction.c    - Type constructors and utilities
 *   type_compatibility.c   - Type comparison and assignability
 *   type_errors.c          - Error reporting and collection
 *   type_infer.c           - Type inference for expressions
 *   type_check_expr.c      - Expression validation
 *   type_check_stmt.c      - Statement validation and program entry
 *   type_unboxing.c        - Unboxing optimization analysis
 *   type_escape_analysis.c - Escape analysis and tail recursion
 *
 * All files share declarations via type_check_internal.h.
 * The public API is declared in compiler/type_check.h.
 */

#include "type_check_internal.h"

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

    // Free type alias definitions
    TypeAliasDef *a = ctx->type_aliases;
    while (a) {
        TypeAliasDef *next = a->next;
        free(a->name);
        checked_type_free(a->aliased_type);
        if (a->type_params) {
            for (int i = 0; i < a->num_type_params; i++) {
                free(a->type_params[i]);
            }
            free(a->type_params);
        }
        free(a);
        a = next;
    }

    // Free flow-sensitive narrowing facts
    TypeCheckNarrowing *n = ctx->narrowings;
    while (n) {
        TypeCheckNarrowing *next = n->next;
        free(n->key);
        checked_type_free(n->type);
        free(n);
        n = next;
    }

    // Free unboxable variable list
    UnboxableVar *u = ctx->unboxable_vars;
    while (u) {
        UnboxableVar *next = u->next;
        free(u->name);
        free(u);
        u = next;
    }

    // Free collected errors
    type_check_free_errors(ctx);

    free(ctx->current_function_name);
    checked_type_free(ctx->current_return_type);
    free(ctx);
}

// ========== ENVIRONMENT OPERATIONS ==========

void type_check_push_scope(TypeCheckContext *ctx) {
    TypeCheckEnv *env = calloc(1, sizeof(TypeCheckEnv));
    env->parent = ctx->current_env;
    ctx->current_env = env;
    ctx->scope_depth++;
}

void type_check_pop_scope(TypeCheckContext *ctx) {
    if (!ctx->current_env) return;

    TypeCheckEnv *env = ctx->current_env;
    ctx->current_env = env->parent;

    TypeCheckNarrowing **pn = &ctx->narrowings;
    while (*pn) {
        TypeCheckNarrowing *cur = *pn;
        if (cur->depth >= ctx->scope_depth) {
            *pn = cur->next;
            free(cur->key);
            checked_type_free(cur->type);
            free(cur);
        } else {
            pn = &cur->next;
        }
    }
    if (ctx->scope_depth > 0) ctx->scope_depth--;

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
                     int is_const, int is_param, int line) {
    // Check for variable shadowing in parent scopes
    if (ctx->current_env->parent) {
        for (TypeCheckEnv *env = ctx->current_env->parent; env; env = env->parent) {
            for (TypeCheckBinding *b = env->bindings; b; b = b->next) {
                if (strcmp(b->name, name) == 0) {
                    type_warning(ctx, line,
                        "variable '%s' shadows variable declared at line %d",
                        name, b->line);
                    break;  // Only warn once
                }
            }
        }
    }

    TypeCheckBinding *binding = calloc(1, sizeof(TypeCheckBinding));
    binding->name = strdup(name);
    binding->type = type;
    binding->is_const = is_const;
    binding->is_param = is_param;  // Function parameters cannot be unboxed
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

static char* join_expr_key(const char *base, const char *op, const char *suffix) {
    if (!base || !op || !suffix) return NULL;
    size_t len = strlen(base) + strlen(op) + strlen(suffix) + 1;
    char *key = malloc(len);
    if (!key) return NULL;
    snprintf(key, len, "%s%s%s", base, op, suffix);
    return key;
}

char* type_check_expr_key(Expr *expr) {
    if (!expr) return NULL;

    switch (expr->type) {
        case EXPR_IDENT:
            return expr->as.ident.name ? strdup(expr->as.ident.name) : NULL;

        case EXPR_GET_PROPERTY: {
            char *base = type_check_expr_key(expr->as.get_property.object);
            char *key = join_expr_key(base, ".", expr->as.get_property.property);
            free(base);
            return key;
        }

        case EXPR_OPTIONAL_CHAIN: {
            if (!expr->as.optional_chain.is_property || !expr->as.optional_chain.property) {
                return NULL;
            }
            char *base = type_check_expr_key(expr->as.optional_chain.object);
            char *key = join_expr_key(base, "?.", expr->as.optional_chain.property);
            free(base);
            return key;
        }

        default:
            return NULL;
    }
}

void type_check_add_narrowing(TypeCheckContext *ctx, const char *key, CheckedType *type) {
    if (!ctx || !key || !type) return;

    for (TypeCheckNarrowing *n = ctx->narrowings; n; n = n->next) {
        if (n->depth == ctx->scope_depth && strcmp(n->key, key) == 0) {
            checked_type_free(n->type);
            n->type = checked_type_clone(type);
            return;
        }
    }

    TypeCheckNarrowing *n = calloc(1, sizeof(TypeCheckNarrowing));
    if (!n) return;
    n->key = strdup(key);
    n->type = checked_type_clone(type);
    n->depth = ctx->scope_depth;
    n->next = ctx->narrowings;
    ctx->narrowings = n;
}

CheckedType* type_check_lookup_narrowing(TypeCheckContext *ctx, const char *key) {
    if (!ctx || !key) return NULL;
    for (TypeCheckNarrowing *n = ctx->narrowings; n; n = n->next) {
        if (strcmp(n->key, key) == 0) {
            return n->type;
        }
    }
    return NULL;
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
                                char **type_params, int num_type_params,
                                char **field_names, CheckedType **field_types,
                                int *field_optional, int num_fields,
                                char **method_names, CheckedType **method_types,
                                int *method_optional, int num_methods) {
    ObjectDef *def = calloc(1, sizeof(ObjectDef));
    def->name = strdup(name);
    def->num_fields = num_fields;

    // Copy type parameters (for generic types)
    def->num_type_params = num_type_params;
    if (num_type_params > 0) {
        def->type_params = calloc(num_type_params, sizeof(char*));
        for (int i = 0; i < num_type_params; i++) {
            def->type_params[i] = strdup(type_params[i]);
        }
    } else {
        def->type_params = NULL;
    }

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

    // Copy method signatures
    def->num_methods = num_methods;
    if (num_methods > 0) {
        def->method_names = calloc(num_methods, sizeof(char*));
        def->method_types = calloc(num_methods, sizeof(CheckedType*));
        def->method_optional = calloc(num_methods, sizeof(int));
        for (int i = 0; i < num_methods; i++) {
            def->method_names[i] = strdup(method_names[i]);
            def->method_types[i] = method_types[i];
            def->method_optional[i] = method_optional ? method_optional[i] : 0;
        }
    } else {
        def->method_names = NULL;
        def->method_types = NULL;
        def->method_optional = NULL;
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

// ========== TYPE ALIAS REGISTRATION ==========

void type_check_register_type_alias(TypeCheckContext *ctx, const char *name,
                                     CheckedType *aliased_type,
                                     char **type_params, int num_type_params) {
    TypeAliasDef *def = calloc(1, sizeof(TypeAliasDef));
    def->name = strdup(name);
    def->aliased_type = aliased_type;
    def->num_type_params = num_type_params;

    if (num_type_params > 0 && type_params) {
        def->type_params = calloc(num_type_params, sizeof(char*));
        for (int i = 0; i < num_type_params; i++) {
            def->type_params[i] = strdup(type_params[i]);
        }
    }

    def->next = ctx->type_aliases;
    ctx->type_aliases = def;
}

TypeAliasDef* type_check_lookup_type_alias(TypeCheckContext *ctx, const char *name) {
    for (TypeAliasDef *a = ctx->type_aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0) {
            return a;
        }
    }
    return NULL;
}
