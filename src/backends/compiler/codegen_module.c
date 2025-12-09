/*
 * Hemlock Code Generator - Module Compilation
 *
 * Handles module resolution, caching, and compilation for the import system.
 */

#include "codegen_internal.h"

// ========== MODULE COMPILATION ==========

// Find the stdlib directory path
static char* find_stdlib_path(void) {
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
        if (ret > 0 && ret < (int)sizeof(stdlib_path)) {
            if (access(stdlib_path, F_OK) == 0) {
                return realpath(stdlib_path, NULL);
            }
        }
    }

    // Last resort: use /usr/local/lib/hemlock/stdlib
    if (access("/usr/local/lib/hemlock/stdlib", F_OK) == 0) {
        return strdup("/usr/local/lib/hemlock/stdlib");
    }

    return NULL;
}

ModuleCache* module_cache_new(const char *main_file_path) {
    ModuleCache *cache = malloc(sizeof(ModuleCache));
    cache->modules = NULL;
    cache->module_counter = 0;

    // Get current working directory
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        cache->current_dir = strdup(cwd);
    } else {
        cache->current_dir = strdup(".");
    }

    // Get directory of main file
    if (main_file_path) {
        char *path_copy = strdup(main_file_path);
        char *dir = dirname(path_copy);
        cache->main_file_dir = realpath(dir, NULL);
        if (!cache->main_file_dir) {
            cache->main_file_dir = strdup(dir);
        }
        free(path_copy);
    } else {
        cache->main_file_dir = strdup(cache->current_dir);
    }

    // Find stdlib path
    cache->stdlib_path = find_stdlib_path();

    return cache;
}

void module_cache_free(ModuleCache *cache) {
    if (!cache) return;

    CompiledModule *mod = cache->modules;
    while (mod) {
        CompiledModule *next = mod->next;
        free(mod->absolute_path);
        free(mod->module_prefix);

        // Free statements
        for (int i = 0; i < mod->num_statements; i++) {
            stmt_free(mod->statements[i]);
        }
        free(mod->statements);

        // Free exports
        for (int i = 0; i < mod->num_exports; i++) {
            free(mod->exports[i].name);
            free(mod->exports[i].mangled_name);
        }
        free(mod->exports);

        // Free imports
        for (int i = 0; i < mod->num_imports; i++) {
            free(mod->imports[i].local_name);
            free(mod->imports[i].original_name);
            free(mod->imports[i].module_prefix);
        }
        free(mod->imports);

        free(mod);
        mod = next;
    }

    free(cache->current_dir);
    free(cache->main_file_dir);
    if (cache->stdlib_path) {
        free(cache->stdlib_path);
    }
    free(cache);
}

char* module_resolve_path(ModuleCache *cache, const char *importer_path, const char *import_path) {
    char resolved[PATH_MAX];

    // Check for @stdlib alias
    if (strncmp(import_path, "@stdlib/", 8) == 0) {
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
        strncpy(resolved, import_path, PATH_MAX);
        resolved[PATH_MAX - 1] = '\0';
    } else {
        // Relative path - resolve relative to importer's directory
        const char *base_dir;
        char importer_dir[PATH_MAX];

        if (importer_path) {
            strncpy(importer_dir, importer_path, PATH_MAX);
            importer_dir[PATH_MAX - 1] = '\0';
            char *dir = dirname(importer_dir);
            base_dir = dir;
        } else {
            base_dir = cache->main_file_dir;
        }

        snprintf(resolved, PATH_MAX, "%s/%s", base_dir, import_path);
    }

    // Add .hml extension if not present
    size_t len = strlen(resolved);
    if (len < 4 || strcmp(resolved + len - 4, ".hml") != 0) {
        if (len + 4 < PATH_MAX) {
            strcat(resolved, ".hml");
        }
    }

    // Resolve to absolute canonical path
    char *absolute = realpath(resolved, NULL);
    if (!absolute) {
        // File doesn't exist - return the resolved path anyway for error reporting
        return strdup(resolved);
    }

    return absolute;
}

CompiledModule* module_get_cached(ModuleCache *cache, const char *absolute_path) {
    CompiledModule *mod = cache->modules;
    while (mod) {
        if (strcmp(mod->absolute_path, absolute_path) == 0) {
            return mod;
        }
        mod = mod->next;
    }
    return NULL;
}

