/*
 * Tricycle - Hemlock's Memory Safety Checker
 *
 * Main implementation file containing context management,
 * environment operations, and analysis entry points.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "tricycle/tricycle.h"
#include "hemlock_limits.h"

// ========== ERROR CODE STRINGS ==========

static const char* error_codes[] = {
    "TRIC001",  // TRIC_DOUBLE_FREE
    "TRIC002",  // TRIC_USE_AFTER_FREE
    "TRIC003",  // TRIC_USE_AFTER_MOVE
    "TRIC004",  // TRIC_MEMORY_LEAK
    "TRIC005",  // TRIC_NULL_DEREF
    "TRIC006",  // TRIC_DANGLING_ASYNC
    "TRIC007",  // TRIC_BORROW_OUTLIVES_OWNER
    "TRIC008",  // TRIC_MOVE_OF_BORROWED
    "TRIC009",  // TRIC_FREE_OF_BORROWED
    "TRIC010",  // TRIC_UNKNOWN_LIFETIME
    "TRIC011",  // TRIC_INVALID_OWNERSHIP
};

static const char* error_names[] = {
    "DOUBLE_FREE",
    "USE_AFTER_FREE",
    "USE_AFTER_MOVE",
    "MEMORY_LEAK",
    "NULL_DEREF",
    "DANGLING_ASYNC",
    "BORROW_OUTLIVES_OWNER",
    "MOVE_OF_BORROWED",
    "FREE_OF_BORROWED",
    "UNKNOWN_LIFETIME",
    "INVALID_OWNERSHIP",
};

// ========== CONTEXT MANAGEMENT ==========

TricycleContext* tricycle_new(const char *filename) {
    TricycleContext *ctx = calloc(1, sizeof(TricycleContext));
    if (!ctx) return NULL;

    ctx->filename = filename ? strdup(filename) : NULL;
    ctx->source = NULL;
    ctx->program = NULL;
    ctx->stmt_count = 0;
    ctx->type_ctx = NULL;

    // Initialize ownership environment with global scope
    ctx->current_env = calloc(1, sizeof(OwnershipEnv));
    if (ctx->current_env) {
        ctx->current_env->bindings = NULL;
        ctx->current_env->parent = NULL;
        ctx->current_env->scope_kind = SCOPE_GLOBAL;
        ctx->current_env->scope_label = NULL;
        ctx->current_env->scope_start_line = 0;
    }

    ctx->async_borrows = NULL;
    ctx->diagnostics = NULL;
    ctx->diagnostics_tail = NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;
    ctx->strict_mode = 0;
    ctx->infer_ownership = 1;
    ctx->verbose = 0;
    ctx->collect_all = 0;

    return ctx;
}

void tricycle_free(TricycleContext *ctx) {
    if (!ctx) return;

    // Free filename
    if (ctx->filename) {
        free((char*)ctx->filename);
    }

    // Free all ownership environments
    while (ctx->current_env) {
        OwnershipEnv *parent = ctx->current_env->parent;

        // Free bindings in current scope
        OwnershipBinding *binding = ctx->current_env->bindings;
        while (binding) {
            OwnershipBinding *next = binding->next;
            if (binding->name) free(binding->name);
            tricycle_free_state(&binding->state);
            free(binding);
            binding = next;
        }

        if (ctx->current_env->scope_label) {
            free(ctx->current_env->scope_label);
        }

        free(ctx->current_env);
        ctx->current_env = parent;
    }

    // Free async borrows
    AsyncBorrow *borrow = ctx->async_borrows;
    while (borrow) {
        AsyncBorrow *next = borrow->next;
        if (borrow->var_name) free(borrow->var_name);
        free(borrow);
        borrow = next;
    }

    // Free diagnostics
    tricycle_free_diagnostics(ctx);

    free(ctx);
}

void tricycle_set_source(TricycleContext *ctx, const char *source) {
    if (ctx) {
        ctx->source = source;
    }
}

void tricycle_set_type_ctx(TricycleContext *ctx, TypeCheckContext *type_ctx) {
    if (ctx) {
        ctx->type_ctx = type_ctx;
    }
}

// ========== ENVIRONMENT OPERATIONS ==========

void tricycle_push_scope(TricycleContext *ctx, ScopeKind kind) {
    tricycle_push_scope_labeled(ctx, kind, NULL);
}

void tricycle_push_scope_labeled(TricycleContext *ctx, ScopeKind kind, const char *label) {
    if (!ctx) return;

    OwnershipEnv *new_env = calloc(1, sizeof(OwnershipEnv));
    if (!new_env) return;

    new_env->bindings = NULL;
    new_env->parent = ctx->current_env;
    new_env->scope_kind = kind;
    new_env->scope_label = label ? strdup(label) : NULL;
    new_env->scope_start_line = 0;

    ctx->current_env = new_env;
}

void tricycle_pop_scope(TricycleContext *ctx) {
    if (!ctx || !ctx->current_env) return;

    // Check for memory leaks before popping
    tricycle_check_leaks(ctx);

    OwnershipEnv *old_env = ctx->current_env;
    ctx->current_env = old_env->parent;

    // Free bindings
    OwnershipBinding *binding = old_env->bindings;
    while (binding) {
        OwnershipBinding *next = binding->next;
        if (binding->name) free(binding->name);
        tricycle_free_state(&binding->state);
        free(binding);
        binding = next;
    }

    if (old_env->scope_label) {
        free(old_env->scope_label);
    }

    free(old_env);
}

void tricycle_bind(TricycleContext *ctx, const char *name, MemoryStateKind state,
                   int line, int column, int is_ptr_type) {
    if (!ctx || !ctx->current_env || !name) return;

    OwnershipBinding *binding = calloc(1, sizeof(OwnershipBinding));
    if (!binding) return;

    binding->name = strdup(name);
    binding->state.kind = state;
    binding->state.allocation_line = (state == MEM_ALLOCATED) ? line : 0;
    binding->state.allocation_column = (state == MEM_ALLOCATED) ? column : 0;
    binding->state.free_line = 0;
    binding->state.free_column = 0;
    binding->state.move_line = 0;
    binding->state.move_column = 0;
    binding->state.owner_name = strdup(name);
    binding->state.moved_to = NULL;
    binding->state.borrow_count = 0;
    binding->is_parameter = 0;
    binding->param_kind = OWNER_DEFAULT;
    binding->declaration_line = line;
    binding->declaration_column = column;
    binding->is_ptr_type = is_ptr_type;

    // Add to front of binding list
    binding->next = ctx->current_env->bindings;
    ctx->current_env->bindings = binding;
}

void tricycle_bind_param(TricycleContext *ctx, const char *name, OwnershipKind kind,
                         int line, int column, int is_ptr_type) {
    if (!ctx || !ctx->current_env || !name) return;

    OwnershipBinding *binding = calloc(1, sizeof(OwnershipBinding));
    if (!binding) return;

    binding->name = strdup(name);

    // Parameters with OWN take ownership, BORROW means borrowed
    if (kind == OWNER_OWN) {
        binding->state.kind = MEM_ALLOCATED;
    } else if (kind == OWNER_BORROW || kind == OWNER_BORROW_MUT) {
        binding->state.kind = MEM_BORROWED;
    } else {
        // Default for pointer params: borrow
        binding->state.kind = is_ptr_type ? MEM_BORROWED : MEM_UNKNOWN;
    }

    binding->state.allocation_line = 0;
    binding->state.allocation_column = 0;
    binding->state.owner_name = NULL;
    binding->state.moved_to = NULL;
    binding->state.borrow_count = 0;
    binding->is_parameter = 1;
    binding->param_kind = kind;
    binding->declaration_line = line;
    binding->declaration_column = column;
    binding->is_ptr_type = is_ptr_type;

    binding->next = ctx->current_env->bindings;
    ctx->current_env->bindings = binding;
}

OwnershipBinding* tricycle_lookup(TricycleContext *ctx, const char *name) {
    if (!ctx || !name) return NULL;

    OwnershipEnv *env = ctx->current_env;
    while (env) {
        OwnershipBinding *binding = env->bindings;
        while (binding) {
            if (strcmp(binding->name, name) == 0) {
                return binding;
            }
            binding = binding->next;
        }
        env = env->parent;
    }

    return NULL;
}

void tricycle_update_state(TricycleContext *ctx, const char *name,
                           MemoryStateKind new_state, int line, int column) {
    OwnershipBinding *binding = tricycle_lookup(ctx, name);
    if (!binding) return;

    MemoryStateKind old_state = binding->state.kind;
    binding->state.kind = new_state;

    if (new_state == MEM_UNALLOCATED) {
        binding->state.free_line = line;
        binding->state.free_column = column;
    } else if (new_state == MEM_MOVED) {
        binding->state.move_line = line;
        binding->state.move_column = column;
    } else if (new_state == MEM_ALLOCATED && old_state != MEM_ALLOCATED) {
        binding->state.allocation_line = line;
        binding->state.allocation_column = column;
    }
}

void tricycle_mark_moved(TricycleContext *ctx, const char *from, const char *to,
                         int line, int column) {
    OwnershipBinding *from_binding = tricycle_lookup(ctx, from);
    if (!from_binding) return;

    from_binding->state.kind = MEM_MOVED;
    from_binding->state.move_line = line;
    from_binding->state.move_column = column;
    if (from_binding->state.moved_to) {
        free(from_binding->state.moved_to);
    }
    from_binding->state.moved_to = strdup(to);
}

// ========== MEMORY STATE UTILITIES ==========

MemoryState tricycle_clone_state(const MemoryState *state) {
    MemoryState clone = {0};
    if (!state) return clone;

    clone.kind = state->kind;
    clone.allocation_line = state->allocation_line;
    clone.allocation_column = state->allocation_column;
    clone.free_line = state->free_line;
    clone.free_column = state->free_column;
    clone.move_line = state->move_line;
    clone.move_column = state->move_column;
    clone.owner_name = state->owner_name ? strdup(state->owner_name) : NULL;
    clone.moved_to = state->moved_to ? strdup(state->moved_to) : NULL;
    clone.borrow_count = state->borrow_count;

    return clone;
}

void tricycle_free_state(MemoryState *state) {
    if (!state) return;
    if (state->owner_name) {
        free(state->owner_name);
        state->owner_name = NULL;
    }
    if (state->moved_to) {
        free(state->moved_to);
        state->moved_to = NULL;
    }
}

// ========== ERROR CODE HELPERS ==========

const char* tricycle_error_code(TricycleErrorKind kind) {
    if (kind >= 0 && kind < (int)(sizeof(error_codes) / sizeof(error_codes[0]))) {
        return error_codes[kind];
    }
    return "TRIC???";
}

const char* tricycle_error_name(TricycleErrorKind kind) {
    if (kind >= 0 && kind < (int)(sizeof(error_names) / sizeof(error_names[0]))) {
        return error_names[kind];
    }
    return "UNKNOWN";
}

const char* tricycle_severity_str(TricycleSeverity severity) {
    switch (severity) {
        case TRIC_ERROR:   return "error";
        case TRIC_WARNING: return "warning";
        case TRIC_NOTE:    return "note";
        default:           return "unknown";
    }
}

// ========== DIAGNOSTICS ==========

static TricycleDiagnostic* create_diagnostic(TricycleSeverity severity,
                                              TricycleErrorKind kind,
                                              int line, int column,
                                              const char *message) {
    TricycleDiagnostic *diag = calloc(1, sizeof(TricycleDiagnostic));
    if (!diag) return NULL;

    diag->severity = severity;
    diag->kind = kind;
    diag->line = line;
    diag->column = column;
    diag->end_column = column;
    diag->message = message ? strdup(message) : NULL;
    diag->hint = NULL;
    diag->related = NULL;
    diag->related_count = 0;
    diag->next = NULL;

    return diag;
}

static void add_diagnostic(TricycleContext *ctx, TricycleDiagnostic *diag) {
    if (!ctx || !diag) return;

    if (ctx->diagnostics_tail) {
        ctx->diagnostics_tail->next = diag;
    } else {
        ctx->diagnostics = diag;
    }
    ctx->diagnostics_tail = diag;

    if (diag->severity == TRIC_ERROR) {
        ctx->error_count++;
    } else if (diag->severity == TRIC_WARNING) {
        ctx->warning_count++;
    }
}

void tricycle_error(TricycleContext *ctx, TricycleErrorKind kind,
                    int line, int column, const char *fmt, ...) {
    if (!ctx) return;

    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    TricycleDiagnostic *diag = create_diagnostic(TRIC_ERROR, kind, line, column, message);
    add_diagnostic(ctx, diag);
}

void tricycle_warning(TricycleContext *ctx, TricycleErrorKind kind,
                      int line, int column, const char *fmt, ...) {
    if (!ctx) return;

    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    TricycleDiagnostic *diag = create_diagnostic(TRIC_WARNING, kind, line, column, message);
    add_diagnostic(ctx, diag);
}

void tricycle_note(TricycleContext *ctx, int line, int column, const char *fmt, ...) {
    if (!ctx) return;

    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // Notes are attached as additional diagnostics
    TricycleDiagnostic *diag = create_diagnostic(TRIC_NOTE, TRIC_UNKNOWN_LIFETIME, line, column, message);
    add_diagnostic(ctx, diag);
}

void tricycle_add_related(TricycleContext *ctx, int line, int column, const char *message) {
    if (!ctx || !ctx->diagnostics_tail) return;

    TricycleDiagnostic *diag = ctx->diagnostics_tail;

    // Grow related locations array
    int new_count = diag->related_count + 1;
    RelatedLocation *new_related = realloc(diag->related, new_count * sizeof(RelatedLocation));
    if (!new_related) return;

    diag->related = new_related;
    diag->related[diag->related_count].line = line;
    diag->related[diag->related_count].column = column;
    diag->related[diag->related_count].message = message ? strdup(message) : NULL;
    diag->related_count = new_count;
}

void tricycle_add_hint(TricycleContext *ctx, const char *hint) {
    if (!ctx || !ctx->diagnostics_tail || !hint) return;

    if (ctx->diagnostics_tail->hint) {
        free(ctx->diagnostics_tail->hint);
    }
    ctx->diagnostics_tail->hint = strdup(hint);
}

TricycleDiagnostic* tricycle_get_diagnostics(TricycleContext *ctx) {
    return ctx ? ctx->diagnostics : NULL;
}

void tricycle_print_diagnostics(TricycleContext *ctx) {
    if (!ctx) return;

    TricycleDiagnostic *diag = ctx->diagnostics;
    while (diag) {
        // Print main diagnostic
        fprintf(stderr, "%s[%s]: %s\n",
                tricycle_severity_str(diag->severity),
                tricycle_error_code(diag->kind),
                diag->message ? diag->message : "");

        // Print location
        fprintf(stderr, "  --> %s:%d:%d\n",
                ctx->filename ? ctx->filename : "<unknown>",
                diag->line, diag->column);

        // Print related locations
        for (int i = 0; i < diag->related_count; i++) {
            fprintf(stderr, "   |\n");
            fprintf(stderr, "%3d|     %s\n",
                    diag->related[i].line,
                    diag->related[i].message ? diag->related[i].message : "");
        }

        // Print hint
        if (diag->hint) {
            fprintf(stderr, "   |\n");
            fprintf(stderr, "   = help: %s\n", diag->hint);
        }

        fprintf(stderr, "\n");
        diag = diag->next;
    }
}

void tricycle_print_diagnostics_json(TricycleContext *ctx) {
    if (!ctx) return;

    printf("{\n");
    printf("  \"uri\": \"file://%s\",\n", ctx->filename ? ctx->filename : "");
    printf("  \"diagnostics\": [\n");

    TricycleDiagnostic *diag = ctx->diagnostics;
    int first = 1;
    while (diag) {
        if (!first) printf(",\n");
        first = 0;

        printf("    {\n");
        printf("      \"range\": {\n");
        printf("        \"start\": {\"line\": %d, \"character\": %d},\n",
               diag->line - 1, diag->column);
        printf("        \"end\": {\"line\": %d, \"character\": %d}\n",
               diag->line - 1, diag->end_column);
        printf("      },\n");
        printf("      \"severity\": %d,\n", diag->severity == TRIC_ERROR ? 1 : 2);
        printf("      \"code\": \"%s\",\n", tricycle_error_code(diag->kind));
        printf("      \"source\": \"tricycle\",\n");
        printf("      \"message\": \"%s\"", diag->message ? diag->message : "");

        if (diag->related_count > 0) {
            printf(",\n      \"relatedInformation\": [\n");
            for (int i = 0; i < diag->related_count; i++) {
                if (i > 0) printf(",\n");
                printf("        {\n");
                printf("          \"location\": {\n");
                printf("            \"uri\": \"file://%s\",\n", ctx->filename ? ctx->filename : "");
                printf("            \"range\": {\n");
                printf("              \"start\": {\"line\": %d, \"character\": %d},\n",
                       diag->related[i].line - 1, diag->related[i].column);
                printf("              \"end\": {\"line\": %d, \"character\": %d}\n",
                       diag->related[i].line - 1, diag->related[i].column);
                printf("            }\n");
                printf("          },\n");
                printf("          \"message\": \"%s\"\n",
                       diag->related[i].message ? diag->related[i].message : "");
                printf("        }");
            }
            printf("\n      ]");
        }

        printf("\n    }");
        diag = diag->next;
    }

    printf("\n  ]\n");
    printf("}\n");
}

void tricycle_free_diagnostics(TricycleContext *ctx) {
    if (!ctx) return;

    TricycleDiagnostic *diag = ctx->diagnostics;
    while (diag) {
        TricycleDiagnostic *next = diag->next;

        if (diag->message) free(diag->message);
        if (diag->hint) free(diag->hint);

        for (int i = 0; i < diag->related_count; i++) {
            if (diag->related[i].message) {
                free(diag->related[i].message);
            }
        }
        if (diag->related) free(diag->related);

        free(diag);
        diag = next;
    }

    ctx->diagnostics = NULL;
    ctx->diagnostics_tail = NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;
}

// ========== UTILITY FUNCTIONS ==========

int tricycle_is_ptr_type(Type *type) {
    if (!type) return 0;
    return type->kind == TYPE_PTR || type->kind == TYPE_BUFFER;
}

const char* tricycle_get_var_name(Expr *expr) {
    if (!expr) return NULL;
    if (expr->type == EXPR_IDENT) {
        return expr->as.ident.name;
    }
    return NULL;
}

int tricycle_is_alloc_call(Expr *call_expr) {
    if (!call_expr || call_expr->type != EXPR_CALL) return 0;

    Expr *func = call_expr->as.call.func;
    if (!func || func->type != EXPR_IDENT) return 0;

    const char *name = func->as.ident.name;
    return (strcmp(name, "alloc") == 0 ||
            strcmp(name, "buffer") == 0 ||
            strcmp(name, "talloc") == 0);
}

int tricycle_is_free_call(Expr *call_expr) {
    if (!call_expr || call_expr->type != EXPR_CALL) return 0;

    Expr *func = call_expr->as.call.func;
    if (!func || func->type != EXPR_IDENT) return 0;

    const char *name = func->as.ident.name;
    return strcmp(name, "free") == 0;
}

int tricycle_is_spawn_call(Expr *call_expr) {
    if (!call_expr || call_expr->type != EXPR_CALL) return 0;

    Expr *func = call_expr->as.call.func;
    if (!func || func->type != EXPR_IDENT) return 0;

    const char *name = func->as.ident.name;
    return strcmp(name, "spawn") == 0;
}

int tricycle_is_join_call(Expr *call_expr) {
    if (!call_expr || call_expr->type != EXPR_CALL) return 0;

    Expr *func = call_expr->as.call.func;
    if (!func || func->type != EXPR_IDENT) return 0;

    const char *name = func->as.ident.name;
    return (strcmp(name, "join") == 0 || strcmp(name, "await") == 0);
}

int tricycle_is_ptr_op(Expr *call_expr) {
    if (!call_expr || call_expr->type != EXPR_CALL) return 0;

    Expr *func = call_expr->as.call.func;
    if (!func || func->type != EXPR_IDENT) return 0;

    const char *name = func->as.ident.name;
    return (strncmp(name, "ptr_read_", 9) == 0 ||
            strncmp(name, "ptr_write_", 10) == 0 ||
            strcmp(name, "ptr_offset") == 0 ||
            strcmp(name, "memcpy") == 0 ||
            strcmp(name, "memset") == 0 ||
            strcmp(name, "memmove") == 0);
}

int tricycle_is_ptr_expr(TricycleContext *ctx, Expr *expr) {
    if (!expr) return 0;

    // Check if it's a call to alloc/buffer
    if (tricycle_is_alloc_call(expr)) return 1;

    // Check if it's an identifier bound as a pointer
    if (expr->type == EXPR_IDENT) {
        OwnershipBinding *binding = tricycle_lookup(ctx, expr->as.ident.name);
        if (binding && binding->is_ptr_type) return 1;
    }

    // If we have type context, use it
    if (ctx->type_ctx) {
        CheckedType *type = type_check_infer_expr(ctx->type_ctx, expr);
        if (type && (type->kind == CHECKED_PTR || type->kind == CHECKED_BUFFER)) {
            return 1;
        }
    }

    return 0;
}
