#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>

// ========== OBJECT TYPE REGISTRY ==========

ObjectTypeRegistry object_types = {0};

void init_object_types(void) {
    if (object_types.types == NULL) {
        object_types.capacity = 16;
        object_types.types = malloc(sizeof(ObjectType*) * object_types.capacity);
        object_types.count = 0;
    }
}

void register_object_type(ObjectType *type) {
    init_object_types();
    if (object_types.count >= object_types.capacity) {
        object_types.capacity *= 2;
        object_types.types = realloc(object_types.types, sizeof(ObjectType*) * object_types.capacity);
    }
    object_types.types[object_types.count++] = type;
}

ObjectType* lookup_object_type(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (int i = 0; i < object_types.count; i++) {
        if (strcmp(object_types.types[i]->name, name) == 0) {
            return object_types.types[i];
        }
    }
    return NULL;
}

void cleanup_object_types(void) {
    if (object_types.types == NULL) {
        return;  // Nothing to clean up
    }

    // Free each ObjectType
    for (int i = 0; i < object_types.count; i++) {
        ObjectType *type = object_types.types[i];
        if (type) {
            // Free type name
            free(type->name);

            // Free type parameters (for generic types)
            if (type->type_params) {
                for (int j = 0; j < type->num_type_params; j++) {
                    free(type->type_params[j]);
                }
                free(type->type_params);
            }

            // Free field names
            for (int j = 0; j < type->num_fields; j++) {
                free(type->field_names[j]);
            }
            free(type->field_names);

            // Free arrays (but not contents - they're AST pointers)
            free(type->field_types);
            free(type->field_optional);
            free(type->field_defaults);

            // Free the ObjectType struct itself
            free(type);
        }
    }

    // Free the types array
    free(object_types.types);

    // Reset the registry
    object_types.types = NULL;
    object_types.count = 0;
    object_types.capacity = 0;
}

// ========== ENUM TYPE REGISTRY ==========

EnumTypeRegistry enum_types = {0};

void init_enum_types(void) {
    if (enum_types.types == NULL) {
        enum_types.capacity = 16;
        enum_types.types = malloc(sizeof(EnumType*) * enum_types.capacity);
        enum_types.count = 0;
    }
}

void register_enum_type(EnumType *type) {
    init_enum_types();
    if (enum_types.count >= enum_types.capacity) {
        enum_types.capacity *= 2;
        enum_types.types = realloc(enum_types.types, sizeof(EnumType*) * enum_types.capacity);
    }
    enum_types.types[enum_types.count++] = type;
}

EnumType* lookup_enum_type(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (int i = 0; i < enum_types.count; i++) {
        if (strcmp(enum_types.types[i]->name, name) == 0) {
            return enum_types.types[i];
        }
    }
    return NULL;
}

void cleanup_enum_types(void) {
    if (enum_types.types == NULL) {
        return;  // Nothing to clean up
    }

    // Free each EnumType
    for (int i = 0; i < enum_types.count; i++) {
        EnumType *type = enum_types.types[i];
        if (type) {
            // Free type name
            free(type->name);

            // Free variant names
            for (int j = 0; j < type->num_variants; j++) {
                free(type->variant_names[j]);
            }
            free(type->variant_names);

            // Free variant values
            free(type->variant_values);

            // Free the EnumType struct itself
            free(type);
        }
    }

    // Free the types array
    free(enum_types.types);

    // Reset the registry
    enum_types.types = NULL;
    enum_types.count = 0;
    enum_types.capacity = 0;
}

// ========== TYPE PARAMETER SUBSTITUTION ==========

