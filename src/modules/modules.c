#define _XOPEN_SOURCE 500
#include "modules.h"
#include "hemlock_compat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "shared/hash.h"

// ========== SHARED CACHE MAP ==========

#define MODULE_CACHE_INITIAL_SIZE 64

#define module_path_hash(s) ((unsigned long)hml_hash_string(s))

void module_cache_map_init(ModuleCacheMap *map) {
    map->size = MODULE_CACHE_INITIAL_SIZE;
    map->count = 0;
    map->entries = calloc(map->size, sizeof(ModuleCacheEntry));
}

static void module_cache_map_resize(ModuleCacheMap *map) {
    int old_size = map->size;
    ModuleCacheEntry *old_entries = map->entries;

    map->size *= 2;
    map->entries = calloc(map->size, sizeof(ModuleCacheEntry));
    map->count = 0;

    for (int i = 0; i < old_size; i++) {
        if (old_entries[i].path) {
            unsigned long hash = module_path_hash(old_entries[i].path);
            int index = hash % map->size;
            while (map->entries[index].path) {
                index = (index + 1) % map->size;
            }
            map->entries[index].path = old_entries[i].path;
            map->entries[index].module = old_entries[i].module;
            map->count++;
        }
    }

    free(old_entries);
}

void* module_cache_map_get(ModuleCacheMap *map, const char *path) {
    if (!map || !map->entries || !path) {
        return NULL;
    }
    unsigned long hash = module_path_hash(path);
    int index = hash % map->size;
    int start = index;

    while (map->entries[index].path) {
        if (strcmp(map->entries[index].path, path) == 0) {
            return map->entries[index].module;
        }
        index = (index + 1) % map->size;
        if (index == start) {
            break;
        }
    }
    return NULL;
}

int module_cache_map_set(ModuleCacheMap *map, const char *path, void *module) {
    if (!map || !path) {
        return -1;
    }

    if (map->count * 10 > map->size * 7) {
        module_cache_map_resize(map);
    }

    unsigned long hash = module_path_hash(path);
    int index = hash % map->size;

    while (map->entries[index].path) {
        if (strcmp(map->entries[index].path, path) == 0) {
            map->entries[index].module = module;
            return 0;
        }
        index = (index + 1) % map->size;
    }

    map->entries[index].path = strdup(path);
    if (!map->entries[index].path) {
        return -1;
    }
    map->entries[index].module = module;
    map->count++;
    return 0;
}

void module_cache_map_free(ModuleCacheMap *map) {
    if (!map || !map->entries) {
        return;
    }
    for (int i = 0; i < map->size; i++) {
        free(map->entries[i].path);
    }
    free(map->entries);
    map->entries = NULL;
    map->size = 0;
    map->count = 0;
}

// ========== PATH SECURITY ==========

// Absolute-path test that understands Windows paths (C:/, C:\, \\server)
// in addition to POSIX ones
static int path_is_absolute(const char *path) {
    if (path[0] == '/') return 1;
#ifdef _WIN32
    if (path[0] == '\\') return 1;
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) {
        return 1;
    }
#endif
    return 0;
}

static int is_safe_subpath(const char *path) {
    if (path == NULL) return 0;
    if (path[0] == '\0' || path_is_absolute(path)) return 0;

    const char *p = path;
    while (*p) {
        if ((p == path || *(p - 1) == '/') && p[0] == '.' && p[1] == '.') {
            if (p[2] == '\0' || p[2] == '/') {
                return 0;
            }
        }
        p++;
    }

    return 1;
}

static int path_is_within_base(const char *resolved_path, const char *base_path) {
    char base_real[PATH_MAX];

    if (!realpath(base_path, base_real)) {
        return 0;
    }

    char resolved_copy[PATH_MAX];
    strncpy(resolved_copy, resolved_path, PATH_MAX - 1);
    resolved_copy[PATH_MAX - 1] = '\0';

    char *dir = dirname(resolved_copy);
    char dir_real[PATH_MAX];

    if (!realpath(dir, dir_real)) {
        return 0;
    }

    size_t base_len = strlen(base_real);
    if (strncmp(dir_real, base_real, base_len) != 0) {
        return 0;
    }

    if (dir_real[base_len] != '\0' && dir_real[base_len] != '/') {
        return 0;
    }

    return 1;
}

