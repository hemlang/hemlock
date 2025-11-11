#include "interpreter.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

// ========== RETURN STATE ==========

typedef struct {
    int is_returning;
    Value return_value;
} ReturnState;

static ReturnState return_state = {0};

// ========== LOOP STATE ==========

typedef struct {
    int is_breaking;
    int is_continuing;
} LoopState;

static LoopState loop_state = {0};

// ========== EXCEPTION STATE ==========

typedef struct {
    int is_throwing;
    Value exception_value;
} ExceptionState;

static ExceptionState exception_state = {0};

// ========== OBJECT TYPE REGISTRY ==========

typedef struct {
    char *name;
    char **field_names;
    Type **field_types;
    int *field_optional;
    Expr **field_defaults;
    int num_fields;
} ObjectType;

typedef struct {
    ObjectType **types;
    int count;
    int capacity;
} ObjectTypeRegistry;

static ObjectTypeRegistry object_types = {0};

static void init_object_types() {
    if (object_types.types == NULL) {
        object_types.capacity = 16;
        object_types.types = malloc(sizeof(ObjectType*) * object_types.capacity);
        object_types.count = 0;
    }
}

static void register_object_type(ObjectType *type) {
    init_object_types();
    if (object_types.count >= object_types.capacity) {
        object_types.capacity *= 2;
        object_types.types = realloc(object_types.types, sizeof(ObjectType*) * object_types.capacity);
    }
    object_types.types[object_types.count++] = type;
}

static ObjectType* lookup_object_type(const char *name) {
    for (int i = 0; i < object_types.count; i++) {
        if (strcmp(object_types.types[i]->name, name) == 0) {
            return object_types.types[i];
        }
    }
    return NULL;
}

// Forward declarations for helper functions
static int is_integer(Value val);
static int is_float(Value val);
static Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args);
static Value call_array_method(Array *arr, const char *method, Value *args, int num_args);