// Substitute type parameters in a Type with concrete type arguments
// Returns a new Type that must be freed by caller, or the original type if no substitution needed
// type_params: array of type parameter names (e.g., ["T", "U"])
// type_args: array of Type* to substitute for each parameter
// num_params: number of type parameters/arguments
static Type* substitute_type_params(Type *type, char **type_params, Type **type_args, int num_params) {
    if (!type) return NULL;

    // Check if this type is a type parameter reference
    if (type->kind == TYPE_PARAM && type->type_name) {
        for (int i = 0; i < num_params; i++) {
            if (type_params[i] && strcmp(type->type_name, type_params[i]) == 0) {
                // Found matching type parameter - return a copy of the type argument
                Type *result = malloc(sizeof(Type));
                result->kind = type_args[i]->kind;
                result->type_name = type_args[i]->type_name ? strdup(type_args[i]->type_name) : NULL;
                result->element_type = type_args[i]->element_type ?
                    substitute_type_params(type_args[i]->element_type, type_params, type_args, num_params) : NULL;
                result->nullable = type_args[i]->nullable;
                result->type_args = NULL;
                result->num_type_args = 0;
                // Copy type_args if present
                if (type_args[i]->num_type_args > 0) {
                    result->type_args = malloc(sizeof(Type*) * type_args[i]->num_type_args);
                    result->num_type_args = type_args[i]->num_type_args;
                    for (int j = 0; j < result->num_type_args; j++) {
                        result->type_args[j] = substitute_type_params(type_args[i]->type_args[j],
                                                                       type_params, type_args, num_params);
                    }
                }
                return result;
            }
        }
    }

    // For array types, substitute element type
    if (type->kind == TYPE_ARRAY && type->element_type) {
        Type *new_element = substitute_type_params(type->element_type, type_params, type_args, num_params);
        if (new_element != type->element_type) {
            Type *result = malloc(sizeof(Type));
            result->kind = TYPE_ARRAY;
            result->type_name = NULL;
            result->element_type = new_element;
            result->nullable = type->nullable;
            result->type_args = NULL;
            result->num_type_args = 0;
            return result;
        }
    }

    // For custom object types with type arguments, substitute recursively
    if (type->kind == TYPE_CUSTOM_OBJECT && type->num_type_args > 0) {
        Type **new_args = malloc(sizeof(Type*) * type->num_type_args);
        int any_changed = 0;
        for (int i = 0; i < type->num_type_args; i++) {
            new_args[i] = substitute_type_params(type->type_args[i], type_params, type_args, num_params);
            if (new_args[i] != type->type_args[i]) {
                any_changed = 1;
            }
        }
        if (any_changed) {
            Type *result = malloc(sizeof(Type));
            result->kind = TYPE_CUSTOM_OBJECT;
            result->type_name = type->type_name ? strdup(type->type_name) : NULL;
            result->element_type = NULL;
            result->nullable = type->nullable;
            result->type_args = new_args;
            result->num_type_args = type->num_type_args;
            return result;
        }
        // No changes, free the copies
        for (int i = 0; i < type->num_type_args; i++) {
            if (new_args[i] != type->type_args[i]) {
                type_free(new_args[i]);
            }
        }
        free(new_args);
    }

    // No substitution needed, return original
    return type;
}

// Free a substituted type (deep free)
static void free_substituted_type(Type *type, Type *original) {
    if (type && type != original) {
        // This is a newly allocated type, free it
        if (type->type_name) free(type->type_name);
        if (type->element_type) {
            // Recursively free if it was also substituted
            free_substituted_type(type->element_type, original ? original->element_type : NULL);
        }
        if (type->type_args) {
            for (int i = 0; i < type->num_type_args; i++) {
                free_substituted_type(type->type_args[i], NULL);
            }
            free(type->type_args);
        }
        free(type);
    }
}

// Forward declaration for convert_to_type (needed for recursive type checking)
Value convert_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx);

