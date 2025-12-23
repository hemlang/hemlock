/**
 * Hemlock Bundler Implementation
 *
 * Resolves all imports from an entry point and flattens into a single AST.
 */

#define _XOPEN_SOURCE 500
#include "bundler.h"
#include "../include/parser.h"
#include "../include/lexer.h"
#include "../include/ast_serialize.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <zlib.h>

// ========== PATH SECURITY ==========

// Check if a path component contains directory traversal attempts
// Returns 1 if path is safe, 0 if it contains traversal
static int is_safe_subpath(const char *path) {
    if (!path) return 0;

    // Reject absolute paths in subpaths
    if (path[0] == '/') return 0;

    // Check for ".." components
    const char *p = path;
    while (*p) {
        // Check for ".." at start or after "/"
        if ((p == path || *(p-1) == '/') && p[0] == '.' && p[1] == '.') {
            // ".." followed by end, "/" or nothing is traversal
            if (p[2] == '\0' || p[2] == '/') {
                return 0;
            }
        }
        p++;
    }

    return 1;
}

// ========== INTERNAL STRUCTURES ==========

typedef struct {
    Bundle *bundle;
    BundleOptions options;
    char *current_dir;
} BundleContext;

// Unprefixed builtin names that are already registered by the interpreter.
// When bundling stdlib modules, we skip declarations that would shadow these.
// This list must match the unprefixed aliases in builtins/registration.c
static const char *BUILTIN_NAMES[] = {
    // Math functions
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
    "sqrt", "pow", "exp", "log", "log10", "log2",
    "floor", "ceil", "round", "trunc",
    // Environment functions
    "getenv", "setenv", "unsetenv", "get_pid",
    // FFI callback functions
    "callback", "callback_free",
    "ptr_read_i32", "ptr_deref_i32", "ptr_write_i32", "ptr_offset",
    NULL  // Sentinel
};