// ========== PATH RESOLUTION HELPERS ==========

static char* find_stdlib_path(void) {
    char exe_path[PATH_MAX];
    char resolved[PATH_MAX];
    int found_exe = 0;

#ifdef __APPLE__
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        char *real = realpath(exe_path, NULL);
        if (real) {
            strncpy(exe_path, real, PATH_MAX - 1);
            exe_path[PATH_MAX - 1] = '\0';
            free(real);
            found_exe = 1;
        }
    }
#elif defined(_WIN32)
    if (hml_get_executable_path(exe_path, sizeof(exe_path))) {
        found_exe = 1;
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        found_exe = 1;
    }
#endif

    if (found_exe) {
        char *exe_copy = strdup(exe_path);
        char *dir = dirname(exe_copy);

        snprintf(resolved, PATH_MAX, "%s/stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            free(exe_copy);
            return realpath(resolved, NULL);
        }

        snprintf(resolved, PATH_MAX, "%s/../stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            free(exe_copy);
            return realpath(resolved, NULL);
        }

        // install.sh layout: <prefix>/bin/hemlockc + <prefix>/lib/hemlock/stdlib
        snprintf(resolved, PATH_MAX, "%s/../lib/hemlock/stdlib", dir);
        if (access(resolved, F_OK) == 0) {
            free(exe_copy);
            return realpath(resolved, NULL);
        }
        free(exe_copy);
    }

    if (getcwd(resolved, sizeof(resolved))) {
        char stdlib_path[PATH_MAX];
        int ret = snprintf(stdlib_path, sizeof(stdlib_path), "%s/stdlib", resolved);
        if (ret > 0 && ret < (int)sizeof(stdlib_path)) {
            if (access(stdlib_path, F_OK) == 0) {
                return realpath(stdlib_path, NULL);
            }
        }
    }

    if (access("/usr/local/lib/hemlock/stdlib", F_OK) == 0) {
        return strdup("/usr/local/lib/hemlock/stdlib");
    }

    return NULL;
}

static char* find_hem_modules(const char *start_path) {
    char search_path[PATH_MAX];
    char hem_modules_path[PATH_MAX];

    strncpy(search_path, start_path, PATH_MAX - 1);
    search_path[PATH_MAX - 1] = '\0';

    while (1) {
        int ret = snprintf(hem_modules_path, PATH_MAX, "%s/hem_modules", search_path);
        if (ret > 0 && ret < PATH_MAX && access(hem_modules_path, F_OK) == 0) {
            return strdup(hem_modules_path);
        }

        // Walk up by truncating at last '/'
        char *last_slash = strrchr(search_path, '/');
        if (!last_slash || last_slash == search_path) {
            // At root or no slash — check root hem_modules then stop
            if (last_slash == search_path && search_path[1] != '\0') {
                search_path[1] = '\0';
                continue;
            }
            break;
        }
        *last_slash = '\0';
    }

    return NULL;
}

static int is_package_import(const char *import_path) {
    if (import_path[0] == '.' || path_is_absolute(import_path)) {
        return 0;
    }

    const char *slash = strchr(import_path, '/');
    if (!slash || slash == import_path) {
        return 0;
    }

    return 1;
}

// ========== MODULE RESOLUTION ==========

ModuleResolution* module_resolution_new(const char *current_dir, const char *main_file_path) {
    ModuleResolution *resolver = malloc(sizeof(ModuleResolution));
    if (!resolver) {
        return NULL;
    }

    if (current_dir) {
        resolver->current_dir = strdup(current_dir);
    } else {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            resolver->current_dir = strdup(cwd);
        } else {
            resolver->current_dir = strdup(".");
        }
    }
    if (!resolver->current_dir) {
        free(resolver);
        return NULL;
    }

    if (main_file_path) {
        char *path_copy = strdup(main_file_path);
        if (!path_copy) {
            free(resolver->current_dir);
            free(resolver);
            return NULL;
        }
        char *dir = dirname(path_copy);
        resolver->main_file_dir = realpath(dir, NULL);
        if (!resolver->main_file_dir) {
            resolver->main_file_dir = strdup(dir);
            if (!resolver->main_file_dir) {
                free(path_copy);
                free(resolver->current_dir);
                free(resolver);
                return NULL;
            }
        }
        free(path_copy);
    } else {
        resolver->main_file_dir = NULL;
    }

    resolver->stdlib_path = find_stdlib_path();
    resolver->dependency_hook = NULL;
    resolver->dependency_hook_data = NULL;

    return resolver;
}