// Check if an object matches a type definition with type arguments (for generics)
// type_args: array of Type* to substitute for type parameters (NULL for non-generic types)
// num_type_args: number of type arguments
Value check_object_type_generic(Value value, ObjectType *object_type,
                                 Type **type_args, int num_type_args,
                                 Environment *env, ExecutionContext *ctx) {
    if (value.type != VAL_OBJECT) {
        fprintf(stderr, "Runtime error: Expected object for type '%s', got non-object\n",
                object_type->name);
        exit(1);
    }

    // Validate type argument count for generic types
    if (object_type->num_type_params > 0) {
        if (num_type_args != object_type->num_type_params) {
            fprintf(stderr, "Runtime error: Type '%s' expects %d type argument(s), got %d\n",
                    object_type->name, object_type->num_type_params, num_type_args);
            exit(1);
        }
    }

    Object *obj = value.as.as_object;

    // Check all required fields
    for (int i = 0; i < object_type->num_fields; i++) {
        const char *field_name = object_type->field_names[i];
        int field_optional = object_type->field_optional[i];
        Type *field_type = object_type->field_types[i];

        // Substitute type parameters if this is a generic type
        Type *substituted_field_type = field_type;
        if (field_type && object_type->num_type_params > 0 && type_args) {
            substituted_field_type = substitute_type_params(
                field_type, object_type->type_params, type_args, num_type_args);
        }

        // Look for field in object
        int found = 0;
        Value field_value;
        int field_index = -1;
        for (int j = 0; j < obj->num_fields; j++) {
            if (strcmp(obj->field_names[j], field_name) == 0) {
                found = 1;
                field_value = obj->field_values[j];
                field_index = j;
                break;
            }
        }

        if (!found) {
            // Field not present - check if it's optional
            if (field_optional) {
                // Add field with default value or null
                if (obj->num_fields >= obj->capacity) {
                    obj->capacity *= 2;
                    obj->field_names = realloc(obj->field_names, sizeof(char*) * obj->capacity);
                    obj->field_values = realloc(obj->field_values, sizeof(Value) * obj->capacity);
                }

                obj->field_names[obj->num_fields] = strdup(field_name);
                if (object_type->field_defaults[i]) {
                    obj->field_values[obj->num_fields] = eval_expr(object_type->field_defaults[i], env, ctx);
                } else {
                    obj->field_values[obj->num_fields] = val_null();
                }
                obj->num_fields++;
            } else {
                // Free substituted type if allocated
                free_substituted_type(substituted_field_type, field_type);
                fprintf(stderr, "Runtime error: Object missing required field '%s' for type '%s'\n",
                        field_name, object_type->name);
                exit(1);
            }
        } else if (substituted_field_type && substituted_field_type->kind != TYPE_INFER) {
            // Type check the field using the (possibly substituted) type
            // Use convert_to_type for recursive type checking (handles arrays, objects, etc.)
            Value converted = convert_to_type(field_value, substituted_field_type, env, ctx);
            obj->field_values[field_index] = converted;
        }

        // Free substituted type if it was newly allocated
        free_substituted_type(substituted_field_type, field_type);
    }

    // Set the type name on the object (include type args for display)
    if (obj->type_name) free(obj->type_name);
    obj->type_name = strdup(object_type->name);

    return value;
}

// Check if an object matches a type definition (duck typing) - non-generic version
Value check_object_type(Value value, ObjectType *object_type, Environment *env, ExecutionContext *ctx) {
    return check_object_type_generic(value, object_type, NULL, 0, env, ctx);
}

// ========== TYPE CHECKING HELPERS ==========

// Helper: Check if a value is any integer type
int is_integer(Value val) {
    return val.type == VAL_I8 || val.type == VAL_I16 || val.type == VAL_I32 || val.type == VAL_I64 ||
           val.type == VAL_U8 || val.type == VAL_U16 || val.type == VAL_U32 || val.type == VAL_U64;
}

// Helper: Check if a value is any float type
int is_float(Value val) {
    return val.type == VAL_F32 || val.type == VAL_F64;
}

// Helper: Check if a value is any numeric type
int is_numeric(Value val) {
    return is_integer(val) || is_float(val);
}

// Helper: Convert any integer to i32 (for operations)
int32_t value_to_int(Value val) {
    switch (val.type) {
        case VAL_I8: return val.as.as_i8;
        case VAL_I16: return val.as.as_i16;
        case VAL_I32: return val.as.as_i32;
        case VAL_I64: return (int32_t)val.as.as_i64;  // potential overflow
        case VAL_U8: return val.as.as_u8;
        case VAL_U16: return val.as.as_u16;
        case VAL_U32: return (int32_t)val.as.as_u32;  // potential overflow
        case VAL_U64: return (int32_t)val.as.as_u64;  // potential overflow
        case VAL_RUNE: return (int32_t)val.as.as_rune;  // rune is a uint32_t codepoint
        case VAL_BOOL: return val.as.as_bool;
        default:
            fprintf(stderr, "Runtime error: Cannot convert to int\n");
            exit(1);
    }
}

// Helper: Convert any integer to i64 (preserves full range)
int64_t value_to_int64(Value val) {
    switch (val.type) {
        case VAL_I8: return (int64_t)val.as.as_i8;
        case VAL_I16: return (int64_t)val.as.as_i16;
        case VAL_I32: return (int64_t)val.as.as_i32;
        case VAL_I64: return val.as.as_i64;
        case VAL_U8: return (int64_t)val.as.as_u8;
        case VAL_U16: return (int64_t)val.as.as_u16;
        case VAL_U32: return (int64_t)val.as.as_u32;
        case VAL_U64: return (int64_t)val.as.as_u64;  // May lose high bit
        case VAL_BOOL: return val.as.as_bool;
        default:
            fprintf(stderr, "Runtime error: Cannot convert to int64\n");
            exit(1);
    }
}