// Check if a name is a builtin that would conflict
static int is_builtin_name(const char *name) {
    for (int i = 0; BUILTIN_NAMES[i] != NULL; i++) {
        if (strcmp(name, BUILTIN_NAMES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// ========== FORWARD DECLARATIONS ==========

static char* find_stdlib_path(void);
static char* resolve_import_path(BundleContext *ctx, const char *importer_path, const char *import_path);
static BundledModule* load_module_for_bundle(BundleContext *ctx, const char *absolute_path, int is_entry);
static int collect_exports(BundledModule *module);

// ========== TREE SHAKING HELPERS ==========

// Create a new symbol
static Symbol* symbol_new(const char *name, const char *module_path, Stmt *definition) {
    Symbol *sym = malloc(sizeof(Symbol));
    if (!sym) {
        fprintf(stderr, "Error: Failed to allocate symbol\n");
        return NULL;
    }
    sym->name = strdup(name);
    sym->module_path = module_path ? strdup(module_path) : NULL;
    if (!sym->name || (module_path && !sym->module_path)) {
        fprintf(stderr, "Error: Failed to allocate symbol name\n");
        free(sym->name);
        free(sym->module_path);
        free(sym);
        return NULL;
    }
    sym->definition = definition;
    sym->is_export = 0;
    sym->is_reachable = 0;
    sym->is_side_effect = 0;
    sym->dependencies = malloc(sizeof(char*) * 16);
    if (!sym->dependencies) {
        fprintf(stderr, "Error: Failed to allocate symbol dependencies\n");
        free(sym->name);
        free(sym->module_path);
        free(sym);
        return NULL;
    }
    sym->num_dependencies = 0;
    sym->dep_capacity = 16;
    return sym;
}

// Add a dependency to a symbol
static void symbol_add_dep(Symbol *sym, const char *dep_name) {
    // Skip self-references
    if (strcmp(sym->name, dep_name) == 0) return;

    // Check for duplicates
    for (int i = 0; i < sym->num_dependencies; i++) {
        if (strcmp(sym->dependencies[i], dep_name) == 0) return;
    }

    if (sym->num_dependencies >= sym->dep_capacity) {
        int new_capacity = sym->dep_capacity * 2;
        char **new_deps = realloc(sym->dependencies, sizeof(char*) * new_capacity);
        if (!new_deps) {
            fprintf(stderr, "Error: Failed to grow symbol dependencies\n");
            return;
        }
        sym->dependencies = new_deps;
        sym->dep_capacity = new_capacity;
    }
    char *dup = strdup(dep_name);
    if (!dup) {
        fprintf(stderr, "Error: Failed to duplicate dependency name\n");
        return;
    }
    sym->dependencies[sym->num_dependencies++] = dup;
}

// Free a symbol
static void symbol_free(Symbol *sym) {
    if (!sym) return;
    free(sym->name);
    free(sym->module_path);
    for (int i = 0; i < sym->num_dependencies; i++) {
        free(sym->dependencies[i]);
    }
    free(sym->dependencies);
    free(sym);
}

// Create a new dependency graph
static DependencyGraph* dep_graph_new(void) {
    DependencyGraph *graph = malloc(sizeof(DependencyGraph));
    if (!graph) {
        fprintf(stderr, "Error: Failed to allocate dependency graph\n");
        return NULL;
    }
    graph->symbols = malloc(sizeof(Symbol*) * 64);
    graph->entry_points = malloc(sizeof(char*) * 32);
    if (!graph->symbols || !graph->entry_points) {
        fprintf(stderr, "Error: Failed to allocate dependency graph arrays\n");
        free(graph->symbols);
        free(graph->entry_points);
        free(graph);
        return NULL;
    }
    graph->num_symbols = 0;
    graph->capacity = 64;
    graph->num_entry_points = 0;
    graph->entry_capacity = 32;
    return graph;
}

// Add a symbol to the graph
static void dep_graph_add_symbol(DependencyGraph *graph, Symbol *sym) {
    if (graph->num_symbols >= graph->capacity) {
        int new_capacity = graph->capacity * 2;
        Symbol **new_symbols = realloc(graph->symbols, sizeof(Symbol*) * new_capacity);
        if (!new_symbols) {
            fprintf(stderr, "Error: Failed to grow dependency graph symbols\n");
            return;
        }
        graph->symbols = new_symbols;
        graph->capacity = new_capacity;
    }
    graph->symbols[graph->num_symbols++] = sym;
}

// Add an entry point
static void dep_graph_add_entry(DependencyGraph *graph, const char *name) {
    // Check for duplicates
    for (int i = 0; i < graph->num_entry_points; i++) {
        if (strcmp(graph->entry_points[i], name) == 0) return;
    }

    if (graph->num_entry_points >= graph->entry_capacity) {
        int new_capacity = graph->entry_capacity * 2;
        char **new_entries = realloc(graph->entry_points, sizeof(char*) * new_capacity);
        if (!new_entries) {
            fprintf(stderr, "Error: Failed to grow entry points\n");
            return;
        }
        graph->entry_points = new_entries;
        graph->entry_capacity = new_capacity;
    }
    char *dup = strdup(name);
    if (!dup) {
        fprintf(stderr, "Error: Failed to duplicate entry point name\n");
        return;
    }
    graph->entry_points[graph->num_entry_points++] = dup;
}

// Find a symbol by name
static Symbol* dep_graph_find(DependencyGraph *graph, const char *name) {
    for (int i = 0; i < graph->num_symbols; i++) {
        if (strcmp(graph->symbols[i]->name, name) == 0) {
            return graph->symbols[i];
        }
    }
    return NULL;
}

// Free a dependency graph
static void dep_graph_free(DependencyGraph *graph) {
    if (!graph) return;
    for (int i = 0; i < graph->num_symbols; i++) {
        symbol_free(graph->symbols[i]);
    }
    free(graph->symbols);
    for (int i = 0; i < graph->num_entry_points; i++) {
        free(graph->entry_points[i]);
    }
    free(graph->entry_points);
    free(graph);
}

// ========== AST DEPENDENCY WALKER ==========

// Forward declarations for mutual recursion
static void collect_expr_deps(Expr *expr, Symbol *sym);
static void collect_stmt_deps(Stmt *stmt, Symbol *sym);

// Collect identifier dependencies from an expression
static void collect_expr_deps(Expr *expr, Symbol *sym) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_IDENT:
            symbol_add_dep(sym, expr->as.ident.name);
            break;

        case EXPR_BINARY:
            collect_expr_deps(expr->as.binary.left, sym);
            collect_expr_deps(expr->as.binary.right, sym);
            break;

        case EXPR_UNARY:
            collect_expr_deps(expr->as.unary.operand, sym);
            break;

        case EXPR_TERNARY:
            collect_expr_deps(expr->as.ternary.condition, sym);
            collect_expr_deps(expr->as.ternary.true_expr, sym);
            collect_expr_deps(expr->as.ternary.false_expr, sym);
            break;

        case EXPR_CALL:
            collect_expr_deps(expr->as.call.func, sym);
            for (int i = 0; i < expr->as.call.num_args; i++) {
                collect_expr_deps(expr->as.call.args[i], sym);
            }
            break;

        case EXPR_ASSIGN:
            symbol_add_dep(sym, expr->as.assign.name);
            collect_expr_deps(expr->as.assign.value, sym);
            break;

        case EXPR_GET_PROPERTY:
            collect_expr_deps(expr->as.get_property.object, sym);
            break;

        case EXPR_SET_PROPERTY:
            collect_expr_deps(expr->as.set_property.object, sym);
            collect_expr_deps(expr->as.set_property.value, sym);
            break;

        case EXPR_INDEX:
            collect_expr_deps(expr->as.index.object, sym);
            collect_expr_deps(expr->as.index.index, sym);
            break;

        case EXPR_INDEX_ASSIGN:
            collect_expr_deps(expr->as.index_assign.object, sym);
            collect_expr_deps(expr->as.index_assign.index, sym);
            collect_expr_deps(expr->as.index_assign.value, sym);
            break;

        case EXPR_FUNCTION:
            // Collect dependencies from function body
            // Note: params are local, so they shadow any outer references
            collect_stmt_deps(expr->as.function.body, sym);
            // Collect default param value dependencies
            for (int i = 0; i < expr->as.function.num_params; i++) {
                if (expr->as.function.param_defaults && expr->as.function.param_defaults[i]) {
                    collect_expr_deps(expr->as.function.param_defaults[i], sym);
                }
            }
            break;

        case EXPR_ARRAY_LITERAL:
            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                collect_expr_deps(expr->as.array_literal.elements[i], sym);
            }
            break;

        case EXPR_OBJECT_LITERAL:
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                collect_expr_deps(expr->as.object_literal.field_values[i], sym);
            }
            break;

        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
            collect_expr_deps(expr->as.prefix_inc.operand, sym);
            break;

        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC:
            collect_expr_deps(expr->as.postfix_inc.operand, sym);
            break;

        case EXPR_AWAIT:
            collect_expr_deps(expr->as.await_expr.awaited_expr, sym);
            break;

        case EXPR_STRING_INTERPOLATION:
            for (int i = 0; i < expr->as.string_interpolation.num_parts; i++) {
                collect_expr_deps(expr->as.string_interpolation.expr_parts[i], sym);
            }
            break;

        case EXPR_OPTIONAL_CHAIN:
            collect_expr_deps(expr->as.optional_chain.object, sym);
            if (expr->as.optional_chain.index) {
                collect_expr_deps(expr->as.optional_chain.index, sym);
            }
            if (expr->as.optional_chain.args) {
                for (int i = 0; i < expr->as.optional_chain.num_args; i++) {
                    collect_expr_deps(expr->as.optional_chain.args[i], sym);
                }
            }
            break;

        case EXPR_NULL_COALESCE:
            collect_expr_deps(expr->as.null_coalesce.left, sym);
            collect_expr_deps(expr->as.null_coalesce.right, sym);
            break;

        case EXPR_NUMBER:
        case EXPR_BOOL:
        case EXPR_STRING:
        case EXPR_RUNE:
        case EXPR_NULL:
            // Literals have no dependencies
            break;
    }
}

