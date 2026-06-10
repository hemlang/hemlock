/*
 * Hemlock Code Generator - Internal Declarations
 *
 * Shared internal declarations for the modular codegen implementation.
 * This header is only included by codegen implementation files.
 */

#ifndef HEMLOCK_CODEGEN_INTERNAL_H
#define HEMLOCK_CODEGEN_INTERNAL_H

#define _GNU_SOURCE
#include "codegen.h"
#include "frontend.h"
#include "hemlock_limits.h"
#include "../../../runtime/include/hemlock_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <libgen.h>
#include <math.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// ========== BUFFER SIZE CONSTANTS ==========

// Buffer size for mangled names (module prefix + symbol name)
#define CODEGEN_MANGLED_NAME_SIZE HML_MANGLED_NAME_SIZE

// Buffer size for environment variable names
#define CODEGEN_ENV_NAME_SIZE HML_GENERATED_NAME_SIZE

// ========== IN-MEMORY BUFFER SUPPORT ==========

// In-memory buffer for code generation (replaces tmpfile for better performance)
typedef struct {
    char *data;      // Buffer data (dynamically allocated by open_memstream)
    size_t size;     // Current size of data
    FILE *stream;    // Stream for writing (NULL after close)
} MemBuffer;

// Create a new in-memory buffer
MemBuffer* membuf_new(void);

// Flush buffer contents to a FILE* (can be called multiple times)
void membuf_flush_to(MemBuffer *buf, FILE *output);

// Close and free the buffer
void membuf_free(MemBuffer *buf);

// ========== FUNCTION GENERATION STATE ==========

// State saved when entering a function body (for nested functions and closures)
typedef struct {
    int num_locals;           // Saved ctx->num_locals
    DeferEntry *defer_stack;  // Saved ctx->defer_stack
    int in_function;          // Saved ctx->in_function
    int has_defers;           // Saved ctx->has_defers
    int defer_unwind_active;  // Saved ctx->defer_unwind_active
    CompiledModule *module;   // Saved ctx->current_module (for closures)
    ClosureInfo *closure;     // Saved ctx->current_closure
    Scope *scope;             // Saved ctx->current_scope
    const char *tail_call_func_name;  // Saved tail call optimization state
    char *tail_call_label;
    Expr *tail_call_func_expr;
    int locals_body_start;    // Saved ctx->locals_body_start
    int loop_body_depth;      // Saved ctx->loop_body_depth
    int loop_depth;           // Saved ctx->loop_depth
    int try_body_depth;       // Saved ctx->try_body_depth
    // Saved ctx->func_params et al. Without restoring these, a
    // function's parameter names leak into the enclosing / top-level
    // scope's codegen_is_func_param() checks, which silently disables
    // the while-loop unboxing optimisation for an unrelated top-level
    // loop counter that happens to share a name: e.g. defining
    // `fn f(n: i64)` makes a later top-level `while (n < 5) { n = n+1 }`
    // read the never-updated boxed _main_n and spin forever.
    char **func_params;       // Saved ctx->func_params
    int num_func_params;      // Saved ctx->num_func_params
    int *func_param_is_ref;   // Saved ctx->func_param_is_ref
} FuncGenState;

// Save function generation state before entering a function body
void funcgen_save_state(CodegenContext *ctx, FuncGenState *state);

// Restore function generation state after exiting a function body
void funcgen_restore_state(CodegenContext *ctx, FuncGenState *state);

// Add function parameters as locals
void funcgen_add_params(CodegenContext *ctx, Expr *func);

// Check if a variable name is a function parameter (cannot be unboxed)
int codegen_is_func_param(CodegenContext *ctx, const char *name);

// Apply default values for optional parameters
void funcgen_apply_defaults(CodegenContext *ctx, Expr *func);

// Scan for closures and create shared environment if needed
void funcgen_setup_shared_env(CodegenContext *ctx, Expr *func, ClosureInfo *closure);

// Generate function body statements
void funcgen_generate_body(CodegenContext *ctx, Expr *func);

// ========== INTERNAL HELPER FUNCTIONS ==========

// Mark a local variable as not needing cleanup (e.g., unboxed to C primitive)
void codegen_mark_local_no_cleanup(CodegenContext *ctx, const char *name);