void module_resolution_free(ModuleResolution *resolver) {
    if (!resolver) {
        return;
    }
    free(resolver->current_dir);
    free(resolver->stdlib_path);
    free(resolver->main_file_dir);
    free(resolver);
}

void module_resolution_set_dependency_hook(ModuleResolution *resolver, ModuleDependencyHook hook, void *data) {
    if (!resolver) {
        return;
    }
    resolver->dependency_hook = hook;
    resolver->dependency_hook_data = data;
}

char* resolve_module_path(ModuleResolution *resolver, const char *importer_path, const char *import_path) {
    char resolved[PATH_MAX];
    const char *fallback_dir = resolver->main_file_dir ? resolver->main_file_dir : resolver->current_dir;

    if (strncmp(import_path, "@stdlib/", 8) == 0) {
        if (!resolver->stdlib_path) {
            fprintf(stderr, "Error: @stdlib alias used but stdlib directory not found\n");
            return NULL;
        }

        const char *module_subpath = import_path + 8;
        if (!is_safe_subpath(module_subpath)) {
            fprintf(stderr, "Error: Invalid module path '%s' - directory traversal not allowed\n", import_path);
            return NULL;
        }

        snprintf(resolved, PATH_MAX, "%s/%s", resolver->stdlib_path, module_subpath);

        if (!path_is_within_base(resolved, resolver->stdlib_path)) {
            fprintf(stderr, "Error: Module path '%s' resolves outside stdlib directory\n", import_path);
            return NULL;
        }
    } else if (path_is_absolute(import_path)) {
        strncpy(resolved, import_path, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
    } else if (is_package_import(import_path)) {
        const char *search_from = importer_path ? importer_path : fallback_dir;

        char search_dir[PATH_MAX];
        strncpy(search_dir, search_from, PATH_MAX - 1);
        search_dir[PATH_MAX - 1] = '\0';

        if (importer_path) {
            char *last_slash = strrchr(search_dir, '/');
            if (last_slash) {
                *last_slash = '\0';
            }
        }

        char *hem_modules = find_hem_modules(search_dir);

        if (hem_modules) {
            char owner[256] = {0};
            char repo[256] = {0};
            const char *subpath = NULL;

            const char *first_slash = strchr(import_path, '/');
            if (!first_slash) {
                free(hem_modules);
                fprintf(stderr, "Error: Invalid package import '%s' - expected owner/repo format\n", import_path);
                return NULL;
            }
            {
                size_t owner_len = first_slash - import_path;
                if (owner_len >= sizeof(owner)) owner_len = sizeof(owner) - 1;
                strncpy(owner, import_path, owner_len);
                owner[owner_len] = '\0';

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

            if (!is_safe_subpath(owner) || !is_safe_subpath(repo)) {
                fprintf(stderr, "Error: Invalid package name - directory traversal not allowed\n");
                free(hem_modules);
                return NULL;
            }

            if (subpath && !is_safe_subpath(subpath)) {
                fprintf(stderr, "Error: Invalid package subpath '%s' - directory traversal not allowed\n", subpath);
                free(hem_modules);
                return NULL;
            }

            char try_path[PATH_MAX];

            if (subpath) {
                snprintf(try_path, PATH_MAX, "%s/%s/%s/%s.hml", hem_modules, owner, repo, subpath);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }

                snprintf(try_path, PATH_MAX, "%s/%s/%s/%s/index.hml", hem_modules, owner, repo, subpath);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }

                snprintf(try_path, PATH_MAX, "%s/%s/%s/src/%s.hml", hem_modules, owner, repo, subpath);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }

                snprintf(try_path, PATH_MAX, "%s/%s/%s/src/%s/index.hml", hem_modules, owner, repo, subpath);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }
            } else {
                char pkg_json_path[PATH_MAX];
                snprintf(pkg_json_path, PATH_MAX, "%s/%s/%s/package.json", hem_modules, owner, repo);

                char main_file[256] = "src/index.hml";
                FILE *pkg_file = fopen(pkg_json_path, "r");
                if (pkg_file) {
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

                if (!is_safe_subpath(main_file)) {
                    fprintf(stderr, "Error: Invalid 'main' field in package.json - directory traversal not allowed\n");
                    free(hem_modules);
                    return NULL;
                }

                snprintf(try_path, PATH_MAX, "%s/%s/%s/%s", hem_modules, owner, repo, main_file);

                int path_len = strlen(try_path);
                if (path_len < 4 || strcmp(try_path + path_len - 4, ".hml") != 0) {
                    strncat(try_path, ".hml", PATH_MAX - path_len - 1);
                }

                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }

                snprintf(try_path, PATH_MAX, "%s/%s/%s/src/index.hml", hem_modules, owner, repo);
                if (access(try_path, F_OK) == 0) {
                    free(hem_modules);
                    return strdup(try_path);
                }
            }

            free(hem_modules);
        }

        snprintf(resolved, PATH_MAX, "%s/%s", fallback_dir, import_path);
    } else {
        const char *base_dir;
        char importer_dir[PATH_MAX];

        if (importer_path) {
            strncpy(importer_dir, importer_path, PATH_MAX - 1);
            importer_dir[PATH_MAX - 1] = '\0';
            base_dir = dirname(importer_dir);
        } else {
            base_dir = fallback_dir;
        }

        snprintf(resolved, PATH_MAX, "%s/%s", base_dir, import_path);
    }

    int len = strlen(resolved);
    if (len < 4 || strcmp(resolved + len - 4, ".hml") != 0) {
        strncat(resolved, ".hml", PATH_MAX - len - 1);
    }

    char *absolute = realpath(resolved, NULL);
    if (!absolute) {
        absolute = strdup(resolved);
    }

    if (absolute && resolver->dependency_hook && importer_path) {
        resolver->dependency_hook(resolver->dependency_hook_data, importer_path, absolute);
    }

    return absolute;
}