// Collect dependencies from a statement
static void collect_stmt_deps(Stmt *stmt, Symbol *sym) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_LET:
            collect_expr_deps(stmt->as.let.value, sym);
            break;

        case STMT_CONST:
            collect_expr_deps(stmt->as.const_stmt.value, sym);
            break;

        case STMT_EXPR:
            collect_expr_deps(stmt->as.expr, sym);
            break;

        case STMT_IF:
            collect_expr_deps(stmt->as.if_stmt.condition, sym);
            collect_stmt_deps(stmt->as.if_stmt.then_branch, sym);
            if (stmt->as.if_stmt.else_branch) {
                collect_stmt_deps(stmt->as.if_stmt.else_branch, sym);
            }
            break;

        case STMT_WHILE:
            collect_expr_deps(stmt->as.while_stmt.condition, sym);
            collect_stmt_deps(stmt->as.while_stmt.body, sym);
            break;

        case STMT_FOR:
            collect_stmt_deps(stmt->as.for_loop.initializer, sym);
            collect_expr_deps(stmt->as.for_loop.condition, sym);
            collect_expr_deps(stmt->as.for_loop.increment, sym);
            collect_stmt_deps(stmt->as.for_loop.body, sym);
            break;

        case STMT_FOR_IN:
            collect_expr_deps(stmt->as.for_in.iterable, sym);
            collect_stmt_deps(stmt->as.for_in.body, sym);
            break;

        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                collect_stmt_deps(stmt->as.block.statements[i], sym);
            }
            break;

        case STMT_RETURN:
            collect_expr_deps(stmt->as.return_stmt.value, sym);
            break;

        case STMT_TRY:
            collect_stmt_deps(stmt->as.try_stmt.try_block, sym);
            collect_stmt_deps(stmt->as.try_stmt.catch_block, sym);
            collect_stmt_deps(stmt->as.try_stmt.finally_block, sym);
            break;

        case STMT_THROW:
            collect_expr_deps(stmt->as.throw_stmt.value, sym);
            break;

        case STMT_SWITCH:
            collect_expr_deps(stmt->as.switch_stmt.expr, sym);
            for (int i = 0; i < stmt->as.switch_stmt.num_cases; i++) {
                collect_expr_deps(stmt->as.switch_stmt.case_values[i], sym);
                collect_stmt_deps(stmt->as.switch_stmt.case_bodies[i], sym);
            }
            break;

        case STMT_DEFER:
            collect_expr_deps(stmt->as.defer_stmt.call, sym);
            break;

        case STMT_DEFINE_OBJECT:
            // Field defaults may have dependencies
            for (int i = 0; i < stmt->as.define_object.num_fields; i++) {
                if (stmt->as.define_object.field_defaults &&
                    stmt->as.define_object.field_defaults[i]) {
                    collect_expr_deps(stmt->as.define_object.field_defaults[i], sym);
                }
            }
            break;

        case STMT_ENUM:
            // Enum variant values may have dependencies
            for (int i = 0; i < stmt->as.enum_decl.num_variants; i++) {
                if (stmt->as.enum_decl.variant_values &&
                    stmt->as.enum_decl.variant_values[i]) {
                    collect_expr_deps(stmt->as.enum_decl.variant_values[i], sym);
                }
            }
            break;

        case STMT_BREAK:
        case STMT_CONTINUE:
        case STMT_IMPORT:
        case STMT_EXPORT:
        case STMT_IMPORT_FFI:
        case STMT_EXTERN_FN:
            // No dependencies to collect
            break;
    }
}

// Check if a statement defines a symbol (returns the name, or NULL)
static const char* stmt_defines_symbol(Stmt *stmt) {
    if (!stmt) return NULL;

    switch (stmt->type) {
        case STMT_LET:
            return stmt->as.let.name;
        case STMT_CONST:
            return stmt->as.const_stmt.name;
        case STMT_DEFINE_OBJECT:
            return stmt->as.define_object.name;
        case STMT_ENUM:
            return stmt->as.enum_decl.name;
        case STMT_EXPR:
            // Check for function assignment: fn foo() {}
            if (stmt->as.expr && stmt->as.expr->type == EXPR_ASSIGN) {
                Expr *assign = stmt->as.expr;
                if (assign->as.assign.value &&
                    assign->as.assign.value->type == EXPR_FUNCTION) {
                    return assign->as.assign.name;
                }
            }
            break;
        default:
            break;
    }
    return NULL;
}

// Check if a statement has side effects (should always be included)
static int stmt_has_side_effects(Stmt *stmt) {
    if (!stmt) return 0;

    switch (stmt->type) {
        case STMT_EXPR: {
            Expr *expr = stmt->as.expr;
            if (!expr) return 0;

            // Function calls have side effects
            if (expr->type == EXPR_CALL) return 1;

            // Assignments to non-function values are side effects
            if (expr->type == EXPR_ASSIGN) {
                if (!expr->as.assign.value ||
                    expr->as.assign.value->type != EXPR_FUNCTION) {
                    return 1;
                }
            }

            // Property/index assignments have side effects
            if (expr->type == EXPR_SET_PROPERTY) return 1;
            if (expr->type == EXPR_INDEX_ASSIGN) return 1;

            // Increment/decrement have side effects
            if (expr->type == EXPR_PREFIX_INC || expr->type == EXPR_PREFIX_DEC ||
                expr->type == EXPR_POSTFIX_INC || expr->type == EXPR_POSTFIX_DEC) {
                return 1;
            }

            // Await has side effects
            if (expr->type == EXPR_AWAIT) return 1;

            break;
        }

        // Control flow statements may have side effects
        case STMT_IF:
        case STMT_WHILE:
        case STMT_FOR:
        case STMT_FOR_IN:
        case STMT_TRY:
        case STMT_THROW:
        case STMT_SWITCH:
        case STMT_DEFER:
            return 1;

        // Import/export statements are structural, not side effects
        case STMT_IMPORT:
        case STMT_EXPORT:
            return 0;

        default:
            break;
    }

    return 0;
}

