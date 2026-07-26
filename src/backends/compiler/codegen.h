/*
 * Hemlock C Code Generator
 *
 * Translates Hemlock AST to C source code.
 */

#ifndef HEMLOCK_CODEGEN_H
#define HEMLOCK_CODEGEN_H

#include "frontend/ast.h"
#include "../../include/modules.h"
#include "compiler/type_check.h"
#include <stdio.h>

// Forward declaration for closure info
typedef struct ClosureInfo ClosureInfo;
typedef struct DeferEntry DeferEntry;
typedef struct CompiledModule CompiledModule;
typedef struct CompilerModuleCache CompilerModuleCache;

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
    CMOD_UNLOADED,
    CMOD_LOADING,
    CMOD_LOADED
} CompilerModuleState;

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
    CompilerModuleState state;  // Loading state for cycle detection
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
struct CompilerModuleCache {
    CompiledModule *modules;    // Linked list of modules
    ModuleResolution *resolver; // Shared module resolution context
    ModuleCacheMap cache_map;   // Shared module cache
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
    int inline_depth;       // Current inlining depth (0 = not inlining, max depth prevents code bloat)
    char **local_vars;      // Stack of local variable names
    const char **local_annots; // Declared primitive annotation per local ("HML_VAL_*" or NULL)
    char **main_annot_names;   // Annotated main-level variable names
    const char **main_annot_types; // Their HML_VAL_* enum names
    int num_main_annots;
    int main_annot_capacity;
    int *local_needs_cleanup; // Parallel array: 1 = needs hml_release at function exit
    int num_locals;         // Number of local variables
    int local_capacity;     // Capacity of local vars array
    int locals_body_start;  // Index where body-locals begin (after params + captures)

    // Consumed temporary tracking (to avoid double-release)
    char **consumed_temps;     // List of consumed temp/short-lived variable names
    int num_consumed_temps;    // Count of consumed temps
    int consumed_temps_capacity;  // Capacity of consumed_temps array

    // Closure support
    Scope *current_scope;   // Current variable scope
    ClosureInfo *closures;  // List of closures to generate
    char **func_params;     // Current function parameters
    int num_func_params;    // Number of current function parameters
    int *func_param_is_ref; // Which params are ref (pass-by-reference)

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
    CompilerModuleCache *module_cache;  // Cache of compiled modules
    CompiledModule *current_module;     // Module currently being compiled (NULL for main)

    // Main file top-level variables (to add prefix and avoid C name conflicts)
    char **main_vars;           // List of top-level variable names in main file
    int num_main_vars;          // Count of main file variables
    int main_vars_capacity;     // Capacity of main_vars array

    // Main file function definitions (subset of main_vars that are actual function defs)
    char **main_funcs;          // List of top-level function names in main file
    int *main_func_params;      // Number of parameters for each main file function
    int *main_func_has_rest;    // Whether each function has rest param (...args)
    int **main_func_param_is_ref;  // Array of param_is_ref arrays for each main file function
    Expr **main_func_ast;       // AST expression for each function (for inlining)
    int *main_func_inlinable;   // Whether each function is inlinable
    int num_main_funcs;         // Count of main file functions
    int main_funcs_capacity;    // Capacity of main_funcs array

    // Main file imports (for function call resolution)
    ImportBinding *main_imports;  // Import bindings for main file
    int num_main_imports;         // Count of main file imports
    int main_imports_capacity;    // Capacity of main_imports array

    // Extern (FFI) function names with generated hml_fn_ wrappers
    char **extern_fns;            // List of extern fn names
    int num_extern_fns;           // Count of extern fns
    int extern_fns_capacity;      // Capacity of extern_fns array

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

    // Try-body tracking (for return/break/continue to pop pushed exception
    // contexts when escaping out of a try block, so that a later throw on the
    // same thread does not longjmp into a dead stack frame).
    int try_body_depth;           // Number of enclosing try-bodies currently being emitted

    // Loop tracking (for runtime defer support)
    int loop_depth;               // Current loop nesting depth (0 = not in loop)

