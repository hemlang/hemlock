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
        snprintf(resolved, PATH_MAX, "%s/%s", ctx->bundle->stdlib_path, module_subpath);
    }
    // Absolute path
    else if (import_path[0] == '/') {
        strncpy(resolved, import_path, PATH_MAX);
    }
    // Relative path
    else {
        const char *base_dir;
        char importer_dir[PATH_MAX];

        if (importer_path) {
            strncpy(importer_dir, importer_path, PATH_MAX);
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
        bundle->capacity *= 2;
        bundle->modules = realloc(bundle->modules, sizeof(BundledModule*) * bundle->capacity);
    }
    bundle->modules[bundle->num_modules++] = module;
}

// Generate module ID from index
static char* generate_module_id(int index) {
    char *id = malloc(16);
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
    module->absolute_path = strdup(absolute_path);
    module->module_id = generate_module_id(ctx->bundle->num_modules);
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

                // Add the underlying declaration
                add_flattened_stmt(bundle, decl);
            }
            // Skip export lists and re-exports
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
}