// Check if an object matches a type definition (duck typing)
static Value check_object_type(Value value, ObjectType *object_type, Environment *env) {
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
                    obj->field_values[obj->num_fields] = eval_expr(object_type->field_defaults[i], env);
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
                case TYPE_I8: case TYPE_I16: case TYPE_I32:
                case TYPE_U8: case TYPE_U16: case TYPE_U32:
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

// ========== HELPERS ==========

// Helper: Check if a value is any integer type
static int is_integer(Value val) {
    return val.type == VAL_I8 || val.type == VAL_I16 || val.type == VAL_I32 ||
           val.type == VAL_U8 || val.type == VAL_U16 || val.type == VAL_U32;
}

// Helper: Check if a value is any float type
static int is_float(Value val) {
    return val.type == VAL_F32 || val.type == VAL_F64;
}

// Helper: Check if a value is any numeric type
static int is_numeric(Value val) {
    return is_integer(val) || is_float(val);
}

// Helper: Convert any integer to i32 (for operations)
static int32_t value_to_int(Value val) {
    switch (val.type) {
        case VAL_I8: return val.as.as_i8;
        case VAL_I16: return val.as.as_i16;
        case VAL_I32: return val.as.as_i32;
        case VAL_U8: return val.as.as_u8;
        case VAL_U16: return val.as.as_u16;
        case VAL_U32: return (int32_t)val.as.as_u32;  // potential overflow
        case VAL_BOOL: return val.as.as_bool;
        default:
            fprintf(stderr, "Runtime error: Cannot convert to int\n");
            exit(1);
    }
}

// Helper: Convert any numeric to f64 (for operations)
static double value_to_float(Value val) {
    switch (val.type) {
        case VAL_I8: return (double)val.as.as_i8;
        case VAL_I16: return (double)val.as.as_i16;
        case VAL_I32: return (double)val.as.as_i32;
        case VAL_U8: return (double)val.as.as_u8;
        case VAL_U16: return (double)val.as.as_u16;
        case VAL_U32: return (double)val.as.as_u32;
        case VAL_F32: return (double)val.as.as_f32;
        case VAL_F64: return val.as.as_f64;
        default:
            fprintf(stderr, "Runtime error: Cannot convert to float\n");
            exit(1);
    }
}

// Helper: Check if value is truthy
static int value_is_truthy(Value val) {
    if (val.type == VAL_BOOL) {
        return val.as.as_bool;
    } else if (is_integer(val)) {
        return value_to_int(val) != 0;
    } else if (is_float(val)) {
        return value_to_float(val) != 0.0;
    } else if (val.type == VAL_NULL) {
        return 0;
    }
    return 1;  // strings, etc. are truthy
}

// Get the "rank" of a type for promotion rules
static int type_rank(ValueType type) {
    switch (type) {
        case VAL_I8: return 0;
        case VAL_U8: return 1;
        case VAL_I16: return 2;
        case VAL_U16: return 3;
        case VAL_I32: return 4;
        case VAL_U32: return 5;
        case VAL_F32: return 6;
        case VAL_F64: return 7;
        default: return -1;
    }
}

// Determine the result type for binary operations
static ValueType promote_types(ValueType left, ValueType right) {
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
static Value promote_value(Value val, ValueType target_type) {
    if (val.type == target_type) return val;
    
    switch (target_type) {
        case VAL_I8: return val_i8((int8_t)value_to_int(val));
        case VAL_I16: return val_i16((int16_t)value_to_int(val));
        case VAL_I32: return val_i32(value_to_int(val));
        case VAL_U8: return val_u8((uint8_t)value_to_int(val));
        case VAL_U16: return val_u16((uint16_t)value_to_int(val));
        case VAL_U32: return val_u32((uint32_t)value_to_int(val));
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
        default:
            fprintf(stderr, "Runtime error: Cannot promote to type\n");
            exit(1);
    }
}

// ========== STRING OPERATIONS ==========

void string_free(String *str) {
    if (str) {
        free(str->data);
        free(str);
    }
}

String* string_new(const char *cstr) {
    int len = strlen(cstr);
    String *str = malloc(sizeof(String));
    str->length = len;
    str->capacity = len + 1;
    str->data = malloc(str->capacity);
    memcpy(str->data, cstr, len);
    str->data[len] = '\0';
    return str;
}

String* string_copy(String *str) {
    String *copy = malloc(sizeof(String));
    copy->length = str->length;
    copy->capacity = str->capacity;
    copy->data = malloc(copy->capacity);
    memcpy(copy->data, str->data, str->length + 1);
    return copy;
}

String* string_concat(String *a, String *b) {
    int new_len = a->length + b->length;
    String *result = malloc(sizeof(String));
    result->length = new_len;
    result->capacity = new_len + 1;
    result->data = malloc(result->capacity);
    
    memcpy(result->data, a->data, a->length);
    memcpy(result->data + a->length, b->data, b->length);
    result->data[new_len] = '\0';
    
    return result;
}

Value val_string(const char *str) {
    Value v;
    v.type = VAL_STRING;
    v.as.as_string = string_new(str);
    return v;
}

Value val_string_take(char *data, int length, int capacity) {
    Value v;
    v.type = VAL_STRING;
    String *str = malloc(sizeof(String));
    str->data = data;
    str->length = length;
    str->capacity = capacity;
    v.as.as_string = str;
    return v;
}

// ========== BUFFER OPERATIONS ==========

void buffer_free(Buffer *buf) {
    if (buf) {
        free(buf->data);
        free(buf);
    }
}

Value val_buffer(int size) {
    if (size <= 0) {
        fprintf(stderr, "Runtime error: buffer size must be positive\n");
        exit(1);
    }

    Value v;
    v.type = VAL_BUFFER;
    Buffer *buf = malloc(sizeof(Buffer));
    buf->data = malloc(size);
    if (buf->data == NULL) {
        fprintf(stderr, "Runtime error: Failed to allocate buffer\n");
        exit(1);
    }
    buf->length = size;
    buf->capacity = size;
    v.as.as_buffer = buf;
    return v;
}

Value val_file(FileHandle *file) {
    Value v;
    v.type = VAL_FILE;
    v.as.as_file = file;
    return v;
}

// ========== ARRAY OPERATIONS ==========

Array* array_new(void) {
    Array *arr = malloc(sizeof(Array));
    arr->capacity = 8;
    arr->length = 0;
    arr->elements = malloc(sizeof(Value) * arr->capacity);
    return arr;
}

void array_free(Array *arr) {
    if (arr) {
        free(arr->elements);
        free(arr);
    }
}

static void array_grow(Array *arr) {
    arr->capacity *= 2;
    arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
}

void array_push(Array *arr, Value val) {
    if (arr->length >= arr->capacity) {
        array_grow(arr);
    }
    arr->elements[arr->length++] = val;
}

Value array_pop(Array *arr) {
    if (arr->length == 0) {
        return val_null();
    }
    return arr->elements[--arr->length];
}

Value array_get(Array *arr, int index) {
    if (index < 0 || index >= arr->length) {
        fprintf(stderr, "Runtime error: Array index %d out of bounds (length %d)\n",
                index, arr->length);
        exit(1);
    }
    return arr->elements[index];
}

void array_set(Array *arr, int index, Value val) {
    if (index < 0) {
        fprintf(stderr, "Runtime error: Negative array index not supported\n");
        exit(1);
    }

    // Extend array if needed, filling with nulls
    while (index >= arr->length) {
        array_push(arr, val_null());
    }

    arr->elements[index] = val;
}

Value val_array(Array *arr) {
    Value v;
    v.type = VAL_ARRAY;
    v.as.as_array = arr;
    return v;
}

// ========== FILE OPERATIONS ==========

void file_free(FileHandle *file) {
    if (file) {
        if (file->fp && !file->closed) {
            fclose(file->fp);
        }
        if (file->path) free(file->path);
        if (file->mode) free(file->mode);
        free(file);
    }
}

// ========== OBJECT OPERATIONS ==========

void object_free(Object *obj) {
    if (obj) {
        if (obj->type_name) free(obj->type_name);
        for (int i = 0; i < obj->num_fields; i++) {
            free(obj->field_names[i]);
            // Note: field values are NOT freed (memory leak in v0.1)
        }
        free(obj->field_names);
        free(obj->field_values);
        free(obj);
    }
}

Object* object_new(char *type_name, int initial_capacity) {
    Object *obj = malloc(sizeof(Object));
    obj->type_name = type_name ? strdup(type_name) : NULL;
    obj->field_names = malloc(sizeof(char*) * initial_capacity);
    obj->field_values = malloc(sizeof(Value) * initial_capacity);
    obj->num_fields = 0;
    obj->capacity = initial_capacity;
    return obj;
}

Value val_object(Object *obj) {
    Value v;
    v.type = VAL_OBJECT;
    v.as.as_object = obj;
    return v;
}

// ========== VALUE OPERATIONS ==========

Value val_i8(int8_t value) {
    Value v;
    v.type = VAL_I8;
    v.as.as_i8 = value;
    return v;
}

Value val_i16(int16_t value) {
    Value v;
    v.type = VAL_I16;
    v.as.as_i16 = value;
    return v;
}

Value val_i32(int32_t value) {
    Value v;
    v.type = VAL_I32;
    v.as.as_i32 = value;
    return v;
}

Value val_u8(uint8_t value) {
    Value v;
    v.type = VAL_U8;
    v.as.as_u8 = value;
    return v;
}

Value val_u16(uint16_t value) {
    Value v;
    v.type = VAL_U16;
    v.as.as_u16 = value;
    return v;
}

Value val_u32(uint32_t value) {
    Value v;
    v.type = VAL_U32;
    v.as.as_u32 = value;
    return v;
}

Value val_f32(float value) {
    Value v;
    v.type = VAL_F32;
    v.as.as_f32 = value;
    return v;
}

Value val_f64(double value) {
    Value v;
    v.type = VAL_F64;
    v.as.as_f64 = value;
    return v;
}

Value val_int(int value) {
    return val_i32((int32_t)value);
}

Value val_float(double value) {
    return val_f64(value);
}

Value val_bool(int value) {
    Value v;
    v.type = VAL_BOOL;
    v.as.as_bool = value ? 1 : 0;
    return v;
}

Value val_ptr(void *ptr) {
    Value v;
    v.type = VAL_PTR;
    v.as.as_ptr = ptr;
    return v;
}

Value val_type(TypeKind kind) {
    Value v;
    v.type = VAL_TYPE;
    v.as.as_type = kind;
    return v;
}

Value val_function(Function *fn) {
    Value v;
    v.type = VAL_FUNCTION;
    v.as.as_function = fn;
    return v;
}

Value val_null(void) {
    Value v;
    v.type = VAL_NULL;
    return v;
}

void print_value(Value val) {
    switch (val.type) {
        case VAL_I8:
            printf("%d", val.as.as_i8);
            break;
        case VAL_I16:
            printf("%d", val.as.as_i16);
            break;
        case VAL_I32:
            printf("%d", val.as.as_i32);
            break;
        case VAL_U8:
            printf("%u", val.as.as_u8);
            break;
        case VAL_U16:
            printf("%u", val.as.as_u16);
            break;
        case VAL_U32:
            printf("%u", val.as.as_u32);
            break;
        case VAL_F32:
            printf("%g", val.as.as_f32);
            break;
        case VAL_F64:
            printf("%g", val.as.as_f64);
            break;
        case VAL_BOOL:
            printf("%s", val.as.as_bool ? "true" : "false");
            break;
        case VAL_STRING:
            printf("%s", val.as.as_string->data);
            break;
        case VAL_PTR:
            printf("%p", val.as.as_ptr);
            break;
        case VAL_BUFFER:
            printf("<buffer %p length=%d capacity=%d>",
                   val.as.as_buffer->data,
                   val.as.as_buffer->length,
                   val.as.as_buffer->capacity);
            break;
        case VAL_ARRAY: {
            Array *arr = val.as.as_array;
            printf("[");
            for (int i = 0; i < arr->length; i++) {
                if (i > 0) printf(", ");
                print_value(arr->elements[i]);
            }
            printf("]");
            break;
        }
        case VAL_FILE: {
            FileHandle *file = val.as.as_file;
            if (file->closed) {
                printf("<file (closed)>");
            } else {
                printf("<file '%s' mode='%s'>", file->path, file->mode);
            }
            break;
        }
        case VAL_OBJECT:
            if (val.as.as_object->type_name) {
                printf("<object:%s>", val.as.as_object->type_name);
            } else {
                printf("<object>");
            }
            break;
        case VAL_TYPE:
            printf("<type>");
            break;
        case VAL_BUILTIN_FN:
            printf("<builtin function>");
            break;
        case VAL_FUNCTION:
            printf("<function>");
            break;
        case VAL_NULL:
            printf("null");
            break;
    }
}

// Helper to convert a value to a target type
static Value convert_to_type(Value value, Type *target_type, Environment *env) {
    if (!target_type) {
        return value;  // No type annotation
    }

    TypeKind kind = target_type->kind;

    // Handle object types
    if (kind == TYPE_CUSTOM_OBJECT) {
        ObjectType *object_type = lookup_object_type(target_type->type_name);
        if (!object_type) {
            fprintf(stderr, "Runtime error: Unknown object type '%s'\n", target_type->type_name);
            exit(1);
        }
        return check_object_type(value, object_type, env);
    }

    if (kind == TYPE_GENERIC_OBJECT) {
        if (value.type != VAL_OBJECT) {
            fprintf(stderr, "Runtime error: Expected object, got non-object\n");
            exit(1);
        }
        return value;
    }

    // Original function continues with TypeKind
    TypeKind target_kind = kind;
    // Get the source value as the widest type for range checking
    int64_t int_val = 0;
    double float_val = 0.0;
    int is_source_float = 0;
    
    // Extract source value
    if (is_integer(value)) {
        int_val = value_to_int(value);
    } else if (is_float(value)) {
        float_val = value_to_float(value);
        is_source_float = 1;
    } else if (value.type == VAL_BOOL) {
        // Allow bool -> int conversions
        int_val = value.as.as_bool;
    } else if (value.type == VAL_STRING && target_kind == TYPE_STRING) {
        return value;  // String to string, ok
    } else if (value.type == VAL_BOOL && target_kind == TYPE_BOOL) {
        return value;  // Bool to bool, ok
    } else if (value.type == VAL_NULL && target_kind == TYPE_NULL) {
        return value;  // Null to null, ok
    } else {
        fprintf(stderr, "Runtime error: Cannot convert type to target type\n");
        exit(1);
    }

    switch (target_kind) {
        case TYPE_I8:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < -128 || int_val > 127) {
                fprintf(stderr, "Runtime error: Value %ld out of range for i8 [-128, 127]\n", int_val);
                exit(1);
            }
            return val_i8((int8_t)int_val);
            
        case TYPE_I16:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < -32768 || int_val > 32767) {
                fprintf(stderr, "Runtime error: Value %ld out of range for i16 [-32768, 32767]\n", int_val);
                exit(1);
            }
            return val_i16((int16_t)int_val);
            
        case TYPE_I32:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < -2147483648LL || int_val > 2147483647LL) {
                fprintf(stderr, "Runtime error: Value %ld out of range for i32 [-2147483648, 2147483647]\n", int_val);
                exit(1);
            }
            return val_i32((int32_t)int_val);
            
        case TYPE_U8:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > 255) {
                fprintf(stderr, "Runtime error: Value %ld out of range for u8 [0, 255]\n", int_val);
                exit(1);
            }
            return val_u8((uint8_t)int_val);
            
        case TYPE_U16:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > 65535) {
                fprintf(stderr, "Runtime error: Value %ld out of range for u16 [0, 65535]\n", int_val);
                exit(1);
            }
            return val_u16((uint16_t)int_val);
            
        case TYPE_U32:
            if (is_source_float) {
                int_val = (int64_t)float_val;
            }
            if (int_val < 0 || int_val > 4294967295LL) {
                fprintf(stderr, "Runtime error: Value %ld out of range for u32 [0, 4294967295]\n", int_val);
                exit(1);
            }
            return val_u32((uint32_t)int_val);
            
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
            fprintf(stderr, "Runtime error: Cannot convert to string\n");
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

        case TYPE_NULL:
            return val_null();

        case TYPE_INFER:
            return value;  // No conversion needed

        case TYPE_CUSTOM_OBJECT:
        case TYPE_GENERIC_OBJECT:
            // These should have been handled above in the early return
            fprintf(stderr, "Runtime error: Internal error - object type not handled properly\n");
            exit(1);
    }

    fprintf(stderr, "Runtime error: Unknown type conversion\n");
    exit(1);
}

// ========== ENVIRONMENT ==========

Environment* env_new(Environment *parent) {
    Environment *env = malloc(sizeof(Environment));
    env->capacity = 16;
    env->count = 0;
    env->names = malloc(sizeof(char*) * env->capacity);
    env->values = malloc(sizeof(Value) * env->capacity);
    env->is_const = malloc(sizeof(int) * env->capacity);
    env->parent = parent;
    return env;
}

void env_free(Environment *env) {
    for (int i = 0; i < env->count; i++) {
        free(env->names[i]);
    }
    free(env->names);
    free(env->values);
    free(env->is_const);
    free(env);
}

static void env_grow(Environment *env) {
    env->capacity *= 2;
    env->names = realloc(env->names, sizeof(char*) * env->capacity);
    env->values = realloc(env->values, sizeof(Value) * env->capacity);
    env->is_const = realloc(env->is_const, sizeof(int) * env->capacity);
}

