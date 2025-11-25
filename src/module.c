#define _XOPEN_SOURCE 500
#include "module.h"
#include "parser.h"
#include "lexer.h"
#include "interpreter/internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

// ========== MODULE CACHE ==========

// Helper function to ensure export array has capacity
static void ensure_export_capacity(Module *module) {
    if (module->num_exports >= module->export_capacity) {
        module->export_capacity *= 2;
        module->export_names = realloc(module->export_names, sizeof(char*) * module->export_capacity);
        if (!module->export_names) {
            fprintf(stderr, "Runtime error: Memory allocation failed for module exports\n");
            exit(1);
        }
    }
}

// Find the stdlib directory path
static char* find_stdlib_path() {
    char exe_path[PATH_MAX];
    char resolved[PATH_MAX];

    // Try to get the executable path
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';

        // Get directory of executable
        char *dir = dirname(exe_path);

        // Try: executable_dir/stdlib
        snprintf(resolved, PATH_MAX, "%s/stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            return realpath(resolved, NULL);
        }

        // Try: executable_dir/../stdlib (for build directory structure)
        snprintf(resolved, PATH_MAX, "%s/../stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            return realpath(resolved, NULL);
        }
    }

    // Fallback: try current working directory + stdlib
    if (getcwd(resolved, sizeof(resolved))) {
        char stdlib_path[PATH_MAX];
        int ret = snprintf(stdlib_path, sizeof(stdlib_path), "%s/stdlib", resolved);
        // Check if snprintf succeeded without truncation
        if (ret > 0 && ret < (int)sizeof(stdlib_path)) {
            if (access(stdlib_path, F_OK) == 0) {
                return realpath(stdlib_path, NULL);
            }
        }
    }

    // Last resort: use /usr/local/lib/hemlock/stdlib (for installed version)
    if (access("/usr/local/lib/hemlock/stdlib", F_OK) == 0) {
        return strdup("/usr/local/lib/hemlock/stdlib");
    }

    // Not found - return NULL
    return NULL;
}

ModuleCache* module_cache_new(const char *initial_dir) {
    ModuleCache *cache = malloc(sizeof(ModuleCache));
    cache->modules = malloc(sizeof(Module*) * 32);
    cache->count = 0;
    cache->capacity = 32;
    cache->current_dir = strdup(initial_dir);
    cache->stdlib_path = find_stdlib_path();

    if (!cache->stdlib_path) {
        fprintf(stderr, "Warning: Could not locate stdlib directory. @stdlib imports will not work.\n");
    }

    return cache;
}

void module_cache_free(ModuleCache *cache) {
    if (!cache) return;

    for (int i = 0; i < cache->count; i++) {
        Module *mod = cache->modules[i];
        free(mod->absolute_path);

        // Free statements
        for (int j = 0; j < mod->num_statements; j++) {
            stmt_free(mod->statements[j]);
        }
        free(mod->statements);

        // Release exports environment
        if (mod->exports_env) {
            env_release(mod->exports_env);
        }

        // Free export names
        for (int j = 0; j < mod->num_exports; j++) {
            free(mod->export_names[j]);
        }
        free(mod->export_names);

        free(mod);
    }

    free(cache->modules);
    free(cache->current_dir);
    if (cache->stdlib_path) {
        free(cache->stdlib_path);
    }
    free(cache);
}

// ========== PATH RESOLUTION ==========

// Resolve relative or absolute path to absolute path
char* resolve_module_path(ModuleCache *cache, const char *importer_path, const char *import_path) {
    char resolved[PATH_MAX];

    // Check for @stdlib alias
    if (strncmp(import_path, "@stdlib/", 8) == 0) {
        // Handle @stdlib alias
        if (!cache->stdlib_path) {
            fprintf(stderr, "Error: @stdlib alias used but stdlib directory not found\n");
            return NULL;
        }

        // Replace @stdlib with actual stdlib path
        const char *module_subpath = import_path + 8;  // Skip "@stdlib/"
        snprintf(resolved, PATH_MAX, "%s/%s", cache->stdlib_path, module_subpath);
    }
    // If import_path is absolute, use it directly
    else if (import_path[0] == '/') {
        // Already absolute
        strncpy(resolved, import_path, PATH_MAX);
    } else {
        // Relative path - resolve relative to importer's directory
        const char *base_dir;
        char importer_dir[PATH_MAX];

        if (importer_path) {
            // Resolve relative to the importing file's directory
            strncpy(importer_dir, importer_path, PATH_MAX);
            char *dir = dirname(importer_dir);
            base_dir = dir;
        } else {
            // No importer - use current directory
            base_dir = cache->current_dir;
        }

        // Build the path
        snprintf(resolved, PATH_MAX, "%s/%s", base_dir, import_path);
    }

    // Add .hml extension if not present
    int len = strlen(resolved);
    if (len < 4 || strcmp(resolved + len - 4, ".hml") != 0) {
        strncat(resolved, ".hml", PATH_MAX - len - 1);
    }

    // Resolve to absolute canonical path
    char *absolute = realpath(resolved, NULL);
    if (!absolute) {
        // File doesn't exist - return the resolved path anyway for error reporting
        return strdup(resolved);
    }

    return absolute;
}