// Emit hml_release() calls for all body-local variables that need cleanup
// skip_var: variable name to skip (e.g., the return value), or NULL
void codegen_emit_local_cleanup(CodegenContext *ctx, const char *skip_var);

// Remove a local variable from scope (used for catch params that go out of scope)
void codegen_remove_local(CodegenContext *ctx, const char *name);

// Shadow variable tracking (locals that shadow main vars, like catch params)
void codegen_add_shadow(CodegenContext *ctx, const char *name);
int codegen_is_shadow(CodegenContext *ctx, const char *name);
void codegen_remove_shadow(CodegenContext *ctx, const char *name);

// Const variable tracking (for preventing reassignment)
void codegen_add_const(CodegenContext *ctx, const char *name);
int codegen_is_const(CodegenContext *ctx, const char *name);

// Consumed temporary tracking (avoid double-release when values are moved)
void codegen_mark_temp_consumed(CodegenContext *ctx, const char *name);
int codegen_is_temp_consumed(CodegenContext *ctx, const char *name);
int codegen_consumed_checkpoint(CodegenContext *ctx);
void codegen_restore_consumed(CodegenContext *ctx, int checkpoint);

// Try-finally context tracking
void codegen_push_try_finally(CodegenContext *ctx, const char *finally_label,
                              const char *return_value_var, const char *has_return_var);
void codegen_pop_try_finally(CodegenContext *ctx);
const char* codegen_get_finally_label(CodegenContext *ctx);
const char* codegen_get_return_value_var(CodegenContext *ctx);
const char* codegen_get_has_return_var(CodegenContext *ctx);

// Switch context tracking (for break -> goto)
void codegen_push_switch(CodegenContext *ctx, const char *end_label);
void codegen_pop_switch(CodegenContext *ctx);
const char* codegen_get_switch_end_label(CodegenContext *ctx);

// For-loop continue tracking (continue -> goto increment label)
void codegen_push_for_continue(CodegenContext *ctx, const char *continue_label);
void codegen_pop_for_continue(CodegenContext *ctx);
const char* codegen_get_for_continue_label(CodegenContext *ctx);

// Main file variable tracking
void codegen_add_main_var(CodegenContext *ctx, const char *name);
int codegen_is_main_var(CodegenContext *ctx, const char *name);

// Main file function definitions
void codegen_add_main_func(CodegenContext *ctx, const char *name, int num_params, int has_rest, int *param_is_ref, Expr *func_ast);
int codegen_is_main_func(CodegenContext *ctx, const char *name);
int codegen_get_main_func_params(CodegenContext *ctx, const char *name);
int codegen_get_main_func_has_rest(CodegenContext *ctx, const char *name);
int* codegen_get_main_func_param_is_ref(CodegenContext *ctx, const char *name);
Expr* codegen_get_main_func_ast(CodegenContext *ctx, const char *name);
int codegen_is_main_func_inlinable(CodegenContext *ctx, const char *name);
int codegen_is_ref_param(CodegenContext *ctx, const char *name);

// Extern (FFI) function tracking - names that have hml_fn_ wrappers generated
void codegen_add_extern_fn(CodegenContext *ctx, const char *name);
int codegen_is_extern_fn(CodegenContext *ctx, const char *name);

// Format an f64 constant as a valid C expression (handles inf/nan/-0.0)
void codegen_format_f64(char *buf, size_t size, double value);

// Main file import tracking
void codegen_add_main_import(CodegenContext *ctx, const char *local_name,
                             const char *original_name, const char *module_prefix,
                             int is_function, int num_params, int is_extern);
ImportBinding* codegen_find_main_import(CodegenContext *ctx, const char *name);

// ========== SHARED ENVIRONMENT SUPPORT ==========

// Add a variable to the shared environment (returns index)
int shared_env_add_var(CodegenContext *ctx, const char *var);

// Get index of a variable in shared environment (-1 if not found)
int shared_env_get_index(CodegenContext *ctx, const char *var);

// Clear shared environment state
void shared_env_clear(CodegenContext *ctx);

// Count required parameters (those without defaults)
int count_required_params(Expr **param_defaults, int num_params);

// ========== CLOSURE SCANNING ==========

// Scan expression for closures and collect captured variables
void scan_closures_expr(CodegenContext *ctx, Expr *expr, Scope *local_scope);