// Define a new variable (for let/const declarations)
void env_define(Environment *env, const char *name, Value value, int is_const) {
    // Check if variable already exists in current scope
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            fprintf(stderr, "Runtime error: Variable '%s' already defined in this scope\n", name);
            exit(1);
        }
    }

    // New variable
    if (env->count >= env->capacity) {
        env_grow(env);
    }

    env->names[env->count] = strdup(name);
    env->values[env->count] = value;
    env->is_const[env->count] = is_const;
    env->count++;
}

// Set a variable (for reassignment or implicit definition in loops/functions)
void env_set(Environment *env, const char *name, Value value) {
    // Check current scope
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            // Check if variable is const
            if (env->is_const[i]) {
                fprintf(stderr, "Runtime error: Cannot assign to const variable '%s'\n", name);
                exit(1);
            }
            env->values[i] = value;
            return;
        }
    }

    // Check parent scope
    if (env->parent != NULL) {
        // Look for variable in parent scopes
        Environment *search_env = env->parent;
        while (search_env != NULL) {
            for (int i = 0; i < search_env->count; i++) {
                if (strcmp(search_env->names[i], name) == 0) {
                    // Found in parent scope - check if const
                    if (search_env->is_const[i]) {
                        fprintf(stderr, "Runtime error: Cannot assign to const variable '%s'\n", name);
                        exit(1);
                    }
                    // Update parent scope variable
                    search_env->values[i] = value;
                    return;
                }
            }
            search_env = search_env->parent;
        }
    }

    // Variable not found anywhere - create new mutable variable in current scope
    // This handles implicit variable creation in loops and function calls
    if (env->count >= env->capacity) {
        env_grow(env);
    }

    env->names[env->count] = strdup(name);
    env->values[env->count] = value;
    env->is_const[env->count] = 0;  // Always mutable for implicit variables
    env->count++;
}

Value env_get(Environment *env, const char *name) {
    // Search current scope
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            return env->values[i];
        }
    }
    
    // Search parent scope
    if (env->parent != NULL) {
        return env_get(env->parent, name);
    }
    
    // Variable not found
    fprintf(stderr, "Runtime error: Undefined variable '%s'\n", name);
    exit(1);
}

// ========== EXPRESSION EVALUATION ==========