    // Loop body locals tracking (for break/continue cleanup of block-scoped vars)
    int *loop_body_locals;        // Stack of num_locals at loop body entry
    int *loop_body_try_depth;     // Stack of try_body_depth at loop body entry
    int loop_body_depth;          // Current loop body nesting depth
    int loop_body_capacity;       // Capacity of loop_body_locals stack

    // Switch tracking (for break/continue handling)
    int switch_depth;             // Current switch nesting depth (0 = not in switch)
    char **switch_end_labels;     // Stack of switch end labels (for break -> goto)
    int switch_end_capacity;      // Capacity of switch_end_labels stack

    // For-loop continue tracking (continue jumps to increment, not condition)
    char **for_continue_labels;   // Stack of for-loop continue labels
    int for_continue_depth;       // Current for-loop nesting depth
    int for_continue_capacity;    // Capacity of for_continue_labels stack

    // Loop label tracking (for labeled break/continue)
    char **loop_labels;           // Stack of loop labels (user-defined)
    char **loop_break_labels;     // Stack of generated break target labels
    char **loop_continue_labels;  // Stack of generated continue target labels
    int *loop_label_body_locals;  // Stack of num_locals at labeled loop body entry
    int *loop_label_try_depth;    // Stack of try_body_depth at labeled loop body entry
    int loop_label_depth;         // Current labeled loop nesting depth
    int loop_label_capacity;      // Capacity of loop label stacks

    // Type checking context (for optimized code generation)
    TypeCheckContext *type_ctx;   // Type check context (NULL if --no-type-check)
    int optimize;                 // Optimization level (0 = none, 1+ = optimize)
    int stack_check;              // Enable stack overflow checking (1 = on, 0 = off)

    // Defer optimization tracking
    int has_defers;               // Whether any defer statements exist in current function
    int defer_unwind_active;      // Whether the current function emitted a defer
                                  // frame (_defer_frame) + unwind exception context

    // Tail call optimization tracking
    const char *tail_call_func_name;    // Current function name if tail-recursive, NULL otherwise
    char *tail_call_label;        // Label for tail call goto, NULL if not tail-recursive
    Expr *tail_call_func_expr;    // Function expression for param access

    // Error tracking
    int error_count;              // Number of compilation errors
    int warning_count;            // Number of compilation warnings

    // Sandbox configuration
    int sandbox_flags;            // Sandbox restriction flags (0 = disabled)
    const char *sandbox_root;     // Sandbox root directory (NULL = no restriction)

    // WASM target configuration
    int target_wasm;              // Targeting WebAssembly via Emscripten (0 = native, 1 = wasm)

