/*
 * Hemlock Compiler - Compile-Time Type Checking
 *
 * Performs static type analysis on the AST before code generation.
 * Reports type errors at compile time rather than runtime.
 */

#ifndef HEMLOCK_TYPE_CHECK_H
#define HEMLOCK_TYPE_CHECK_H

#include "../../include/ast.h"

// ========== CHECKED TYPE REPRESENTATION ==========

// Represents a type during compile-time checking
// More detailed than InferredType to support full type checking
typedef enum {
    CHECKED_UNKNOWN,    // Type not known (dynamic)
    CHECKED_I8,
    CHECKED_I16,
    CHECKED_I32,
    CHECKED_I64,
    CHECKED_U8,
    CHECKED_U16,
    CHECKED_U32,
    CHECKED_U64,
    CHECKED_F32,
    CHECKED_F64,
    CHECKED_BOOL,
    CHECKED_STRING,
    CHECKED_RUNE,
    CHECKED_NULL,
    CHECKED_PTR,
    CHECKED_BUFFER,
    CHECKED_ARRAY,      // Array with optional element type
    CHECKED_OBJECT,     // Generic object
    CHECKED_CUSTOM,     // Custom object type (Person, User, etc.)
    CHECKED_FUNCTION,   // Function type
    CHECKED_TASK,       // Async task
    CHECKED_CHANNEL,    // Channel
    CHECKED_FILE,       // File handle
    CHECKED_ENUM,       // Enum type
    CHECKED_VOID,       // Void (for functions with no return)
    CHECKED_ANY,        // Any type (escape hatch for dynamic code)
    CHECKED_NUMERIC,    // Any numeric type (for mixed arithmetic)
    CHECKED_INTEGER,    // Any integer type (i8-i64, u8-u64)
} CheckedTypeKind;

// Full type information for compile-time checking
typedef struct CheckedType {
    CheckedTypeKind kind;
    char *type_name;                // For CHECKED_CUSTOM (e.g., "Person")
    struct CheckedType *element_type;  // For CHECKED_ARRAY
    int nullable;                   // If true, type allows null

    // For CHECKED_FUNCTION
    struct CheckedType **param_types;
    int num_params;
    struct CheckedType *return_type;
    int has_rest_param;             // Has ...args rest parameter
} CheckedType;

// ========== TYPE ENVIRONMENT ==========

// Variable binding in the type environment
typedef struct TypeCheckBinding {
    char *name;
    CheckedType *type;
    int is_const;                   // Whether this is a const binding
    int line;                       // Line where declared (for error messages)
    struct TypeCheckBinding *next;
} TypeCheckBinding;

// Scoped type environment
typedef struct TypeCheckEnv {
    TypeCheckBinding *bindings;
    struct TypeCheckEnv *parent;
} TypeCheckEnv;

// Function signature for checking calls
typedef struct FunctionSig {
    char *name;
    CheckedType **param_types;
    char **param_names;             // Parameter names (for error messages)
    int num_params;
    CheckedType *return_type;
    int has_rest_param;
    int is_async;
    struct FunctionSig *next;
} FunctionSig;

// Object type definition (from 'define' statements)
typedef struct ObjectDef {
    char *name;
    char **field_names;
    CheckedType **field_types;
    int *field_optional;
    int num_fields;
    struct ObjectDef *next;
} ObjectDef;

// Enum type definition
typedef struct EnumDef {
    char *name;
    char **variant_names;
    int num_variants;
    struct EnumDef *next;
} EnumDef;

// ========== TYPE CHECK CONTEXT ==========

typedef struct {
    // Type environment (scoped)
    TypeCheckEnv *current_env;

    // Global registries
    FunctionSig *functions;     // Registered function signatures
    ObjectDef *object_defs;     // Registered object type definitions
    EnumDef *enum_defs;         // Registered enum definitions

    // Current function being checked (for return type validation)
    CheckedType *current_return_type;
    char *current_function_name;
    int in_async_function;

    // Error tracking
    int error_count;
    int warning_count;
    const char *filename;       // Current file being checked

    // Configuration
    int strict_mode;            // If true, require type annotations
    int warn_implicit_any;      // Warn when falling back to CHECKED_ANY
} TypeCheckContext;

// ========== CONTEXT MANAGEMENT ==========

// Create a new type check context
TypeCheckContext* type_check_new(const char *filename);