// Helper: Convert any numeric to f64 (for operations)
double value_to_float(Value val) {
    switch (val.type) {
        case VAL_I8: return (double)val.as.as_i8;
        case VAL_I16: return (double)val.as.as_i16;
        case VAL_I32: return (double)val.as.as_i32;
        case VAL_I64: return (double)val.as.as_i64;
        case VAL_U8: return (double)val.as.as_u8;
        case VAL_U16: return (double)val.as.as_u16;
        case VAL_U32: return (double)val.as.as_u32;
        case VAL_U64: return (double)val.as.as_u64;
        case VAL_F32: return (double)val.as.as_f32;
        case VAL_F64: return val.as.as_f64;
        default:
            fprintf(stderr, "Runtime error: Cannot convert to float\n");
            exit(1);
    }
}

// Helper: Check if value is truthy
int value_is_truthy(Value val) {
    if (val.type == VAL_BOOL) {
        return val.as.as_bool;
    } else if (is_integer(val)) {
        return value_to_int(val) != 0;
    } else if (is_float(val)) {
        return value_to_float(val) != 0.0;
    } else if (val.type == VAL_NULL) {
        return 0;
    } else if (val.type == VAL_STRING) {
        // Empty string is falsy, non-empty string is truthy
        return val.as.as_string != NULL && val.as.as_string->length > 0;
    } else if (val.type == VAL_ARRAY) {
        // Empty array is falsy, non-empty array is truthy
        return val.as.as_array != NULL && val.as.as_array->length > 0;
    }
    return 1;  // objects, functions, etc. are truthy
}

// ========== TYPE PROMOTION ==========

// Get the "rank" of a type for promotion rules
int type_rank(ValueType type) {
    switch (type) {
        case VAL_I8: return 0;
        case VAL_U8: return 1;
        case VAL_I16: return 2;
        case VAL_U16: return 3;
        case VAL_I32: return 4;
        case VAL_U32: return 5;
        case VAL_I64: return 6;
        case VAL_U64: return 7;
        case VAL_F32: return 8;
        case VAL_F64: return 9;
        default: return -1;
    }
}

// Determine the result type for binary operations
ValueType promote_types(ValueType left, ValueType right) {
    // If types are the same, no promotion needed
    if (left == right) return left;

    // Check if either operand is a float
    int left_is_float = is_float((Value){.type = left});
    int right_is_float = is_float((Value){.type = right});

    if (left_is_float && right_is_float) {
        // Both floats - take the larger
        return (left == VAL_F64 || right == VAL_F64) ? VAL_F64 : VAL_F32;
    }

    if (left_is_float || right_is_float) {
        // Mixed float and integer
        // To avoid precision loss, promote to f64 when mixing i64/u64 with f32
        ValueType float_type = left_is_float ? left : right;
        ValueType int_type = left_is_float ? right : left;

        // If either operand is f64, result is f64
        if (float_type == VAL_F64) return VAL_F64;

        // f32 with i64/u64 should promote to f64 to preserve precision
        // (f32 has only 24-bit mantissa, i64/u64 need 53+ bits)
        if (int_type == VAL_I64 || int_type == VAL_U64) {
            return VAL_F64;
        }

        // f32 with smaller integers is fine
        return VAL_F32;
    }

    // Both are integers - promote to higher rank
    int left_rank = type_rank(left);
    int right_rank = type_rank(right);

    return (left_rank > right_rank) ? left : right;
}

// Convert a value to a specific ValueType (for promotions during operations)
Value promote_value(Value val, ValueType target_type) {
    if (val.type == target_type) return val;

    switch (target_type) {
        case VAL_I8: return val_i8((int8_t)value_to_int(val));
        case VAL_I16: return val_i16((int16_t)value_to_int(val));
        case VAL_I32: return val_i32(value_to_int(val));
        case VAL_I64:
            // For i64, need to handle full range
            if (is_float(val)) {
                return val_i64((int64_t)value_to_float(val));
            } else if (val.type == VAL_I64) {
                return val;
            } else if (val.type == VAL_U64) {
                return val_i64((int64_t)val.as.as_u64);
            } else {
                return val_i64((int64_t)value_to_int(val));
            }
        case VAL_U8: return val_u8((uint8_t)value_to_int(val));
        case VAL_U16: return val_u16((uint16_t)value_to_int(val));
        case VAL_U32: return val_u32((uint32_t)value_to_int(val));
        case VAL_U64:
            // For u64, need to handle full range
            if (is_float(val)) {
                return val_u64((uint64_t)value_to_float(val));
            } else if (val.type == VAL_U64) {
                return val;
            } else if (val.type == VAL_I64) {
                return val_u64((uint64_t)val.as.as_i64);
            } else {
                return val_u64((uint64_t)value_to_int(val));
            }
        case VAL_F32:
            // Use value_to_float to preserve precision for i64/u64 values
            return val_f32((float)value_to_float(val));
        case VAL_F64:
            // Use value_to_float to preserve precision for i64/u64 values
            return val_f64(value_to_float(val));
        case VAL_RUNE:
            // Rune is a Unicode codepoint (u32)
            if (val.type == VAL_RUNE) {
                return val;
            } else {
                return val_rune((uint32_t)value_to_int(val));
            }
        default:
            fprintf(stderr, "Runtime error: Cannot promote to type\n");
            exit(1);
    }
}

