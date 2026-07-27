#define _XOPEN_SOURCE 500
#include "frontend/module.h"
#include "frontend/parser.h"
#include "frontend/lexer.h"
#include "interpreter/internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

// ========== MODULE CACHE ==========

// Helper function to ensure export array has capacity
static void ensure_export_capacity(Module *module) {
    if (module->num_exports >= module->export_capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (module->export_capacity > INT_MAX / 2) {
            fprintf(stderr, "error: module export capacity overflow\n");
            exit(1);
        }
        module->export_capacity *= 2;
        char **new_names = realloc(module->export_names, sizeof(char*) * module->export_capacity);
        if (!new_names) {
            fprintf(stderr, "error: memory allocation failed for module exports\n");
            exit(1);
        }
        module->export_names = new_names;
    }
}

ModuleCache* module_cache_new(const char *initial_dir) {
    ModuleCache *cache = malloc(sizeof(ModuleCache));
    cache->modules = malloc(sizeof(Module*) * 32);
    cache->count = 0;
    cache->capacity = 32;
    cache->resolver = module_resolution_new(initial_dir, NULL);
    if (!cache->resolver) {
        fprintf(stderr, "error: failed to initialize module resolution\n");
        free(cache->modules);
        free(cache);
        exit(1);
    }
    module_cache_map_init(&cache->cache_map);
    cache->entry_virtual_path = NULL;
    cache->entry_source = NULL;

    if (!cache->resolver->stdlib_path) {
        fprintf(stderr, "warning: could not locate stdlib directory. @stdlib imports will not work.\n");
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
    module_cache_map_free(&cache->cache_map);
    module_resolution_free(cache->resolver);
    free(cache->entry_virtual_path);

    free(cache);
}

// ========== MODULE LOADING ==========

static Stmt** parse_module_file(const char *path, int *stmt_count, ExecutionContext *ctx) {
    (void)ctx;
    return parse_file_to_ast(path, stmt_count);
}

// Parse the entry-module source override (`hemlock -e` code). Mirrors
// parse_file_to_ast, but from an in-memory string.
static Stmt** parse_entry_source(const char *source, int *stmt_count) {
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    Stmt **statements = parse_program(&parser, stmt_count);

    if (parser.had_error) {
        fprintf(stderr, "error: failed to parse command-line code\n");
        *stmt_count = 0;
        return NULL;
    }

    return statements;
}

Module* get_cached_module(ModuleCache *cache, const char *absolute_path) {
    return (Module *)module_cache_map_get(&cache->cache_map, absolute_path);
}

// Recursively load a module and its dependencies
Module* load_module(ModuleCache *cache, const char *module_path, ExecutionContext *ctx) {
    // Resolve to absolute path
    char *absolute_path = resolve_module_path(cache->resolver, NULL, module_path);

    // Check if already in cache
    Module *cached = get_cached_module(cache, absolute_path);
    if (cached) {
        if (cached->state == MODULE_LOADING) {
            // Circular dependency detected!
            fprintf(stderr, "error: circular dependency detected when loading '%s'\n", absolute_path);
            free(absolute_path);
            return NULL;
        }
        if (cached->state == MODULE_UNLOADED) {
            // Module failed to load previously (parse error or other failure)
            fprintf(stderr, "error: module '%s' failed to load previously\n", absolute_path);
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
        // SECURITY: Check for integer overflow before doubling capacity
        if (cache->capacity > INT_MAX / 2) {
            fprintf(stderr, "error: module cache capacity overflow\n");
            free(module->export_names);
            free(absolute_path);
            free(module);
            return NULL;
        }
        cache->capacity *= 2;
        Module **new_modules = realloc(cache->modules, sizeof(Module*) * cache->capacity);
        if (!new_modules) {
            fprintf(stderr, "error: memory allocation failed for module cache\n");
            free(module->export_names);
            free(absolute_path);
            free(module);
            return NULL;
        }
        cache->modules = new_modules;
    }

    // Add to array and hash table for O(1) lookup
    cache->modules[cache->count++] = module;
    if (module_cache_map_set(&cache->cache_map, absolute_path, module) != 0) {
        fprintf(stderr, "error: failed to cache module '%s'\n", absolute_path);
        cache->count--;
        free(module->export_names);
        free(module->absolute_path);
        free(module);
        return NULL;
    }

    // Parse the module file (or the in-memory entry source for `hemlock -e`)
    if (cache->entry_source && cache->entry_virtual_path &&
        strcmp(absolute_path, cache->entry_virtual_path) == 0) {
        module->statements = parse_entry_source(cache->entry_source, &module->num_statements);
    } else {
        module->statements = parse_module_file(absolute_path, &module->num_statements, ctx);
    }
    if (!module->statements) {
        module->state = MODULE_UNLOADED;
        return NULL;
    }

    // Recursively load imported modules
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_IMPORT) {
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = resolve_module_path(cache->resolver, absolute_path, import_path);

            // Recursively load the imported module
            Module *imported = load_module(cache, resolved, ctx);
            free(resolved);

            if (!imported) {
                fprintf(stderr, "error: failed to load imported module '%s' from '%s'\n",
                        import_path, absolute_path);
                return NULL;
            }
        } else if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_reexport) {
            // Re-export: also need to load that module
            char *reexport_path = stmt->as.export_stmt.module_path;
            char *resolved = resolve_module_path(cache->resolver, absolute_path, reexport_path);

            Module *reexported = load_module(cache, resolved, ctx);
            free(resolved);

            if (!reexported) {
                fprintf(stderr, "error: failed to load re-exported module '%s' from '%s'\n",
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

    // Set current source file for stack traces
    const char *previous_file = get_current_source_file();
    char *saved_file = previous_file ? strdup(previous_file) : NULL;
    set_current_source_file(module->absolute_path);

    // First, execute all imported modules
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_IMPORT) {
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = resolve_module_path(cache->resolver, module->absolute_path, import_path);
            Module *imported = get_cached_module(cache, resolved);
            free(resolved);

            if (imported) {
                execute_module(imported, cache, global_env, ctx);
            }
        } else if (stmt->type == STMT_EXPORT && stmt->as.export_stmt.is_reexport) {
            char *reexport_path = stmt->as.export_stmt.module_path;
            char *resolved = resolve_module_path(cache->resolver, module->absolute_path, reexport_path);
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
            char *resolved = resolve_module_path(cache->resolver, module->absolute_path, import_path);
            Module *imported = get_cached_module(cache, resolved);
            free(resolved);

            if (!imported || !imported->exports_env) {
                fprintf(stderr, "Error: Imported module '%s' not found or not executed\n", import_path);
                continue;
            }

            if (stmt->as.import_stmt.is_namespace) {
                char *ns_name = stmt->as.import_stmt.namespace_name;
                if (ns_name != NULL) {
                    // Namespace import: create an object with all exports
                    Object *ns = object_new(NULL, imported->num_exports);
                    for (int j = 0; j < imported->num_exports; j++) {
                        char *export_name = imported->export_names[j];
                        Value val = env_get(imported->exports_env, export_name, ctx);

                        // Add to namespace object
                        ns->fields[ns->num_fields].name = strdup(export_name);
                        ns->fields[ns->num_fields].value = val;
                        ns->num_fields++;
                    }

                    // Bind namespace to environment
                    Value ns_val = val_object(ns);
                    env_define(module_env, ns_name, ns_val, 1, ctx);  // immutable
                    value_release(ns_val);  // Release temp reference (env_define retained it)
                } else {
                    // Star import: import * from "module" - import all exports directly
                    // NOTE: matches the compiler's current star-import semantics, which
                    // snapshots values at import time. Named imports use live bindings
                    // (see env_define_import). Aligning star imports to live bindings
                    // would require a corresponding compiler change.
                    for (int j = 0; j < imported->num_exports; j++) {
                        char *export_name = imported->export_names[j];
                        Value val = env_get(imported->exports_env, export_name, ctx);
                        env_define(module_env, export_name, val, 1, ctx);  // immutable
                        value_release(val);  // Release temp reference from env_get
                    }
                }
            } else {
                // Named imports - install live aliases (see env_define_import).
                // Reassignments to a `let` in the exporting module are reflected
                // here on next read, matching the compiler's module-global
                // semantics and standard ES module live-binding behavior.
                for (int j = 0; j < stmt->as.import_stmt.num_imports; j++) {
                    char *import_name = stmt->as.import_stmt.import_names[j];
                    char *alias = stmt->as.import_stmt.import_aliases[j];
                    char *bind_name = alias ? alias : import_name;

                    env_define_import(module_env, bind_name,
                                      imported->exports_env, import_name, ctx);
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
                } else if (decl->type == STMT_EXTERN_FN) {
                    ensure_export_capacity(module);
                    module->export_names[module->num_exports] = strdup(decl->as.extern_fn.function_name);
                    module->num_exports++;
                }
                // Note: STMT_DEFINE_OBJECT doesn't add to export_names since define types
                // are registered globally when the module loads and don't have environment values.
                // The type will be available to all code that imports this module.
            } else if (stmt->as.export_stmt.is_reexport) {
                // Re-export: copy exports from another module
                char *reexport_path = stmt->as.export_stmt.module_path;
                char *resolved = resolve_module_path(cache->resolver, module->absolute_path, reexport_path);
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

        // Check for uncaught exception after each statement
        if (ctx->exception_state.is_throwing) {
            // Convert exception value to string for stderr output
            char *error_msg = value_to_string(ctx->exception_state.exception_value);
            fprintf(stderr, "Uncaught exception: %s\n", error_msg);
            free(error_msg);
            // Release exception value before exiting
            VALUE_RELEASE(ctx->exception_state.exception_value);
            // Restore previous source file before exit
            set_current_source_file(saved_file);
            if (saved_file) {
                free(saved_file);
            }
            exit(1);
        }
    }

    // Save the module's environment as exports
    module->exports_env = module_env;

    // Restore previous source file
    set_current_source_file(saved_file);
    if (saved_file) {
        free(saved_file);
    }
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

int execute_source_with_modules(const char *source, Environment *global_env, int argc, char **argv, ExecutionContext *ctx) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "Error: Could not get current directory\n");
        return 1;
    }

    ModuleCache *cache = module_cache_new(cwd);

    // A virtual path inside the working directory: relative imports in the
    // command-line code resolve against cwd, and the name cannot collide
    // with a real file. It carries the .hml extension so resolution keeps it
    // verbatim (resolve_module_path appends .hml otherwise, and its realpath
    // fallback passes nonexistent absolute paths through unchanged), so this
    // flows through load_module like any other entry file.
    char virtual_path[PATH_MAX + 32];
    snprintf(virtual_path, sizeof(virtual_path), "%s/<command-line>.hml", cwd);
    cache->entry_virtual_path = strdup(virtual_path);
    cache->entry_source = source;

    Module *main_module = load_module(cache, virtual_path, ctx);
    if (!main_module) {
        fprintf(stderr, "Error: Failed to load command-line code\n");
        module_cache_free(cache);
        return 1;
    }

    execute_module(main_module, cache, global_env, ctx);

    module_cache_free(cache);

    (void)argc;
    (void)argv;

    return 0;
}
