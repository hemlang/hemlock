/*
 * Tricycle - Hemlock's Memory Safety Checker
 *
 * "Training wheels for the unsafe. Take them off when you're ready."
 *
 * Tricycle is an opt-in static analyzer that proves memory safety properties
 * for Hemlock programs. It provides:
 *   - Double-free detection
 *   - Use-after-free detection
 *   - Memory leak detection
 *   - Null pointer safety
 *   - Async lifetime safety
 *   - Ownership tracking
 *
 * Tricycle does NOT change Hemlock's semantics. It's a separate tool that
 * rejects programs it can't prove safe while letting unsafe programs compile
 * normally with hemlock/hemlockc.
 */

#ifndef HEMLOCK_TRICYCLE_H
#define HEMLOCK_TRICYCLE_H

#include "frontend/ast.h"
#include "compiler/type_check.h"

// ========== VERSION ==========

#define TRICYCLE_VERSION_MAJOR 0
#define TRICYCLE_VERSION_MINOR 1
#define TRICYCLE_VERSION_PATCH 0
#define TRICYCLE_VERSION "0.1.0"

// ========== FORWARD DECLARATIONS ==========

typedef struct TricycleContext TricycleContext;
typedef struct OwnershipEnv OwnershipEnv;
typedef struct OwnershipBinding OwnershipBinding;
typedef struct MemoryState MemoryState;
typedef struct BorrowInfo BorrowInfo;
typedef struct TricycleDiagnostic TricycleDiagnostic;
typedef struct RelatedLocation RelatedLocation;

// ========== MEMORY STATE ==========

// Represents the allocation state of a pointer-typed variable
typedef enum {
    MEM_UNALLOCATED,    // Not allocated (or after free)
    MEM_ALLOCATED,      // Valid, owned - after alloc()/buffer()
    MEM_MOVED,          // Ownership transferred elsewhere
    MEM_BORROWED,       // Reference to owned memory (Phase 2)
    MEM_UNKNOWN,        // External/unknown origin - conservatively unsafe
} MemoryStateKind;

// Full memory state information for a pointer
struct MemoryState {
    MemoryStateKind kind;
    int allocation_line;        // Where allocated (for diagnostics)
    int allocation_column;      // Column of allocation
    int free_line;              // Where freed (if applicable)
    int free_column;            // Column of free
    int move_line;              // Where moved (if applicable)
    int move_column;            // Column of move
    char *owner_name;           // Variable that owns this memory
    char *moved_to;             // If MOVED, which variable now owns it
    int borrow_count;           // Number of active borrows (Phase 2)
};

// ========== OWNERSHIP ENVIRONMENT ==========

// Parameter ownership modifiers (Phase 2)
typedef enum {
    OWNER_DEFAULT,      // Default: borrow for pointers
    OWNER_OWN,          // Takes ownership (own keyword)
    OWNER_BORROW,       // Borrows (borrow keyword)
    OWNER_BORROW_MUT,   // Mutable borrow (borrow mut keyword)
} OwnershipKind;

// Scope kind for tracking context
typedef enum {
    SCOPE_GLOBAL,       // Top-level scope
    SCOPE_FUNCTION,     // Function body
    SCOPE_BLOCK,        // Regular block {}
    SCOPE_LOOP,         // Loop body (while, for, loop)
    SCOPE_IF,           // If/else branch
    SCOPE_TRY,          // Try block
    SCOPE_ASYNC,        // Spawned task body
} ScopeKind;

// Variable binding in the ownership environment
struct OwnershipBinding {
    char *name;                 // Variable name
    MemoryState state;          // Current memory state
    int is_parameter;           // Came from function parameter?
    OwnershipKind param_kind;   // OWN, BORROW, BORROW_MUT (for params)
    int declaration_line;       // Line where declared
    int declaration_column;     // Column where declared
    int is_ptr_type;            // Is this a pointer type?
    struct OwnershipBinding *next;  // Linked list
};

// Scoped ownership environment
struct OwnershipEnv {
    OwnershipBinding *bindings; // Linked list of bindings in this scope
    struct OwnershipEnv *parent; // Enclosing scope
    ScopeKind scope_kind;       // What kind of scope is this?
    char *scope_label;          // Optional label (for labeled loops)
    int scope_start_line;       // Line where scope started
};

// ========== ASYNC BORROW TRACKING ==========

