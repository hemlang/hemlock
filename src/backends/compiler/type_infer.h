/*
 * Type Inference for Hemlock Compiler
 *
 * Performs static type analysis to enable optimized code generation.
 * When types are known at compile time, we can emit direct C operations
 * instead of runtime type dispatch.
 */

#ifndef HEMLOCK_TYPE_INFER_H
#define HEMLOCK_TYPE_INFER_H

#include "../../include/ast.h"

// Inferred type kinds
typedef enum {
    INFER_UNKNOWN,      // Type not yet inferred / can be anything
    INFER_I8,           // Known to be i8
    INFER_I16,          // Known to be i16
    INFER_I32,          // Known to be i32
    INFER_I64,          // Known to be i64
    INFER_U8,           // Known to be u8
    INFER_U16,          // Known to be u16
    INFER_U32,          // Known to be u32
    INFER_U64,          // Known to be u64
    INFER_F32,          // Known to be f32
    INFER_F64,          // Known to be f64
    INFER_BOOL,         // Known to be bool
    INFER_STRING,       // Known to be string
    INFER_NULL,         // Known to be null
    INFER_ARRAY,        // Known to be array (element type may be known)
    INFER_OBJECT,       // Known to be object
    INFER_FUNCTION,     // Known to be function
    INFER_NUMERIC,      // Known to be numeric (i32, i64, or f64) but not which
    INFER_INTEGER,      // Known to be integer (i32 or i64) but not which
} InferredTypeKind;

// Inferred type with optional element type for arrays
typedef struct InferredType {
    InferredTypeKind kind;
    struct InferredType *element_type;  // For arrays
} InferredType;

// Variable type binding
typedef struct TypeBinding {
    char *name;
    InferredType type;
    struct TypeBinding *next;
} TypeBinding;

// Type environment (scoped type bindings)
typedef struct TypeEnv {
    TypeBinding *bindings;
    struct TypeEnv *parent;
} TypeEnv;

// Function return type tracking
typedef struct FuncReturnType {
    char *name;              // Function name
    InferredType return_type;
    struct FuncReturnType *next;
} FuncReturnType;

// Unboxable variable info (variables that can use native C types)
typedef struct UnboxableVar {
    char *name;
    InferredTypeKind native_type;  // INFER_I32, INFER_I64, INFER_BOOL, INFER_F64
    int is_loop_counter;           // Whether this is a loop counter
    int is_accumulator;            // Whether this is used as an accumulator
    int is_typed_var;              // Whether this is a typed variable (let x: i32 = ...)
    struct UnboxableVar *next;
} UnboxableVar;

// Type inference context
typedef struct {
    TypeEnv *current_env;
    FuncReturnType *func_returns;  // Registry of function return types
    int changed;  // Set to 1 if any type was refined this pass
    UnboxableVar *unboxable_vars;  // Variables that can be unboxed
} TypeInferContext;

// ========== TYPE CONSTRUCTORS ==========

InferredType infer_unknown(void);
InferredType infer_i32(void);
InferredType infer_i64(void);
InferredType infer_f64(void);
InferredType infer_bool(void);
InferredType infer_string(void);
InferredType infer_null(void);
InferredType infer_numeric(void);
InferredType infer_integer(void);

// ========== TYPE OPERATIONS ==========

// Check if type is known (not UNKNOWN)
int infer_is_known(InferredType t);

// Check if type is a specific kind
int infer_is_i32(InferredType t);
int infer_is_i64(InferredType t);
int infer_is_f64(InferredType t);
int infer_is_bool(InferredType t);
int infer_is_integer(InferredType t);  // i32, i64, or INTEGER
int infer_is_numeric(InferredType t);  // any numeric type

// Meet operation: find common type (for control flow merge)
InferredType infer_meet(InferredType a, InferredType b);

// Result type of binary operation
InferredType infer_binary_result(BinaryOp op, InferredType left, InferredType right);

// Result type of unary operation
InferredType infer_unary_result(UnaryOp op, InferredType operand);

// ========== ENVIRONMENT OPERATIONS ==========

TypeInferContext* type_infer_new(void);
void type_infer_free(TypeInferContext *ctx);