void module_add_export(CompiledModule *module, const char *name, const char *mangled_name, int is_function, int num_params) {
    if (module->num_exports >= module->export_capacity) {
        module->export_capacity = module->export_capacity == 0 ? 16 : module->export_capacity * 2;
        module->exports = realloc(module->exports, sizeof(ExportedSymbol) * module->export_capacity);
    }
    module->exports[module->num_exports].name = strdup(name);
    module->exports[module->num_exports].mangled_name = strdup(mangled_name);
    module->exports[module->num_exports].is_function = is_function;
    module->exports[module->num_exports].num_params = num_params;
    module->num_exports++;
}

ExportedSymbol* module_find_export(CompiledModule *module, const char *name) {
    for (int i = 0; i < module->num_exports; i++) {
        if (strcmp(module->exports[i].name, name) == 0) {
            return &module->exports[i];
        }
    }
    return NULL;
}

void module_add_import(CompiledModule *module, const char *local_name, const char *original_name, const char *module_prefix, int is_function, int num_params) {
    if (module->num_imports >= module->import_capacity) {
        module->import_capacity = module->import_capacity == 0 ? 16 : module->import_capacity * 2;
        module->imports = realloc(module->imports, sizeof(ImportBinding) * module->import_capacity);
    }
    module->imports[module->num_imports].local_name = strdup(local_name);
    module->imports[module->num_imports].original_name = strdup(original_name);
    module->imports[module->num_imports].module_prefix = strdup(module_prefix);
    module->imports[module->num_imports].is_function = is_function;
    module->imports[module->num_imports].num_params = num_params;
    module->num_imports++;
}

ImportBinding* module_find_import(CompiledModule *module, const char *name) {
    if (!module) return NULL;
    for (int i = 0; i < module->num_imports; i++) {
        if (strcmp(module->imports[i].local_name, name) == 0) {
            return &module->imports[i];
        }
    }
    return NULL;
}