// ========== HELPER FUNCTIONS ==========

static char* find_stdlib_path(void) {
    char exe_path[PATH_MAX];
    char resolved[PATH_MAX];

    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char *dir = dirname(exe_path);

        snprintf(resolved, sizeof(resolved), "%s/stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            return realpath(resolved, NULL);
        }

        snprintf(resolved, sizeof(resolved), "%s/../stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            return realpath(resolved, NULL);
        }
    }

    if (getcwd(resolved, sizeof(resolved))) {
        char stdlib_path[PATH_MAX + 16];
        snprintf(stdlib_path, sizeof(stdlib_path), "%s/stdlib", resolved);
        if (access(stdlib_path, F_OK) == 0) {
            return realpath(stdlib_path, NULL);
        }
    }

    if (access("/usr/local/lib/hemlock/stdlib", F_OK) == 0) {
        return strdup("/usr/local/lib/hemlock/stdlib");
    }

    return NULL;
}

static char* resolve_import_path(BundleContext *ctx, const char *importer_path, const char *import_path) {
    char resolved[PATH_MAX];

    // Handle @stdlib alias
    if (strncmp(import_path, "@stdlib/", 8) == 0) {
        if (!ctx->bundle->stdlib_path) {
            fprintf(stderr, "Error: @stdlib alias used but stdlib directory not found\n");
            return NULL;
        }
        const char *module_subpath = import_path + 8;

        // SECURITY: Validate subpath doesn't contain directory traversal
        if (!is_safe_subpath(module_subpath)) {
            fprintf(stderr, "Error: Invalid module path '%s' - directory traversal not allowed\n", import_path);
            return NULL;
        }

        snprintf(resolved, PATH_MAX, "%s/%s", ctx->bundle->stdlib_path, module_subpath);
    }
    // Absolute path
    else if (import_path[0] == '/') {
        strncpy(resolved, import_path, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
    }
    // Relative path
    else {
        const char *base_dir;
        char importer_dir[PATH_MAX];

        if (importer_path) {
            strncpy(importer_dir, importer_path, PATH_MAX - 1);
            importer_dir[PATH_MAX - 1] = '\0';
            base_dir = dirname(importer_dir);
        } else {
            base_dir = ctx->current_dir;
        }

        snprintf(resolved, PATH_MAX, "%s/%s", base_dir, import_path);
    }

    // Add .hml extension if needed
    size_t len = strlen(resolved);
    if (len < 4 || strcmp(resolved + len - 4, ".hml") != 0) {
        strncat(resolved, ".hml", PATH_MAX - len - 1);
    }

    char *absolute = realpath(resolved, NULL);
    if (!absolute) {
        fprintf(stderr, "Error: Cannot resolve import path '%s' -> '%s'\n", import_path, resolved);
        return NULL;
    }

    return absolute;
}

// Parse a module file
static Stmt** parse_file(const char *path, int *stmt_count) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *source = malloc(file_size + 1);
    size_t bytes_read = fread(source, 1, file_size, file);
    source[bytes_read] = '\0';
    fclose(file);

    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Stmt **statements = parse_program(&parser, stmt_count);
    free(source);

    if (parser.had_error) {
        fprintf(stderr, "Error: Failed to parse '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    return statements;
}

// Check if module is already in bundle
static BundledModule* find_module_in_bundle(Bundle *bundle, const char *absolute_path) {
    for (int i = 0; i < bundle->num_modules; i++) {
        if (strcmp(bundle->modules[i]->absolute_path, absolute_path) == 0) {
            return bundle->modules[i];
        }
    }
    return NULL;
}

// Add module to bundle
static void add_module_to_bundle(Bundle *bundle, BundledModule *module) {
    if (bundle->num_modules >= bundle->capacity) {
        int new_capacity = bundle->capacity * 2;
        BundledModule **new_modules = realloc(bundle->modules, sizeof(BundledModule*) * new_capacity);
        if (!new_modules) {
            fprintf(stderr, "Error: Failed to grow bundle modules\n");
            return;
        }
        bundle->modules = new_modules;
        bundle->capacity = new_capacity;
    }
    bundle->modules[bundle->num_modules++] = module;
}

// Generate module ID from index
static char* generate_module_id(int index) {
    char *id = malloc(16);
    if (!id) {
        fprintf(stderr, "Error: Failed to allocate module ID\n");
        return NULL;
    }
    snprintf(id, 16, "mod_%d", index);
    return id;
}

// Recursively load a module and its dependencies
static BundledModule* load_module_for_bundle(BundleContext *ctx, const char *absolute_path, int is_entry) {
    // Check if already loaded
    BundledModule *existing = find_module_in_bundle(ctx->bundle, absolute_path);
    if (existing) {
        return existing;
    }

    if (ctx->options.verbose) {
        fprintf(stderr, "  Loading: %s\n", absolute_path);
    }

    // Create new module
    BundledModule *module = malloc(sizeof(BundledModule));
    if (!module) {
        fprintf(stderr, "Error: Failed to allocate bundled module\n");
        return NULL;
    }
    module->absolute_path = strdup(absolute_path);
    module->module_id = generate_module_id(ctx->bundle->num_modules);
    if (!module->absolute_path || !module->module_id) {
        fprintf(stderr, "Error: Failed to allocate module path or ID\n");
        free(module->absolute_path);
        free(module->module_id);
        free(module);
        return NULL;
    }
    module->is_entry = is_entry;
    module->is_flattened = 0;
    module->export_names = NULL;
    module->num_exports = 0;

    // Add to bundle immediately (for cycle detection)
    add_module_to_bundle(ctx->bundle, module);

    // Parse the file
    module->statements = parse_file(absolute_path, &module->num_statements);
    if (!module->statements) {
        return NULL;
    }

    // Collect exports
    collect_exports(module);

    // Recursively load imported modules
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        if (stmt->type == STMT_IMPORT) {
            char *resolved = resolve_import_path(ctx, absolute_path, stmt->as.import_stmt.module_path);
            if (!resolved) {
                return NULL;
            }

            BundledModule *imported = load_module_for_bundle(ctx, resolved, 0);
            free(resolved);

            if (!imported) {
                fprintf(stderr, "Error: Failed to load import '%s' from '%s'\n",
                        stmt->as.import_stmt.module_path, absolute_path);
                return NULL;
            }
        }
        else if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_reexport) {
            char *resolved = resolve_import_path(ctx, absolute_path, stmt->as.export_stmt.module_path);
            if (!resolved) {
                return NULL;
            }

            BundledModule *reexported = load_module_for_bundle(ctx, resolved, 0);
            free(resolved);

            if (!reexported) {
                return NULL;
            }
        }
    }

    return module;
}