// Free a type check context
void type_check_free(TypeCheckContext *ctx);

// ========== ENVIRONMENT OPERATIONS ==========

// Push a new scope onto the environment
void type_check_push_scope(TypeCheckContext *ctx);

// Pop the current scope
void type_check_pop_scope(TypeCheckContext *ctx);

// Bind a variable to a type in the current scope
void type_check_bind(TypeCheckContext *ctx, const char *name, CheckedType *type,
                     int is_const, int line);

// Look up a variable's type (searches parent scopes)
CheckedType* type_check_lookup(TypeCheckContext *ctx, const char *name);

// Check if a variable is const
int type_check_is_const(TypeCheckContext *ctx, const char *name);

// ========== FUNCTION REGISTRATION ==========

// Register a function signature
void type_check_register_function(TypeCheckContext *ctx, const char *name,
                                  CheckedType **param_types, char **param_names,
                                  int num_params, CheckedType *return_type,
                                  int has_rest_param, int is_async);

// Look up a function signature
FunctionSig* type_check_lookup_function(TypeCheckContext *ctx, const char *name);

// ========== OBJECT DEFINITION REGISTRATION ==========

// Register an object type definition
void type_check_register_object(TypeCheckContext *ctx, const char *name,
                                char **field_names, CheckedType **field_types,
                                int *field_optional, int num_fields);

// Look up an object type definition
ObjectDef* type_check_lookup_object(TypeCheckContext *ctx, const char *name);

// ========== ENUM REGISTRATION ==========

// Register an enum definition
void type_check_register_enum(TypeCheckContext *ctx, const char *name,
                              char **variant_names, int num_variants);

// Look up an enum definition
EnumDef* type_check_lookup_enum(TypeCheckContext *ctx, const char *name);

// ========== TYPE CONSTRUCTORS ==========

// Create a primitive type
CheckedType* checked_type_primitive(CheckedTypeKind kind);

// Create an array type with element type
CheckedType* checked_type_array(CheckedType *element_type);

// Create a custom object type
CheckedType* checked_type_custom(const char *name);

// Create a function type
CheckedType* checked_type_function(CheckedType **param_types, int num_params,
                                   CheckedType *return_type, int has_rest_param);

// Create a nullable version of a type
CheckedType* checked_type_nullable(CheckedType *base);

// Clone a type (deep copy)
CheckedType* checked_type_clone(const CheckedType *type);

// Free a type
void checked_type_free(CheckedType *type);

// Convert AST Type to CheckedType
CheckedType* checked_type_from_ast(Type *ast_type);

// ========== TYPE COMPATIBILITY ==========

// Check if 'from' type is assignable to 'to' type
// Returns 1 if compatible, 0 if not
int type_is_assignable(CheckedType *to, CheckedType *from);

// Check if two types are equivalent
int type_equals(CheckedType *a, CheckedType *b);

// Get the common type of two types (for binary operations)
// Returns NULL if types are incompatible
CheckedType* type_common(CheckedType *a, CheckedType *b);

// Check if a type is numeric
int type_is_numeric(CheckedType *type);

// Check if a type is an integer type
int type_is_integer(CheckedType *type);

// Check if a type is a floating point type
int type_is_float(CheckedType *type);

// ========== TYPE INFERENCE ==========

// Infer the type of an expression
CheckedType* type_check_infer_expr(TypeCheckContext *ctx, Expr *expr);

// ========== TYPE CHECKING ==========

// Check a complete program
// Returns the number of type errors found
int type_check_program(TypeCheckContext *ctx, Stmt **stmts, int stmt_count);

// Check a single statement
void type_check_stmt(TypeCheckContext *ctx, Stmt *stmt);

// Check an expression and validate its type
void type_check_expr(TypeCheckContext *ctx, Expr *expr);

// ========== ERROR REPORTING ==========

// Report a type error
void type_error(TypeCheckContext *ctx, int line, const char *fmt, ...);

// Report a type warning
void type_warning(TypeCheckContext *ctx, int line, const char *fmt, ...);

// ========== DEBUG ==========

// Get a human-readable name for a type
const char* checked_type_name(CheckedType *type);

// Get a human-readable name (with static buffer, for simple cases)
const char* checked_type_kind_name(CheckedTypeKind kind);

#endif // HEMLOCK_TYPE_CHECK_H