// ========== MODULE LOADING ==========

Module* get_cached_module(ModuleCache *cache, const char *absolute_path) {
    for (int i = 0; i < cache->count; i++) {
        if (strcmp(cache->modules[i]->absolute_path, absolute_path) == 0) {
            return cache->modules[i];
        }
    }
    return NULL;
}

// Parse a module file and return statements
Stmt** parse_module_file(const char *path, int *stmt_count, ExecutionContext *ctx) {
    (void)ctx;  // Suppress unused parameter warning
    // Read file
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open module file '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    // Read entire file into memory
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *source = malloc(file_size + 1);
    fread(source, 1, file_size, file);
    source[file_size] = '\0';
    fclose(file);

    // Parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Stmt **statements = parse_program(&parser, stmt_count);

    free(source);

    if (parser.had_error) {
        fprintf(stderr, "Error: Failed to parse module '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    return statements;
}

// Recursively load a module and its dependencies
Module* load_module(ModuleCache *cache, const char *module_path, ExecutionContext *ctx) {
    // Resolve to absolute path
    char *absolute_path = resolve_module_path(cache, NULL, module_path);

    // Check if already in cache
    Module *cached = get_cached_module(cache, absolute_path);
    if (cached) {
        if (cached->state == MODULE_LOADING) {
            // Circular dependency detected!
            fprintf(stderr, "Error: Circular dependency detected when loading '%s'\n", absolute_path);
            free(absolute_path);
            return NULL;
        }
        if (cached->state == MODULE_UNLOADED) {
            // Module failed to load previously (parse error or other failure)
            fprintf(stderr, "Error: Module '%s' failed to load previously\n", absolute_path);
            free(absolute_path);
            return NULL;
        }
        // Already loaded successfully
        free(absolute_path);
        return cached;
    }

    // Create new module
    Module *module = malloc(sizeof(Module));
    module->absolute_path = absolute_path;
    module->state = MODULE_LOADING;  // Mark as loading for cycle detection
    module->exports_env = NULL;
    module->export_capacity = 32;
    module->export_names = malloc(sizeof(char*) * module->export_capacity);
    module->num_exports = 0;

    // Add to cache immediately (for cycle detection)
    if (cache->count >= cache->capacity) {
        cache->capacity *= 2;
        cache->modules = realloc(cache->modules, sizeof(Module*) * cache->capacity);
    }
    cache->modules[cache->count++] = module;

    // Parse the module file
    module->statements = parse_module_file(absolute_path, &module->num_statements, ctx);
    if (!module->statements) {
        module->state = MODULE_UNLOADED;
        return NULL;
    }

    // Recursively load imported modules
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_IMPORT) {
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = resolve_module_path(cache, absolute_path, import_path);

            // Recursively load the imported module
            Module *imported = load_module(cache, resolved, ctx);
            free(resolved);

            if (!imported) {
                fprintf(stderr, "Error: Failed to load imported module '%s' from '%s'\n",
                        import_path, absolute_path);
                return NULL;
            }
        } else if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_reexport) {
            // Re-export: also need to load that module
            char *reexport_path = stmt->as.export_stmt.module_path;
            char *resolved = resolve_module_path(cache, absolute_path, reexport_path);

            Module *reexported = load_module(cache, resolved, ctx);
            free(resolved);

            if (!reexported) {
                fprintf(stderr, "Error: Failed to load re-exported module '%s' from '%s'\n",
                        reexport_path, absolute_path);
                return NULL;
            }
        }
    }

    module->state = MODULE_LOADED;
    return module;
}

// ========== MODULE EXECUTION ==========