void type_env_push(TypeInferContext *ctx);
void type_env_pop(TypeInferContext *ctx);

void type_env_bind(TypeInferContext *ctx, const char *name, InferredType type);
InferredType type_env_lookup(TypeInferContext *ctx, const char *name);

// Refine a variable's type (if new type is more specific)
void type_env_refine(TypeInferContext *ctx, const char *name, InferredType type);

// ========== FUNCTION RETURN TYPE TRACKING ==========

// Register a function's return type
void type_register_func_return(TypeInferContext *ctx, const char *name, InferredType ret_type);

// Look up a function's return type
InferredType type_lookup_func_return(TypeInferContext *ctx, const char *name);

// ========== INFERENCE ==========

// Infer type of an expression given current environment
InferredType infer_expr(TypeInferContext *ctx, Expr *expr);

// Analyze a statement (updates environment with bindings)
void infer_stmt(TypeInferContext *ctx, Stmt *stmt);

// Analyze a function and infer parameter/return types
void infer_function(TypeInferContext *ctx, Expr *func_expr);

// ========== ESCAPE ANALYSIS & UNBOXING ==========

// Mark a variable as unboxable (can use native C type instead of HmlValue)
void type_mark_unboxable(TypeInferContext *ctx, const char *name,
                         InferredTypeKind native_type, int is_loop_counter, int is_accumulator);

// Check if a variable is unboxable (returns native type, or INFER_UNKNOWN if not unboxable)
InferredTypeKind type_get_unboxable(TypeInferContext *ctx, const char *name);

// Check if variable is an unboxable loop counter
int type_is_loop_counter(TypeInferContext *ctx, const char *name);

// Check if variable is an unboxable accumulator
int type_is_accumulator(TypeInferContext *ctx, const char *name);

// Analyze a for-loop and detect unboxable loop counters
void type_analyze_for_loop(TypeInferContext *ctx, Stmt *stmt);

// Analyze a while-loop for unboxable accumulators
void type_analyze_while_loop(TypeInferContext *ctx, Stmt *stmt);

// ========== TYPED VARIABLE UNBOXING ==========

// Analyze a typed variable declaration and determine if it can be unboxed
// A variable can be unboxed if:
// 1. It has a primitive type annotation (i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, bool)
// 2. It doesn't escape (not passed to functions, stored in arrays/objects, returned, etc.)
// 3. It's only used in operations that preserve its type
void type_analyze_typed_let(TypeInferContext *ctx, Stmt *stmt, Stmt *containing_block, int stmt_index);

// Analyze all statements in a function/block for unboxable typed variables
void type_analyze_block_for_unboxing(TypeInferContext *ctx, Stmt *block);

// Check if a typed variable can be unboxed based on its type annotation
// Returns the native type if unboxable, INFER_UNKNOWN otherwise
InferredTypeKind type_can_unbox_annotation(Type *type_annotation);

// Check if a variable escapes in the given statement(s)
// Returns 1 if the variable escapes (needs to stay boxed), 0 otherwise
int type_variable_escapes(const char *var_name, Stmt *stmt);

// Check if a variable escapes in an expression
int type_variable_escapes_in_expr(const char *var_name, Expr *expr);

// Check if variable is an unboxable typed variable (not loop counter or accumulator)
int type_is_unboxed_typed_var(TypeInferContext *ctx, const char *name);

// ========== TAIL CALL OPTIMIZATION ==========

// Check if a function has a single tail recursive call pattern
// Returns 1 if the function can be optimized with tail call elimination, 0 otherwise
// A function is tail-recursive if all return statements either:
//   - Return a non-recursive value (base case)
//   - Return a call to the same function (tail call)
int is_tail_recursive_function(Stmt *body, const char *func_name);

// Check if a statement contains only tail calls or non-recursive returns
int stmt_is_tail_recursive(Stmt *stmt, const char *func_name);

// Check if an expression is a tail call to the given function
int is_tail_call_expr(Expr *expr, const char *func_name);

// ========== DEBUG ==========

const char* infer_type_name(InferredType t);

#endif // HEMLOCK_TYPE_INFER_H