// Collect export names from a module
static int collect_exports(BundledModule *module) {
    int capacity = 16;
    module->export_names = malloc(sizeof(char*) * capacity);
    module->num_exports = 0;

    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        if (stmt->type == STMT_EXPORT) {
            if (stmt->as.export_stmt.is_declaration) {
                // Export declaration
                Stmt *decl = stmt->as.export_stmt.declaration;
                const char *name = NULL;

                if (decl->type == STMT_LET) {
                    name = decl->as.let.name;
                } else if (decl->type == STMT_CONST) {
                    name = decl->as.const_stmt.name;
                }

                if (name) {
                    if (module->num_exports >= capacity) {
                        capacity *= 2;
                        module->export_names = realloc(module->export_names, sizeof(char*) * capacity);
                    }
                    module->export_names[module->num_exports++] = strdup(name);
                }
            } else {
                // Export list
                for (int j = 0; j < stmt->as.export_stmt.num_exports; j++) {
                    char *name = stmt->as.export_stmt.export_aliases[j]
                        ? stmt->as.export_stmt.export_aliases[j]
                        : stmt->as.export_stmt.export_names[j];

                    if (module->num_exports >= capacity) {
                        capacity *= 2;
                        module->export_names = realloc(module->export_names, sizeof(char*) * capacity);
                    }
                    module->export_names[module->num_exports++] = strdup(name);
                }
            }
        }
    }

    return 0;
}

// ========== FLATTENING ==========

// Forward declaration for tree shaking filter
static int should_include_stmt(Bundle *bundle, Stmt *stmt);

// Add statement to bundle's flattened output
static void add_flattened_stmt(Bundle *bundle, Stmt *stmt) {
    if (bundle->num_statements >= bundle->stmt_capacity) {
        bundle->stmt_capacity = bundle->stmt_capacity ? bundle->stmt_capacity * 2 : 64;
        bundle->statements = realloc(bundle->statements, sizeof(Stmt*) * bundle->stmt_capacity);
    }
    bundle->statements[bundle->num_statements++] = stmt;
}

