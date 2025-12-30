/*
 * Hemlock C Code Generator
 *
 * Translates Hemlock AST to C source code.
 */

#ifndef HEMLOCK_CODEGEN_H
#define HEMLOCK_CODEGEN_H

#include "../../include/ast.h"
#include "type_infer.h"
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
    int scope_depth;      // Scope depth when this defer was added
};

// Closure information for anonymous functions
struct ClosureInfo {
    char *func_name;        // Generated function name
    char **captured_vars;   // Names of captured variables
    int num_captured;       // Number of captured variables
    int *shared_env_indices;  // Indices into shared env for each captured var, or NULL if not using shared env
    Expr *func_expr;        // The function expression
    CompiledModule *source_module;  // Module where closure was defined (for function resolution)
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
    int is_function;        // Whether this export is a function
    int num_params;         // Number of parameters (for functions)
} ExportedSymbol;

// Import binding: maps local name to mangled name
typedef struct {
    char *local_name;       // Name used in this module (e.g., "plus" if aliased)
    char *original_name;    // Original export name (e.g., "multiply")
    char *module_prefix;    // Module prefix (e.g., "_mod1_")
    int is_function;        // Whether this is a function binding
    int num_params;         // Number of parameters (for functions)
    int is_extern;          // Whether this is an extern FFI function
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
    int defer_scope_depth;    // Current scope depth for defer tracking

    // Current closure being generated (for mutable captured variable support)
    ClosureInfo *current_closure;  // NULL if not in a closure body

    // Shared environment support (for multiple closures capturing same variables)
    char *shared_env_name;          // Name of shared environment variable (e.g., "_shared_env_5")
    char **shared_env_vars;         // Variables in the shared environment
    int shared_env_num_vars;        // Number of variables in shared environment
    int shared_env_capacity;        // Capacity of shared_env_vars array

    // Last created closure (for self-reference fixup in let statements)
    int last_closure_env_id;       // -1 if no closure, otherwise the env counter
    char **last_closure_captured;  // Captured variable names
    int last_closure_num_captured; // Number of captured variables

    // Module support
    ModuleCache *module_cache;          // Cache of compiled modules
    CompiledModule *current_module;     // Module currently being compiled (NULL for main)

    // Main file top-level variables (to add prefix and avoid C name conflicts)
    char **main_vars;           // List of top-level variable names in main file
    int num_main_vars;          // Count of main file variables
    int main_vars_capacity;     // Capacity of main_vars array

    // Main file function definitions (subset of main_vars that are actual function defs)
    char **main_funcs;          // List of top-level function names in main file
    int *main_func_params;      // Number of parameters for each main file function
    int *main_func_has_rest;    // Whether each function has rest param (...args)
    int num_main_funcs;         // Count of main file functions
    int main_funcs_capacity;    // Capacity of main_funcs array

    // Main file imports (for function call resolution)
    ImportBinding *main_imports;  // Import bindings for main file
    int num_main_imports;         // Count of main file imports
    int main_imports_capacity;    // Capacity of main_imports array

    // Shadow locals (like catch params that shadow main vars)
    char **shadow_vars;           // Variables that shadow main vars (use bare name)
    int num_shadow_vars;          // Count of shadow variables
    int shadow_vars_capacity;     // Capacity of shadow_vars array

    // Const variable tracking (for preventing reassignment)
    char **const_vars;            // List of const variable names
    int num_const_vars;           // Count of const variables
    int const_vars_capacity;      // Capacity of const_vars array

    // Try-finally support (for return/break/continue to jump to finally first)
    int try_finally_depth;        // Current nesting depth of try-finally blocks
    char **finally_labels;        // Stack of finally labels (for goto)
    char **return_value_vars;     // Stack of return value variable names
    char **has_return_vars;       // Stack of "has return" flag variable names
    int try_finally_capacity;     // Capacity of the stacks

    // Loop tracking (for runtime defer support)
    int loop_depth;               // Current loop nesting depth (0 = not in loop)

    // Switch tracking (for break/continue handling)
    int switch_depth;             // Current switch nesting depth (0 = not in switch)
    char **switch_end_labels;     // Stack of switch end labels (for break -> goto)
    int switch_end_capacity;      // Capacity of switch_end_labels stack

    // For-loop continue tracking (continue jumps to increment, not condition)
    char **for_continue_labels;   // Stack of for-loop continue labels
    int for_continue_depth;       // Current for-loop nesting depth
    int for_continue_capacity;    // Capacity of for_continue_labels stack

    // Type inference (for optimized code generation)
    TypeInferContext *type_ctx;   // Type inference context
    int optimize;                 // Optimization level (0 = none, 1+ = optimize)

    // Defer optimization tracking
    int has_defers;               // Whether any defer statements exist in current function

    // Tail call optimization tracking
    char *tail_call_func_name;    // Current function name if tail-recursive, NULL otherwise
    char *tail_call_label;        // Label for tail call goto, NULL if not tail-recursive
    Expr *tail_call_func_expr;    // Function expression for param access

    // Error tracking
    int error_count;              // Number of compilation errors
    int warning_count;            // Number of compilation warnings
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

// Generate C code for an expression with type info
// Returns temp var name and sets *out_type to inferred type
char* codegen_expr_typed(CodegenContext *ctx, Expr *expr, InferredType *out_type);

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

// Helper: Report a compilation error (prints to stderr, increments error_count)
void codegen_error(CodegenContext *ctx, int line, const char *fmt, ...);

// Helper: Report a compilation warning (prints to stderr, increments warning_count)
void codegen_warning(CodegenContext *ctx, int line, const char *fmt, ...);

// Helper: Add a local variable to the tracking list
void codegen_add_local(CodegenContext *ctx, const char *name);

// Helper: Check if a variable is local
int codegen_is_local(CodegenContext *ctx, const char *name);

// Helper: Escape a string for C output
char* codegen_escape_string(const char *str);

// Helper: Sanitize an identifier to avoid C keyword conflicts
// Returns "_v_<name>" if name is a C keyword, otherwise returns a copy of name
// Caller must free the returned string
char* codegen_sanitize_ident(const char *name);

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
// is_function: 1 if this is a function, 0 otherwise
// num_params: number of parameters (only meaningful if is_function is 1)
void module_add_export(CompiledModule *module, const char *name, const char *mangled_name, int is_function, int num_params);

// Find an export in a module by name
ExportedSymbol* module_find_export(CompiledModule *module, const char *name);

// Generate unique module prefix
char* module_gen_prefix(ModuleCache *cache);

// Set the module cache for a codegen context
void codegen_set_module_cache(CodegenContext *ctx, ModuleCache *cache);

#endif // HEMLOCK_CODEGEN_H