// Track pointers passed to spawned tasks
typedef struct AsyncBorrow {
    char *var_name;             // Variable name borrowed
    int spawn_line;             // Line of spawn() call
    int spawn_column;           // Column of spawn() call
    int task_id;                // Internal task identifier
    int is_joined;              // Whether join() has been called
    struct AsyncBorrow *next;   // Linked list
} AsyncBorrow;

// ========== DIAGNOSTICS ==========

// Diagnostic severity levels
typedef enum {
    TRIC_ERROR,         // Definite memory error - must fix
    TRIC_WARNING,       // Potential issue - should review
    TRIC_NOTE,          // Additional context information
} TricycleSeverity;

// Error categories
typedef enum {
    TRIC_DOUBLE_FREE,           // Memory freed twice
    TRIC_USE_AFTER_FREE,        // Use of freed memory
    TRIC_USE_AFTER_MOVE,        // Use of moved value
    TRIC_MEMORY_LEAK,           // Allocated memory not freed
    TRIC_NULL_DEREF,            // Possible null dereference
    TRIC_DANGLING_ASYNC,        // Pointer freed while task running
    TRIC_BORROW_OUTLIVES_OWNER, // Borrow outlives owner (Phase 2)
    TRIC_MOVE_OF_BORROWED,      // Moving borrowed value (Phase 2)
    TRIC_FREE_OF_BORROWED,      // Freeing borrowed value (Phase 2)
    TRIC_UNKNOWN_LIFETIME,      // Cannot determine lifetime
    TRIC_INVALID_OWNERSHIP,     // Invalid ownership operation
} TricycleErrorKind;

// Related location for multi-location diagnostics
struct RelatedLocation {
    int line;
    int column;
    char *message;              // e.g., "allocated here", "freed here"
};

// Full diagnostic information
struct TricycleDiagnostic {
    TricycleSeverity severity;
    TricycleErrorKind kind;
    int line;
    int column;
    int end_column;             // For range highlighting
    char *message;              // Main error message
    char *hint;                 // Suggested fix
    RelatedLocation *related;   // Related locations (allocated here, freed here, etc.)
    int related_count;
    struct TricycleDiagnostic *next;  // Linked list
};

// ========== TRICYCLE CONTEXT ==========

struct TricycleContext {
    // Input
    Stmt **program;             // AST from parser
    int stmt_count;             // Number of top-level statements
    TypeCheckContext *type_ctx; // Type information (from type checker)
    const char *filename;       // Source filename
    const char *source;         // Source code (for column calculation)

    // Analysis state
    OwnershipEnv *current_env;  // Current ownership scope
    AsyncBorrow *async_borrows; // Pointers borrowed by async tasks

    // Output
    TricycleDiagnostic *diagnostics;  // Head of diagnostics list
    TricycleDiagnostic *diagnostics_tail;  // Tail for O(1) append
    int error_count;            // Number of errors
    int warning_count;          // Number of warnings

    // Configuration
    int strict_mode;            // Treat warnings as errors
    int infer_ownership;        // Auto-infer vs require annotations
    int verbose;                // Verbose output mode
    int collect_all;            // Collect all diagnostics vs stop at first error
};

// ========== CONTEXT MANAGEMENT ==========

// Create a new Tricycle context
TricycleContext* tricycle_new(const char *filename);

// Free a Tricycle context and all associated memory
void tricycle_free(TricycleContext *ctx);

// Set the source code (for column calculation in diagnostics)
void tricycle_set_source(TricycleContext *ctx, const char *source);

// Set the type checking context (optional, enables type-aware analysis)
void tricycle_set_type_ctx(TricycleContext *ctx, TypeCheckContext *type_ctx);

// ========== ENVIRONMENT OPERATIONS ==========

// Push a new scope onto the ownership environment
void tricycle_push_scope(TricycleContext *ctx, ScopeKind kind);

// Push a labeled scope (for labeled loops)
void tricycle_push_scope_labeled(TricycleContext *ctx, ScopeKind kind, const char *label);

// Pop the current scope and check for leaks
void tricycle_pop_scope(TricycleContext *ctx);

// Bind a variable in the current scope
void tricycle_bind(TricycleContext *ctx, const char *name, MemoryStateKind state,
                   int line, int column, int is_ptr_type);

