#include "hemlock_alloc.h"
#include "internal.h"
#include "hemlock_limits.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

// ========== ENVIRONMENT POOL ==========
// Pre-allocate environments to avoid malloc/free overhead in recursive calls
// Uses a free list for O(1) alloc/free

#define ENV_POOL_SIZE HML_ENV_POOL_SIZE
#define ENV_DEFAULT_CAPACITY HML_ENV_DEFAULT_CAPACITY

typedef struct {
    Environment envs[ENV_POOL_SIZE];
    char *names_storage[ENV_POOL_SIZE][ENV_DEFAULT_CAPACITY];
    Value values_storage[ENV_POOL_SIZE][ENV_DEFAULT_CAPACITY];
    int is_const_storage[ENV_POOL_SIZE][ENV_DEFAULT_CAPACITY];
    int hash_table_storage[ENV_POOL_SIZE][ENV_DEFAULT_CAPACITY * 2];
    pthread_mutex_t env_mutexes[ENV_POOL_SIZE];  // Per-environment mutexes for thread safety
    int free_list[ENV_POOL_SIZE];  // Stack of free indices
    int free_count;                 // Number of free slots
    pthread_mutex_t mutex;          // Protects pool operations for thread safety
} EnvironmentPool;

static EnvironmentPool env_pool = {0};
static int env_pool_initialized = 0;
static pthread_once_t env_pool_once = PTHREAD_ONCE_INIT;

// ========== SINGLE-THREADED LOCK ELISION ==========
// Environments are shared across task threads, so every access takes the
// per-environment mutex. A program that never spawns a task has exactly one
// thread, and the uncontended lock/unlock pairs were ~25% of all instructions
// in tight loops. g_interp_has_spawned flips (monotonically, before the first
// task thread is created) so until then no lock is taken at all. Once set,
// every thread sees it and locks as before.
static inline int interp_is_threaded(void) {
    return __atomic_load_n(&g_interp_has_spawned, __ATOMIC_ACQUIRE);
}

static inline pthread_mutex_t *env_lock_of(Environment *env) {
    return interp_is_threaded() ? (pthread_mutex_t*)env->mutex : NULL;
}

static void env_pool_init_internal(void) {
    pthread_mutex_init(&env_pool.mutex, NULL);
    for (int i = 0; i < ENV_POOL_SIZE; i++) {
        // Pre-initialize the environment structures
        env_pool.envs[i].names = env_pool.names_storage[i];
        env_pool.envs[i].values = env_pool.values_storage[i];
        env_pool.envs[i].is_const = env_pool.is_const_storage[i];
        env_pool.envs[i].hash_table = env_pool.hash_table_storage[i];
        env_pool.envs[i].capacity = ENV_DEFAULT_CAPACITY;
        env_pool.envs[i].hash_capacity = ENV_DEFAULT_CAPACITY * 2;
        // Initialize per-environment mutex for thread-safe access
        pthread_mutex_init(&env_pool.env_mutexes[i], NULL);
        env_pool.envs[i].mutex = &env_pool.env_mutexes[i];
        // Add to free list (in reverse order so 0 is popped first)
        env_pool.free_list[i] = ENV_POOL_SIZE - 1 - i;
    }
    env_pool.free_count = ENV_POOL_SIZE;
    env_pool_initialized = 1;
}

static void env_pool_init(void) {
    pthread_once(&env_pool_once, env_pool_init_internal);
}

static Environment* env_pool_alloc(void) {
    env_pool_init();  // Ensure initialized (thread-safe via pthread_once)

    int threaded = interp_is_threaded();
    if (threaded) pthread_mutex_lock(&env_pool.mutex);

    // O(1) allocation from free list
    if (env_pool.free_count > 0) {
        int idx = env_pool.free_list[--env_pool.free_count];
        if (threaded) pthread_mutex_unlock(&env_pool.mutex);

        Environment *env = &env_pool.envs[idx];
        // Reset to default storage (in case it was grown)
        env->names = env_pool.names_storage[idx];
        env->values = env_pool.values_storage[idx];
        env->is_const = env_pool.is_const_storage[idx];
        env->hash_table = env_pool.hash_table_storage[idx];
        env->capacity = ENV_DEFAULT_CAPACITY;
        env->hash_capacity = ENV_DEFAULT_CAPACITY * 2;
        env->declared_types = NULL;
        return env;
    }

    if (threaded) pthread_mutex_unlock(&env_pool.mutex);
    return NULL;  // Pool exhausted
}

static int env_is_pooled(Environment *env) {
    return env >= &env_pool.envs[0] && env < &env_pool.envs[ENV_POOL_SIZE];
}

static void env_pool_free(Environment *env) {
    if (!env_is_pooled(env)) return;
    int idx = (int)(env - &env_pool.envs[0]);
    // If it was grown beyond default, free the grown arrays
    if (env->names != env_pool.names_storage[idx]) {
        free(env->names);
        env->names = env_pool.names_storage[idx];
    }
    if (env->values != env_pool.values_storage[idx]) {
        free(env->values);
        env->values = env_pool.values_storage[idx];
    }
    if (env->is_const != env_pool.is_const_storage[idx]) {
        free(env->is_const);
        env->is_const = env_pool.is_const_storage[idx];
    }
    if (env->hash_table != env_pool.hash_table_storage[idx]) {
        free(env->hash_table);
        env->hash_table = env_pool.hash_table_storage[idx];
    }
    // Declared-type arrays are always heap-allocated (lazily); release them.
    free(env->declared_types);
    env->declared_types = NULL;
    // Reset capacities to match the pool storage the pointers now reference.
    // Leaving a grown capacity here would make the next user of this slot
    // write past the fixed-size pool arrays, corrupting neighboring
    // environments' storage.
    env->capacity = ENV_DEFAULT_CAPACITY;
    env->hash_capacity = ENV_DEFAULT_CAPACITY * 2;
    // O(1) return to free list (thread-safe)
    int threaded = interp_is_threaded();
    if (threaded) pthread_mutex_lock(&env_pool.mutex);
    env_pool.free_list[env_pool.free_count++] = idx;
    if (threaded) pthread_mutex_unlock(&env_pool.mutex);
}

