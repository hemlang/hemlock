/*
 * Hemlock C Code Generator
 *
 * Translates Hemlock AST to C source code.
 */

#ifndef HEMLOCK_CODEGEN_H
#define HEMLOCK_CODEGEN_H

#include "../../include/ast.h"
#include <stdio.h>

// Forward declaration for closure info
typedef struct ClosureInfo ClosureInfo;
typedef struct DeferEntry DeferEntry;
typedef struct CompiledModule CompiledModule;
typedef struct ModuleCache ModuleCache;

// Deferred expression entry for LIFO execution
struct DeferEntry {
    Expr *expr;           // The expression to defer
    DeferEntry *next;     // Next entry (forms a stack)
};

// Closure information for anonymous functions
struct ClosureInfo {
    char *func_name;        // Generated function name
    char **captured_vars;   // Names of captured variables
    int num_captured;       // Number of captured variables
    Expr *func_expr;        // The function expression
    ClosureInfo *next;      // Linked list of closures
};

// Scope tracking for variable resolution
typedef struct Scope {
    char **vars;            // Variables in this scope
    int num_vars;           // Number of variables
    int capacity;           // Capacity
    struct Scope *parent;   // Parent scope
} Scope;

// ========== MODULE COMPILATION ==========

// Module loading state (for circular dependency detection)
typedef enum {
    MOD_UNLOADED,
    MOD_LOADING,
    MOD_LOADED
} ModuleState;

// Exported symbol from a module
typedef struct {
    char *name;             // Export name
    char *mangled_name;     // C variable name (module prefix + name)
} ExportedSymbol;

// Import binding: maps local name to mangled name
typedef struct {
    char *local_name;       // Name used in this module (e.g., "plus" if aliased)
    char *original_name;    // Original export name (e.g., "multiply")
    char *module_prefix;    // Module prefix (e.g., "_mod1_")
    int is_function;        // Whether this is a function binding
} ImportBinding;

// Compiled module tracking
struct CompiledModule {
    char *absolute_path;        // Resolved absolute path (cache key)
    char *module_prefix;        // Unique prefix for this module's symbols
    ModuleState state;          // Loading state for cycle detection
    Stmt **statements;          // Parsed AST
    int num_statements;
    ExportedSymbol *exports;    // Exported symbols
    int num_exports;
    int export_capacity;
    ImportBinding *imports;     // Import bindings for this module
    int num_imports;
    int import_capacity;
    CompiledModule *next;       // Linked list
};

// Module cache for tracking all compiled modules
struct ModuleCache {
    CompiledModule *modules;    // Linked list of modules
    char *current_dir;          // Current working directory
    char *stdlib_path;          // Path to standard library
    char *main_file_dir;        // Directory of the main file being compiled
    int module_counter;         // Counter for generating unique module prefixes
};

// Code generation context
typedef struct {
    FILE *output;           // Output file/stream
    int indent;             // Current indentation level
    int temp_counter;       // Counter for temporary variables
    int label_counter;      // Counter for labels
    int func_counter;       // Counter for anonymous functions
    int in_function;        // Whether we're inside a function
    char **local_vars;      // Stack of local variable names
    int num_locals;         // Number of local variables
    int local_capacity;     // Capacity of local vars array

    // Closure support
    Scope *current_scope;   // Current variable scope
    ClosureInfo *closures;  // List of closures to generate
    char **func_params;     // Current function parameters
    int num_func_params;    // Number of current function parameters

    // Defer support
    DeferEntry *defer_stack;  // Stack of deferred expressions (LIFO)

    // Module support
    ModuleCache *module_cache;          // Cache of compiled modules
    CompiledModule *current_module;     // Module currently being compiled (NULL for main)
} CodegenContext;

// Initialize code generation context
CodegenContext* codegen_new(FILE *output);

// Free code generation context
void codegen_free(CodegenContext *ctx);

// Generate C code for a complete program
void codegen_program(CodegenContext *ctx, Stmt **stmts, int stmt_count);

// Generate C code for a single statement
void codegen_stmt(CodegenContext *ctx, Stmt *stmt);

// Generate C code for an expression
// Returns the name of the temporary variable holding the result
char* codegen_expr(CodegenContext *ctx, Expr *expr);