// Bind a function parameter with ownership kind
void tricycle_bind_param(TricycleContext *ctx, const char *name, OwnershipKind kind,
                         int line, int column, int is_ptr_type);

// Look up a variable's ownership binding (searches parent scopes)
OwnershipBinding* tricycle_lookup(TricycleContext *ctx, const char *name);

// Update a variable's memory state
void tricycle_update_state(TricycleContext *ctx, const char *name,
                           MemoryStateKind new_state, int line, int column);

// Mark a variable as moved to another variable
void tricycle_mark_moved(TricycleContext *ctx, const char *from, const char *to,
                         int line, int column);

// ========== ANALYSIS ENTRY POINTS ==========

// Analyze a complete program
// Returns the number of memory safety errors found
int tricycle_analyze_program(TricycleContext *ctx, Stmt **stmts, int stmt_count);

// Analyze a single statement
void tricycle_analyze_stmt(TricycleContext *ctx, Stmt *stmt);

// Analyze an expression and check for memory safety issues
void tricycle_analyze_expr(TricycleContext *ctx, Expr *expr);

// ========== SPECIFIC ANALYSIS FUNCTIONS ==========

// Check for double-free
void tricycle_check_free(TricycleContext *ctx, Expr *ptr_expr, int line, int column);

// Check for use-after-free/move
void tricycle_check_use(TricycleContext *ctx, const char *var_name, int line, int column);

// Check for memory leaks at scope exit
void tricycle_check_leaks(TricycleContext *ctx);

// Check async safety (spawn/join)
void tricycle_check_spawn(TricycleContext *ctx, Expr *call_expr, int line, int column);
void tricycle_check_join(TricycleContext *ctx, Expr *task_expr, int line, int column);

// ========== DIAGNOSTICS ==========

// Add an error diagnostic
void tricycle_error(TricycleContext *ctx, TricycleErrorKind kind,
                    int line, int column, const char *fmt, ...);

// Add a warning diagnostic
void tricycle_warning(TricycleContext *ctx, TricycleErrorKind kind,
                      int line, int column, const char *fmt, ...);

// Add a note (additional context)
void tricycle_note(TricycleContext *ctx, int line, int column, const char *fmt, ...);

// Add a related location to the most recent diagnostic
void tricycle_add_related(TricycleContext *ctx, int line, int column, const char *message);

// Add a hint to the most recent diagnostic
void tricycle_add_hint(TricycleContext *ctx, const char *hint);

// Get the diagnostic list
TricycleDiagnostic* tricycle_get_diagnostics(TricycleContext *ctx);

// Print diagnostics to stderr
void tricycle_print_diagnostics(TricycleContext *ctx);

// Print diagnostics as JSON (for IDE integration)
void tricycle_print_diagnostics_json(TricycleContext *ctx);

// Free all diagnostics
void tricycle_free_diagnostics(TricycleContext *ctx);

// ========== ERROR CODE HELPERS ==========

// Get error code string (e.g., "TRIC001")
const char* tricycle_error_code(TricycleErrorKind kind);

// Get error name string (e.g., "DOUBLE_FREE")
const char* tricycle_error_name(TricycleErrorKind kind);

// Get severity string (e.g., "error", "warning", "note")
const char* tricycle_severity_str(TricycleSeverity severity);

// ========== UTILITY FUNCTIONS ==========

// Check if an expression is a pointer type
int tricycle_is_ptr_expr(TricycleContext *ctx, Expr *expr);

// Check if a type is a pointer type
int tricycle_is_ptr_type(Type *type);

// Get the variable name from an expression (if it's a simple identifier)
const char* tricycle_get_var_name(Expr *expr);

// Check if a function call is an allocation function (alloc, buffer)
int tricycle_is_alloc_call(Expr *call_expr);

// Check if a function call is a deallocation function (free)
int tricycle_is_free_call(Expr *call_expr);

// Check if a function call is spawn
int tricycle_is_spawn_call(Expr *call_expr);

// Check if a function call is join
int tricycle_is_join_call(Expr *call_expr);

// Check if a function call is a pointer operation (ptr_read_*, ptr_write_*, etc.)
int tricycle_is_ptr_op(Expr *call_expr);

// Clone a memory state
MemoryState tricycle_clone_state(const MemoryState *state);

// Free memory state resources (owner_name, moved_to)
void tricycle_free_state(MemoryState *state);

#endif // HEMLOCK_TRICYCLE_H