Value eval_expr(Expr *expr, Environment *env) {
    switch (expr->type) {
        case EXPR_NUMBER:
            if (expr->as.number.is_float) {
                return val_float(expr->as.number.float_value);
            } else {
                return val_int(expr->as.number.int_value);
            }
            break;

        case EXPR_BOOL:
            return val_bool(expr->as.boolean);

        case EXPR_NULL:
            return val_null();

        case EXPR_STRING:
            return val_string(expr->as.string);

        case EXPR_UNARY: {
            Value operand = eval_expr(expr->as.unary.operand, env);
            
            switch (expr->as.unary.op) {
                case UNARY_NOT:
                    return val_bool(!value_is_truthy(operand));
                    
                case UNARY_NEGATE:
                    if (is_float(operand)) {
                        return val_f64(-value_to_float(operand));
                    } else if (is_integer(operand)) {
                        return val_i32(-value_to_int(operand));
                    }
                    fprintf(stderr, "Runtime error: Cannot negate non-numeric value\n");
                    exit(1);
            }
            break;
        }
            
        case EXPR_IDENT:
            return env_get(env, expr->as.ident);

        case EXPR_ASSIGN: {
            Value value = eval_expr(expr->as.assign.value, env);
            env_set(env, expr->as.assign.name, value);
            return value;
        }
           
        case EXPR_BINARY: {
            // Handle && and || with short-circuit evaluation
            if (expr->as.binary.op == OP_AND) {
                Value left = eval_expr(expr->as.binary.left, env);
                if (!value_is_truthy(left)) return val_bool(0);
                
                Value right = eval_expr(expr->as.binary.right, env);
                return val_bool(value_is_truthy(right));
            }
            
            if (expr->as.binary.op == OP_OR) {
                Value left = eval_expr(expr->as.binary.left, env);
                if (value_is_truthy(left)) return val_bool(1);
                
                Value right = eval_expr(expr->as.binary.right, env);
                return val_bool(value_is_truthy(right));
            }
            
            // Evaluate both operands
            Value left = eval_expr(expr->as.binary.left, env);
            Value right = eval_expr(expr->as.binary.right, env);
            
            // String concatenation
            if (expr->as.binary.op == OP_ADD && left.type == VAL_STRING && right.type == VAL_STRING) {
                String *result = string_concat(left.as.as_string, right.as.as_string);
                return (Value){ .type = VAL_STRING, .as.as_string = result };
            }

            // Pointer arithmetic
            if (left.type == VAL_PTR && is_integer(right)) {
                if (expr->as.binary.op == OP_ADD) {
                    void *ptr = left.as.as_ptr;
                    int32_t offset = value_to_int(right);
                    return val_ptr((char *)ptr + offset);
                } else if (expr->as.binary.op == OP_SUB) {
                    void *ptr = left.as.as_ptr;
                    int32_t offset = value_to_int(right);
                    return val_ptr((char *)ptr - offset);
                }
            }

            if (is_integer(left) && right.type == VAL_PTR) {
                if (expr->as.binary.op == OP_ADD) {
                    int32_t offset = value_to_int(left);
                    void *ptr = right.as.as_ptr;
                    return val_ptr((char *)ptr + offset);
                }
            }
            
            // Numeric operations
            if (!is_numeric(left) || !is_numeric(right)) {
                fprintf(stderr, "Runtime error: Binary operation requires numeric operands\n");
                exit(1);
            }
            
            // Determine result type and promote operands
            ValueType result_type = promote_types(left.type, right.type);
            left = promote_value(left, result_type);
            right = promote_value(right, result_type);
            
            // Perform operation based on result type
            if (is_float(left)) {
                // Float operation
                double l = value_to_float(left);
                double r = value_to_float(right);
                
                switch (expr->as.binary.op) {
                    case OP_ADD: 
                        return (result_type == VAL_F32) ? val_f32((float)(l + r)) : val_f64(l + r);
                    case OP_SUB:
                        return (result_type == VAL_F32) ? val_f32((float)(l - r)) : val_f64(l - r);
                    case OP_MUL:
                        return (result_type == VAL_F32) ? val_f32((float)(l * r)) : val_f64(l * r);
                    case OP_DIV:
                        if (r == 0.0) {
                            fprintf(stderr, "Runtime error: Division by zero\n");
                            exit(1);
                        }
                        return (result_type == VAL_F32) ? val_f32((float)(l / r)) : val_f64(l / r);
                    case OP_EQUAL: return val_bool(l == r);
                    case OP_NOT_EQUAL: return val_bool(l != r);
                    case OP_LESS: return val_bool(l < r);
                    case OP_LESS_EQUAL: return val_bool(l <= r);
                    case OP_GREATER: return val_bool(l > r);
                    case OP_GREATER_EQUAL: return val_bool(l >= r);
                    default: break;
                }
            } else {
                // Integer operation - use the promoted type
                int64_t l = value_to_int(left);
                int64_t r = value_to_int(right);
                
                switch (expr->as.binary.op) {
                    case OP_ADD:
                        return promote_value(val_i32((int32_t)(l + r)), result_type);
                    case OP_SUB:
                        return promote_value(val_i32((int32_t)(l - r)), result_type);
                    case OP_MUL:
                        return promote_value(val_i32((int32_t)(l * r)), result_type);
                    case OP_DIV:
                        if (r == 0) {
                            fprintf(stderr, "Runtime error: Division by zero\n");
                            exit(1);
                        }
                        return promote_value(val_i32((int32_t)(l / r)), result_type);
                    case OP_EQUAL: return val_bool(l == r);
                    case OP_NOT_EQUAL: return val_bool(l != r);
                    case OP_LESS: return val_bool(l < r);
                    case OP_LESS_EQUAL: return val_bool(l <= r);
                    case OP_GREATER: return val_bool(l > r);
                    case OP_GREATER_EQUAL: return val_bool(l >= r);
                    default: break;
                }
            }
            
            fprintf(stderr, "Runtime error: Unknown binary operator\n");
            exit(1);
        }
            
        case EXPR_CALL: {
            // Check if this is a method call (obj.method(...))
            int is_method_call = 0;
            Value method_self;

            if (expr->as.call.func->type == EXPR_GET_PROPERTY) {
                is_method_call = 1;
                method_self = eval_expr(expr->as.call.func->as.get_property.object, env);

                // Special handling for file methods
                if (method_self.type == VAL_FILE) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env);
                        }
                    }

                    Value result = call_file_method(method_self.as.as_file, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }

                // Special handling for array methods
                if (method_self.type == VAL_ARRAY) {
                    const char *method = expr->as.call.func->as.get_property.property;

                    // Evaluate arguments
                    Value *args = NULL;
                    if (expr->as.call.num_args > 0) {
                        args = malloc(sizeof(Value) * expr->as.call.num_args);
                        for (int i = 0; i < expr->as.call.num_args; i++) {
                            args[i] = eval_expr(expr->as.call.args[i], env);
                        }
                    }

                    Value result = call_array_method(method_self.as.as_array, method, args, expr->as.call.num_args);
                    if (args) free(args);
                    return result;
                }
            }

            // Evaluate the function expression
            Value func = eval_expr(expr->as.call.func, env);

            // Evaluate arguments
            Value *args = NULL;
            if (expr->as.call.num_args > 0) {
                args = malloc(sizeof(Value) * expr->as.call.num_args);
                for (int i = 0; i < expr->as.call.num_args; i++) {
                    args[i] = eval_expr(expr->as.call.args[i], env);
                }
            }

            Value result;

            if (func.type == VAL_BUILTIN_FN) {
                // Call builtin function
                BuiltinFn fn = func.as.as_builtin_fn;
                result = fn(args, expr->as.call.num_args);
            } else if (func.type == VAL_FUNCTION) {
                // Call user-defined function
                Function *fn = func.as.as_function;

                // Check argument count
                if (expr->as.call.num_args != fn->num_params) {
                    fprintf(stderr, "Runtime error: Function expects %d arguments, got %d\n",
                            fn->num_params, expr->as.call.num_args);
                    exit(1);
                }

                // Create call environment with closure_env as parent
                Environment *call_env = env_new(fn->closure_env);

                // Inject 'self' if this is a method call
                if (is_method_call) {
                    env_set(call_env, "self", method_self);
                }

                // Bind parameters
                for (int i = 0; i < fn->num_params; i++) {
                    Value arg_value = args[i];

                    // Type check if parameter has type annotation
                    if (fn->param_types[i]) {
                        arg_value = convert_to_type(arg_value, fn->param_types[i], call_env);
                    }

                    env_set(call_env, fn->param_names[i], arg_value);
                }

                // Execute body
                return_state.is_returning = 0;
                eval_stmt(fn->body, call_env);

                // Get result
                result = return_state.return_value;

                // Check return type if specified
                if (fn->return_type) {
                    if (!return_state.is_returning) {
                        fprintf(stderr, "Runtime error: Function with return type must return a value\n");
                        exit(1);
                    }
                    result = convert_to_type(result, fn->return_type, call_env);
                }

                // Reset return state
                return_state.is_returning = 0;

                // Cleanup
                // NOTE: We don't free call_env here because closures might reference it
                // This is a known memory leak in v0.1, to be fixed with refcounting in v0.2
                // env_free(call_env);
            } else {
                fprintf(stderr, "Runtime error: Value is not a function\n");
                exit(1);
            }

            if (args) free(args);
            return result;
        }

        case EXPR_GET_PROPERTY: {
            Value object = eval_expr(expr->as.get_property.object, env);
            const char *property = expr->as.get_property.property;

            if (object.type == VAL_STRING) {
                if (strcmp(property, "length") == 0) {
                    return val_int(object.as.as_string->length);
                }
                fprintf(stderr, "Runtime error: Unknown property '%s' for string\n", property);
                exit(1);
            } else if (object.type == VAL_BUFFER) {
                if (strcmp(property, "length") == 0) {
                    return val_int(object.as.as_buffer->length);
                } else if (strcmp(property, "capacity") == 0) {
                    return val_int(object.as.as_buffer->capacity);
                }
                fprintf(stderr, "Runtime error: Unknown property '%s' for buffer\n", property);
                exit(1);
            } else if (object.type == VAL_FILE) {
                FileHandle *file = object.as.as_file;
                if (strcmp(property, "mode") == 0) {
                    return val_string(file->mode);
                } else if (strcmp(property, "closed") == 0) {
                    return val_bool(file->closed);
                }
                fprintf(stderr, "Runtime error: Unknown property '%s' for file\n", property);
                exit(1);
            } else if (object.type == VAL_ARRAY) {
                // Array properties
                if (strcmp(property, "length") == 0) {
                    return val_i32(object.as.as_array->length);
                }
                fprintf(stderr, "Runtime error: Array has no property '%s'\n", property);
                exit(1);
            } else if (object.type == VAL_OBJECT) {
                // Look up field in object
                Object *obj = object.as.as_object;
                for (int i = 0; i < obj->num_fields; i++) {
                    if (strcmp(obj->field_names[i], property) == 0) {
                        return obj->field_values[i];
                    }
                }
                fprintf(stderr, "Runtime error: Object has no field '%s'\n", property);
                exit(1);
            } else {
                fprintf(stderr, "Runtime error: Only strings, buffers, arrays, and objects have properties\n");
                exit(1);
            }
        }
        
        case EXPR_INDEX: {
            Value object = eval_expr(expr->as.index.object, env);
            Value index_val = eval_expr(expr->as.index.index, env);

            if (!is_integer(index_val)) {
                fprintf(stderr, "Runtime error: Index must be an integer\n");
                exit(1);
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                if (index < 0 || index >= str->length) {
                    fprintf(stderr, "Runtime error: String index %d out of bounds (length %d)\n",
                            index, str->length);
                    exit(1);
                }

                // Return the byte as an integer (u8/char)
                return val_u8((unsigned char)str->data[index]);
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    fprintf(stderr, "Runtime error: Buffer index %d out of bounds (length %d)\n",
                            index, buf->length);
                    exit(1);
                }

                // Return the byte as an integer (u8)
                return val_u8(((unsigned char *)buf->data)[index]);
            } else if (object.type == VAL_ARRAY) {
                // Array indexing
                return array_get(object.as.as_array, index);
            } else {
                fprintf(stderr, "Runtime error: Only strings, buffers, and arrays can be indexed\n");
                exit(1);
            }
        }
        
        case EXPR_INDEX_ASSIGN: {
            Value object = eval_expr(expr->as.index_assign.object, env);
            Value index_val = eval_expr(expr->as.index_assign.index, env);
            Value value = eval_expr(expr->as.index_assign.value, env);

            if (!is_integer(index_val)) {
                fprintf(stderr, "Runtime error: Index must be an integer\n");
                exit(1);
            }

            int32_t index = value_to_int(index_val);

            if (object.type == VAL_ARRAY) {
                // Array assignment - value can be any type
                array_set(object.as.as_array, index, value);
                return value;
            }

            // For strings and buffers, value must be an integer (byte)
            if (!is_integer(value)) {
                fprintf(stderr, "Runtime error: Index value must be an integer (byte) for strings/buffers\n");
                exit(1);
            }

            if (object.type == VAL_STRING) {
                String *str = object.as.as_string;

                if (index < 0 || index >= str->length) {
                    fprintf(stderr, "Runtime error: String index %d out of bounds (length %d)\n",
                            index, str->length);
                    exit(1);
                }

                // Strings are mutable - set the byte
                str->data[index] = (char)value_to_int(value);
                return value;
            } else if (object.type == VAL_BUFFER) {
                Buffer *buf = object.as.as_buffer;

                if (index < 0 || index >= buf->length) {
                    fprintf(stderr, "Runtime error: Buffer index %d out of bounds (length %d)\n",
                            index, buf->length);
                    exit(1);
                }

                // Buffers are mutable - set the byte
                ((unsigned char *)buf->data)[index] = (unsigned char)value_to_int(value);
                return value;
            } else {
                fprintf(stderr, "Runtime error: Only strings, buffers, and arrays support index assignment\n");
                exit(1);
            }
        }

        case EXPR_FUNCTION: {
            // Create function object and capture current environment
            Function *fn = malloc(sizeof(Function));

            // Copy parameter names
            fn->param_names = malloc(sizeof(char*) * expr->as.function.num_params);
            for (int i = 0; i < expr->as.function.num_params; i++) {
                fn->param_names[i] = strdup(expr->as.function.param_names[i]);
            }

            // Copy parameter types (may be NULL)
            fn->param_types = malloc(sizeof(Type*) * expr->as.function.num_params);
            for (int i = 0; i < expr->as.function.num_params; i++) {
                if (expr->as.function.param_types[i]) {
                    fn->param_types[i] = type_new(expr->as.function.param_types[i]->kind);
                } else {
                    fn->param_types[i] = NULL;
                }
            }

            fn->num_params = expr->as.function.num_params;

            // Copy return type (may be NULL)
            if (expr->as.function.return_type) {
                fn->return_type = type_new(expr->as.function.return_type->kind);
            } else {
                fn->return_type = NULL;
            }

            // Store body AST (shared, not copied)
            fn->body = expr->as.function.body;

            // CRITICAL: Capture current environment
            fn->closure_env = env;

            return val_function(fn);
        }

        case EXPR_ARRAY_LITERAL: {
            // Create array and evaluate elements
            Array *arr = array_new();

            for (int i = 0; i < expr->as.array_literal.num_elements; i++) {
                Value element = eval_expr(expr->as.array_literal.elements[i], env);
                array_push(arr, element);
            }

            return val_array(arr);
        }

        case EXPR_OBJECT_LITERAL: {
            // Create anonymous object
            Object *obj = object_new(NULL, expr->as.object_literal.num_fields);

            // Evaluate and store fields
            for (int i = 0; i < expr->as.object_literal.num_fields; i++) {
                obj->field_names[i] = strdup(expr->as.object_literal.field_names[i]);
                obj->field_values[i] = eval_expr(expr->as.object_literal.field_values[i], env);
                obj->num_fields++;
            }

            return val_object(obj);
        }

        case EXPR_SET_PROPERTY: {
            Value object = eval_expr(expr->as.set_property.object, env);
            const char *property = expr->as.set_property.property;
            Value value = eval_expr(expr->as.set_property.value, env);

            if (object.type != VAL_OBJECT) {
                fprintf(stderr, "Runtime error: Only objects can have properties set\n");
                exit(1);
            }

            Object *obj = object.as.as_object;

            // Look for existing field
            for (int i = 0; i < obj->num_fields; i++) {
                if (strcmp(obj->field_names[i], property) == 0) {
                    obj->field_values[i] = value;
                    return value;
                }
            }

            // Field doesn't exist - add it dynamically!
            if (obj->num_fields >= obj->capacity) {
                // Grow arrays
                obj->capacity *= 2;
                obj->field_names = realloc(obj->field_names, sizeof(char*) * obj->capacity);
                obj->field_values = realloc(obj->field_values, sizeof(Value) * obj->capacity);
            }

            obj->field_names[obj->num_fields] = strdup(property);
            obj->field_values[obj->num_fields] = value;
            obj->num_fields++;

            return value;
        }
    }

    return val_null();
}

// ========== STATEMENT EVALUATION ==========