// Scan statement for closures
void scan_closures_stmt(CodegenContext *ctx, Stmt *stmt, Scope *local_scope);

// ========== MODULE FUNCTIONS ==========

// Find an import binding in a module
ImportBinding* module_find_import(CompiledModule *module, const char *name);

// Add an import to a module
void module_add_import(CompiledModule *module, const char *local_name,
                       const char *original_name, const char *module_prefix,
                       int is_function, int num_params, int is_extern);

// Check if a name is an extern function in the given module
int module_is_extern_fn(CompiledModule *module, const char *name);

// ========== CLOSURE GENERATION ==========

// Generate a closure function implementation
void codegen_closure_impl(CodegenContext *ctx, ClosureInfo *closure);

// Generate wrapper function for closure
void codegen_closure_wrapper(CodegenContext *ctx, ClosureInfo *closure);

// ========== FUNCTION GENERATION ==========

// Generate a top-level function declaration
void codegen_function_decl(CodegenContext *ctx, Expr *func, const char *name, Annotation **annotations, int annotation_count, int is_export);

// Check if a statement is a function definition
int is_function_def(Stmt *stmt, char **name_out, Expr **func_out);

// Generate module init function
void codegen_module_init(CodegenContext *ctx, CompiledModule *module);

// Generate function declarations for a module
void codegen_module_funcs(CodegenContext *ctx, CompiledModule *module,
                          MemBuffer *decl_buffer, MemBuffer *impl_buffer);

// ========== MODULE COMPILATION ==========

// Parse a module file
Stmt** parse_module_file(const char *path, int *stmt_count);

// ========== PATTERN MATCHING CODEGEN ==========

// Generate code to match a pattern against a value, jumping to fail_label if no match
// Creates local variables for any bindings in the pattern
void codegen_pattern_match(CodegenContext *ctx, Pattern *pattern, const char *scrutinee, const char *fail_label);

// ========== TYPE MAPPING HELPERS ==========

// Convert TypeKind to HML_VAL_* string (e.g., TYPE_I8 -> "HML_VAL_I8")
// Returns NULL for types that don't have a direct HML_VAL mapping
const char* type_kind_to_hml_val(TypeKind kind);

// Convert TypeKind to HML_FFI_* string (e.g., TYPE_I8 -> "HML_FFI_I8")
// Returns "HML_FFI_VOID" for unknown types
const char* type_kind_to_ffi_type(TypeKind kind);

// ========== UNBOXED TYPE HELPERS ==========

// Convert CheckedTypeKind to C type string (e.g., CHECKED_I32 -> "int32_t")
// Returns NULL for types that can't be unboxed
const char* checked_type_to_c_type(CheckedTypeKind kind);

// Convert CheckedTypeKind to hml_val_* constructor (e.g., CHECKED_I32 -> "hml_val_i32")
// Returns NULL for types that can't be unboxed
const char* checked_type_to_box_func(CheckedTypeKind kind);

// Convert CheckedTypeKind to hml_to_* extractor (e.g., CHECKED_I32 -> "hml_to_i32")
// Returns NULL for types that need casts (use checked_type_to_unbox_cast instead)
const char* checked_type_to_unbox_func(CheckedTypeKind kind);

// Get the cast wrapper for unboxing (e.g., CHECKED_I8 -> "(int8_t)hml_to_i32")
// This handles all types including those that need casts
const char* checked_type_to_unbox_cast(CheckedTypeKind kind);

// Check if CheckedTypeKind is a numeric type that can use direct C arithmetic
int checked_kind_is_numeric(CheckedTypeKind kind);

// Check if CheckedTypeKind is an integer type
int checked_kind_is_integer(CheckedTypeKind kind);

// Check if CheckedTypeKind is a floating point type
int checked_kind_is_float(CheckedTypeKind kind);

// ========== FFI HELPERS ==========

// Sanitize a library path (e.g. "libsqlite3.so.0") into a C identifier suffix
// (e.g. "libsqlite3_so_0"). Writes at most `out_size` bytes including the NUL.
// Used by both the IMPORT_FFI codegen and the extern-fn wrapper codegen so they
// agree on which `_ffi_lib_<name>` global to read/write.
void codegen_ffi_sanitize_libname(const char *path, char *out, size_t out_size);

#endif // HEMLOCK_CODEGEN_INTERNAL_H
