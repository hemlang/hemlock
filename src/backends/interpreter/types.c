#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

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

// Check if an object matches a type definition (duck typing)
Value check_object_type(Value value, ObjectType *object_type, Environment *env, ExecutionContext *ctx) {
    if (value.type != VAL_OBJECT) {
        fprintf(stderr, "Runtime error: Expected object for type '%s', got non-object\n",
                object_type->name);
        exit(1);
    }

    Object *obj = value.as.as_object;

    // Check all required fields
    for (int i = 0; i < object_type->num_fields; i++) {
        const char *field_name = object_type->field_names[i];
        int field_optional = object_type->field_optional[i];
        Type *field_type = object_type->field_types[i];

        // Look for field in object
        int found = 0;
        Value field_value;
        for (int j = 0; j < obj->num_fields; j++) {
            if (strcmp(obj->field_names[j], field_name) == 0) {
                found = 1;
                field_value = obj->field_values[j];
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
                fprintf(stderr, "Runtime error: Object missing required field '%s' for type '%s'\n",
                        field_name, object_type->name);
                exit(1);
            }
        } else if (field_type && field_type->kind != TYPE_INFER) {
            // Type check the field if it has a type annotation
            // For now, skip nested object type checking to keep it simple
            // Just verify basic types
            int type_ok = 0;
            switch (field_type->kind) {
                case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
                case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
                    type_ok = is_integer(field_value);
                    break;
                case TYPE_F32: case TYPE_F64:
                    type_ok = is_float(field_value);
                    break;
                case TYPE_BOOL:
                    type_ok = (field_value.type == VAL_BOOL);
                    break;
                case TYPE_STRING:
                    type_ok = (field_value.type == VAL_STRING);
                    break;
                case TYPE_PTR:
                    type_ok = (field_value.type == VAL_PTR);
                    break;
                case TYPE_BUFFER:
                    type_ok = (field_value.type == VAL_BUFFER);
                    break;
                default:
                    type_ok = 1;  // Unknown types pass for now
                    break;
            }

            if (!type_ok) {
                fprintf(stderr, "Runtime error: Field '%s' has wrong type for '%s'\n",
                        field_name, object_type->name);
                exit(1);
            }
        }
    }

    // Set the type name on the object
    if (obj->type_name) free(obj->type_name);
    obj->type_name = strdup(object_type->name);

    return value;
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

    // Any float beats any int
    if (is_float((Value){.type = left})) {
        if (is_float((Value){.type = right})) {
            // Both floats - take the larger
            return (left == VAL_F64 || right == VAL_F64) ? VAL_F64 : VAL_F32;
        }
        // left is float, right is int
        return left;
    }
    if (is_float((Value){.type = right})) {
        // right is float, left is int
        return right;
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
            if (is_float(val)) {
                return val_f32((float)value_to_float(val));
            } else {
                return val_f32((float)value_to_int(val));
            }
        case VAL_F64:
            if (is_float(val)) {
                return val_f64(value_to_float(val));
            } else {
                return val_f64((double)value_to_int(val));
            }
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
        return check_object_type(value, object_type, env, ctx);
    }

    if (kind == TYPE_GENERIC_OBJECT) {
        if (value.type != VAL_OBJECT) {
            fprintf(stderr, "Runtime error: Expected object, got non-object\n");
            exit(1);
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
    } else if (value.type == VAL_BOOL && target_kind == TYPE_BOOL) {
        return value;  // Bool to bool, ok
    } else if (value.type == VAL_NULL && target_kind == TYPE_NULL) {
        return value;  // Null to null, ok
    } else if (value.type == VAL_RUNE && target_kind == TYPE_RUNE) {
        return value;  // Rune to rune, ok
    } else {
        runtime_error(ctx, "Cannot convert type to target type");
        return val_null();
    }

    switch (target_kind) {
        case TYPE_I8:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < -128 || int_val > 127) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i8 [-128, 127]\n", int_val);
                exit(1);
            }
            return val_i8((int8_t)int_val);

        case TYPE_I16:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < -32768 || int_val > 32767) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i16 [-32768, 32767]\n", int_val);
                exit(1);
            }
            return val_i16((int16_t)int_val);

        case TYPE_I32:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < -2147483648LL || int_val > 2147483647LL) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for i32 [-2147483648, 2147483647]\n", int_val);
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
            if (int_val < 0 || int_val > 255) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u8 [0, 255]\n", int_val);
                exit(1);
            }
            return val_u8((uint8_t)int_val);

        case TYPE_U16:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > 65535) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u16 [0, 65535]\n", int_val);
                exit(1);
            }
            return val_u16((uint16_t)int_val);

        case TYPE_U32:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > 4294967295LL) {
                fprintf(stderr, "Runtime error: Value %" PRId64 " out of range for u32 [0, 4294967295]\n", int_val);
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
            fprintf(stderr, "Runtime error: Cannot convert to bool\n");
            exit(1);

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
    }

    fprintf(stderr, "Runtime error: Unknown type conversion\n");
    exit(1);
}