void eval_stmt(Stmt *stmt, Environment *env) {
    switch (stmt->type) {
        case STMT_LET: {
            Value value = eval_expr(stmt->as.let.value, env);
            // If there's a type annotation, convert/check the value
            if (stmt->as.let.type_annotation != NULL) {
                value = convert_to_type(value, stmt->as.let.type_annotation, env);
            }
            env_define(env, stmt->as.let.name, value, 0);  // 0 = mutable
            break;
        }

        case STMT_CONST: {
            Value value = eval_expr(stmt->as.const_stmt.value, env);
            // If there's a type annotation, convert/check the value
            if (stmt->as.const_stmt.type_annotation != NULL) {
                value = convert_to_type(value, stmt->as.const_stmt.type_annotation, env);
            }
            env_define(env, stmt->as.const_stmt.name, value, 1);  // 1 = const
            break;
        }

        case STMT_EXPR: {
            eval_expr(stmt->as.expr, env);
            break;
        }
            
        case STMT_IF: {
            Value condition = eval_expr(stmt->as.if_stmt.condition, env);

            if (value_is_truthy(condition)) {
                eval_stmt(stmt->as.if_stmt.then_branch, env);
            } else if (stmt->as.if_stmt.else_branch != NULL) {
                eval_stmt(stmt->as.if_stmt.else_branch, env);
            }
            // No need to check return here, it propagates automatically
            break;
        }

        case STMT_WHILE: {
            for (;;) {
                Value condition = eval_expr(stmt->as.while_stmt.condition, env);

                if (!value_is_truthy(condition)) break;

                eval_stmt(stmt->as.while_stmt.body, env);

                // Check for break/continue/return/exception
                if (loop_state.is_breaking) {
                    loop_state.is_breaking = 0;
                    break;
                }
                if (loop_state.is_continuing) {
                    loop_state.is_continuing = 0;
                    continue;
                }
                if (return_state.is_returning || exception_state.is_throwing) {
                    break;
                }
            }
            break;
        }

        case STMT_FOR: {
            // Create new environment for loop scope
            Environment *loop_env = env_new(env);

            // Execute initializer
            if (stmt->as.for_loop.initializer) {
                eval_stmt(stmt->as.for_loop.initializer, loop_env);
            }

            // Loop
            for (;;) {
                // Check condition
                if (stmt->as.for_loop.condition) {
                    Value cond = eval_expr(stmt->as.for_loop.condition, loop_env);
                    if (!value_is_truthy(cond)) {
                        break;
                    }
                }

                // Execute body
                eval_stmt(stmt->as.for_loop.body, loop_env);

                // Check for break/continue/return/exception
                if (loop_state.is_breaking) {
                    loop_state.is_breaking = 0;
                    break;
                }
                if (loop_state.is_continuing) {
                    loop_state.is_continuing = 0;
                    // Fall through to increment
                }
                if (return_state.is_returning || exception_state.is_throwing) {
                    break;
                }

                // Execute increment
                if (stmt->as.for_loop.increment) {
                    eval_expr(stmt->as.for_loop.increment, loop_env);
                }
            }

            env_free(loop_env);
            break;
        }

        case STMT_FOR_IN: {
            Value iterable = eval_expr(stmt->as.for_in.iterable, env);

            Environment *loop_env = env_new(env);

            if (iterable.type == VAL_ARRAY) {
                Array *arr = iterable.as.as_array;

                for (int i = 0; i < arr->length; i++) {
                    // Bind variables
                    if (stmt->as.for_in.key_var) {
                        env_set(loop_env, stmt->as.for_in.key_var, val_i32(i));
                    }
                    env_set(loop_env, stmt->as.for_in.value_var, arr->elements[i]);

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, loop_env);

                    // Check break/continue/return/exception
                    if (loop_state.is_breaking) {
                        loop_state.is_breaking = 0;
                        break;
                    }
                    if (loop_state.is_continuing) {
                        loop_state.is_continuing = 0;
                        continue;
                    }
                    if (return_state.is_returning || exception_state.is_throwing) {
                        break;
                    }
                }
            } else if (iterable.type == VAL_OBJECT) {
                Object *obj = iterable.as.as_object;

                for (int i = 0; i < obj->num_fields; i++) {
                    // Bind variables
                    if (stmt->as.for_in.key_var) {
                        env_set(loop_env, stmt->as.for_in.key_var, val_string(obj->field_names[i]));
                    }
                    env_set(loop_env, stmt->as.for_in.value_var, obj->field_values[i]);

                    // Execute body
                    eval_stmt(stmt->as.for_in.body, loop_env);

                    // Check break/continue/return/exception
                    if (loop_state.is_breaking) {
                        loop_state.is_breaking = 0;
                        break;
                    }
                    if (loop_state.is_continuing) {
                        loop_state.is_continuing = 0;
                        continue;
                    }
                    if (return_state.is_returning || exception_state.is_throwing) {
                        break;
                    }
                }
            } else {
                fprintf(stderr, "Runtime error: for-in requires array or object\n");
                exit(1);
            }

            env_free(loop_env);
            break;
        }

        case STMT_BREAK:
            loop_state.is_breaking = 1;
            break;

        case STMT_CONTINUE:
            loop_state.is_continuing = 1;
            break;

        case STMT_BLOCK: {
            for (int i = 0; i < stmt->as.block.count; i++) {
                eval_stmt(stmt->as.block.statements[i], env);
                // Check if a return/break/continue/exception happened
                if (return_state.is_returning || loop_state.is_breaking ||
                    loop_state.is_continuing || exception_state.is_throwing) {
                    break;
                }
            }
            break;
        }

        case STMT_RETURN: {
            // Evaluate return value (or null if none)
            if (stmt->as.return_stmt.value) {
                return_state.return_value = eval_expr(stmt->as.return_stmt.value, env);
            } else {
                return_state.return_value = val_null();
            }
            return_state.is_returning = 1;
            break;
        }

        case STMT_DEFINE_OBJECT: {
            // Create object type definition
            ObjectType *type = malloc(sizeof(ObjectType));
            type->name = strdup(stmt->as.define_object.name);
            type->num_fields = stmt->as.define_object.num_fields;

            // Copy field information
            type->field_names = malloc(sizeof(char*) * type->num_fields);
            type->field_types = malloc(sizeof(Type*) * type->num_fields);
            type->field_optional = malloc(sizeof(int) * type->num_fields);
            type->field_defaults = malloc(sizeof(Expr*) * type->num_fields);

            for (int i = 0; i < type->num_fields; i++) {
                type->field_names[i] = strdup(stmt->as.define_object.field_names[i]);
                type->field_types[i] = stmt->as.define_object.field_types[i];
                type->field_optional[i] = stmt->as.define_object.field_optional[i];
                type->field_defaults[i] = stmt->as.define_object.field_defaults[i];
            }

            // Register the type
            register_object_type(type);
            break;
        }

        case STMT_TRY: {
            // Execute try block
            eval_stmt(stmt->as.try_stmt.try_block, env);

            // Check if exception was thrown
            if (exception_state.is_throwing) {
                // Exception thrown - execute catch block if present
                if (stmt->as.try_stmt.catch_block != NULL) {
                    // Create new scope for catch parameter
                    Environment *catch_env = env_new(env);
                    env_set(catch_env, stmt->as.try_stmt.catch_param, exception_state.exception_value);

                    // Clear exception state
                    exception_state.is_throwing = 0;

                    // Execute catch block
                    eval_stmt(stmt->as.try_stmt.catch_block, catch_env);

                    env_free(catch_env);
                }
            }

            // Execute finally block if present (always executes)
            if (stmt->as.try_stmt.finally_block != NULL) {
                // Save current state (return/exception/break/continue)
                int was_returning = return_state.is_returning;
                Value saved_return = return_state.return_value;
                int was_throwing = exception_state.is_throwing;
                Value saved_exception = exception_state.exception_value;
                int was_breaking = loop_state.is_breaking;
                int was_continuing = loop_state.is_continuing;

                // Clear states before finally
                return_state.is_returning = 0;
                exception_state.is_throwing = 0;
                loop_state.is_breaking = 0;
                loop_state.is_continuing = 0;

                // Execute finally block
                eval_stmt(stmt->as.try_stmt.finally_block, env);

                // If finally didn't throw/return/break/continue, restore previous state
                if (!return_state.is_returning && !exception_state.is_throwing &&
                    !loop_state.is_breaking && !loop_state.is_continuing) {
                    return_state.is_returning = was_returning;
                    return_state.return_value = saved_return;
                    exception_state.is_throwing = was_throwing;
                    exception_state.exception_value = saved_exception;
                    loop_state.is_breaking = was_breaking;
                    loop_state.is_continuing = was_continuing;
                }
            }
            break;
        }

        case STMT_THROW: {
            // Evaluate the value to throw
            exception_state.exception_value = eval_expr(stmt->as.throw_stmt.value, env);
            exception_state.is_throwing = 1;
            break;
        }
    }
}

void eval_program(Stmt **stmts, int count, Environment *env) {
    for (int i = 0; i < count; i++) {
        eval_stmt(stmts[i], env);

        // Check for uncaught exception
        if (exception_state.is_throwing) {
            fprintf(stderr, "Runtime error: ");
            print_value(exception_state.exception_value);
            fprintf(stderr, "\n");
            // TODO: Add stack trace here
            exit(1);
        }
    }
}

// ========== BUILTIN FUNCTIONS ==========

// Helper: Get the size of a type
static int get_type_size(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:
        case TYPE_U8:
            return 1;
        case TYPE_I16:
        case TYPE_U16:
            return 2;
        case TYPE_I32:
        case TYPE_U32:
        case TYPE_F32:
            return 4;
        case TYPE_F64:
            return 8;
        case TYPE_PTR:
        case TYPE_BUFFER:
            return sizeof(void*);  // 8 on 64-bit systems
        case TYPE_BOOL:
            return sizeof(int);    // bool is stored as int
        case TYPE_STRING:
            return sizeof(String*); // pointer to String struct
        default:
            fprintf(stderr, "Runtime error: Cannot get size of this type\n");
            exit(1);
    }
}


static Value builtin_print(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: print() expects 1 argument\n");
        exit(1);
    }

    print_value(args[0]);
    printf("\n");
    return val_null();
}