// ========== TYPE CONVERSION ==========

// Helper to check if a TypeKind is a numeric type
static int is_numeric_type(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:
        case TYPE_I16:
        case TYPE_I32:
        case TYPE_I64:
        case TYPE_U8:
        case TYPE_U16:
        case TYPE_U32:
        case TYPE_U64:
        case TYPE_F32:
        case TYPE_F64:
            return 1;
        default:
            return 0;
    }
}

// Helper to convert TypeKind to string name
static const char* type_kind_to_string(TypeKind kind) {
    switch (kind) {
        case TYPE_I8: return "i8";
        case TYPE_I16: return "i16";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_U8: return "u8";
        case TYPE_U16: return "u16";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_RUNE: return "rune";
        case TYPE_PTR: return "ptr";
        case TYPE_BUFFER: return "buffer";
        case TYPE_ARRAY: return "array";
        case TYPE_NULL: return "null";
        default: return "unknown";
    }
}

// Helper to convert a value to a target type
Value convert_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx) {
    if (!target_type) {
        return value;  // No type annotation
    }

    // If target type is nullable and value is null, allow it
    if (target_type->nullable && value.type == VAL_NULL) {
        return value;
    }

    TypeKind kind = target_type->kind;

    // Handle object and enum types (both use TYPE_CUSTOM_OBJECT at parse time)
    if (kind == TYPE_CUSTOM_OBJECT) {
        // Check if it's an enum type first
        EnumType *enum_type = lookup_enum_type(target_type->type_name);
        if (enum_type) {
            // Enum values are i32
            if (value.type != VAL_I32) {
                fprintf(stderr, "Runtime error: Expected enum value (i32) for type '%s'\n",
                        target_type->type_name);
                exit(1);
            }
            // Optionally validate that the value is a valid variant (future enhancement)
            return value;
        }

        // Not an enum, try object type
        ObjectType *object_type = lookup_object_type(target_type->type_name);
        if (!object_type) {
            fprintf(stderr, "Runtime error: Unknown type '%s'\n", target_type->type_name);
            exit(1);
        }

        // For generic types, pass type arguments for substitution
        if (target_type->num_type_args > 0) {
            return check_object_type_generic(value, object_type,
                                             target_type->type_args, target_type->num_type_args,
                                             env, ctx);
        }
        return check_object_type(value, object_type, env, ctx);
    }

    if (kind == TYPE_GENERIC_OBJECT) {
        if (value.type != VAL_OBJECT) {
            fprintf(stderr, "Runtime error: Expected object, got non-object\n");
            exit(1);
        }
        return value;
    }

    // Handle compound types (A & B & C) - value must satisfy ALL constituent types
    if (kind == TYPE_COMPOUND) {
        if (value.type != VAL_OBJECT) {
            fprintf(stderr, "Runtime error: Compound type requires an object\n");
            exit(1);
        }

        // Check against each constituent type
        for (int i = 0; i < target_type->num_compound_types; i++) {
            Type *constituent = target_type->compound_types[i];
            value = convert_to_type(value, constituent, env, ctx);
        }
        return value;
    }

    // Handle typed arrays
    if (kind == TYPE_ARRAY) {
        if (value.type != VAL_ARRAY) {
            fprintf(stderr, "Runtime error: Expected array, got non-array\n");
            exit(1);
        }

        Array *arr = value.as.as_array;

        // If target_type has no element_type, it's an untyped array (let arr: array = ...)
        if (target_type->element_type == NULL) {
            // No type constraint, just verify it's an array (already done above)
            return value;
        }

        // Target has element type constraint - apply it to the array
        // If array already has element_type, validate compatibility
        if (arr->element_type != NULL) {
            // Check if existing type matches target type
            if (arr->element_type->kind != target_type->element_type->kind) {
                fprintf(stderr, "Runtime error: Array element type mismatch\n");
                exit(1);
            }
        } else {
            // Set the element type constraint on the array
            // Clone the type to avoid lifetime issues
            arr->element_type = malloc(sizeof(Type));
            arr->element_type->kind = target_type->element_type->kind;
            arr->element_type->type_name = target_type->element_type->type_name ? strdup(target_type->element_type->type_name) : NULL;
            arr->element_type->element_type = NULL;  // Don't support nested typed arrays yet
            arr->element_type->nullable = 0;
            arr->element_type->compound_types = NULL;
            arr->element_type->num_compound_types = 0;
            arr->element_type->type_args = NULL;
            arr->element_type->num_type_args = 0;
        }

        // Validate all existing elements match the type constraint
        for (int i = 0; i < arr->length; i++) {
            Value elem = arr->elements[i];
            // Convert/validate each element against the element type
            arr->elements[i] = convert_to_type(elem, target_type->element_type, env, ctx);
        }

        return value;
    }

    // Original function continues with TypeKind
    TypeKind target_kind = kind;
    // Get the source value as the widest type for range checking
    int64_t int_val = 0;
    double float_val = 0.0;
    int is_source_float = 0;

    // Extract source value (use int64 to preserve full range)
    if (is_integer(value)) {
        int_val = value_to_int64(value);
    } else if (is_float(value)) {
        float_val = value_to_float(value);
        is_source_float = 1;
    } else if (value.type == VAL_BOOL) {
        // Allow bool -> int conversions
        int_val = value.as.as_bool;
    } else if (value.type == VAL_RUNE) {
        // Allow rune -> int conversions (get codepoint value)
        int_val = value.as.as_rune;
    } else if (value.type == VAL_STRING && target_kind == TYPE_STRING) {
        return value;  // String to string, ok
    } else if (value.type == VAL_STRING && target_kind == TYPE_BOOL) {
        // String to bool via type annotation is not allowed
        // Use explicit conversion: bool("true") or bool("false")
        fprintf(stderr, "Runtime error: Cannot convert string to bool via type annotation. Use bool(\"...\") instead.\n");
        exit(1);
    } else if (value.type == VAL_STRING && is_numeric_type(target_kind)) {
        // String to numeric via type annotation is not allowed
        // Use explicit conversion: i32("42"), f64("3.14"), etc.
        const char *type_name = type_kind_to_string(target_kind);
        fprintf(stderr, "Runtime error: Cannot convert string to %s via type annotation. Use %s(\"...\") instead.\n",
                type_name, type_name);
        exit(1);
    } else if (value.type == VAL_BOOL && target_kind == TYPE_BOOL) {
        return value;  // Bool to bool, ok
    } else if (value.type == VAL_NULL && target_kind == TYPE_NULL) {
        return value;  // Null to null, ok
    } else if (value.type == VAL_RUNE && target_kind == TYPE_RUNE) {
        return value;  // Rune to rune, ok
    } else if (value.type == VAL_PTR && target_kind == TYPE_PTR) {
        return value;  // Ptr to ptr, ok
    } else if (value.type == VAL_BUFFER && target_kind == TYPE_BUFFER) {
        return value;  // Buffer to buffer, ok
    } else if (value.type == VAL_OBJECT && target_kind == TYPE_CUSTOM_OBJECT) {
        // Object to custom object - already handled above, but be permissive
        return value;
    } else {
        runtime_error(ctx, "Cannot convert type to target type");
        return val_null();
    }

    switch (target_kind) {
        case TYPE_I8:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < HML_I8_MIN || int_val > HML_I8_MAX) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i8 [%d, %d]\n", int_val, HML_I8_MIN, HML_I8_MAX);
                exit(1);
            }
            return val_i8((int8_t)int_val);

        case TYPE_I16:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < HML_I16_MIN || int_val > HML_I16_MAX) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i16 [%d, %d]\n", int_val, HML_I16_MIN, HML_I16_MAX);
                exit(1);
            }
            return val_i16((int16_t)int_val);

        case TYPE_I32:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < HML_I32_MIN || int_val > HML_I32_MAX) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i32 [%lld, %lld]\n", int_val, (long long)HML_I32_MIN, (long long)HML_I32_MAX);
                exit(1);
            }
            return val_i32((int32_t)int_val);

        case TYPE_I64:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            // i64 can hold full int64_t range, no additional check needed for int_val
            // For float values, precision loss may occur but that's expected
            return val_i64(int_val);

        case TYPE_U8:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > HML_U8_MAX) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u8 [0, %d]\n", int_val, HML_U8_MAX);
                exit(1);
            }
            return val_u8((uint8_t)int_val);

        case TYPE_U16:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > HML_U16_MAX) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u16 [0, %d]\n", int_val, HML_U16_MAX);
                exit(1);
            }
            return val_u16((uint16_t)int_val);

        case TYPE_U32:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > HML_U32_MAX) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u32 [0, %lld]\n", int_val, (long long)HML_U32_MAX);
                exit(1);
            }
            return val_u32((uint32_t)int_val);

        case TYPE_U64:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u64 [0, 18446744073709551615]\n", int_val);
                exit(1);
            }
            return val_u64((uint64_t)int_val);

        case TYPE_F32:
            if (is_source_float) {
                return val_f32((float)float_val);
            } else {
                return val_f32((float)int_val);
            }

        case TYPE_F64:
            if (is_source_float) {
                return val_f64(float_val);
            } else {
                return val_f64((double)int_val);
            }

        case TYPE_BOOL:
            if (value.type == VAL_BOOL) {
                return value;
            }
            // String -> bool is handled above with early return
            // Allow conversion from numeric types to bool (0 = false, non-zero = true)
            if (is_source_float) {
                return val_bool(float_val != 0.0);
            }
            return val_bool(int_val != 0);

        case TYPE_STRING:
            if (value.type == VAL_STRING) {
                return value;
            }
            // Allow conversion from rune to string
            if (value.type == VAL_RUNE) {
                char rune_bytes[5];  // Max 4 bytes + null terminator
                int rune_len = utf8_encode(value.as.as_rune, rune_bytes);
                rune_bytes[rune_len] = '\0';
                return val_string(rune_bytes);
            }
            // Allow conversion from bool to string
            if (value.type == VAL_BOOL) {
                return val_string(value.as.as_bool ? "true" : "false");
            }
            // Allow conversion from numeric types to string
            if (is_integer(value)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRId64, value_to_int64(value));
                return val_string(buf);
            }
            if (is_float(value)) {
                char buf[64];
                double fval = value_to_float(value);
                // Use %g to avoid trailing zeros, but ensure we get enough precision
                snprintf(buf, sizeof(buf), "%.17g", fval);
                return val_string(buf);
            }
            fprintf(stderr, "Runtime error: Cannot convert to string\n");
            exit(1);

        case TYPE_RUNE:
            if (value.type == VAL_RUNE) {
                return value;
            }
            // Allow conversion from integers to rune
            if (is_integer(value)) {
                int64_t codepoint = value_to_int(value);
                if (codepoint < 0 || codepoint > 0x10FFFF) {
                    fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for rune [0, 0x10FFFF]\n", codepoint);
                    exit(1);
                }
                return val_rune((uint32_t)codepoint);
            }
            fprintf(stderr, "Runtime error: Cannot convert to rune\n");
            exit(1);

        case TYPE_PTR:
            if (value.type == VAL_PTR) {
                return value;
            }
            fprintf(stderr, "Runtime error: Cannot convert to ptr\n");
            exit(1);

        case TYPE_BUFFER:
            if (value.type == VAL_BUFFER) {
                return value;
            }
            fprintf(stderr, "Runtime error: Cannot convert to buffer\n");
            exit(1);

        case TYPE_ARRAY:
            if (value.type == VAL_ARRAY) {
                return value;
            }
            fprintf(stderr, "Runtime error: Cannot convert to array\n");
            exit(1);

        case TYPE_NULL:
            return val_null();

        case TYPE_INFER:
            return value;  // No conversion needed

        case TYPE_ENUM:
            // Enum type should be handled earlier in the function
            // If we reach here, something went wrong
            fprintf(stderr, "Runtime error: Enum type should be handled earlier\n");
            exit(1);

        case TYPE_VOID:
            // Void type is only used for FFI function return types
            // Should not be converted to in normal code
            fprintf(stderr, "Runtime error: Cannot convert to void type\n");
            exit(1);

        case TYPE_CUSTOM_OBJECT:
        case TYPE_GENERIC_OBJECT:
            // These should have been handled above in the early return
            fprintf(stderr, "Runtime error: Internal error - object type not handled properly\n");
            exit(1);

        case TYPE_PARAM:
            // Type parameters should be substituted before reaching this point
            // If we get here, it means we're using a generic type without type arguments
            fprintf(stderr, "Runtime error: Unresolved type parameter - generic type requires type arguments\n");
            exit(1);
    }

    fprintf(stderr, "Runtime error: Unknown type conversion\n");
    exit(1);
}

