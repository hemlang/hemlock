#ifndef HEMLOCK_MODULE_H
#define HEMLOCK_MODULE_H

#include "ast.h"
#include "interpreter.h"

// Forward declarations
typedef struct Module Module;
typedef struct ModuleCache ModuleCache;

// Module states
typedef enum {
    MODULE_UNLOADED,     // Not yet parsed
    MODULE_LOADING,      // Currently being loaded (for cycle detection)
    MODULE_LOADED,       // Parsed and executed
} ModuleState;

// Module structure
typedef struct Module {
    char *absolute_path;         // Resolved absolute path (cache key)
    ModuleState state;           // Current state
    Stmt **statements;           // Parsed AST
    int num_statements;
    Environment *exports_env;    // Environment containing exported values
    char **export_names;         // List of exported names
    int num_exports;
    int export_capacity;         // Capacity of export_names array
} Module;

// Module cache structure
typedef struct ModuleCache {
    Module **modules;            // Array of modules
    int count;
    int capacity;
    char *current_dir;           // Current working directory for relative imports
    char *stdlib_path;           // Path to standard library directory
} ModuleCache;

// Public interface

// Create and destroy module cache
ModuleCache* module_cache_new(const char *initial_dir);
void module_cache_free(ModuleCache *cache);

// Module loading and resolution
char* resolve_module_path(ModuleCache *cache, const char *importer_path, const char *import_path);
Module* load_module(ModuleCache *cache, const char *module_path, ExecutionContext *ctx);
Module* get_cached_module(ModuleCache *cache, const char *absolute_path);

// Module execution
void execute_module(Module *module, ModuleCache *cache, Environment *global_env, ExecutionContext *ctx);

// High-level API
int execute_file_with_modules(const char *file_path, Environment *global_env, int argc, char **argv, ExecutionContext *ctx);

#endif // HEMLOCK_MODULE_H
