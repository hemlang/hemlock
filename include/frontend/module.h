#ifndef HEMLOCK_MODULE_H
#define HEMLOCK_MODULE_H

#include "frontend/ast.h"
#include "modules.h"

typedef struct ExecutionContext ExecutionContext;
typedef struct Environment Environment;

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

typedef struct ModuleCache {
    Module **modules;            // Array of modules
    int count;
    int capacity;
    ModuleResolution *resolver;  // Shared module resolution context
    ModuleCacheMap cache_map;    // Shared module cache
    // Entry-module source override (for `hemlock -e` code with imports):
    // when load_module() reaches entry_virtual_path it parses entry_source
    // instead of reading a file. Imports made by the entry resolve relative
    // to the virtual path's directory (the working directory).
    char *entry_virtual_path;    // owned; NULL when no override
    const char *entry_source;    // borrowed; NULL when no override
} ModuleCache;

// Public interface

// Create and destroy module cache
ModuleCache* module_cache_new(const char *initial_dir);
void module_cache_free(ModuleCache *cache);

// Module loading and resolution
Module* load_module(ModuleCache *cache, const char *module_path, ExecutionContext *ctx);
Module* get_cached_module(ModuleCache *cache, const char *absolute_path);

// Module execution
void execute_module(Module *module, ModuleCache *cache, Environment *global_env, ExecutionContext *ctx);

// High-level API
int execute_file_with_modules(const char *file_path, Environment *global_env, int argc, char **argv, ExecutionContext *ctx);

// Execute a source string (e.g. `hemlock -e '<code>'`) through the module
// system so its import statements work. Relative imports resolve against the
// current working directory.
int execute_source_with_modules(const char *source, Environment *global_env, int argc, char **argv, ExecutionContext *ctx);

#endif // HEMLOCK_MODULE_H