// Execute a module in topological order (dependencies first)
void execute_module(Module *module, ModuleCache *cache, Environment *global_env, ExecutionContext *ctx) {
    if (module->exports_env) {
        // Already executed
        return;
    }

    // First, execute all imported modules
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_IMPORT) {
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = resolve_module_path(cache, module->absolute_path, import_path);
            Module *imported = get_cached_module(cache, resolved);
            free(resolved);

            if (imported) {
                execute_module(imported, cache, global_env, ctx);
            }
        } else if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_reexport) {
            char *reexport_path = stmt->as.export_stmt.module_path;
            char *resolved = resolve_module_path(cache, module->absolute_path, reexport_path);
            Module *reexported = get_cached_module(cache, resolved);
            free(resolved);

            if (reexported) {
                execute_module(reexported, cache, global_env, ctx);
            }
        }
    }

    // Create module's execution environment (with global_env as parent for builtins)
    Environment *module_env = env_new(global_env);

    // Execute module's statements (except import/export)
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];

        if (stmt->type == STMT_IMPORT) {
            // Handle import: bind imported values into this module's environment
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = resolve_module_path(cache, module->absolute_path, import_path);
            Module *imported = get_cached_module(cache, resolved);
            free(resolved);

            if (!imported || !imported->exports_env) {
                fprintf(stderr, "Error: Imported module '%s' not found or not executed\n", import_path);
                continue;
            }

            if (stmt->as.import_stmt.is_namespace) {
                // Namespace import: create an object with all exports
                Object *ns = object_new(NULL, imported->num_exports);
                for (int j = 0; j < imported->num_exports; j++) {
                    char *export_name = imported->export_names[j];
                    Value val = env_get(imported->exports_env, export_name, ctx);

                    // Add to namespace object
                    ns->field_names[ns->num_fields] = strdup(export_name);
                    ns->field_values[ns->num_fields] = val;
                    ns->num_fields++;
                }

                // Bind namespace to environment
                char *ns_name = stmt->as.import_stmt.namespace_name;
                env_define(module_env, ns_name, val_object(ns), 1, ctx);  // immutable
            } else {
                // Named imports
                for (int j = 0; j < stmt->as.import_stmt.num_imports; j++) {
                    char *import_name = stmt->as.import_stmt.import_names[j];
                    char *alias = stmt->as.import_stmt.import_aliases[j];
                    char *bind_name = alias ? alias : import_name;

                    Value val = env_get(imported->exports_env, import_name, ctx);
                    env_define(module_env, bind_name, val, 1, ctx);  // immutable
                    value_release(val);  // Release temp reference from env_get (env_define already retained)
                }
            }
        } else if (stmt->type == STMT_EXPORT) {
            // Handle export
            if (stmt->as.export_stmt.is_declaration) {
                // Export declaration: execute it
                Stmt *decl = stmt->as.export_stmt.declaration;
                eval_stmt(decl, module_env, ctx);

                // Extract the name and mark as exported
                if (decl->type == STMT_LET) {
                    ensure_export_capacity(module);
                    module->export_names[module->num_exports] = strdup(decl->as.let.name);
                    module->num_exports++;
                } else if (decl->type == STMT_CONST) {
                    ensure_export_capacity(module);
                    module->export_names[module->num_exports] = strdup(decl->as.const_stmt.name);
                    module->num_exports++;
                }
            } else if (stmt->as.export_stmt.is_reexport) {
                // Re-export: copy exports from another module
                char *reexport_path = stmt->as.export_stmt.module_path;
                char *resolved = resolve_module_path(cache, module->absolute_path, reexport_path);
                Module *reexported = get_cached_module(cache, resolved);
                free(resolved);

                if (!reexported || !reexported->exports_env) {
                    fprintf(stderr, "Error: Re-exported module '%s' not found\n", reexport_path);
                    continue;
                }

                // Copy each re-exported value
                for (int j = 0; j < stmt->as.export_stmt.num_exports; j++) {
                    char *export_name = stmt->as.export_stmt.export_names[j];
                    char *alias = stmt->as.export_stmt.export_aliases[j];
                    char *final_name = alias ? alias : export_name;

                    Value val = env_get(reexported->exports_env, export_name, ctx);
                    env_define(module_env, final_name, val, 1, ctx);
                    value_release(val);  // Release temp reference from env_get (env_define already retained)
                    ensure_export_capacity(module);
                    module->export_names[module->num_exports++] = strdup(final_name);
                }
            } else {
                // Export list: mark existing names as exported
                for (int j = 0; j < stmt->as.export_stmt.num_exports; j++) {
                    char *export_name = stmt->as.export_stmt.export_names[j];
                    char *alias = stmt->as.export_stmt.export_aliases[j];
                    char *final_name = alias ? alias : export_name;

                    ensure_export_capacity(module);
                    module->export_names[module->num_exports++] = strdup(final_name);
                }
            }
        } else {
            // Regular statement: execute it
            eval_stmt(stmt, module_env, ctx);
        }
    }

    // Save the module's environment as exports
    module->exports_env = module_env;
}

// ========== HIGH-LEVEL API ==========

// Execute a file using the module system
// Returns 0 on success, non-zero on error
int execute_file_with_modules(const char *file_path, Environment *global_env, int argc, char **argv, ExecutionContext *ctx) {
    // Get current working directory
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "Error: Could not get current directory\n");
        return 1;
    }

    // Create module cache
    ModuleCache *cache = module_cache_new(cwd);

    // Load the main module (and all its dependencies)
    Module *main_module = load_module(cache, file_path, ctx);
    if (!main_module) {
        fprintf(stderr, "Error: Failed to load module '%s'\n", file_path);
        module_cache_free(cache);
        return 1;
    }

    // Execute the main module (and all its dependencies in topological order)
    execute_module(main_module, cache, global_env, ctx);

    // Cleanup
    module_cache_free(cache);

    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;

    return 0;
}