static Value builtin_alloc(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: alloc() expects 1 argument (size in bytes)\n");
        exit(1);
    }
    
    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: alloc() size must be an integer\n");
        exit(1);
    }
    
    int32_t size = value_to_int(args[0]);
    
    if (size <= 0) {
        fprintf(stderr, "Runtime error: alloc() size must be positive\n");
        exit(1);
    }
    
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Runtime error: alloc() failed to allocate memory\n");
        exit(1);
    }
    
    return val_ptr(ptr);
}

static Value builtin_free(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: free() expects 1 argument (pointer or buffer)\n");
        exit(1);
    }

    if (args[0].type == VAL_PTR) {
        free(args[0].as.as_ptr);
        return val_null();
    } else if (args[0].type == VAL_BUFFER) {
        buffer_free(args[0].as.as_buffer);
        return val_null();
    } else {
        fprintf(stderr, "Runtime error: free() requires a pointer or buffer\n");
        exit(1);
    }
}

static Value builtin_memset(Value *args, int num_args) {
    if (num_args != 3) {
        fprintf(stderr, "Runtime error: memset() expects 3 arguments (ptr, byte, size)\n");
        exit(1);
    }
    
    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: memset() requires pointer as first argument\n");
        exit(1);
    }
    
    if (!is_integer(args[1]) || !is_integer(args[2])) {
        fprintf(stderr, "Runtime error: memset() byte and size must be integers\n");
        exit(1);
    }
    
    void *ptr = args[0].as.as_ptr;
    int byte = value_to_int(args[1]);
    int size = value_to_int(args[2]);
    
    memset(ptr, byte, size);
    return val_null();
}

static Value builtin_memcpy(Value *args, int num_args) {
    if (num_args != 3) {
        fprintf(stderr, "Runtime error: memcpy() expects 3 arguments (dest, src, size)\n");
        exit(1);
    }
    
    if (args[0].type != VAL_PTR || args[1].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: memcpy() requires pointers for dest and src\n");
        exit(1);
    }
    
    if (!is_integer(args[2])) {
        fprintf(stderr, "Runtime error: memcpy() size must be an integer\n");
        exit(1);
    }
    
    void *dest = args[0].as.as_ptr;
    void *src = args[1].as.as_ptr;
    int size = value_to_int(args[2]);
    
    memcpy(dest, src, size);
    return val_null();
}

static Value builtin_sizeof(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sizeof() expects 1 argument (type)\n");
        exit(1);
    }

    if (args[0].type != VAL_TYPE) {
        fprintf(stderr, "Runtime error: sizeof() requires a type argument\n");
        exit(1);
    }

    TypeKind kind = args[0].as.as_type;
    int size = get_type_size(kind);
    return val_i32(size);
}

static Value builtin_buffer(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: buffer() expects 1 argument (size in bytes)\n");
        exit(1);
    }

    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: buffer() size must be an integer\n");
        exit(1);
    }

    int32_t size = value_to_int(args[0]);
    return val_buffer(size);
}

static Value builtin_talloc(Value *args, int num_args) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: talloc() expects 2 arguments (type, count)\n");
        exit(1);
    }

    if (args[0].type != VAL_TYPE) {
        fprintf(stderr, "Runtime error: talloc() first argument must be a type\n");
        exit(1);
    }

    if (!is_integer(args[1])) {
        fprintf(stderr, "Runtime error: talloc() count must be an integer\n");
        exit(1);
    }

    TypeKind type = args[0].as.as_type;
    int32_t count = value_to_int(args[1]);

    if (count <= 0) {
        fprintf(stderr, "Runtime error: talloc() count must be positive\n");
        exit(1);
    }

    int elem_size = get_type_size(type);
    size_t total_size = (size_t)elem_size * (size_t)count;

    void *ptr = malloc(total_size);
    if (ptr == NULL) {
        fprintf(stderr, "Runtime error: talloc() failed to allocate memory\n");
        exit(1);
    }

    return val_ptr(ptr);
}

static Value builtin_realloc(Value *args, int num_args) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: realloc() expects 2 arguments (ptr, new_size)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: realloc() first argument must be a pointer\n");
        exit(1);
    }

    if (!is_integer(args[1])) {
        fprintf(stderr, "Runtime error: realloc() new_size must be an integer\n");
        exit(1);
    }

    void *old_ptr = args[0].as.as_ptr;
    int32_t new_size = value_to_int(args[1]);

    if (new_size <= 0) {
        fprintf(stderr, "Runtime error: realloc() new_size must be positive\n");
        exit(1);
    }

    void *new_ptr = realloc(old_ptr, new_size);
    if (new_ptr == NULL) {
        fprintf(stderr, "Runtime error: realloc() failed to allocate memory\n");
        exit(1);
    }

    return val_ptr(new_ptr);
}

// Cycle detection for serialization
typedef struct {
    Object **visited;
    int count;
    int capacity;
} VisitedSet;

static void visited_init(VisitedSet *set) {
    set->capacity = 16;
    set->count = 0;
    set->visited = malloc(sizeof(Object*) * set->capacity);
}

static int visited_contains(VisitedSet *set, Object *obj) {
    for (int i = 0; i < set->count; i++) {
        if (set->visited[i] == obj) {
            return 1;
        }
    }
    return 0;
}

static void visited_add(VisitedSet *set, Object *obj) {
    if (set->count >= set->capacity) {
        set->capacity *= 2;
        set->visited = realloc(set->visited, sizeof(Object*) * set->capacity);
    }
    set->visited[set->count++] = obj;
}

static void visited_free(VisitedSet *set) {
    free(set->visited);
}

// Helper to escape strings for JSON
static char* escape_json_string(const char *str) {
    // Count how many chars need escaping
    int escape_count = 0;
    for (const char *p = str; *p; p++) {
        if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t') {
            escape_count++;
        }
    }

    // Allocate new string with room for escapes
    int len = strlen(str);
    char *escaped = malloc(len + escape_count + 1);
    char *out = escaped;

    for (const char *p = str; *p; p++) {
        if (*p == '"') {
            *out++ = '\\';
            *out++ = '"';
        } else if (*p == '\\') {
            *out++ = '\\';
            *out++ = '\\';
        } else if (*p == '\n') {
            *out++ = '\\';
            *out++ = 'n';
        } else if (*p == '\r') {
            *out++ = '\\';
            *out++ = 'r';
        } else if (*p == '\t') {
            *out++ = '\\';
            *out++ = 't';
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';
    return escaped;
}

// Recursive serialization helper
static char* serialize_value(Value val, VisitedSet *visited) {
    char buffer[256];

    switch (val.type) {
        case VAL_I8:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i8);
            return strdup(buffer);
        case VAL_I16:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i16);
            return strdup(buffer);
        case VAL_I32:
            snprintf(buffer, sizeof(buffer), "%d", val.as.as_i32);
            return strdup(buffer);
        case VAL_U8:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u8);
            return strdup(buffer);
        case VAL_U16:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u16);
            return strdup(buffer);
        case VAL_U32:
            snprintf(buffer, sizeof(buffer), "%u", val.as.as_u32);
            return strdup(buffer);
        case VAL_F32:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f32);
            return strdup(buffer);
        case VAL_F64:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f64);
            return strdup(buffer);
        case VAL_BOOL:
            return strdup(val.as.as_bool ? "true" : "false");
        case VAL_STRING: {
            char *escaped = escape_json_string(val.as.as_string->data);
            int len = strlen(escaped) + 3;  // quotes + null
            char *result = malloc(len);
            snprintf(result, len, "\"%s\"", escaped);
            free(escaped);
            return result;
        }
        case VAL_NULL:
            return strdup("null");
        case VAL_OBJECT: {
            Object *obj = val.as.as_object;

            // Check for cycles
            if (visited_contains(visited, obj)) {
                fprintf(stderr, "Runtime error: serialize() detected circular reference\n");
                exit(1);
            }

            // Mark as visited
            visited_add(visited, obj);

            // Build JSON object
            size_t capacity = 256;
            size_t len = 0;
            char *json = malloc(capacity);
            json[len++] = '{';

            for (int i = 0; i < obj->num_fields; i++) {
                // Escape field name
                char *escaped_name = escape_json_string(obj->field_names[i]);

                // Serialize field value
                char *value_str = serialize_value(obj->field_values[i], visited);

                // Calculate space needed
                size_t needed = len + strlen(escaped_name) + strlen(value_str) + 10;
                if (needed > capacity) {
                    capacity *= 2;
                    json = realloc(json, capacity);
                }

                // Add field to JSON
                len += snprintf(json + len, capacity - len, "\"%s\":%s",
                               escaped_name, value_str);

                if (i < obj->num_fields - 1) {
                    json[len++] = ',';
                }

                free(escaped_name);
                free(value_str);
            }

            json[len++] = '}';
            json[len] = '\0';

            return json;
        }
        case VAL_ARRAY: {
            Array *arr = val.as.as_array;

            // Check for cycles (cast array pointer to object pointer for visited set)
            if (visited_contains(visited, (Object*)arr)) {
                fprintf(stderr, "Runtime error: serialize() detected circular reference\n");
                exit(1);
            }

            // Mark as visited
            visited_add(visited, (Object*)arr);

            // Build JSON array
            size_t capacity = 256;
            size_t len = 0;
            char *json = malloc(capacity);
            json[len++] = '[';

            for (int i = 0; i < arr->length; i++) {
                // Serialize element
                char *elem_str = serialize_value(arr->elements[i], visited);

                // Calculate space needed
                size_t needed = len + strlen(elem_str) + 2;
                if (needed > capacity) {
                    capacity *= 2;
                    json = realloc(json, capacity);
                }

                // Add element to JSON
                len += snprintf(json + len, capacity - len, "%s", elem_str);

                if (i < arr->length - 1) {
                    json[len++] = ',';
                }

                free(elem_str);
            }

            json[len++] = ']';
            json[len] = '\0';

            return json;
        }
        default:
            fprintf(stderr, "Runtime error: Cannot serialize value of this type\n");
            exit(1);
    }
}