    // Source location tracking (for runtime error messages with source context)
    const char *source_code;      // Full source text (for embedding; NULL = unavailable)
    const char *source_file;      // Source file path (for [file:line] error prefix)
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
__attribute__((format(printf, 2, 3)))
void codegen_write(CodegenContext *ctx, const char *fmt, ...);

// Helper: Write a line with indentation
__attribute__((format(printf, 2, 3)))
void codegen_writeln(CodegenContext *ctx, const char *fmt, ...);

// Helper: Write a blank line (no indentation)
void codegen_blank_line(CodegenContext *ctx);

// Helper: Report a compilation error (prints to stderr, increments error_count)
__attribute__((format(printf, 3, 4)))
void codegen_error(CodegenContext *ctx, int line, const char *fmt, ...);

// Helper: Report a compilation warning (prints to stderr, increments warning_count)
__attribute__((format(printf, 3, 4)))
void codegen_warning(CodegenContext *ctx, int line, const char *fmt, ...);

// Helper: Add a local variable to the tracking list
void codegen_add_local(CodegenContext *ctx, const char *name);
void codegen_set_local_annot(CodegenContext *ctx, const char *name, const char *hml_type);
const char *codegen_get_local_annot(CodegenContext *ctx, const char *name);
void codegen_set_main_annot(CodegenContext *ctx, const char *name, const char *hml_type);
const char *codegen_get_main_annot(CodegenContext *ctx, const char *name);
const char *codegen_annot_primitive_name(Type *annotation);

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

// Mirror a write to a captured local into the function's shared environment
void codegen_sync_captured_var(CodegenContext *ctx, const char *hml_name, const char *c_name);
// Shared-env index for reads of a captured local (-1 = read the C local)
int codegen_captured_var_env_index(CodegenContext *ctx, const char *hml_name);

// Pre-scan a statement tree for defer statements (skips nested functions)
int codegen_body_has_defer(Stmt *stmt);

// Emit the per-function defer frame + unwind context (if the body has defers)
void codegen_emit_defer_prologue(CodegenContext *ctx, Stmt *body);

// Emit defer cleanup at an actual function exit (pop unwind context, run frame)
void codegen_emit_defer_exit(CodegenContext *ctx);

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

// ========== LOOP LABEL TRACKING ==========

// Push a labeled loop onto the stack
void codegen_push_loop_label(CodegenContext *ctx, const char *label, const char *break_label, const char *continue_label);

// Pop a labeled loop from the stack
void codegen_pop_loop_label(CodegenContext *ctx);

// Get the break label for a given user-defined label (returns NULL if not found)
const char* codegen_get_labeled_break(CodegenContext *ctx, const char *label);

// Get the continue label for a given user-defined label (returns NULL if not found)
const char* codegen_get_labeled_continue(CodegenContext *ctx, const char *label);

// ========== LOOP BODY LOCALS TRACKING ==========

// Push the current num_locals as the loop body start (call before loop body)
void codegen_push_loop_body(CodegenContext *ctx);

// Pop the loop body locals start (call after loop ends)
void codegen_pop_loop_body(CodegenContext *ctx);

// Emit hml_release for locals in the half-open index range [start, end).
void codegen_release_locals_range(CodegenContext *ctx, int start, int end);

// Emit cleanup for block-scoped locals before break/continue
void codegen_emit_break_cleanup(CodegenContext *ctx);

// Emit cleanup for block-scoped locals before labeled break/continue
void codegen_emit_labeled_break_cleanup(CodegenContext *ctx, const char *label);

// ========== TRY-BODY EXCEPTION CONTEXT POPS ==========
// Helpers used to pop exception contexts that were pushed by enclosing try
// blocks when control escapes the try-body via return/break/continue. This
// prevents a later throw from longjmp'ing into a dead stack frame.

// Emit `pops` hml_exception_pop() calls.
void codegen_emit_exception_pops(CodegenContext *ctx, int pops);

// Emit pops for all enclosing try-bodies in the current function (for return).
void codegen_emit_return_try_pops(CodegenContext *ctx);

// Emit pops for try-bodies that are nested inside the innermost loop (for break/continue).
void codegen_emit_break_try_pops(CodegenContext *ctx);

// Emit pops for try-bodies that are nested inside a labeled loop (for labeled break/continue).
void codegen_emit_labeled_break_try_pops(CodegenContext *ctx, const char *label);

// ========== MODULE COMPILATION ==========

// Initialize module cache
CompilerModuleCache* compiler_module_cache_new(const char *main_file_path);

// Free module cache
void compiler_module_cache_free(CompilerModuleCache *cache);

// Compile a module (recursively compiles dependencies)
CompiledModule* module_compile(CodegenContext *ctx, const char *absolute_path);

// Get a cached module by path
CompiledModule* module_get_cached(CompilerModuleCache *cache, const char *absolute_path);

// Add an export to a module
// is_function: 1 if this is a function, 0 otherwise
// num_params: number of parameters (only meaningful if is_function is 1)
void module_add_export(CompiledModule *module, const char *name, const char *mangled_name, int is_function, int num_params);

// Find an export in a module by name
ExportedSymbol* module_find_export(CompiledModule *module, const char *name);

// Generate unique module prefix
char* module_gen_prefix(CompilerModuleCache *cache);

// Set the module cache for a codegen context
void codegen_set_module_cache(CodegenContext *ctx, CompilerModuleCache *cache);

#endif // HEMLOCK_CODEGEN_H
