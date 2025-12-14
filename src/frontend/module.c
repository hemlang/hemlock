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

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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

// Validate that resolved path stays within base directory
// Returns 1 if path is contained within base, 0 otherwise
static int path_is_within_base(const char *resolved_path, const char *base_path) {
    char base_real[PATH_MAX];

    // Get canonical paths
    if (!realpath(base_path, base_real)) {
        return 0;
    }

    // For resolved_path, we need to handle non-existent files
    // Try to resolve the directory part
    char resolved_copy[PATH_MAX];
    strncpy(resolved_copy, resolved_path, PATH_MAX - 1);
    resolved_copy[PATH_MAX - 1] = '\0';

    // Get the directory part
    char *dir = dirname(resolved_copy);
    char dir_real[PATH_MAX];

    if (!realpath(dir, dir_real)) {
        // If directory doesn't exist, this is already suspicious
        return 0;
    }

    // Check that dir_real starts with base_real
    size_t base_len = strlen(base_real);
    if (strncmp(dir_real, base_real, base_len) != 0) {
        return 0;
    }

    // Ensure it's not a prefix match (e.g., /foo/bar vs /foo/barbaz)
    if (dir_real[base_len] != '\0' && dir_real[base_len] != '/') {
        return 0;
    }

    return 1;
}

// ========== MODULE CACHE ==========

// Helper function to ensure export array has capacity
static void ensure_export_capacity(Module *module) {
    if (module->num_exports >= module->export_capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (module->export_capacity > INT_MAX / 2) {
            fprintf(stderr, "Runtime error: Module export capacity overflow\n");
            exit(1);
        }
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
    int found_exe = 0;

#ifdef __APPLE__
    // macOS: use _NSGetExecutablePath
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        // Resolve any symlinks
        char *real = realpath(exe_path, NULL);
        if (real) {
            strncpy(exe_path, real, PATH_MAX - 1);
            exe_path[PATH_MAX - 1] = '\0';
            free(real);
            found_exe = 1;
        }
    }
#else
    // Linux: use /proc/self/exe
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        found_exe = 1;
    }