static Value builtin_serialize(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: serialize() expects 1 argument\n");
        exit(1);
    }

    VisitedSet visited;
    visited_init(&visited);

    char *json = serialize_value(args[0], &visited);
    Value result = val_string(json);

    visited_free(&visited);
    free(json);

    return result;
}

// JSON deserialization
typedef struct {
    const char *input;
    int pos;
} JSONParser;

static void json_skip_whitespace(JSONParser *p) {
    while (p->input[p->pos] == ' ' || p->input[p->pos] == '\t' ||
           p->input[p->pos] == '\n' || p->input[p->pos] == '\r') {
        p->pos++;
    }
}

static Value json_parse_value(JSONParser *p);
static Value json_parse_array(JSONParser *p);

static Value json_parse_string(JSONParser *p) {
    if (p->input[p->pos] != '"') {
        fprintf(stderr, "Runtime error: Expected '\"' in JSON\n");
        exit(1);
    }
    p->pos++;  // skip opening quote

    int start = p->pos;
    int len = 0;
    char *buf = malloc(256);
    int capacity = 256;

    while (p->input[p->pos] != '"' && p->input[p->pos] != '\0') {
        if (len >= capacity - 1) {
            capacity *= 2;
            buf = realloc(buf, capacity);
        }

        if (p->input[p->pos] == '\\') {
            p->pos++;
            switch (p->input[p->pos]) {
                case 'n': buf[len++] = '\n'; break;
                case 'r': buf[len++] = '\r'; break;
                case 't': buf[len++] = '\t'; break;
                case '"': buf[len++] = '"'; break;
                case '\\': buf[len++] = '\\'; break;
                default:
                    fprintf(stderr, "Runtime error: Invalid escape sequence in JSON string\n");
                    exit(1);
            }
            p->pos++;
        } else {
            buf[len++] = p->input[p->pos++];
        }
    }

    if (p->input[p->pos] != '"') {
        fprintf(stderr, "Runtime error: Unterminated string in JSON\n");
        exit(1);
    }
    p->pos++;  // skip closing quote

    buf[len] = '\0';
    Value result = val_string(buf);
    free(buf);
    return result;
}

static Value json_parse_number(JSONParser *p) {
    int start = p->pos;
    int is_float = 0;

    // Handle negative sign
    if (p->input[p->pos] == '-') {
        p->pos++;
    }

    // Parse digits
    while (p->input[p->pos] >= '0' && p->input[p->pos] <= '9') {
        p->pos++;
    }

    // Check for decimal point
    if (p->input[p->pos] == '.') {
        is_float = 1;
        p->pos++;
        while (p->input[p->pos] >= '0' && p->input[p->pos] <= '9') {
            p->pos++;
        }
    }

    // Parse the number
    char *num_str = strndup(p->input + start, p->pos - start);
    Value result;
    if (is_float) {
        result = val_f64(atof(num_str));
    } else {
        result = val_i32(atoi(num_str));
    }
    free(num_str);
    return result;
}

static Value json_parse_object(JSONParser *p) {
    if (p->input[p->pos] != '{') {
        fprintf(stderr, "Runtime error: Expected '{' in JSON\n");
        exit(1);
    }
    p->pos++;  // skip opening brace

    char **field_names = malloc(sizeof(char*) * 32);
    Value *field_values = malloc(sizeof(Value) * 32);
    int num_fields = 0;

    json_skip_whitespace(p);

    // Handle empty object
    if (p->input[p->pos] == '}') {
        p->pos++;
        Object *obj = malloc(sizeof(Object));
        obj->field_names = field_names;
        obj->field_values = field_values;
        obj->num_fields = 0;
        obj->capacity = 32;
        obj->type_name = NULL;
        return val_object(obj);
    }

    while (p->input[p->pos] != '}' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        // Parse field name (must be a string)
        Value name_val = json_parse_string(p);
        field_names[num_fields] = strdup(name_val.as.as_string->data);

        json_skip_whitespace(p);

        // Expect colon
        if (p->input[p->pos] != ':') {
            fprintf(stderr, "Runtime error: Expected ':' in JSON object\n");
            exit(1);
        }
        p->pos++;

        json_skip_whitespace(p);

        // Parse field value
        field_values[num_fields] = json_parse_value(p);
        num_fields++;

        json_skip_whitespace(p);

        // Check for comma
        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != '}') {
            fprintf(stderr, "Runtime error: Expected ',' or '}' in JSON object\n");
            exit(1);
        }
    }

    if (p->input[p->pos] != '}') {
        fprintf(stderr, "Runtime error: Unterminated object in JSON\n");
        exit(1);
    }
    p->pos++;  // skip closing brace

    Object *obj = malloc(sizeof(Object));
    obj->field_names = field_names;
    obj->field_values = field_values;
    obj->num_fields = num_fields;
    obj->capacity = 32;
    obj->type_name = NULL;
    return val_object(obj);
}

static Value json_parse_array(JSONParser *p) {
    if (p->input[p->pos] != '[') {
        fprintf(stderr, "Runtime error: Expected '[' in JSON\n");
        exit(1);
    }
    p->pos++;  // skip opening bracket

    Array *arr = array_new();

    json_skip_whitespace(p);

    // Handle empty array
    if (p->input[p->pos] == ']') {
        p->pos++;
        return val_array(arr);
    }

    while (p->input[p->pos] != ']' && p->input[p->pos] != '\0') {
        json_skip_whitespace(p);

        // Parse element value
        Value element = json_parse_value(p);
        array_push(arr, element);

        json_skip_whitespace(p);

        // Check for comma
        if (p->input[p->pos] == ',') {
            p->pos++;
        } else if (p->input[p->pos] != ']') {
            fprintf(stderr, "Runtime error: Expected ',' or ']' in JSON array\n");
            exit(1);
        }
    }

    if (p->input[p->pos] != ']') {
        fprintf(stderr, "Runtime error: Unterminated array in JSON\n");
        exit(1);
    }
    p->pos++;  // skip closing bracket

    return val_array(arr);
}

static Value json_parse_value(JSONParser *p) {
    json_skip_whitespace(p);

    // Check first character to determine type
    char c = p->input[p->pos];

    if (c == '"') {
        return json_parse_string(p);
    } else if (c == '{') {
        return json_parse_object(p);
    } else if (c == '[') {
        return json_parse_array(p);
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        return json_parse_number(p);
    } else if (strncmp(p->input + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return val_bool(1);
    } else if (strncmp(p->input + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return val_bool(0);
    } else if (strncmp(p->input + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return val_null();
    } else {
        fprintf(stderr, "Runtime error: Unexpected character in JSON: '%c'\n", c);
        exit(1);
    }
}

static Value builtin_deserialize(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: deserialize() expects 1 argument (JSON string)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: deserialize() requires a string argument\n");
        exit(1);
    }

    JSONParser parser;
    parser.input = args[0].as.as_string->data;
    parser.pos = 0;

    Value result = json_parse_value(&parser);

    // Check that we consumed all the input
    json_skip_whitespace(&parser);
    if (parser.input[parser.pos] != '\0') {
        fprintf(stderr, "Runtime error: Unexpected trailing characters in JSON\n");
        exit(1);
    }

    return result;
}

static Value builtin_typeof(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: typeof() expects 1 argument\n");
        exit(1);
    }

    const char *type_name;
    switch (args[0].type) {
        case VAL_I8:
        case VAL_I16:
        case VAL_I32:
            type_name = "i32";
            break;
        case VAL_U8:
        case VAL_U16:
        case VAL_U32:
            type_name = "u32";
            break;
        case VAL_F32:
        case VAL_F64:
            type_name = "f64";
            break;
        case VAL_BOOL:
            type_name = "bool";
            break;
        case VAL_STRING:
            type_name = "string";
            break;
        case VAL_PTR:
            type_name = "ptr";
            break;
        case VAL_BUFFER:
            type_name = "buffer";
            break;
        case VAL_ARRAY:
            type_name = "array";
            break;
        case VAL_FILE:
            type_name = "file";
            break;
        case VAL_NULL:
            type_name = "null";
            break;
        case VAL_FUNCTION:
            type_name = "function";
            break;
        case VAL_BUILTIN_FN:
            type_name = "builtin";
            break;
        case VAL_OBJECT:
            // Check if object has a custom type name
            if (args[0].as.as_object->type_name) {
                type_name = args[0].as.as_object->type_name;
            } else {
                type_name = "object";
            }
            break;
        case VAL_TYPE:
            type_name = "type";
            break;
        default:
            type_name = "unknown";
            break;
    }

    return val_string(type_name);
}

// ========== FILE METHOD HANDLING ==========

static Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args) {
    if (file->closed) {
        fprintf(stderr, "Runtime error: Cannot call method on closed file\n");
        exit(1);
    }

    if (strcmp(method, "read_text") == 0) {
        // Read up to N bytes as string
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: read_text() expects 1 integer argument (size)\n");
            exit(1);
        }

        int size = value_to_int(args[0]);
        char *buffer = malloc(size + 1);
        size_t read = fread(buffer, 1, size, file->fp);
        buffer[read] = '\0';

        String *str = malloc(sizeof(String));
        str->data = buffer;
        str->length = read;
        str->capacity = size + 1;

        return (Value){ .type = VAL_STRING, .as.as_string = str };
    }

    if (strcmp(method, "read_bytes") == 0) {
        // Read up to N bytes as buffer
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: read_bytes() expects 1 integer argument (size)\n");
            exit(1);
        }

        int size = value_to_int(args[0]);
        void *data = malloc(size);
        size_t read = fread(data, 1, size, file->fp);

        Buffer *buf = malloc(sizeof(Buffer));
        buf->data = data;
        buf->length = read;
        buf->capacity = size;

        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    if (strcmp(method, "write") == 0) {
        // Write string or buffer
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: write() expects 1 argument (data)\n");
            exit(1);
        }

        size_t written = 0;
        if (args[0].type == VAL_STRING) {
            String *str = args[0].as.as_string;
            written = fwrite(str->data, 1, str->length, file->fp);
        } else if (args[0].type == VAL_BUFFER) {
            Buffer *buf = args[0].as.as_buffer;
            written = fwrite(buf->data, 1, buf->length, file->fp);
        } else {
            fprintf(stderr, "Runtime error: write() expects string or buffer\n");
            exit(1);
        }

        return val_i32((int32_t)written);
    }

    if (strcmp(method, "seek") == 0) {
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: seek() expects 1 integer argument (offset)\n");
            exit(1);
        }

        int offset = value_to_int(args[0]);
        fseek(file->fp, offset, SEEK_SET);
        return val_null();
    }

    if (strcmp(method, "tell") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: tell() expects no arguments\n");
            exit(1);
        }

        long pos = ftell(file->fp);
        return val_i32((int32_t)pos);
    }

    if (strcmp(method, "close") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: close() expects no arguments\n");
            exit(1);
        }

        if (file->fp) {
            fclose(file->fp);
            file->fp = NULL;
        }
        file->closed = 1;
        return val_null();
    }

    fprintf(stderr, "Runtime error: File has no method '%s'\n", method);
    exit(1);
}

