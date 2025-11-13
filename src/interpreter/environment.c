#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ========== ENVIRONMENT ==========

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
    env->parent = parent;
    // Retain parent environment if it exists
    if (parent) {
        env_retain(parent);
    }
    return env;
}

// Break circular references by releasing closure environments from functions
// This should be called on global/top-level environments before final env_release
void env_break_cycles(Environment *env) {
    for (int i = 0; i < env->count; i++) {
        Value val = env->values[i];
        if (val.type == VAL_FUNCTION && val.as.as_function) {
            Function *fn = val.as.as_function;
            if (fn->closure_env) {
                env_release(fn->closure_env);
                fn->closure_env = NULL;  // Prevent double-release in value_free
            }
        }
    }
}

void env_free(Environment *env) {
    // Free all variable names and release values
    for (int i = 0; i < env->count; i++) {
        free(env->names[i]);
        value_release(env->values[i]);  // Decrement reference count
    }
    free(env->names);
    free(env->values);
    free(env->is_const);

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
}

// Define a new variable (for let/const declarations)
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx) {
    // Check if variable already exists in current scope
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            // Throw exception instead of exiting
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Variable '%s' already defined in this scope", name);
            ctx->exception_state.exception_value = val_string(error_msg);
            ctx->exception_state.is_throwing = 1;
            return;
        }
    }

    // New variable
    if (env->count >= env->capacity) {
        env_grow(env);
    }

    env->names[env->count] = strdup(name);
    value_retain(value);  // Retain the value
    env->values[env->count] = value;
    env->is_const[env->count] = is_const;
    env->count++;
}

// Set a variable (for reassignment or implicit definition in loops/functions)
void env_set(Environment *env, const char *name, Value value, ExecutionContext *ctx) {
    // Check current scope
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            // Check if variable is const
            if (env->is_const[i]) {
                // Throw exception instead of exiting
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", name);
                ctx->exception_state.exception_value = val_string(error_msg);
                ctx->exception_state.is_throwing = 1;
                return;
            }
            // Release old value, retain new value
            value_release(env->values[i]);
            value_retain(value);
            env->values[i] = value;
            return;
        }
    }

    // Check parent scope
    if (env->parent != NULL) {
        // Look for variable in parent scopes
        Environment *search_env = env->parent;
        while (search_env != NULL) {
            for (int i = 0; i < search_env->count; i++) {
                if (strcmp(search_env->names[i], name) == 0) {
                    // Found in parent scope - check if const
                    if (search_env->is_const[i]) {
                        // Throw exception instead of exiting
                        char error_msg[256];
                        snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", name);
                        ctx->exception_state.exception_value = val_string(error_msg);
                        ctx->exception_state.is_throwing = 1;
                        return;
                    }
                    // Release old value, retain new value
                    value_release(search_env->values[i]);
                    value_retain(value);
                    // Update parent scope variable
                    search_env->values[i] = value;
                    return;
                }
            }
            search_env = search_env->parent;
        }
    }

    // Variable not found anywhere - create new mutable variable in current scope
    // This handles implicit variable creation in loops and function calls
    if (env->count >= env->capacity) {
        env_grow(env);
    }

    env->names[env->count] = strdup(name);
    value_retain(value);  // Retain the value
    env->values[env->count] = value;
    env->is_const[env->count] = 0;  // Always mutable for implicit variables
    env->count++;
}

Value env_get(Environment *env, const char *name, ExecutionContext *ctx) {
    // Search current scope
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            return env->values[i];
        }
    }

    // Search parent scope
    if (env->parent != NULL) {
        return env_get(env->parent, name, ctx);
    }

    // Variable not found - throw exception instead of exiting
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
    ctx->exception_state.exception_value = val_string(error_msg);
    ctx->exception_state.is_throwing = 1;
    return val_null();  // Return dummy value when exception is thrown
}