// Flatten a single module into the bundle
static int flatten_module(Bundle *bundle, BundledModule *module) {
    // Check if already processed
    if (module->is_flattened) {
        return 0;
    }

    // Mark as flattened early to prevent infinite recursion
    module->is_flattened = 1;

    // First, flatten all dependencies
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        if (stmt->type == STMT_IMPORT) {
            // Find the imported module by matching the import path
            const char *import_path = stmt->as.import_stmt.module_path;

            // Skip leading "./" for relative paths
            if (strncmp(import_path, "./", 2) == 0) {
                import_path += 2;
            }

            for (int j = 0; j < bundle->num_modules; j++) {
                BundledModule *dep = bundle->modules[j];

                // Match by checking if the module's path ends with the import path
                // For @stdlib/X, match /stdlib/X.hml
                // For relative paths, match the basename
                int matches = 0;

                if (strncmp(import_path, "@stdlib/", 8) == 0) {
                    // Check for stdlib module: look for /stdlib/module_name.hml
                    const char *module_name = import_path + 8;  // Skip "@stdlib/"
                    char expected[256];
                    snprintf(expected, sizeof(expected), "/stdlib/%s.hml", module_name);
                    if (strstr(dep->absolute_path, expected)) {
                        matches = 1;
                    }
                } else {
                    // For relative imports, check if absolute path ends with /import_path
                    size_t path_len = strlen(dep->absolute_path);
                    size_t import_len = strlen(import_path);

                    // Build expected suffix: /module_name (with .hml only if not already present)
                    char expected_suffix[256];
                    if (import_len >= 4 && strcmp(import_path + import_len - 4, ".hml") == 0) {
                        // Import already ends with .hml
                        snprintf(expected_suffix, sizeof(expected_suffix), "/%s", import_path);
                    } else {
                        // Add .hml extension
                        snprintf(expected_suffix, sizeof(expected_suffix), "/%s.hml", import_path);
                    }
                    size_t suffix_len = strlen(expected_suffix);

                    if (path_len >= suffix_len) {
                        const char *actual_suffix = dep->absolute_path + path_len - suffix_len;
                        if (strcmp(actual_suffix, expected_suffix) == 0) {
                            matches = 1;
                        }
                    }
                }

                if (matches) {
                    flatten_module(bundle, dep);
                    break;
                }
            }
        }
    }

    // Now add this module's statements (excluding imports/exports)
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        // Handle import statements: generate alias assignments for aliased imports
        if (stmt->type == STMT_IMPORT) {
            // For each aliased import, generate: let alias = original;
            if (!stmt->as.import_stmt.is_namespace && stmt->as.import_stmt.import_aliases) {
                for (int j = 0; j < stmt->as.import_stmt.num_imports; j++) {
                    char *alias = stmt->as.import_stmt.import_aliases[j];
                    char *original = stmt->as.import_stmt.import_names[j];

                    // Only generate assignment if there's an alias different from the original
                    if (alias && strcmp(alias, original) != 0) {
                        // Create: let alias = original;
                        Expr *var_ref = expr_ident(original);
                        Stmt *let_stmt = stmt_let(alias, var_ref);
                        add_flattened_stmt(bundle, let_stmt);
                    }
                }
            }
            continue;
        }

        // Handle export declarations
        if (stmt->type == STMT_EXPORT) {
            if (stmt->as.export_stmt.is_declaration) {
                Stmt *decl = stmt->as.export_stmt.declaration;
                const char *decl_name = NULL;

                // Get the declaration name
                if (decl->type == STMT_LET) {
                    decl_name = decl->as.let.name;
                } else if (decl->type == STMT_CONST) {
                    decl_name = decl->as.const_stmt.name;
                }

                // For stdlib modules, skip declarations that shadow builtins
                // This prevents "Variable already defined" errors when bundling
                if (decl_name && bundle->stdlib_path &&
                    strstr(module->absolute_path, bundle->stdlib_path) != NULL &&
                    is_builtin_name(decl_name)) {
                    // Skip - this would shadow a builtin
                    continue;
                }

                // Tree shaking: skip unreachable exports
                if (!should_include_stmt(bundle, stmt)) {
                    continue;
                }

                // Add the underlying declaration
                add_flattened_stmt(bundle, decl);
            }
            // Skip export lists and re-exports
            continue;
        }

        // Tree shaking: skip unreachable statements
        if (!should_include_stmt(bundle, stmt)) {
            continue;
        }

        // Add regular statement
        add_flattened_stmt(bundle, stmt);
    }

    return 0;
}

// ========== PUBLIC API IMPLEMENTATION ==========

BundleOptions bundle_options_default(void) {
    BundleOptions opts = {
        .include_stdlib = 1,
        .tree_shake = 0,
        .namespace_symbols = 0,  // Disabled for now - simpler flattening
        .verbose = 0
    };
    return opts;
}

Bundle* bundle_create(const char *entry_path, const BundleOptions *options) {
    BundleOptions opts = options ? *options : bundle_options_default();

    // Get current directory
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "Error: Could not get current directory\n");
        return NULL;
    }

    // Resolve entry path
    char *absolute_entry = realpath(entry_path, NULL);
    if (!absolute_entry) {
        fprintf(stderr, "Error: Cannot find entry file '%s'\n", entry_path);
        return NULL;
    }

    // Create bundle
    Bundle *bundle = malloc(sizeof(Bundle));
    bundle->modules = malloc(sizeof(BundledModule*) * 32);
    bundle->num_modules = 0;
    bundle->capacity = 32;
    bundle->entry_path = absolute_entry;
    bundle->stdlib_path = find_stdlib_path();
    bundle->statements = NULL;
    bundle->num_statements = 0;
    bundle->stmt_capacity = 0;
    bundle->dep_graph = NULL;  // Created by bundle_tree_shake if needed

    if (opts.verbose) {
        fprintf(stderr, "Bundling: %s\n", absolute_entry);
        if (bundle->stdlib_path) {
            fprintf(stderr, "Stdlib: %s\n", bundle->stdlib_path);
        }
    }

    // Create context
    BundleContext ctx = {
        .bundle = bundle,
        .options = opts,
        .current_dir = cwd
    };

    // Load entry module and all dependencies
    BundledModule *entry = load_module_for_bundle(&ctx, absolute_entry, 1);
    if (!entry) {
        bundle_free(bundle);
        return NULL;
    }

    if (opts.verbose) {
        fprintf(stderr, "Loaded %d module(s)\n", bundle->num_modules);
    }

    return bundle;
}

int bundle_flatten(Bundle *bundle) {
    if (!bundle || bundle->num_modules == 0) {
        return -1;
    }

    // Find entry module
    BundledModule *entry = NULL;
    for (int i = 0; i < bundle->num_modules; i++) {
        if (bundle->modules[i]->is_entry) {
            entry = bundle->modules[i];
            break;
        }
    }

    if (!entry) {
        fprintf(stderr, "Error: No entry module found\n");
        return -1;
    }

    // Flatten starting from entry (will recursively flatten dependencies first)
    int result = flatten_module(bundle, entry);

    return result;
}

Stmt** bundle_get_statements(Bundle *bundle, int *out_count) {
    if (!bundle) {
        *out_count = 0;
        return NULL;
    }
    *out_count = bundle->num_statements;
    return bundle->statements;
}

int bundle_write_hmlc(Bundle *bundle, const char *output_path, uint16_t flags) {
    if (!bundle || !bundle->statements) {
        fprintf(stderr, "Error: Bundle not flattened\n");
        return -1;
    }

    return ast_serialize_to_file(output_path, bundle->statements, bundle->num_statements, flags);
}

