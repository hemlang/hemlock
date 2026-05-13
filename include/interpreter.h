#ifndef HEMLOCK_INTERPRETER_H
#define HEMLOCK_INTERPRETER_H

#include "runtime/types.h"

// Live-binding alias for imported module bindings.
// `import { X } from "mod"` installs an EnvImport so reads of X dispatch
// to the exporting module's env on every access, instead of capturing
// a stale snapshot at import time.
typedef struct EnvImport {
    char *alias_name;             // name in the importing scope
    char *source_name;             // name in the exporting module's exports env
    struct Environment *source_env;  // exporter's exports env (retained)
    struct EnvImport *next;
} EnvImport;

// Environment (symbol table for variables)
typedef struct Environment {
    char **names;
    Value *values;
    int *is_const;  // 1 if const, 0 if mutable (let)
    int count;
    int capacity;
    int ref_count;  // Reference count for memory management
    struct Environment *parent;  // for nested scopes later
    // Hash table for O(1) variable lookup (linear probing)
    int *hash_table;     // Array of variable indices, -1 = empty slot
    int hash_capacity;   // Size of hash table (usually 2x capacity)
    // Borrowed names optimization: bit flags (1 = borrowed, don't free)
    unsigned int borrowed_flags;  // Bit flags for first 32 names
    // Thread-safety: mutex for concurrent access from tasks
    void *mutex;  // pthread_mutex_t* (opaque pointer for header compatibility)
    // Live aliases for imported module bindings (NULL when no imports)
    EnvImport *imports;
} Environment;

// Public interface
Environment* env_new(Environment *parent);
void env_free(Environment *env);
void env_retain(Environment *env);
void env_release(Environment *env);
void env_break_cycles(Environment *env);  // Break circular references before final release
void env_define(Environment *env, const char *name, Value value, int is_const, ExecutionContext *ctx);
void env_define_import(Environment *env, const char *alias_name,
                       Environment *source_env, const char *source_name,
                       ExecutionContext *ctx);
void env_set(Environment *env, const char *name, Value value, ExecutionContext *ctx);
Value env_get(Environment *env, const char *name, ExecutionContext *ctx);

// Fast resolved variable access (using pre-computed depth/slot indices)
Value env_get_resolved(Environment *env, int depth, int slot);
int env_set_resolved(Environment *env, int depth, int slot, Value value, ExecutionContext *ctx);

// Execution context management (opaque pointer pattern)
ExecutionContext* exec_context_new(void);
void exec_context_free(ExecutionContext *ctx);

Value eval_expr(Expr *expr, Environment *env, ExecutionContext *ctx);
void eval_stmt(Stmt *stmt, Environment *env, ExecutionContext *ctx);
void eval_program(Stmt **stmts, int count, Environment *env, ExecutionContext *ctx);

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx);

#endif // HEMLOCK_INTERPRETER_H