// Helper: Generate a new temporary variable name
char* codegen_temp(CodegenContext *ctx);

// Helper: Generate a new label name
char* codegen_label(CodegenContext *ctx);

// Helper: Generate a new anonymous function name
char* codegen_anon_func(CodegenContext *ctx);

// Helper: Write indentation
void codegen_indent(CodegenContext *ctx);

// Helper: Increase/decrease indent level
void codegen_indent_inc(CodegenContext *ctx);
void codegen_indent_dec(CodegenContext *ctx);

// Helper: Write formatted output
void codegen_write(CodegenContext *ctx, const char *fmt, ...);

// Helper: Write a line with indentation
void codegen_writeln(CodegenContext *ctx, const char *fmt, ...);

// Helper: Add a local variable to the tracking list
void codegen_add_local(CodegenContext *ctx, const char *name);

// Helper: Check if a variable is local
int codegen_is_local(CodegenContext *ctx, const char *name);

// Helper: Escape a string for C output
char* codegen_escape_string(const char *str);

// Helper: Get the C operator string for a binary op
const char* codegen_binary_op_str(BinaryOp op);

// Helper: Get the HmlBinaryOp enum name
const char* codegen_hml_binary_op(BinaryOp op);

// Helper: Get the HmlUnaryOp enum name
const char* codegen_hml_unary_op(UnaryOp op);

// ========== SCOPE MANAGEMENT ==========

// Create a new scope
Scope* scope_new(Scope *parent);

// Free a scope
void scope_free(Scope *scope);

// Add a variable to the current scope
void scope_add_var(Scope *scope, const char *name);

// Check if a variable is in the given scope (not parents)
int scope_has_var(Scope *scope, const char *name);

// Check if a variable is defined in scope or any parent
int scope_is_defined(Scope *scope, const char *name);

// Push a new scope onto the stack
void codegen_push_scope(CodegenContext *ctx);

// Pop the current scope
void codegen_pop_scope(CodegenContext *ctx);

// ========== DEFER SUPPORT ==========

// Push a deferred expression onto the defer stack
void codegen_defer_push(CodegenContext *ctx, Expr *expr);

// Generate code to execute all defers in LIFO order (and clear the stack)
void codegen_defer_execute_all(CodegenContext *ctx);

// Clear the defer stack without generating code (for cleanup)
void codegen_defer_clear(CodegenContext *ctx);

// ========== CLOSURE ANALYSIS ==========

// Free variable info for a function
typedef struct {
    char **vars;
    int num_vars;
    int capacity;
} FreeVarSet;

// Find free variables in an expression
void find_free_vars(Expr *expr, Scope *local_scope, FreeVarSet *free_vars);

// Find free variables in a statement
void find_free_vars_stmt(Stmt *stmt, Scope *local_scope, FreeVarSet *free_vars);

// Add a free variable if not already present
void free_var_set_add(FreeVarSet *set, const char *var);

// Create a new free variable set
FreeVarSet* free_var_set_new(void);

// Free a free variable set
void free_var_set_free(FreeVarSet *set);

// ========== MODULE COMPILATION ==========

// Initialize module cache
ModuleCache* module_cache_new(const char *main_file_path);

// Free module cache
void module_cache_free(ModuleCache *cache);

// Resolve a module path (handles @stdlib/, relative, absolute)
char* module_resolve_path(ModuleCache *cache, const char *importer_path, const char *import_path);

// Compile a module (recursively compiles dependencies)
CompiledModule* module_compile(CodegenContext *ctx, const char *absolute_path);

// Get a cached module by path
CompiledModule* module_get_cached(ModuleCache *cache, const char *absolute_path);

// Add an export to a module
void module_add_export(CompiledModule *module, const char *name, const char *mangled_name);

// Find an export in a module by name
ExportedSymbol* module_find_export(CompiledModule *module, const char *name);

// Generate unique module prefix
char* module_gen_prefix(ModuleCache *cache);

// Set the module cache for a codegen context
void codegen_set_module_cache(CodegenContext *ctx, ModuleCache *cache);

#endif // HEMLOCK_CODEGEN_H