int bundle_write_compressed(Bundle *bundle, const char *output_path) {
    if (!bundle || !bundle->statements) {
        fprintf(stderr, "Error: Bundle not flattened\n");
        return -1;
    }

    // First serialize to memory
    size_t serialized_size;
    uint8_t *serialized = ast_serialize(bundle->statements, bundle->num_statements,
                                         HMLC_FLAG_DEBUG, &serialized_size);
    if (!serialized) {
        return -1;
    }

    // Compress with zlib
    uLongf compressed_size = compressBound(serialized_size);
    uint8_t *compressed = malloc(compressed_size);

    int ret = compress2(compressed, &compressed_size, serialized, serialized_size, Z_BEST_COMPRESSION);
    free(serialized);

    if (ret != Z_OK) {
        fprintf(stderr, "Error: Compression failed\n");
        free(compressed);
        return -1;
    }

    // Write header + compressed data
    FILE *f = fopen(output_path, "wb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open output file '%s'\n", output_path);
        free(compressed);
        return -1;
    }

    // Write magic "HMLB" + version + uncompressed size + compressed data
    uint32_t magic = 0x424C4D48;  // "HMLB"
    uint16_t version = 1;
    uint32_t orig_size = (uint32_t)serialized_size;

    fwrite(&magic, 4, 1, f);
    fwrite(&version, 2, 1, f);
    fwrite(&orig_size, 4, 1, f);
    fwrite(compressed, 1, compressed_size, f);

    fclose(f);
    free(compressed);

    return 0;
}

void bundle_free(Bundle *bundle) {
    if (!bundle) return;

    for (int i = 0; i < bundle->num_modules; i++) {
        BundledModule *mod = bundle->modules[i];
        free(mod->absolute_path);
        free(mod->module_id);

        // Don't free statements - they may be referenced in flattened output
        // They'll be freed when the program exits

        for (int j = 0; j < mod->num_exports; j++) {
            free(mod->export_names[j]);
        }
        free(mod->export_names);
        free(mod);
    }

    free(bundle->modules);
    free(bundle->entry_path);
    if (bundle->stdlib_path) {
        free(bundle->stdlib_path);
    }

    // Free dependency graph if it was created
    if (bundle->dep_graph) {
        dep_graph_free(bundle->dep_graph);
    }

    // Don't free individual statements - they're owned by modules
    free(bundle->statements);
    free(bundle);
}

BundledModule* bundle_get_module(Bundle *bundle, const char *path) {
    return find_module_in_bundle(bundle, path);
}

void bundle_print_summary(Bundle *bundle) {
    if (!bundle) {
        printf("Bundle: (null)\n");
        return;
    }

    printf("=== Bundle Summary ===\n");
    printf("Entry: %s\n", bundle->entry_path);
    printf("Modules: %d\n", bundle->num_modules);

    for (int i = 0; i < bundle->num_modules; i++) {
        BundledModule *mod = bundle->modules[i];
        printf("  [%s] %s%s\n",
               mod->module_id,
               mod->absolute_path,
               mod->is_entry ? " (entry)" : "");
        printf("       Statements: %d, Exports: %d\n",
               mod->num_statements, mod->num_exports);

        if (mod->num_exports > 0) {
            printf("       Exports: ");
            for (int j = 0; j < mod->num_exports; j++) {
                printf("%s%s", mod->export_names[j],
                       j < mod->num_exports - 1 ? ", " : "");
            }
            printf("\n");
        }
    }

    if (bundle->statements) {
        printf("Flattened: %d statements\n", bundle->num_statements);
    }

    // Print tree shaking stats if available
    if (bundle->dep_graph) {
        int total = 0, reachable = 0, eliminated = 0;
        bundle_get_shake_stats(bundle, &total, &reachable, &eliminated);
        printf("Tree Shaking: %d/%d symbols reachable (%d eliminated)\n",
               reachable, total, eliminated);
    }
}

// ========== TREE SHAKING IMPLEMENTATION ==========

// Build dependency graph from all modules
static int build_dependency_graph(Bundle *bundle, int verbose) {
    DependencyGraph *graph = dep_graph_new();
    bundle->dep_graph = graph;

    // Counter for anonymous side-effect symbols
    int side_effect_counter = 0;

    // Phase 1: Collect all symbols and their definitions
    for (int m = 0; m < bundle->num_modules; m++) {
        BundledModule *mod = bundle->modules[m];

        for (int i = 0; i < mod->num_statements; i++) {
            Stmt *stmt = mod->statements[i];

            // Handle export declarations
            if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_declaration) {
                Stmt *decl = stmt->as.export_stmt.declaration;
                const char *name = stmt_defines_symbol(decl);
                if (name) {
                    Symbol *sym = symbol_new(name, mod->absolute_path, decl);
                    sym->is_export = 1;
                    collect_stmt_deps(decl, sym);
                    dep_graph_add_symbol(graph, sym);

                    if (verbose) {
                        fprintf(stderr, "  Symbol: %s (export, %d deps)\n",
                                name, sym->num_dependencies);
                    }
                }
                continue;
            }

            // Handle regular declarations
            const char *name = stmt_defines_symbol(stmt);
            if (name) {
                Symbol *sym = symbol_new(name, mod->absolute_path, stmt);
                collect_stmt_deps(stmt, sym);
                dep_graph_add_symbol(graph, sym);

                if (verbose) {
                    fprintf(stderr, "  Symbol: %s (%d deps)\n",
                            name, sym->num_dependencies);
                }
            }
            // Handle side-effecting statements (not declarations)
            else if (stmt_has_side_effects(stmt)) {
                // Create a synthetic symbol for side-effecting code
                char synth_name[64];
                snprintf(synth_name, sizeof(synth_name), "__side_effect_%d", side_effect_counter++);

                Symbol *sym = symbol_new(synth_name, mod->absolute_path, stmt);
                sym->is_side_effect = 1;
                collect_stmt_deps(stmt, sym);
                dep_graph_add_symbol(graph, sym);

                // Side effects in entry module are always entry points
                if (mod->is_entry) {
                    dep_graph_add_entry(graph, synth_name);
                }

                if (verbose) {
                    fprintf(stderr, "  Side effect: %s (%d deps)\n",
                            synth_name, sym->num_dependencies);
                }
            }
        }
    }

    // Phase 2: Collect entry points from entry module's imports
    for (int m = 0; m < bundle->num_modules; m++) {
        BundledModule *mod = bundle->modules[m];
        if (!mod->is_entry) continue;

        for (int i = 0; i < mod->num_statements; i++) {
            Stmt *stmt = mod->statements[i];
            if (stmt->type == STMT_IMPORT) {
                // Add all imported names as entry points
                if (!stmt->as.import_stmt.is_namespace) {
                    for (int j = 0; j < stmt->as.import_stmt.num_imports; j++) {
                        const char *name = stmt->as.import_stmt.import_names[j];
                        dep_graph_add_entry(graph, name);
                        if (verbose) {
                            fprintf(stderr, "  Entry point (import): %s\n", name);
                        }
                    }
                }
            }
        }
    }

    if (verbose) {
        fprintf(stderr, "Dependency graph: %d symbols, %d entry points\n",
                graph->num_symbols, graph->num_entry_points);
    }

    return 0;
}