// ========== ENVIRONMENT ==========

// DJB2 hash - delegates to shared inline in hash.h
#include "shared/hash.h"
uint32_t hash_string(const char *str) {
    return hml_hash_string(str);
}

Environment* env_new(Environment *parent) {
    // Try to get from pool first
    Environment *env = env_pool_alloc();

    if (env) {
        // Got from pool - just initialize
        env->count = 0;
        env->ref_count = 1;
        env->borrowed_flags = 0;  // Clear borrowed flags
        env->imports = NULL;
        env->declared_types = NULL;
        // Clear hash table using memset (faster than loop)
        memset(env->hash_table, 0xFF, sizeof(int) * env->hash_capacity);
        // mutex is already set to pooled mutex in env_pool_alloc
        env->parent = parent;
        if (parent) {
            env_retain(parent);
        }
        return env;
    }

    // Pool exhausted - fall back to malloc
    env = malloc(sizeof(Environment));
    if (!env) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    env->capacity = 16;
    env->count = 0;
    env->ref_count = 1;  // Initialize reference count to 1
    env->declared_types = NULL;
    env->names = malloc(sizeof(char*) * env->capacity);
    if (!env->names) {
        free(env);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    env->values = malloc(sizeof(Value) * env->capacity);
    if (!env->values) {
        free(env->names);
        free(env);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    env->is_const = malloc(sizeof(int) * env->capacity);
    if (!env->is_const) {
        free(env->values);
        free(env->names);
        free(env);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    // Initialize hash table (2x capacity for good load factor)
    env->hash_capacity = 32;
    env->hash_table = malloc(sizeof(int) * env->hash_capacity);
    if (!env->hash_table) {
        free(env->is_const);
        free(env->values);
        free(env->names);
        free(env);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    // Initialize all slots to -1 (empty) using memset (faster than loop)
    memset(env->hash_table, 0xFF, sizeof(int) * env->hash_capacity);
    env->borrowed_flags = 0;  // Initialize borrowed flags
    // Allocate and initialize mutex for thread-safe access
    env->mutex = malloc(sizeof(pthread_mutex_t));
    if (!env->mutex) {
        free(env->hash_table);
        free(env->is_const);
        free(env->values);
        free(env->names);
        free(env);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    pthread_mutex_init((pthread_mutex_t*)env->mutex, NULL);
    env->parent = parent;
    env->imports = NULL;
    // Retain parent environment if it exists
    if (parent) {
        env_retain(parent);
    }
    return env;
}

// Free all import aliases attached to the environment. Releases the source
// envs each import retained. Called from env_free before the env itself is
// torn down or returned to the pool.
static void env_free_imports(Environment *env) {
    EnvImport *imp = env->imports;
    while (imp) {
        EnvImport *next = imp->next;
        if (imp->source_env) {
            env_release(imp->source_env);
        }
        free(imp->alias_name);
        free(imp->source_name);
        free(imp);
        imp = next;
    }
    env->imports = NULL;
}

// ========== CYCLE BREAKING ==========

// VisitedSet is declared in internal.h and implemented in values.c

// Forward declaration
static void value_break_cycles_internal(Value val, VisitedSet *visited);

// Recursively break cycles in a value (handles nested objects/arrays)
static void value_break_cycles_internal(Value val, VisitedSet *visited) {
    switch (val.type) {
        case VAL_FUNCTION:
            if (val.as.as_function) {
                Function *fn = val.as.as_function;
                if (fn->closure_env) {
                    env_release(fn->closure_env);
                    fn->closure_env = NULL;  // Prevent double-release when the function is released
                }
            }
            break;

        case VAL_OBJECT:
            if (val.as.as_object) {
                Object *obj = val.as.as_object;
                // Skip objects that have been manually freed via builtin_free()
                if (atomic_load(&obj->freed)) {
                    return;
                }
                // Check if already visited (cycle detection)
                if (visited_set_contains(visited, obj)) {
                    return;
                }
                visited_set_add(visited, obj);

                // Recursively process all field values
                for (int i = 0; i < obj->num_fields; i++) {
                    value_break_cycles_internal(obj->fields[i].value, visited);
                }
            }
            break;

        case VAL_ARRAY:
            if (val.as.as_array) {
                Array *arr = val.as.as_array;
                // Skip arrays that have been manually freed via builtin_free()
                if (atomic_load(&arr->freed)) {
                    return;
                }
                // Check if already visited (cycle detection)
                if (visited_set_contains(visited, arr)) {
                    return;
                }
                visited_set_add(visited, arr);

                // Recursively process all elements
                for (int i = 0; i < arr->length; i++) {
                    value_break_cycles_internal(arr->elements[i], visited);
                }
            }
            break;

        default:
            // Other types don't contain nested functions
            break;
    }
}

// Break circular references by releasing closure environments from functions
// This now works recursively, finding functions nested in objects/arrays
// This should be called on global/top-level environments before final env_release
void env_break_cycles(Environment *env) {
    VisitedSet *visited = visited_set_new();

    for (int i = 0; i < env->count; i++) {
        value_break_cycles_internal(env->values[i], visited);
    }

    visited_set_free(visited);

    // NOTE: Do NOT clear manually_freed_pointers here!
    // It needs to persist until after env_free() is called
    // The set will be cleared by the caller after env_release()
}

// Clear all variables from environment without deallocating (for loop reuse)
void env_clear(Environment *env) {
    // Release all values and free names
    for (int i = 0; i < env->count; i++) {
        // Only free name if it's not borrowed (check bit flag)
        if (i >= 32 || !(env->borrowed_flags & (1U << i))) {
            free(env->names[i]);
        }
        VALUE_RELEASE(env->values[i]);
    }
    // Reset count but keep capacity and allocated arrays
    env->count = 0;
    env->borrowed_flags = 0;  // Clear borrowed flags
    if (env->imports) {
        env_free_imports(env);
    }
    // Clear hash table (reset all slots to -1)
    for (int i = 0; i < env->hash_capacity; i++) {
        env->hash_table[i] = -1;
    }
}

void env_free(Environment *env) {
    // Free all variable names and release values
    for (int i = 0; i < env->count; i++) {
        // Only free name if it's not borrowed (check bit flag)
        if (i >= 32 || !(env->borrowed_flags & (1U << i))) {
            free(env->names[i]);
        }
        VALUE_RELEASE(env->values[i]);  // Decrement reference count
    }

    // Tear down live-import aliases (releases the source envs)
    if (env->imports) {
        env_free_imports(env);
    }

    // Release parent environment (may trigger cascade of frees)
    Environment *parent = env->parent;

    // Return to pool or free the environment
    if (env_is_pooled(env)) {
        env_pool_free(env);
    } else {
        // Destroy mutex for non-pooled environments
        if (env->mutex) {
            pthread_mutex_destroy((pthread_mutex_t*)env->mutex);
            free(env->mutex);
        }
        free(env->names);
        free(env->values);
        free(env->is_const);
        free(env->declared_types);
        free(env->hash_table);
        free(env);
    }

    // Release parent after freeing this environment to avoid use-after-free
    if (parent) {
        env_release(parent);
    }
}

// Increment reference count (thread-safe using atomic operations)
void env_retain(Environment *env) {
    if (env) {
        __atomic_add_fetch(&env->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

// Decrement reference count and free if it reaches 0 (thread-safe using atomic operations)
void env_release(Environment *env) {
    if (env) {
        int old_count = __atomic_sub_fetch(&env->ref_count, 1, __ATOMIC_SEQ_CST);
        if (old_count == 0) {
            env_free(env);
        }
    }
}

// Rehash all entries into the hash table (called after growing)
static void env_rehash(Environment *env) {
    // Clear hash table
    for (int i = 0; i < env->hash_capacity; i++) {
        env->hash_table[i] = -1;
    }
    // Re-insert all entries
    for (int i = 0; i < env->count; i++) {
        uint32_t hash = hash_string(env->names[i]);
        int slot = hash % env->hash_capacity;
        // Linear probing to find empty slot
        while (env->hash_table[slot] != -1) {
            slot = (slot + 1) % env->hash_capacity;
        }
        env->hash_table[slot] = i;
    }
}

// Check if array is from pool storage (can't be realloc'd)
static int env_names_is_pooled(Environment *env) {
    if (!env_is_pooled(env)) return 0;
    int idx = (int)(env - &env_pool.envs[0]);
    return env->names == env_pool.names_storage[idx];
}

static int env_values_is_pooled(Environment *env) {
    if (!env_is_pooled(env)) return 0;
    int idx = (int)(env - &env_pool.envs[0]);
    return env->values == env_pool.values_storage[idx];
}

static int env_is_const_is_pooled(Environment *env) {
    if (!env_is_pooled(env)) return 0;
    int idx = (int)(env - &env_pool.envs[0]);
    return env->is_const == env_pool.is_const_storage[idx];
}

static int env_hash_table_is_pooled(Environment *env) {
    if (!env_is_pooled(env)) return 0;
    int idx = (int)(env - &env_pool.envs[0]);
    return env->hash_table == env_pool.hash_table_storage[idx];
}

static void env_grow(Environment *env) {
    int old_capacity = env->capacity;
    env->capacity *= 2;

    // For pooled arrays, we need to malloc new storage (can't realloc static arrays)
    char **new_names;
    if (env_names_is_pooled(env)) {
        new_names = malloc(sizeof(char*) * env->capacity);
        if (new_names) memcpy(new_names, env->names, sizeof(char*) * old_capacity);
    } else {
        new_names = realloc(env->names, sizeof(char*) * env->capacity);
    }
    if (!new_names) {
        fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
        exit(1);
    }
    env->names = new_names;

    Value *new_values;
    if (env_values_is_pooled(env)) {
        new_values = malloc(sizeof(Value) * env->capacity);
        if (new_values) memcpy(new_values, env->values, sizeof(Value) * old_capacity);
    } else {
        new_values = realloc(env->values, sizeof(Value) * env->capacity);
    }
    if (!new_values) {
        fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
        exit(1);
    }
    env->values = new_values;

    int *new_is_const;
    if (env_is_const_is_pooled(env)) {
        new_is_const = malloc(sizeof(int) * env->capacity);
        if (new_is_const) memcpy(new_is_const, env->is_const, sizeof(int) * old_capacity);
    } else {
        new_is_const = realloc(env->is_const, sizeof(int) * env->capacity);
    }
    if (!new_is_const) {
        fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
        exit(1);
    }
    env->is_const = new_is_const;

    // Declared-type array (lazily allocated, never pooled storage)
    if (env->declared_types) {
        struct Type **new_declared = realloc(env->declared_types,
                                             sizeof(struct Type*) * env->capacity);
        if (!new_declared) {
            fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
            exit(1);
        }
        memset(new_declared + old_capacity, 0,
               sizeof(struct Type*) * (env->capacity - old_capacity));
        env->declared_types = new_declared;
    }

    // Grow hash table (keep it at 2x capacity for good load factor)
    int old_hash_capacity = env->hash_capacity;
    env->hash_capacity = env->capacity * 2;
    int *new_hash_table;
    if (env_hash_table_is_pooled(env)) {
        new_hash_table = malloc(sizeof(int) * env->hash_capacity);
        if (new_hash_table) memcpy(new_hash_table, env->hash_table, sizeof(int) * old_hash_capacity);
    } else {
        new_hash_table = realloc(env->hash_table, sizeof(int) * env->hash_capacity);
    }
    if (!new_hash_table) {
        fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
        exit(1);
    }
    env->hash_table = new_hash_table;

    // Rehash all existing entries
    env_rehash(env);
}

// Search an env's import alias list for `name`. Returns the matching
// EnvImport or NULL. Caller must already hold env->mutex.
static EnvImport* env_find_import(Environment *env, const char *name) {
    for (EnvImport *imp = env->imports; imp; imp = imp->next) {
        if (imp->alias_name == name ||
            (imp->alias_name[0] == name[0] && strcmp(imp->alias_name, name) == 0)) {
            return imp;
        }
    }
    return NULL;
}

// O(1) hash table lookup - returns index or -1 if not found
// Fast path for short identifiers using first 8 bytes comparison
static inline int env_lookup(Environment *env, const char *name, uint32_t hash) {
    int slot = hash % env->hash_capacity;
    int start_slot = slot;

    while (env->hash_table[slot] != -1) {
        int idx = env->hash_table[slot];
        const char *stored = env->names[idx];
        // Fast comparison: check if pointers match (interned strings)
        // or if first chars match before full strcmp
        if (stored == name ||
            (stored[0] == name[0] && strcmp(stored, name) == 0)) {
            return idx;
        }
        slot = (slot + 1) % env->hash_capacity;
        if (slot == start_slot) break;  // Full loop, not found
    }
    return -1;
}

// Insert into hash table (assumes slot is available)
static void env_hash_insert(Environment *env, const char *name, int index) {
    uint32_t hash = hash_string(name);
    int slot = hash % env->hash_capacity;

    while (env->hash_table[slot] != -1) {
        slot = (slot + 1) % env->hash_capacity;
    }
    env->hash_table[slot] = index;
}

// Fast version with pre-computed hash
static void env_hash_insert_with_hash(Environment *env, uint32_t hash, int index) {
    int slot = hash % env->hash_capacity;
    while (env->hash_table[slot] != -1) {
        slot = (slot + 1) % env->hash_capacity;
    }
    env->hash_table[slot] = index;
}

// Define a new variable (for let/const declarations)
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx) {
    uint32_t hash = hash_string(name);

    // Lock environment for thread-safe access
    pthread_mutex_t *mutex = env_lock_of(env);
    if (mutex) pthread_mutex_lock(mutex);

    // Check if variable already exists in current scope using hash table
    int existing = env_lookup(env, name, hash);
    if (existing >= 0 || env_find_import(env, name) != NULL) {
        if (mutex) pthread_mutex_unlock(mutex);
        // Throw exception instead of exiting
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Variable '%s' already defined in this scope", name);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return;
    }

    // New variable - grow if needed
    if (env->count >= env->capacity) {
        env_grow(env);
    }

    int index = env->count;
    env->names[index] = hml_xstrdup(name);
    if (!env->names[index]) {
        if (mutex) pthread_mutex_unlock(mutex);
        ctx->exception_state.exception_value = val_string("Memory allocation failed in env_define");
        ctx->exception_state.is_throwing = 1;
        return;
    }
    VALUE_RETAIN(value);  // Retain the value
    env->values[index] = value;
    env->is_const[index] = is_const;
    env->count++;

    // Insert into hash table
    env_hash_insert(env, name, index);

    if (mutex) pthread_mutex_unlock(mutex);
}

// Fast variant that borrows the name string without strdup
// Caller must ensure name outlives the environment
void env_define_borrowed(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx) {
    uint32_t hash = hash_string(name);

    // Lock environment for thread-safe access
    pthread_mutex_t *mutex = env_lock_of(env);
    if (mutex) pthread_mutex_lock(mutex);

    // Check if variable already exists in current scope using hash table
    int existing = env_lookup(env, name, hash);
    if (existing >= 0) {
        if (mutex) pthread_mutex_unlock(mutex);
        // Throw exception instead of exiting
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Variable '%s' already defined in this scope", name);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return;
    }

    // New variable - grow if needed
    if (env->count >= env->capacity) {
        env_grow(env);
    }

    int index = env->count;
    memcpy(&env->names[index], &name, sizeof(char *));  // Borrow without strdup
    // Mark as borrowed if index < 32
    if (index < 32) {
        env->borrowed_flags |= (1U << index);
    }
    VALUE_RETAIN(value);  // Retain the value
    env->values[index] = value;
    env->is_const[index] = is_const;
    env->count++;

    // Insert into hash table
    env_hash_insert(env, name, index);

    if (mutex) pthread_mutex_unlock(mutex);
}

// Fastest variant for function parameters - uses pre-computed hash, skips "already defined" check
// This is safe because we know params are unique (enforced by parser) and env is freshly created
// Note: This is typically called on newly created environments that aren't shared yet,
// but we still lock for consistency if the env happens to be shared.
void env_define_param(Environment *env, const char *name, uint32_t hash, Value value) {
    // Lock environment for thread-safe access
    pthread_mutex_t *mutex = env_lock_of(env);
    if (mutex) pthread_mutex_lock(mutex);

    // Grow if needed (unlikely for params, but be safe)
    if (env->count >= env->capacity) {
        env_grow(env);
    }

    int index = env->count;
    memcpy(&env->names[index], &name, sizeof(char *));  // Borrow without strdup
    if (index < 32) {
        env->borrowed_flags |= (1U << index);
    }
    VALUE_RETAIN(value);
    env->values[index] = value;
    env->is_const[index] = 0;  // Params are never const
    env->count++;

    // Insert with pre-computed hash
    env_hash_insert_with_hash(env, hash, index);

    if (mutex) pthread_mutex_unlock(mutex);
}

// Install a live-binding alias from the importing scope to a name in another
// module's exports environment. Subsequent `env_get(env, alias_name, ...)`
// calls dispatch to `env_get(source_env, source_name, ...)`, so the importer
// always sees the exporter's current value (matching the compiler's
// module-global semantics and standard ES module live bindings).
void env_define_import(Environment *env, const char *alias_name,
                       Environment *source_env, const char *source_name,
                       ExecutionContext *ctx) {
    if (!env || !source_env) return;

    uint32_t hash = hash_string(alias_name);

    pthread_mutex_t *mutex = env_lock_of(env);
    if (mutex) pthread_mutex_lock(mutex);

    if (env_lookup(env, alias_name, hash) >= 0 ||
        env_find_import(env, alias_name) != NULL) {
        if (mutex) pthread_mutex_unlock(mutex);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                 "Variable '%s' already defined in this scope", alias_name);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return;
    }

    EnvImport *imp = malloc(sizeof(EnvImport));
    if (!imp) {
        if (mutex) pthread_mutex_unlock(mutex);
        ctx->exception_state.exception_value = val_string("Memory allocation failed in env_define_import");
        ctx->exception_state.is_throwing = 1;
        return;
    }
    imp->alias_name = strdup(alias_name);
    imp->source_name = strdup(source_name);
    if (!imp->alias_name || !imp->source_name) {
        free(imp->alias_name);
        free(imp->source_name);
        free(imp);
        if (mutex) pthread_mutex_unlock(mutex);
        ctx->exception_state.exception_value = val_string("Memory allocation failed in env_define_import");
        ctx->exception_state.is_throwing = 1;
        return;
    }
    imp->source_env = source_env;
    env_retain(source_env);
    imp->next = env->imports;
    env->imports = imp;

    if (mutex) pthread_mutex_unlock(mutex);
}

// Set a variable (for reassignment or implicit definition in loops/functions)
void env_set(Environment *env, const char *name, Value value, ExecutionContext *ctx) {
    uint32_t hash = hash_string(name);

    // Lock current environment for thread-safe access
    pthread_mutex_t *mutex = env_lock_of(env);
    if (mutex) pthread_mutex_lock(mutex);

    // Fast path: check first variable (common for loop counters and function params)
    if (env->count > 0 && env->names[0][0] == name[0] &&
        (env->names[0] == name || strcmp(env->names[0], name) == 0)) {
        if (!env->is_const[0]) {
            VALUE_RELEASE(env->values[0]);
            VALUE_RETAIN(value);
            env->values[0] = value;
            if (mutex) pthread_mutex_unlock(mutex);
            return;
        }
    }

    // Check current scope using hash table
    int idx = env_lookup(env, name, hash);
    if (idx >= 0) {
        // Check if variable is const
        if (env->is_const[idx]) {
            if (mutex) pthread_mutex_unlock(mutex);
            // Throw exception instead of exiting
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", name);
            ctx->exception_state.exception_value = val_string(error_msg);
            ctx->exception_state.is_throwing = 1;
            return;
        }
        // Release old value, retain new value
        VALUE_RELEASE(env->values[idx]);
        VALUE_RETAIN(value);
        env->values[idx] = value;
        if (mutex) pthread_mutex_unlock(mutex);
        return;
    }

    // Imported bindings are read-only at the import site (mutations belong
    // to the exporting module). Reject assignment so we don't shadow the
    // alias with a new local variable.
    if (env_find_import(env, name) != NULL) {
        if (mutex) pthread_mutex_unlock(mutex);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Cannot assign to imported binding '%s'", name);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return;
    }

    // Unlock current env before checking parents (to prevent deadlock)
    if (mutex) pthread_mutex_unlock(mutex);

    // Check parent scopes using hash table
    Environment *search_env = env->parent;
    while (search_env != NULL) {
        pthread_mutex_t *parent_mutex = env_lock_of(search_env);
        if (parent_mutex) pthread_mutex_lock(parent_mutex);

        int pidx = env_lookup(search_env, name, hash);
        if (pidx >= 0) {
            // Found in parent scope - check if const
            if (search_env->is_const[pidx]) {
                if (parent_mutex) pthread_mutex_unlock(parent_mutex);
                // Throw exception instead of exiting
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", name);
                ctx->exception_state.exception_value = val_string(error_msg);
                ctx->exception_state.is_throwing = 1;
                return;
            }
            // Release old value, retain new value
            VALUE_RELEASE(search_env->values[pidx]);
            VALUE_RETAIN(value);
            // Update parent scope variable
            search_env->values[pidx] = value;
            if (parent_mutex) pthread_mutex_unlock(parent_mutex);
            return;
        }

        if (env_find_import(search_env, name) != NULL) {
            if (parent_mutex) pthread_mutex_unlock(parent_mutex);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Cannot assign to imported binding '%s'", name);
            ctx->exception_state.exception_value = val_string(error_msg);
            ctx->exception_state.is_throwing = 1;
            return;
        }

        Environment *next_parent = search_env->parent;
        if (parent_mutex) pthread_mutex_unlock(parent_mutex);
        search_env = next_parent;
    }

    // Variable not found anywhere - create new mutable variable in current scope
    // This handles implicit variable creation in loops and function calls
    // Re-lock current env for modification
    if (mutex) pthread_mutex_lock(mutex);

    if (env->count >= env->capacity) {
        env_grow(env);
    }

    int index = env->count;
    env->names[index] = hml_xstrdup(name);
    if (!env->names[index]) {
        if (mutex) pthread_mutex_unlock(mutex);
        ctx->exception_state.exception_value = val_string("Memory allocation failed in env_set");
        ctx->exception_state.is_throwing = 1;
        return;
    }
    VALUE_RETAIN(value);  // Retain the value
    env->values[index] = value;
    env->is_const[index] = 0;  // Always mutable for implicit variables
    env->count++;

    // Insert into hash table
    env_hash_insert(env, name, index);

    if (mutex) pthread_mutex_unlock(mutex);
}

Value env_get(Environment *env, const char *name, ExecutionContext *ctx) {
    uint32_t hash = hash_string(name);

    // Search current and parent scopes using hash table
    // Each environment is locked individually to prevent deadlock
    Environment *search_env = env;
    while (search_env != NULL) {
        // Lock this environment for thread-safe access
        pthread_mutex_t *mutex = env_lock_of(search_env);
        if (mutex) pthread_mutex_lock(mutex);

        // Fast path: check if first variable in current scope matches
        if (search_env == env && search_env->count > 0 &&
            search_env->names[0][0] == name[0] &&
            (search_env->names[0] == name || strcmp(search_env->names[0], name) == 0)) {
            Value val = search_env->values[0];
            VALUE_RETAIN(val);
            if (mutex) pthread_mutex_unlock(mutex);
            return val;
        }

        int idx = env_lookup(search_env, name, hash);
        if (idx >= 0) {
            Value val = search_env->values[idx];
            VALUE_RETAIN(val);  // Retain for the caller (caller now owns a reference)
            if (mutex) pthread_mutex_unlock(mutex);
            return val;
        }

        // Check live-binding aliases installed at this scope. Imports are
        // resolved to the exporter's exports env, so subsequent mutations in
        // the exporter are visible here (live bindings, not snapshots).
        EnvImport *imp = env_find_import(search_env, name);
        if (imp) {
            Environment *src = imp->source_env;
            // Copy source_name; releasing the mutex before recursing is required
            // to avoid deadlock if the source env shares a mutex chain.
            char *src_name = strdup(imp->source_name);
            if (mutex) pthread_mutex_unlock(mutex);
            if (!src_name) {
                ctx->exception_state.exception_value = val_string("Memory allocation failed in env_get import");
                ctx->exception_state.is_throwing = 1;
                return val_null();
            }
            Value val = env_get(src, src_name, ctx);
            free(src_name);
            return val;
        }

        Environment *parent = search_env->parent;
        if (mutex) pthread_mutex_unlock(mutex);
        search_env = parent;
    }

    // Variable not found - throw exception instead of exiting
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
    ctx->exception_state.exception_value = val_string(error_msg);
    ctx->exception_state.is_throwing = 1;
    return val_null();  // Return dummy value when exception is thrown
}

/*
 * Fast variable lookup using pre-resolved (depth, slot) indices.
 * This is an O(1) operation for local variables (depth=0) and O(depth) for closures.
 * Called when the resolver has computed resolution info for a variable reference.
 *
 * Parameters:
 *   env   - Current environment
 *   depth - Number of scope hops to reach the defining scope (0 = current)
 *   slot  - Index within that environment's values array
 *
 * Returns the value with an incremented reference count (caller owns a reference).
 */
Value env_get_resolved(Environment *env, int depth, int slot) {
    // Walk up the environment chain to the target depth
    Environment *target = env;
    for (int i = 0; i < depth; i++) {
        target = target->parent;
        // Should not happen if resolver is correct, but guard anyway
        if (!target) {
            return val_null();
        }
    }

    // Lock target environment for thread-safe access
    pthread_mutex_t *mutex = env_lock_of(target);
    if (mutex) pthread_mutex_lock(mutex);

    // Direct indexed access - O(1) instead of hash lookup
    if (slot >= 0 && slot < target->count) {
        Value val = target->values[slot];
        VALUE_RETAIN(val);
        if (mutex) pthread_mutex_unlock(mutex);
        return val;
    }

    if (mutex) pthread_mutex_unlock(mutex);
    // Invalid slot (should not happen if resolver is correct)
    return val_null();
}

/*
 * Fast variable assignment using pre-resolved (depth, slot) indices.
 * This is O(1) for local variables and O(depth) for closures.
 *
 * Parameters:
 *   env   - Current environment
 *   depth - Number of scope hops to reach the defining scope
 *   slot  - Index within that environment's values array
 *   value - New value to assign
 *   ctx   - Execution context (for error handling)
 *
 * Returns 1 on success, 0 on failure (const violation).
 */
int env_set_resolved(Environment *env, int depth, int slot, Value value, ExecutionContext *ctx) {
    // Walk up the environment chain to the target depth
    Environment *target = env;
    for (int i = 0; i < depth; i++) {
        target = target->parent;
        if (!target) {
            runtime_error(ctx, "Invalid resolved variable depth");
            return 0;
        }
    }

    // Lock target environment for thread-safe access
    pthread_mutex_t *mutex = env_lock_of(target);
    if (mutex) pthread_mutex_lock(mutex);

    // Check bounds
    if (slot < 0 || slot >= target->count) {
        if (mutex) pthread_mutex_unlock(mutex);
        runtime_error(ctx, "Invalid resolved variable slot");
        return 0;
    }

    // Check for const violation
    if (target->is_const[slot]) {
        if (mutex) pthread_mutex_unlock(mutex);
        runtime_error(ctx, "Cannot assign to constant '%s'", target->names[slot]);
        return 0;
    }

    // Release old value and assign new one
    VALUE_RELEASE(target->values[slot]);
    VALUE_RETAIN(value);
    target->values[slot] = value;

    if (mutex) pthread_mutex_unlock(mutex);
    return 1;
}

// ========== DECLARED-TYPE TRACKING ==========
// Annotated `let` bindings remember their annotation so reassignment can
// enforce it with the same conversion the declaration used. The Type nodes
// belong to the AST (which outlives execution) and are not owned here.

void env_set_declared_type_last(Environment *env, struct Type *type) {
    if (!env || !type || env->count == 0) return;

    pthread_mutex_t *mutex = env_lock_of(env);
    if (mutex) pthread_mutex_lock(mutex);

    if (!env->declared_types) {
        env->declared_types = calloc(env->capacity, sizeof(struct Type*));
        if (!env->declared_types) {
            if (mutex) pthread_mutex_unlock(mutex);
            return;  // Out of memory: fall back to unenforced (pre-existing behavior)
        }
    }
    env->declared_types[env->count - 1] = type;

    if (mutex) pthread_mutex_unlock(mutex);
}

struct Type *env_get_declared_type(Environment *env, const char *name) {
    uint32_t hash = hash_string(name);

    for (Environment *e = env; e != NULL; e = e->parent) {
        pthread_mutex_t *mutex = env_lock_of(e);
        if (mutex) pthread_mutex_lock(mutex);

        int idx = env_lookup(e, name, hash);
        if (idx >= 0) {
            // The name resolves here: an inner untyped binding shadows any
            // outer annotated one, so stop at the first match either way.
            struct Type *t = e->declared_types ? e->declared_types[idx] : NULL;
            if (mutex) pthread_mutex_unlock(mutex);
            return t;
        }

        if (mutex) pthread_mutex_unlock(mutex);
    }
    return NULL;
}

struct Type *env_get_declared_type_resolved(Environment *env, int depth, int slot) {
    Environment *target = env;
    for (int i = 0; i < depth; i++) {
        target = target->parent;
        if (!target) return NULL;
    }

    pthread_mutex_t *mutex = env_lock_of(target);
    if (mutex) pthread_mutex_lock(mutex);

    struct Type *t = NULL;
    if (target->declared_types && slot >= 0 && slot < target->count) {
        t = target->declared_types[slot];
    }

    if (mutex) pthread_mutex_unlock(mutex);
    return t;
}

// ========== FAST PATHS: ONE-WALK ASSIGNMENT AND UNRESOLVED-NAME INLINE CACHE ==========

Environment *env_walk(Environment *env, int depth) {
    while (depth-- > 0 && env) {
        env = env->parent;
    }
    return env;
}

// Store `new_value` into target->values[slot] with the full assignment
// semantics the by-name and resolved paths share: write through a ref
// binding, reject const, enforce the declared annotation. `env` is the
// executing environment (convert_to_type evaluates in it). Takes ownership
// of new_value and returns the stored value.
static Value env_assign_slot(Environment *target, int slot, Value new_value,
                             Environment *env, ExecutionContext *ctx,
                             int resolved_messages) {
    pthread_mutex_t *mutex = env_lock_of(target);
    if (mutex) pthread_mutex_lock(mutex);
    Value current = target->values[slot];
    int is_const = target->is_const[slot];
    struct Type *declared = target->declared_types ? target->declared_types[slot] : NULL;
    const char *vname = target->names[slot];
    if (current.type == VAL_REF) value_retain(current);  // keep alive across the write-through
    if (mutex) pthread_mutex_unlock(mutex);

    if (current.type == VAL_REF) {
        ref_assign(current.as.as_ref, new_value, ctx);
        value_release(current);
        return new_value;
    }

    if (is_const) {
        VALUE_RELEASE(new_value);
        if (resolved_messages) {
            runtime_error(ctx, "Cannot assign to constant '%s'", vname);
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", vname);
            ctx->exception_state.exception_value = val_string(error_msg);
            ctx->exception_state.is_throwing = 1;
        }
        return val_null();
    }

    if (declared != NULL) {
        new_value = convert_to_type(new_value, declared, env, ctx);
        if (ctx->exception_state.is_throwing) {
            VALUE_RELEASE(new_value);
            return val_null();
        }
    }

    if (mutex) pthread_mutex_lock(mutex);
    VALUE_RELEASE(target->values[slot]);
    VALUE_RETAIN(new_value);
    target->values[slot] = new_value;
    if (mutex) pthread_mutex_unlock(mutex);
    return new_value;
}

Value env_assign_resolved(Environment *env, int depth, int slot, Value new_value,
                          ExecutionContext *ctx) {
    Environment *target = env_walk(env, depth);
    if (!target) {
        VALUE_RELEASE(new_value);
        runtime_error(ctx, "Invalid resolved variable depth");
        return val_null();
    }
    if (slot < 0 || slot >= target->count) {
        VALUE_RELEASE(new_value);
        runtime_error(ctx, "Invalid resolved variable slot");
        return val_null();
    }
    return env_assign_slot(target, slot, new_value, env, ctx, 1);
}

// Find a real binding (not an import alias) for `name` by walking the chain.
// Returns 1 and the location on success. Returns 0 when the name is absent
// or the first match is a live import alias (*out_import set, *out_env is the
// env holding the alias) -- callers decide whether to follow it.
static int env_locate(Environment *env, const char *name, uint32_t hash,
                      Environment **out_env, int *out_slot, int *out_depth,
                      EnvImport **out_import) {
    int depth = 0;
    *out_import = NULL;
    for (Environment *e = env; e != NULL; e = e->parent, depth++) {
        pthread_mutex_t *mutex = env_lock_of(e);
        if (mutex) pthread_mutex_lock(mutex);
        int idx = env_lookup(e, name, hash);
        if (idx >= 0) {
            if (mutex) pthread_mutex_unlock(mutex);
            *out_env = e;
            *out_slot = idx;
            *out_depth = depth;
            return 1;
        }
        EnvImport *imp = env_find_import(e, name);
        if (mutex) pthread_mutex_unlock(mutex);
        if (imp) {
            *out_env = e;
            *out_depth = depth;
            *out_import = imp;
            return 0;
        }
    }
    return 0;
}

// Resolve `name` through the node's inline cache. Only called when the
// interpreter is single-threaded (the cache lives in the shared AST). On
// success *out_env/*out_slot name a live binding reached by walking from
// `env`, so the pointer is valid; the cached pointer itself is only ever
// compared, never dereferenced before that walk confirms it.
//
// The cache is bypassed for `self`: it is injected per call, so the same
// node can see it at different depths depending on how the enclosing
// function was invoked.
static int env_ic_locate(Environment *env, const char *name, ResolvedVar *rv,
                         Environment **out_env, int *out_slot) {
    if (rv->ic_state == HML_IC_STATE_MEGAMORPHIC) return 0;

    if (rv->ic_state == HML_IC_STATE_MONOMORPHIC) {
        Environment *t = env_walk(env, rv->ic_depth);
        if (t == (Environment*)rv->ic_env) {
            if (rv->ic_slot >= 0) {
                int slot = rv->ic_slot;
                if (slot < t->count &&
                    (t->names[slot] == name || strcmp(t->names[slot], name) == 0)) {
                    *out_env = t;
                    *out_slot = slot;
                    return 1;
                }
            } else {
                // Cached import alias: confirm it is still installed here,
                // then look the exported name up in the exporting scope.
                EnvImport *imp = NULL;
                for (EnvImport *p = t->imports; p; p = p->next) {
                    if (p == (EnvImport*)rv->ic_import) { imp = p; break; }
                }
                if (imp) {
                    Environment *src = imp->source_env;
                    int src_slot = env_lookup(src, imp->source_name,
                                              hash_string(imp->source_name));
                    if (src_slot >= 0) {
                        *out_env = src;
                        *out_slot = src_slot;
                        return 1;
                    }
                }
            }
        }
        if (++rv->ic_misses >= HML_IC_MAX_MISSES) {
            rv->ic_state = HML_IC_STATE_MEGAMORPHIC;
            return 0;
        }
    } else if (name[0] == 's' && strcmp(name, "self") == 0) {
        rv->ic_state = HML_IC_STATE_MEGAMORPHIC;
        return 0;
    }

    if (rv->ic_hash == 0) rv->ic_hash = hash_string(name);
    Environment *found_env;
    int found_slot, found_depth;
    EnvImport *imp;
    if (env_locate(env, name, rv->ic_hash, &found_env, &found_slot, &found_depth, &imp)) {
        rv->ic_env = found_env;
        rv->ic_depth = found_depth;
        rv->ic_slot = found_slot;
        rv->ic_import = NULL;
        rv->ic_state = HML_IC_STATE_MONOMORPHIC;
        *out_env = found_env;
        *out_slot = found_slot;
        return 1;
    }
    if (imp) {
        // Alias at found_env: only cache when the exported name is a plain
        // binding in the exporting scope itself (re-export chains stay slow).
        Environment *src = imp->source_env;
        int src_slot = env_lookup(src, imp->source_name, hash_string(imp->source_name));
        if (src_slot >= 0) {
            rv->ic_env = found_env;
            rv->ic_depth = found_depth;
            rv->ic_slot = -1;
            rv->ic_import = imp;
            rv->ic_state = HML_IC_STATE_MONOMORPHIC;
            *out_env = src;
            *out_slot = src_slot;
            return 1;
        }
    }
    return 0;
}

Value env_get_ic(Environment *env, const char *name, ResolvedVar *rv, ExecutionContext *ctx) {
    if (!interp_is_threaded()) {
        Environment *target;
        int slot;
        if (env_ic_locate(env, name, rv, &target, &slot)) {
            Value val = target->values[slot];
            VALUE_RETAIN(val);
            return val;
        }
    }
    return env_get(env, name, ctx);
}

Value env_assign_ic(Environment *env, const char *name, ResolvedVar *rv, Value new_value,
                    ExecutionContext *ctx) {
    if (!interp_is_threaded()) {
        Environment *target;
        int slot;
        // Imports are read-only at the import site; let env_set report that.
        if (env_ic_locate(env, name, rv, &target, &slot) && rv->ic_slot >= 0) {
            return env_assign_slot(target, slot, new_value, env, ctx, 0);
        }
    }

    // By-name path (mirrors the historical EXPR_ASSIGN sequence).
    Value target_val = env_get(env, name, ctx);
    if (target_val.type == VAL_REF) {
        ref_assign(target_val.as.as_ref, new_value, ctx);
        VALUE_RELEASE(target_val);
        return new_value;
    }
    VALUE_RELEASE(target_val);
    struct Type *declared = env_get_declared_type(env, name);
    if (declared != NULL) {
        new_value = convert_to_type(new_value, declared, env, ctx);
        if (ctx->exception_state.is_throwing) {
            VALUE_RELEASE(new_value);
            return val_null();
        }
    }
    env_set(env, name, new_value, ctx);
    return new_value;
}

void env_bind_self(Environment *env, Value value) {
    static const char self_name[] = "self";
    uint32_t hash = hash_string(self_name);

    pthread_mutex_t *mutex = env_lock_of(env);
    if (mutex) pthread_mutex_lock(mutex);

    int idx = env_lookup(env, self_name, hash);
    if (idx >= 0) {
        VALUE_RELEASE(env->values[idx]);
        VALUE_RETAIN(value);
        env->values[idx] = value;
        if (mutex) pthread_mutex_unlock(mutex);
        return;
    }

    if (env->count >= env->capacity) {
        env_grow(env);
    }
    int index = env->count;
    const char *borrowed = self_name;
    memcpy(&env->names[index], &borrowed, sizeof(char *));  // static storage, never freed
    if (index < 32) {
        env->borrowed_flags |= (1U << index);
    }
    VALUE_RETAIN(value);
    env->values[index] = value;
    env->is_const[index] = 0;
    env->count++;
    env_hash_insert_with_hash(env, hash, index);

    if (mutex) pthread_mutex_unlock(mutex);
}