#endif

    if (found_exe) {
        // Make a copy because dirname may modify the string
        char *exe_copy = strdup(exe_path);
        char *dir = dirname(exe_copy);

        // Try: executable_dir/stdlib
        snprintf(resolved, PATH_MAX, "%s/stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            free(exe_copy);
            return realpath(resolved, NULL);
        }

        // Try: executable_dir/../stdlib (for build directory structure)
        snprintf(resolved, PATH_MAX, "%s/../stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            free(exe_copy);
            return realpath(resolved, NULL);
        }
        free(exe_copy);
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

// Helper: Find hem_modules directory by walking up from a path
static char* find_hem_modules(const char *start_path) {
    char search_path[PATH_MAX];
    char hem_modules_path[PATH_MAX];

    // Make a copy of start_path
    strncpy(search_path, start_path, PATH_MAX - 1);
    search_path[PATH_MAX - 1] = '\0';

    // Walk up the directory tree looking for hem_modules
    while (1) {
        int ret = snprintf(hem_modules_path, PATH_MAX, "%s/hem_modules", search_path);
        // Check if snprintf succeeded without truncation
        if (ret > 0 && ret < PATH_MAX && access(hem_modules_path, F_OK) == 0) {
            return strdup(hem_modules_path);
        }

        // Go up one directory
        char *parent = dirname(search_path);
        if (strcmp(parent, search_path) == 0 || strcmp(parent, "/") == 0) {
            // Reached root, not found
            break;
        }
        strncpy(search_path, parent, PATH_MAX - 1);
    }

    return NULL;
}

// Helper: Check if import path looks like a package (owner/repo format)
static int is_package_import(const char *import_path) {
    // Must not start with ./ or ../ or /
    if (import_path[0] == '.' || import_path[0] == '/') {
        return 0;
    }

    // Must contain exactly at least one slash (owner/repo)
    const char *slash = strchr(import_path, '/');
    if (!slash || slash == import_path) {
        return 0;
    }

    return 1;
}

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

        // SECURITY: Validate subpath doesn't contain directory traversal
        if (!is_safe_subpath(module_subpath)) {
            fprintf(stderr, "Error: Invalid module path '%s' - directory traversal not allowed\n", import_path);
            return NULL;
        }

        snprintf(resolved, PATH_MAX, "%s/%s", cache->stdlib_path, module_subpath);

        // SECURITY: Double-check resolved path stays within stdlib directory
        if (!path_is_within_base(resolved, cache->stdlib_path)) {
            fprintf(stderr, "Error: Module path '%s' resolves outside stdlib directory\n", import_path);
            return NULL;
        }
    }
    // If import_path is absolute, use it directly
    else if (import_path[0] == '/') {
        // Already absolute
        strncpy(resolved, import_path, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
    }
    // Check for package import (owner/repo or owner/repo/subpath)
    else if (is_package_import(import_path)) {
        // Try to find hem_modules directory
        const char *search_from = importer_path ? importer_path : cache->current_dir;

        // Make a copy for dirname since it may modify the string
        char search_dir[PATH_MAX];
        strncpy(search_dir, search_from, PATH_MAX - 1);
        search_dir[PATH_MAX - 1] = '\0';

        // If search_from is a file, get its directory
        if (importer_path) {
            dirname(search_dir);
        }

        char *hem_modules = find_hem_modules(search_dir);

        if (hem_modules) {
            // Parse owner/repo from import path
            char owner[256], repo[256];
            const char *subpath = NULL;

            // Find first slash (after owner)
            const char *first_slash = strchr(import_path, '/');
            if (first_slash) {
                size_t owner_len = first_slash - import_path;
                if (owner_len >= sizeof(owner)) owner_len = sizeof(owner) - 1;
                strncpy(owner, import_path, owner_len);
                owner[owner_len] = '\0';

                // Find second slash (after repo) if exists
                const char *second_slash = strchr(first_slash + 1, '/');
                if (second_slash) {
                    size_t repo_len = second_slash - first_slash - 1;
                    if (repo_len >= sizeof(repo)) repo_len = sizeof(repo) - 1;
                    strncpy(repo, first_slash + 1, repo_len);
                    repo[repo_len] = '\0';
                    subpath = second_slash + 1;
                } else {
                    strncpy(repo, first_slash + 1, sizeof(repo) - 1);
                    repo[sizeof(repo) - 1] = '\0';
                }
            }

            // SECURITY: Validate owner and repo names don't contain traversal
            if (!is_safe_subpath(owner) || !is_safe_subpath(repo)) {
                fprintf(stderr, "Error: Invalid package name - directory traversal not allowed\n");
                free(hem_modules);
                return NULL;
            }

            // SECURITY: Validate subpath if present
            if (subpath && !is_safe_subpath(subpath)) {
                fprintf(stderr, "Error: Invalid package subpath '%s' - directory traversal not allowed\n", subpath);
                free(hem_modules);
                return NULL;
            }

            // Try different resolution patterns per spec:
            // 1. hem_modules/owner/repo/path.hml
            // 2. hem_modules/owner/repo/path/index.hml
            // 3. hem_modules/owner/repo/src/path.hml
            // 4. hem_modules/owner/repo/src/path/index.hml
            // For root imports (no subpath):
            // - Read main from package.json, default to src/index.hml

            char try_path[PATH_MAX];

            if (subpath) {
                // Has subpath - try direct file
                snprintf(try_path, PATH_MAX, "%s/%s/%s/%s.hml", hem_modules, owner, repo, subpath);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }

                // Try as directory with index.hml
                snprintf(try_path, PATH_MAX, "%s/%s/%s/%s/index.hml", hem_modules, owner, repo, subpath);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }

                // Try in src/ directory
                snprintf(try_path, PATH_MAX, "%s/%s/%s/src/%s.hml", hem_modules, owner, repo, subpath);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }

                // Try in src/ as directory
                snprintf(try_path, PATH_MAX, "%s/%s/%s/src/%s/index.hml", hem_modules, owner, repo, subpath);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }
            } else {
                // No subpath - import root of package
                // Try to read main from package.json
                char pkg_json_path[PATH_MAX];
                snprintf(pkg_json_path, PATH_MAX, "%s/%s/%s/package.json", hem_modules, owner, repo);

                char main_file[256] = "src/index.hml";  // Default
                FILE *pkg_file = fopen(pkg_json_path, "r");
                if (pkg_file) {
                    // Simple JSON parsing to find "main" field
                    char line[1024];
                    while (fgets(line, sizeof(line), pkg_file)) {
                        char *main_pos = strstr(line, "\"main\"");
                        if (main_pos) {
                            char *colon = strchr(main_pos, ':');
                            if (colon) {
                                char *quote1 = strchr(colon, '"');
                                if (quote1) {
                                    char *quote2 = strchr(quote1 + 1, '"');
                                    if (quote2) {
                                        size_t len = quote2 - quote1 - 1;
                                        if (len < sizeof(main_file)) {
                                            strncpy(main_file, quote1 + 1, len);
                                            main_file[len] = '\0';
                                        }
                                    }
                                }
                            }
                            break;
                        }
                    }
                    fclose(pkg_file);
                }

                // SECURITY: Validate main_file from package.json doesn't contain traversal
                if (!is_safe_subpath(main_file)) {
                    fprintf(stderr, "Error: Invalid 'main' field in package.json - directory traversal not allowed\n");
                    free(hem_modules);
                    return NULL;
                }

                // Build path to main file
                snprintf(try_path, PATH_MAX, "%s/%s/%s/%s", hem_modules, owner, repo, main_file);

                // Add .hml if not present
                int path_len = strlen(try_path);
                if (path_len < 4 || strcmp(try_path + path_len - 4, ".hml") != 0) {
                    strncat(try_path, ".hml", PATH_MAX - path_len - 1);
                }

                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }

                // Fallback: try src/index.hml
                snprintf(try_path, PATH_MAX, "%s/%s/%s/src/index.hml", hem_modules, owner, repo);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }
            }

            // If nothing found in hem_modules, fall through to relative path resolution
            // (package might not be installed yet)
            free(hem_modules);
        }

        // Fall back to relative path resolution for uninstalled packages
        // This will result in a "file not found" error, which is appropriate
        snprintf(resolved, PATH_MAX, "%s/%s", cache->current_dir, import_path);
    } else {
        // Relative path - resolve relative to importer's directory
        const char *base_dir;
        char importer_dir[PATH_MAX];

        if (importer_path) {
            // Resolve relative to the importing file's directory
            strncpy(importer_dir, importer_path, PATH_MAX - 1);
            importer_dir[PATH_MAX - 1] = '\0';
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
    if (fread(source, 1, file_size, file) != (size_t)file_size) {
        fprintf(stderr, "Error: Failed to read module file: %s\n", path);
        free(source);
        fclose(file);
        return NULL;
    }
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
        // SECURITY: Check for integer overflow before doubling capacity
        if (cache->capacity > INT_MAX / 2) {
            fprintf(stderr, "Error: Module cache capacity overflow\n");
            free(module->export_names);
            free(absolute_path);
            free(module);
            return NULL;
        }
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

    // Set current source file for stack traces
    const char *previous_file = get_current_source_file();
    char *saved_file = previous_file ? strdup(previous_file) : NULL;
    set_current_source_file(module->absolute_path);

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