static Value call_array_method(Array *arr, const char *method, Value *args, int num_args) {
    if (strcmp(method, "push") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: push() expects 1 argument\n");
            exit(1);
        }
        array_push(arr, args[0]);
        return val_null();
    }

    if (strcmp(method, "pop") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: pop() expects no arguments\n");
            exit(1);
        }
        return array_pop(arr);
    }

    fprintf(stderr, "Runtime error: Array has no method '%s'\n", method);
    exit(1);
}

// ========== I/O BUILTIN FUNCTIONS ==========

static Value builtin_read_file(Value *args, int num_args) {
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: read_file() expects 1 string argument (path)\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    // Read entire file
    char *buffer = malloc(size + 1);
    size_t read = fread(buffer, 1, size, fp);
    buffer[read] = '\0';
    fclose(fp);

    String *str = malloc(sizeof(String));
    str->data = buffer;
    str->length = read;
    str->capacity = size + 1;

    return (Value){ .type = VAL_STRING, .as.as_string = str };
}

static Value builtin_write_file(Value *args, int num_args) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: write_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: write_file() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Write string or buffer
    if (args[1].type == VAL_STRING) {
        String *str = args[1].as.as_string;
        fwrite(str->data, 1, str->length, fp);
    } else if (args[1].type == VAL_BUFFER) {
        Buffer *buf = args[1].as.as_buffer;
        fwrite(buf->data, 1, buf->length, fp);
    } else {
        fclose(fp);
        fprintf(stderr, "Runtime error: write_file() content must be string or buffer\n");
        exit(1);
    }

    fclose(fp);
    return val_null();
}

static Value builtin_append_file(Value *args, int num_args) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: append_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: append_file() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "ab");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Write string or buffer
    if (args[1].type == VAL_STRING) {
        String *str = args[1].as.as_string;
        fwrite(str->data, 1, str->length, fp);
    } else if (args[1].type == VAL_BUFFER) {
        Buffer *buf = args[1].as.as_buffer;
        fwrite(buf->data, 1, buf->length, fp);
    } else {
        fclose(fp);
        fprintf(stderr, "Runtime error: append_file() content must be string or buffer\n");
        exit(1);
    }

    fclose(fp);
    return val_null();
}

static Value builtin_read_bytes(Value *args, int num_args) {
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: read_bytes() expects 1 string argument (path)\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    // Read entire file into buffer
    void *data = malloc(size);
    size_t read = fread(data, 1, size, fp);
    fclose(fp);

    Buffer *buf = malloc(sizeof(Buffer));
    buf->data = data;
    buf->length = read;
    buf->capacity = size;

    return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
}

static Value builtin_write_bytes(Value *args, int num_args) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: write_bytes() expects 2 arguments (path, data)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: write_bytes() path must be a string\n");
        exit(1);
    }

    if (args[1].type != VAL_BUFFER) {
        fprintf(stderr, "Runtime error: write_bytes() data must be a buffer\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;
    Buffer *buf = args[1].as.as_buffer;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    fwrite(buf->data, 1, buf->length, fp);
    fclose(fp);
    return val_null();
}

static Value builtin_file_exists(Value *args, int num_args) {
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: file_exists() expects 1 string argument\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        return val_bool(1);
    }
    return val_bool(0);
}

static Value builtin_read_line(Value *args, int num_args) {
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: read_line() expects no arguments\n");
        exit(1);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read = getline(&line, &len, stdin);

    if (read == -1) {
        free(line);
        return val_null();  // EOF
    }

    // Strip newline
    if (read > 0 && line[read - 1] == '\n') {
        line[read - 1] = '\0';
        read--;
    }
    if (read > 0 && line[read - 1] == '\r') {
        line[read - 1] = '\0';
        read--;
    }

    String *str = malloc(sizeof(String));
    str->data = line;
    str->length = read;
    str->capacity = len;

    return (Value){ .type = VAL_STRING, .as.as_string = str };
}

static Value builtin_eprint(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: eprint() expects 1 argument\n");
        exit(1);
    }

    // Print to stderr
    switch (args[0].type) {
        case VAL_I8:
            fprintf(stderr, "%d", args[0].as.as_i8);
            break;
        case VAL_I16:
            fprintf(stderr, "%d", args[0].as.as_i16);
            break;
        case VAL_I32:
            fprintf(stderr, "%d", args[0].as.as_i32);
            break;
        case VAL_U8:
            fprintf(stderr, "%u", args[0].as.as_u8);
            break;
        case VAL_U16:
            fprintf(stderr, "%u", args[0].as.as_u16);
            break;
        case VAL_U32:
            fprintf(stderr, "%u", args[0].as.as_u32);
            break;
        case VAL_F32:
            fprintf(stderr, "%g", args[0].as.as_f32);
            break;
        case VAL_F64:
            fprintf(stderr, "%g", args[0].as.as_f64);
            break;
        case VAL_BOOL:
            fprintf(stderr, "%s", args[0].as.as_bool ? "true" : "false");
            break;
        case VAL_STRING:
            fprintf(stderr, "%s", args[0].as.as_string->data);
            break;
        case VAL_NULL:
            fprintf(stderr, "null");
            break;
        default:
            // For complex types, use simpler representation
            fprintf(stderr, "<value>");
            break;
    }
    fprintf(stderr, "\n");
    return val_null();
}

static Value builtin_open(Value *args, int num_args) {
    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: open() expects 1-2 arguments (path, [mode])\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: open() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;
    const char *mode = "r";  // Default mode

    if (num_args == 2) {
        if (args[1].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: open() mode must be a string\n");
            exit(1);
        }
        mode = args[1].as.as_string->data;
    }

    FILE *fp = fopen(path, mode);
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s' with mode '%s': %s\n",
                path, mode, strerror(errno));
        exit(1);
    }

    FileHandle *file = malloc(sizeof(FileHandle));
    file->fp = fp;
    file->path = strdup(path);
    file->mode = strdup(mode);
    file->closed = 0;

    return val_file(file);
}

// Structure to hold builtin function info
typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinInfo;

static BuiltinInfo builtins[] = {
    {"print", builtin_print},
    {"alloc", builtin_alloc},
    {"talloc", builtin_talloc},
    {"realloc", builtin_realloc},
    {"free", builtin_free},
    {"memset", builtin_memset},
    {"memcpy", builtin_memcpy},
    {"sizeof", builtin_sizeof},
    {"buffer", builtin_buffer},
    {"typeof", builtin_typeof},
    {"serialize", builtin_serialize},
    {"deserialize", builtin_deserialize},
    {"read_file", builtin_read_file},
    {"write_file", builtin_write_file},
    {"append_file", builtin_append_file},
    {"read_bytes", builtin_read_bytes},
    {"write_bytes", builtin_write_bytes},
    {"file_exists", builtin_file_exists},
    {"read_line", builtin_read_line},
    {"eprint", builtin_eprint},
    {"open", builtin_open},
    {NULL, NULL}  // Sentinel
};

Value val_builtin_fn(BuiltinFn fn) {
    Value v;
    v.type = VAL_BUILTIN_FN;
    v.as.as_builtin_fn = fn;
    return v;
}

void register_builtins(Environment *env, int argc, char **argv) {
    // Register type constants FIRST for use with sizeof() and talloc()
    // These must be registered before builtin functions to avoid conflicts
    env_set(env, "i8", val_type(TYPE_I8));
    env_set(env, "i16", val_type(TYPE_I16));
    env_set(env, "i32", val_type(TYPE_I32));
    env_set(env, "u8", val_type(TYPE_U8));
    env_set(env, "u16", val_type(TYPE_U16));
    env_set(env, "u32", val_type(TYPE_U32));
    env_set(env, "f32", val_type(TYPE_F32));
    env_set(env, "f64", val_type(TYPE_F64));
    env_set(env, "ptr", val_type(TYPE_PTR));

    // Type aliases
    env_set(env, "integer", val_type(TYPE_I32));
    env_set(env, "number", val_type(TYPE_F64));
    env_set(env, "char", val_type(TYPE_U8));

    // Register builtin functions (may overwrite some type names if there are conflicts)
    for (int i = 0; builtins[i].name != NULL; i++) {
        env_set(env, builtins[i].name, val_builtin_fn(builtins[i].fn));
    }

    // Register command-line arguments as 'args' array
    Array *args_array = array_new();
    for (int i = 0; i < argc; i++) {
        array_push(args_array, val_string(argv[i]));
    }
    env_set(env, "args", val_array(args_array));
}