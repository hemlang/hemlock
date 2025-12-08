#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

// ========== ENVIRONMENT ==========

// DJB2 hash function - fast and good distribution for variable names
static uint32_t hash_string(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    return hash;
}

Environment* env_new(Environment *parent) {
    Environment *env = malloc(sizeof(Environment));
    if (!env) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    env->capacity = 16;
    env->count = 0;
    env->ref_count = 1;  // Initialize reference count to 1
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
    // Initialize all slots to -1 (empty)
    for (int i = 0; i < env->hash_capacity; i++) {
        env->hash_table[i] = -1;
    }
    env->parent = parent;
    // Retain parent environment if it exists
    if (parent) {
        env_retain(parent);
    }
    return env;
}

// ========== CYCLE BREAKING ==========

// DEPRECATED: The global manually_freed_pointers array has been replaced with
// atomic 'freed' flags on Buffer, Array, and Object structs for thread-safe
// double-free detection. These functions are kept as no-ops for backward compatibility.

void register_manually_freed_pointer(void *ptr) {
    (void)ptr;  // No-op - freed flag is now set atomically on the struct itself
}

int is_manually_freed_pointer(void *ptr) {
    (void)ptr;
    // No-op - callers should use atomic_load(&struct->freed) instead
    return 0;
}

void clear_manually_freed_pointers(void) {
    // No-op - nothing to clear since we no longer track globally
}

// Visited set for tracking processed pointers during cycle breaking
typedef struct {
    void **pointers;
    int count;
    int capacity;
} VisitedSet;

static VisitedSet* visited_set_new(void) {
    VisitedSet *set = malloc(sizeof(VisitedSet));
    if (!set) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    set->capacity = 16;
    set->count = 0;
    set->pointers = malloc(sizeof(void*) * set->capacity);
    if (!set->pointers) {
        free(set);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    return set;
}

static void visited_set_free(VisitedSet *set) {
    if (set) {
        free(set->pointers);
        free(set);
    }
}

static int visited_set_contains(VisitedSet *set, void *ptr) {
    for (int i = 0; i < set->count; i++) {
        if (set->pointers[i] == ptr) {
            return 1;
        }
    }
    return 0;
}

static void visited_set_add(VisitedSet *set, void *ptr) {
    // Grow if needed
    if (set->count >= set->capacity) {
        set->capacity *= 2;
        void **new_pointers = realloc(set->pointers, sizeof(void*) * set->capacity);
        if (!new_pointers) {
            fprintf(stderr, "Runtime error: Memory allocation failed\n");
            exit(1);
        }
        set->pointers = new_pointers;
    }
    set->pointers[set->count++] = ptr;
}

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
                    fn->closure_env = NULL;  // Prevent double-release in value_free
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
                    value_break_cycles_internal(obj->field_values[i], visited);
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
        free(env->names[i]);
        VALUE_RELEASE(env->values[i]);
    }
    // Reset count but keep capacity and allocated arrays
    env->count = 0;
    // Clear hash table (reset all slots to -1)
    for (int i = 0; i < env->hash_capacity; i++) {
        env->hash_table[i] = -1;
    }
}

void env_free(Environment *env) {
    // Free all variable names and release values
    for (int i = 0; i < env->count; i++) {
        free(env->names[i]);
        VALUE_RELEASE(env->values[i]);  // Decrement reference count
    }
    free(env->names);
    free(env->values);
    free(env->is_const);
    free(env->hash_table);

    // Release parent environment (may trigger cascade of frees)
    Environment *parent = env->parent;

    // Free the environment itself
    free(env);

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

static void env_grow(Environment *env) {
    env->capacity *= 2;
    char **new_names = realloc(env->names, sizeof(char*) * env->capacity);
    if (!new_names) {
        fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
        exit(1);
    }
    env->names = new_names;

    Value *new_values = realloc(env->values, sizeof(Value) * env->capacity);
    if (!new_values) {
        fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
        exit(1);
    }
    env->values = new_values;

    int *new_is_const = realloc(env->is_const, sizeof(int) * env->capacity);
    if (!new_is_const) {
        fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
        exit(1);
    }
    env->is_const = new_is_const;

    // Grow hash table (keep it at 2x capacity for good load factor)
    env->hash_capacity = env->capacity * 2;
    int *new_hash_table = realloc(env->hash_table, sizeof(int) * env->hash_capacity);
    if (!new_hash_table) {
        fprintf(stderr, "Runtime error: Memory allocation failed during environment growth\n");
        exit(1);
    }
    env->hash_table = new_hash_table;

    // Rehash all existing entries
    env_rehash(env);
}

// O(1) hash table lookup - returns index or -1 if not found
static int env_lookup(Environment *env, const char *name, uint32_t hash) {
    int slot = hash % env->hash_capacity;
    int start_slot = slot;

    while (env->hash_table[slot] != -1) {
        int idx = env->hash_table[slot];
        if (strcmp(env->names[idx], name) == 0) {
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

// Define a new variable (for let/const declarations)
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx) {
    uint32_t hash = hash_string(name);

    // Check if variable already exists in current scope using hash table
    int existing = env_lookup(env, name, hash);
    if (existing >= 0) {
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
    env->names[index] = strdup(name);
    VALUE_RETAIN(value);  // Retain the value
    env->values[index] = value;
    env->is_const[index] = is_const;
    env->count++;

    // Insert into hash table
    env_hash_insert(env, name, index);
}

// Set a variable (for reassignment or implicit definition in loops/functions)
void env_set(Environment *env, const char *name, Value value, ExecutionContext *ctx) {
    uint32_t hash = hash_string(name);

    // Check current scope using hash table
    int idx = env_lookup(env, name, hash);
    if (idx >= 0) {
        // Check if variable is const
        if (env->is_const[idx]) {
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
        return;
    }

    // Check parent scopes using hash table
    Environment *search_env = env->parent;
    while (search_env != NULL) {
        int pidx = env_lookup(search_env, name, hash);
        if (pidx >= 0) {
            // Found in parent scope - check if const
            if (search_env->is_const[pidx]) {
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
            return;
        }
        search_env = search_env->parent;
    }

    // Variable not found anywhere - create new mutable variable in current scope
    // This handles implicit variable creation in loops and function calls
    if (env->count >= env->capacity) {
        env_grow(env);
    }

    int index = env->count;
    env->names[index] = strdup(name);
    VALUE_RETAIN(value);  // Retain the value
    env->values[index] = value;
    env->is_const[index] = 0;  // Always mutable for implicit variables
    env->count++;

    // Insert into hash table
    env_hash_insert(env, name, index);
}

Value env_get(Environment *env, const char *name, ExecutionContext *ctx) {
    uint32_t hash = hash_string(name);

    // Search current and parent scopes using hash table
    Environment *search_env = env;
    while (search_env != NULL) {
        int idx = env_lookup(search_env, name, hash);
        if (idx >= 0) {
            Value val = search_env->values[idx];
            VALUE_RETAIN(val);  // Retain for the caller (caller now owns a reference)
            return val;
        }
        search_env = search_env->parent;
    }

    // Variable not found - throw exception instead of exiting
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
    ctx->exception_state.exception_value = val_string(error_msg);
    ctx->exception_state.is_throwing = 1;
    return val_null();  // Return dummy value when exception is thrown
}