// Mark reachable symbols using worklist algorithm
static void mark_reachable(DependencyGraph *graph, int verbose) {
    // Create worklist with all entry points
    char **worklist = malloc(sizeof(char*) * (graph->num_entry_points + graph->num_symbols));
    int worklist_size = 0;

    // Add all entry points to worklist
    for (int i = 0; i < graph->num_entry_points; i++) {
        worklist[worklist_size++] = graph->entry_points[i];
    }

    // Process worklist
    while (worklist_size > 0) {
        // Pop from worklist
        const char *name = worklist[--worklist_size];

        // Find symbol
        Symbol *sym = dep_graph_find(graph, name);
        if (!sym || sym->is_reachable) continue;

        // Mark as reachable
        sym->is_reachable = 1;

        if (verbose) {
            fprintf(stderr, "  Marking reachable: %s\n", name);
        }

        // Add dependencies to worklist
        for (int i = 0; i < sym->num_dependencies; i++) {
            const char *dep = sym->dependencies[i];
            Symbol *dep_sym = dep_graph_find(graph, dep);
            if (dep_sym && !dep_sym->is_reachable) {
                worklist[worklist_size++] = sym->dependencies[i];
            }
        }
    }

    free(worklist);
}

// Public tree shaking function
int bundle_tree_shake(Bundle *bundle, int verbose) {
    if (!bundle || bundle->num_modules == 0) {
        return -1;
    }

    if (verbose) {
        fprintf(stderr, "\n=== Tree Shaking Analysis ===\n");
    }

    // Build dependency graph
    if (build_dependency_graph(bundle, verbose) != 0) {
        return -1;
    }

    // Mark reachable symbols
    if (verbose) {
        fprintf(stderr, "\nReachability analysis:\n");
    }
    mark_reachable(bundle->dep_graph, verbose);

    // Print statistics
    if (verbose) {
        int total = 0, reachable = 0, eliminated = 0;
        bundle_get_shake_stats(bundle, &total, &reachable, &eliminated);
        fprintf(stderr, "\nTree shaking result: %d/%d symbols reachable (%d eliminated)\n",
                reachable, total, eliminated);

        // List eliminated symbols
        if (eliminated > 0) {
            fprintf(stderr, "Eliminated symbols:\n");
            for (int i = 0; i < bundle->dep_graph->num_symbols; i++) {
                Symbol *sym = bundle->dep_graph->symbols[i];
                if (!sym->is_reachable && !sym->is_side_effect) {
                    fprintf(stderr, "  - %s\n", sym->name);
                }
            }
        }
    }

    return 0;
}

// Get tree shaking statistics
void bundle_get_shake_stats(Bundle *bundle, int *total, int *reachable, int *eliminated) {
    *total = 0;
    *reachable = 0;
    *eliminated = 0;

    if (!bundle || !bundle->dep_graph) return;

    DependencyGraph *graph = bundle->dep_graph;
    for (int i = 0; i < graph->num_symbols; i++) {
        Symbol *sym = graph->symbols[i];
        // Don't count side-effect symbols in statistics
        if (sym->is_side_effect) continue;

        (*total)++;
        if (sym->is_reachable) {
            (*reachable)++;
        } else {
            (*eliminated)++;
        }
    }
}

// Check if a statement should be included after tree shaking
static int should_include_stmt(Bundle *bundle, Stmt *stmt) {
    if (!bundle->dep_graph) return 1;  // No tree shaking, include everything

    // Import statements are structural, handle separately during flattening
    if (stmt->type == STMT_IMPORT) {
        return 1;
    }

    // For export declarations, check if the exported symbol is reachable
    if (stmt->type == STMT_EXPORT) {
        if (stmt->as.export_stmt.is_declaration) {
            Stmt *decl = stmt->as.export_stmt.declaration;
            const char *name = stmt_defines_symbol(decl);
            if (name) {
                Symbol *sym = dep_graph_find(bundle->dep_graph, name);
                return sym && sym->is_reachable;
            }
        }
        // Export lists and re-exports are handled during flattening
        return 1;
    }

    // Check if this statement defines a symbol
    const char *name = stmt_defines_symbol(stmt);
    if (name) {
        Symbol *sym = dep_graph_find(bundle->dep_graph, name);
        return sym && sym->is_reachable;
    }

    // Check if this is a side-effecting statement
    if (stmt_has_side_effects(stmt)) {
        // Side-effect statements have synthetic names, need to find by definition
        for (int i = 0; i < bundle->dep_graph->num_symbols; i++) {
            Symbol *sym = bundle->dep_graph->symbols[i];
            if (sym->definition == stmt) {
                return sym->is_reachable;
            }
        }
        // If not found in graph, it's probably from a non-entry module
        // Include it to be safe (conservative)
        return 1;
    }

    // Default: include
    return 1;
}
