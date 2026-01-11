#ifndef HEMLOCK_MODULES_H
#define HEMLOCK_MODULES_H

#include <stddef.h>

// ========== SHARED MODULE CACHE ==========

typedef struct ModuleCacheEntry {
    char *path;       // Absolute path key
    void *module;     // Opaque module pointer
} ModuleCacheEntry;

typedef struct ModuleCacheMap {
    ModuleCacheEntry *entries;
    int size;
    int count;
} ModuleCacheMap;

void module_cache_map_init(ModuleCacheMap *map);
void module_cache_map_free(ModuleCacheMap *map);
void* module_cache_map_get(ModuleCacheMap *map, const char *path);
int module_cache_map_set(ModuleCacheMap *map, const char *path, void *module);

// ========== MODULE RESOLUTION ==========

typedef void (*ModuleDependencyHook)(void *user_data, const char *importer_path, const char *resolved_path);

typedef struct ModuleResolution {
    char *current_dir;              // Current working directory
    char *stdlib_path;              // Resolved stdlib path (if found)
    char *main_file_dir;            // Directory for fallback resolution
    ModuleDependencyHook dependency_hook;
    void *dependency_hook_data;
} ModuleResolution;

ModuleResolution* module_resolution_new(const char *current_dir, const char *main_file_path);
void module_resolution_free(ModuleResolution *resolver);
void module_resolution_set_dependency_hook(ModuleResolution *resolver, ModuleDependencyHook hook, void *data);
char* resolve_module_path(ModuleResolution *resolver, const char *importer_path, const char *import_path);

// ========== MODULE DEPENDENCY GRAPH (BUNDLER HOOK) ==========

typedef struct ModuleDependency {
    char *importer_path;
    char *resolved_path;
} ModuleDependency;

typedef struct ModuleDependencyGraph {
    ModuleDependency *edges;
    int count;
    int capacity;
} ModuleDependencyGraph;

ModuleDependencyGraph* module_dependency_graph_new(void);
void module_dependency_graph_free(ModuleDependencyGraph *graph);
void module_dependency_graph_add(ModuleDependencyGraph *graph, const char *importer_path, const char *resolved_path);
void module_dependency_graph_hook(void *user_data, const char *importer_path, const char *resolved_path);

#endif // HEMLOCK_MODULES_H