// ========== MODULE DEPENDENCY GRAPH ==========

ModuleDependencyGraph* module_dependency_graph_new(void) {
    ModuleDependencyGraph *graph = malloc(sizeof(ModuleDependencyGraph));
    if (!graph) {
        return NULL;
    }
    graph->edges = NULL;
    graph->count = 0;
    graph->capacity = 0;
    return graph;
}

void module_dependency_graph_free(ModuleDependencyGraph *graph) {
    if (!graph) {
        return;
    }
    for (int i = 0; i < graph->count; i++) {
        free(graph->edges[i].importer_path);
        free(graph->edges[i].resolved_path);
    }
    free(graph->edges);
    free(graph);
}

void module_dependency_graph_add(ModuleDependencyGraph *graph, const char *importer_path, const char *resolved_path) {
    if (!graph || !importer_path || !resolved_path) {
        return;
    }
    if (graph->count >= graph->capacity) {
        int new_capacity = graph->capacity == 0 ? 16 : graph->capacity * 2;
        ModuleDependency *new_edges = realloc(graph->edges, sizeof(ModuleDependency) * new_capacity);
        if (!new_edges) {
            return;
        }
        graph->edges = new_edges;
        graph->capacity = new_capacity;
    }
    graph->edges[graph->count].importer_path = strdup(importer_path);
    if (!graph->edges[graph->count].importer_path) {
        return;  // Allocation failed
    }
    graph->edges[graph->count].resolved_path = strdup(resolved_path);
    if (!graph->edges[graph->count].resolved_path) {
        free(graph->edges[graph->count].importer_path);
        return;  // Allocation failed
    }
    graph->count++;
}

void module_dependency_graph_hook(void *user_data, const char *importer_path, const char *resolved_path) {
    ModuleDependencyGraph *graph = (ModuleDependencyGraph *)user_data;
    module_dependency_graph_add(graph, importer_path, resolved_path);
}