// Check if a name is an extern function in the given module
int module_is_extern_fn(CompiledModule *module, const char *name) {
    if (!module || !module->statements) return 0;
    for (int i = 0; i < module->num_statements; i++) {
        if (module->statements[i]->type == STMT_EXTERN_FN) {
            if (strcmp(module->statements[i]->as.extern_fn.function_name, name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

char* module_gen_prefix(ModuleCache *cache) {
    char *prefix = malloc(32);
    snprintf(prefix, 32, "_mod%d_", cache->module_counter++);
    return prefix;
}

void codegen_set_module_cache(CodegenContext *ctx, ModuleCache *cache) {
    ctx->module_cache = cache;
}

// Parse a module file
Stmt** parse_module_file(const char *path, int *stmt_count) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open module file '%s'\n", path);
        *stmt_count = 0;
        return NULL;
    }

    // Read entire file
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

CompiledModule* module_compile(CodegenContext *ctx, const char *absolute_path) {
    ModuleCache *cache = ctx->module_cache;

    // Check if already compiled
    CompiledModule *cached = module_get_cached(cache, absolute_path);
    if (cached) {
        if (cached->state == MOD_LOADING) {
            fprintf(stderr, "Error: Circular dependency detected when compiling '%s'\n", absolute_path);
            return NULL;
        }
        return cached;
    }

    // Create new module
    CompiledModule *module = malloc(sizeof(CompiledModule));
    module->absolute_path = strdup(absolute_path);
    module->module_prefix = module_gen_prefix(cache);
    module->state = MOD_LOADING;
    module->exports = NULL;
    module->num_exports = 0;
    module->export_capacity = 0;
    module->imports = NULL;
    module->num_imports = 0;
    module->import_capacity = 0;

    // Add to cache (for cycle detection)
    module->next = cache->modules;
    cache->modules = module;

    // Parse the module
    module->statements = parse_module_file(absolute_path, &module->num_statements);
    if (!module->statements) {
        module->state = MOD_UNLOADED;
        return NULL;
    }

    // First pass: recursively compile imported modules and track import bindings
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_IMPORT) {
            char *import_path = stmt->as.import_stmt.module_path;
            char *resolved = module_resolve_path(cache, absolute_path, import_path);
            if (!resolved) {
                fprintf(stderr, "Error: Could not resolve import '%s' in '%s'\n", import_path, absolute_path);
                return NULL;
            }

            CompiledModule *imported = module_compile(ctx, resolved);
            free(resolved);

            if (!imported) {
                fprintf(stderr, "Error: Failed to compile imported module '%s'\n", import_path);
                return NULL;
            }

            // Track import bindings for this module
            if (stmt->as.import_stmt.is_namespace) {
                // Namespace imports don't need individual bindings - they're accessed via object
            } else {
                // Named imports: track each import binding
                for (int j = 0; j < stmt->as.import_stmt.num_imports; j++) {
                    const char *import_name = stmt->as.import_stmt.import_names[j];
                    const char *alias = stmt->as.import_stmt.import_aliases[j];
                    const char *bind_name = alias ? alias : import_name;

                    ExportedSymbol *exp = module_find_export(imported, import_name);
                    if (exp) {
                        // Track this as an import binding with original name, module prefix, and func info
                        module_add_import(module, bind_name, import_name, imported->module_prefix,
                                        exp->is_function, exp->num_params);
                    }
                }
            }
        }
    }

    // Second pass: collect exports
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_EXPORT) {
            if (stmt->as.export_stmt.is_declaration) {
                Stmt *decl = stmt->as.export_stmt.declaration;
                const char *name = NULL;
                int is_function = 0;
                int num_params = 0;
                if (decl->type == STMT_LET) {
                    name = decl->as.let.name;
                    // Check if value is a function expression
                    if (decl->as.let.value && decl->as.let.value->type == EXPR_FUNCTION) {
                        is_function = 1;
                        num_params = decl->as.let.value->as.function.num_params;
                    }
                } else if (decl->type == STMT_CONST) {
                    name = decl->as.const_stmt.name;
                    // Check if value is a function expression
                    if (decl->as.const_stmt.value && decl->as.const_stmt.value->type == EXPR_FUNCTION) {
                        is_function = 1;
                        num_params = decl->as.const_stmt.value->as.function.num_params;
                    }
                }
                if (name) {
                    char mangled[256];
                    snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
                    module_add_export(module, name, mangled, is_function, num_params);
                }
            } else if (!stmt->as.export_stmt.is_reexport) {
                // Export list - need to find the declaration to get function info
                for (int j = 0; j < stmt->as.export_stmt.num_exports; j++) {
                    const char *name = stmt->as.export_stmt.export_names[j];
                    const char *alias = stmt->as.export_stmt.export_aliases[j];
                    const char *export_name = alias ? alias : name;

                    // Look for the declaration to determine if it's a function
                    int is_function = 0;
                    int num_params = 0;
                    for (int k = 0; k < module->num_statements; k++) {
                        Stmt *s = module->statements[k];
                        if (s->type == STMT_LET && strcmp(s->as.let.name, name) == 0) {
                            if (s->as.let.value && s->as.let.value->type == EXPR_FUNCTION) {
                                is_function = 1;
                                num_params = s->as.let.value->as.function.num_params;
                            }
                            break;
                        } else if (s->type == STMT_CONST && strcmp(s->as.const_stmt.name, name) == 0) {
                            if (s->as.const_stmt.value && s->as.const_stmt.value->type == EXPR_FUNCTION) {
                                is_function = 1;
                                num_params = s->as.const_stmt.value->as.function.num_params;
                            }
                            break;
                        }
                    }

                    char mangled[256];
                    snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
                    module_add_export(module, export_name, mangled, is_function, num_params);
                }
            }
        }
    }

    // Third pass: add implicit exports for top-level functions without explicit exports
    // This allows modules to work without explicit export statements (like @stdlib modules)
    for (int i = 0; i < module->num_statements; i++) {
        Stmt *stmt = module->statements[i];
        if (stmt->type == STMT_LET && stmt->as.let.value &&
            stmt->as.let.value->type == EXPR_FUNCTION) {
            const char *name = stmt->as.let.name;
            // Check if already exported
            if (!module_find_export(module, name)) {
                char mangled[256];
                snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
                int num_params = stmt->as.let.value->as.function.num_params;
                module_add_export(module, name, mangled, 1, num_params);
            }
        } else if (stmt->type == STMT_CONST && stmt->as.const_stmt.value &&
                   stmt->as.const_stmt.value->type == EXPR_FUNCTION) {
            const char *name = stmt->as.const_stmt.name;
            // Check if already exported
            if (!module_find_export(module, name)) {
                char mangled[256];
                snprintf(mangled, sizeof(mangled), "%s%s", module->module_prefix, name);
                int num_params = stmt->as.const_stmt.value->as.function.num_params;
                module_add_export(module, name, mangled, 1, num_params);
            }
        }
    }

    module->state = MOD_LOADED;
    return module;
}