// Parse a value to a target type (for type constructors like i32("42"))
// This function ALLOWS string parsing, unlike convert_to_type
Value parse_string_to_type(Value value, Type *target_type, Environment *env, ExecutionContext *ctx) {
    if (!target_type) {
        return value;
    }

    TypeKind target_kind = target_type->kind;

    // Handle string parsing for type constructors
    if (value.type == VAL_STRING && target_kind == TYPE_BOOL) {
        // String to bool: check for "true" or "false"
        String *str = value.as.as_string;
        if (str && str->length == 4 &&
            str->data[0] == 't' && str->data[1] == 'r' &&
            str->data[2] == 'u' && str->data[3] == 'e') {
            return val_bool(1);
        } else if (str && str->length == 5 &&
            str->data[0] == 'f' && str->data[1] == 'a' &&
            str->data[2] == 'l' && str->data[3] == 's' && str->data[4] == 'e') {
            return val_bool(0);
        }
        fprintf(stderr, "Runtime error: Cannot parse string as bool (expected 'true' or 'false')\n");
        exit(1);
    } else if (value.type == VAL_STRING && is_numeric_type(target_kind)) {
        // String to numeric conversion - parse the string
        String *str = value.as.as_string;
        int64_t int_val = 0;
        double float_val = 0.0;
        int is_float = 0;

        if (str && str->length > 0) {
            // Create null-terminated copy for parsing
            char *cstr = malloc(str->length + 1);
            memcpy(cstr, str->data, str->length);
            cstr[str->length] = '\0';

            // Try to parse as number
            char *endptr;
            errno = 0;

            // Check for float (contains '.' or 'e'/'E')
            int has_decimal = 0;
            for (int i = 0; i < str->length; i++) {
                if (cstr[i] == '.' || cstr[i] == 'e' || cstr[i] == 'E') {
                    has_decimal = 1;
                    break;
                }
            }

            if (has_decimal) {
                float_val = strtod(cstr, &endptr);
                if (endptr == cstr || *endptr != '\0') {
                    fprintf(stderr, "Runtime error: Cannot parse '%s' as number\n", cstr);
                    free(cstr);
                    exit(1);
                }
                is_float = 1;
            } else {
                int_val = strtoll(cstr, &endptr, 0);  // base 0 supports hex, octal
                if (endptr == cstr || *endptr != '\0') {
                    fprintf(stderr, "Runtime error: Cannot parse '%s' as integer\n", cstr);
                    free(cstr);
                    exit(1);
                }
            }
            free(cstr);
        } else {
            fprintf(stderr, "Runtime error: Cannot convert empty string to number\n");
            exit(1);
        }

        // Now convert to target type with range checking
        switch (target_kind) {
            case TYPE_I8:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -128 || int_val > 127) {
                    fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i8 [-128, 127]\n", int_val);
                    exit(1);
                }
                return val_i8((int8_t)int_val);

            case TYPE_I16:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -32768 || int_val > 32767) {
                    fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i16 [-32768, 32767]\n", int_val);
                    exit(1);
                }
                return val_i16((int16_t)int_val);

            case TYPE_I32:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < HML_I32_MIN || int_val > HML_I32_MAX) {
                    fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i32\n", int_val);
                    exit(1);
                }
                return val_i32((int32_t)int_val);

            case TYPE_I64:
                if (is_float) int_val = (int64_t)float_val;
                return val_i64(int_val);

            case TYPE_U8:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > HML_U8_MAX) {
                    fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u8 [0, %d]\n", int_val, HML_U8_MAX);
                    exit(1);
                }
                return val_u8((uint8_t)int_val);

            case TYPE_U16:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > HML_U16_MAX) {
                    fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u16 [0, %d]\n", int_val, HML_U16_MAX);
                    exit(1);
                }
                return val_u16((uint16_t)int_val);

            case TYPE_U32:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > HML_U32_MAX) {
                    fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u32\n", int_val);
                    exit(1);
                }
                return val_u32((uint32_t)int_val);

            case TYPE_U64:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0) {
                    fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u64\n", int_val);
                    exit(1);
                }
                return val_u64((uint64_t)int_val);

            case TYPE_F32:
                if (is_float) {
                    return val_f32((float)float_val);
                } else {
                    return val_f32((float)int_val);
                }

            case TYPE_F64:
                if (is_float) {
                    return val_f64(float_val);
                } else {
                    return val_f64((double)int_val);
                }

            default:
                break;
        }
    }

    // For non-string values, fall back to regular convert_to_type
    return convert_to_type(value, target_type, env, ctx);
}
