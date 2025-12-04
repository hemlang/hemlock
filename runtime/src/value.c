/*
 * Hemlock Runtime Library - Value Implementation
 *
 * This file implements value constructors, reference counting,
 * type checking, and basic value operations.
 */

#include "../include/hemlock_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ========== VALUE CONSTRUCTORS ==========

HmlValue hml_val_i8(int8_t val) {
    HmlValue v;
    v.type = HML_VAL_I8;
    v.as.as_i8 = val;
    return v;
}

HmlValue hml_val_i16(int16_t val) {
    HmlValue v;
    v.type = HML_VAL_I16;
    v.as.as_i16 = val;
    return v;
}

HmlValue hml_val_i32(int32_t val) {
    HmlValue v;
    v.type = HML_VAL_I32;
    v.as.as_i32 = val;
    return v;
}

HmlValue hml_val_i64(int64_t val) {
    HmlValue v;
    v.type = HML_VAL_I64;
    v.as.as_i64 = val;
    return v;
}

HmlValue hml_val_u8(uint8_t val) {
    HmlValue v;
    v.type = HML_VAL_U8;
    v.as.as_u8 = val;
    return v;
}

HmlValue hml_val_u16(uint16_t val) {
    HmlValue v;
    v.type = HML_VAL_U16;
    v.as.as_u16 = val;
    return v;
}

HmlValue hml_val_u32(uint32_t val) {
    HmlValue v;
    v.type = HML_VAL_U32;
    v.as.as_u32 = val;
    return v;
}

HmlValue hml_val_u64(uint64_t val) {
    HmlValue v;
    v.type = HML_VAL_U64;
    v.as.as_u64 = val;
    return v;
}

HmlValue hml_val_f32(float val) {
    HmlValue v;
    v.type = HML_VAL_F32;
    v.as.as_f32 = val;
    return v;
}

HmlValue hml_val_f64(double val) {
    HmlValue v;
    v.type = HML_VAL_F64;
    v.as.as_f64 = val;
    return v;
}

HmlValue hml_val_bool(int val) {
    HmlValue v;
    v.type = HML_VAL_BOOL;
    v.as.as_bool = val ? 1 : 0;
    return v;
}

HmlValue hml_val_string(const char *str) {
    HmlValue v;
    v.type = HML_VAL_STRING;

    int len = (str != NULL) ? strlen(str) : 0;
    int capacity = len + 1;

    HmlString *s = malloc(sizeof(HmlString));
    s->data = malloc(capacity);
    if (str != NULL) {
        memcpy(s->data, str, len);
    }
    s->data[len] = '\0';
    s->length = len;
    s->char_length = -1;  // Uncalculated
    s->capacity = capacity;
    s->ref_count = 1;

    v.as.as_string = s;
    return v;
}

HmlValue hml_val_string_owned(char *str, int length, int capacity) {
    HmlValue v;
    v.type = HML_VAL_STRING;

    HmlString *s = malloc(sizeof(HmlString));
    s->data = str;
    s->length = length;
    s->char_length = -1;
    s->capacity = capacity;
    s->ref_count = 1;

    v.as.as_string = s;
    return v;
}

HmlValue hml_val_rune(uint32_t codepoint) {
    HmlValue v;
    v.type = HML_VAL_RUNE;
    v.as.as_rune = codepoint;
    return v;
}

HmlValue hml_val_ptr(void *ptr) {
    HmlValue v;
    v.type = HML_VAL_PTR;
    v.as.as_ptr = ptr;
    return v;
}

HmlValue hml_val_buffer(int size) {
    HmlValue v;
    v.type = HML_VAL_BUFFER;

    HmlBuffer *b = malloc(sizeof(HmlBuffer));
    b->data = calloc(size, 1);  // Zero-initialized
    b->length = size;
    b->capacity = size;
    b->ref_count = 1;

    v.as.as_buffer = b;
    return v;
}

HmlValue hml_val_array(void) {
    HmlValue v;
    v.type = HML_VAL_ARRAY;

    HmlArray *a = malloc(sizeof(HmlArray));
    a->elements = NULL;
    a->length = 0;
    a->capacity = 0;
    a->ref_count = 1;
    a->element_type = HML_VAL_NULL;  // Untyped

    v.as.as_array = a;
    return v;
}

HmlValue hml_val_object(void) {
    HmlValue v;
    v.type = HML_VAL_OBJECT;

    HmlObject *o = malloc(sizeof(HmlObject));
    o->type_name = NULL;
    o->field_names = NULL;
    o->field_values = NULL;
    o->num_fields = 0;
    o->capacity = 0;
    o->ref_count = 1;

    v.as.as_object = o;
    return v;
}

HmlValue hml_val_null(void) {
    HmlValue v;
    v.type = HML_VAL_NULL;
    v.as.as_ptr = NULL;
    return v;
}

HmlValue hml_val_function(void *fn_ptr, int num_params, int num_required, int is_async) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = malloc(sizeof(HmlFunction));
    f->fn_ptr = fn_ptr;
    f->closure_env = NULL;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_function_with_env(void *fn_ptr, void *env, int num_params, int num_required, int is_async) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = malloc(sizeof(HmlFunction));
    f->fn_ptr = fn_ptr;
    f->closure_env = env;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_builtin_fn(HmlBuiltinFn fn) {
    HmlValue v;
    v.type = HML_VAL_BUILTIN_FN;
    v.as.as_builtin_fn = fn;
    return v;
}

HmlValue hml_val_socket(HmlSocket *sock) {
    HmlValue v;
    v.type = HML_VAL_SOCKET;
    v.as.as_socket = sock;
    return v;
}

// ========== REFERENCE COUNTING ==========

void hml_retain(HmlValue *val) {
    if (val == NULL) return;

    switch (val->type) {
        case HML_VAL_STRING:
            if (val->as.as_string) val->as.as_string->ref_count++;
            break;
        case HML_VAL_BUFFER:
            if (val->as.as_buffer) val->as.as_buffer->ref_count++;
            break;
        case HML_VAL_ARRAY:
            if (val->as.as_array) val->as.as_array->ref_count++;
            break;
        case HML_VAL_OBJECT:
            if (val->as.as_object) val->as.as_object->ref_count++;
            break;
        case HML_VAL_FUNCTION:
            if (val->as.as_function) val->as.as_function->ref_count++;
            break;
        case HML_VAL_CHANNEL:
            if (val->as.as_channel) val->as.as_channel->ref_count++;
            break;
        case HML_VAL_TASK:
            if (val->as.as_task) val->as.as_task->ref_count++;
            break;
        default:
            break;  // Primitive types don't need reference counting
    }
}

static void string_free(HmlString *str) {
    if (str) {
        free(str->data);
        free(str);
    }
}

static void buffer_free(HmlBuffer *buf) {
    if (buf) {
        free(buf->data);
        free(buf);
    }
}

static void array_free(HmlArray *arr) {
    if (arr) {
        // Release all elements
        for (int i = 0; i < arr->length; i++) {
            hml_release(&arr->elements[i]);
        }
        free(arr->elements);
        free(arr);
    }
}

static void object_free(HmlObject *obj) {
    if (obj) {
        // Free field names and release field values
        for (int i = 0; i < obj->num_fields; i++) {
            free(obj->field_names[i]);
            hml_release(&obj->field_values[i]);
        }
        free(obj->field_names);
        free(obj->field_values);
        free(obj->type_name);
        free(obj);
    }
}

static void function_free(HmlFunction *fn) {
    if (fn) {
        // Note: closure_env is not freed here - it may be shared
        free(fn);
    }
}

void hml_release(HmlValue *val) {
    if (val == NULL) return;

    switch (val->type) {
        case HML_VAL_STRING:
            if (val->as.as_string) {
                val->as.as_string->ref_count--;
                if (val->as.as_string->ref_count <= 0) {
                    string_free(val->as.as_string);
                }
                val->as.as_string = NULL;
            }
            break;
        case HML_VAL_BUFFER:
            if (val->as.as_buffer) {
                val->as.as_buffer->ref_count--;
                if (val->as.as_buffer->ref_count <= 0) {
                    buffer_free(val->as.as_buffer);
                }
                val->as.as_buffer = NULL;
            }
            break;
        case HML_VAL_ARRAY:
            if (val->as.as_array) {
                val->as.as_array->ref_count--;
                if (val->as.as_array->ref_count <= 0) {
                    array_free(val->as.as_array);
                }
                val->as.as_array = NULL;
            }
            break;
        case HML_VAL_OBJECT:
            if (val->as.as_object) {
                val->as.as_object->ref_count--;
                if (val->as.as_object->ref_count <= 0) {
                    object_free(val->as.as_object);
                }
                val->as.as_object = NULL;
            }
            break;
        case HML_VAL_FUNCTION:
            if (val->as.as_function) {
                val->as.as_function->ref_count--;
                if (val->as.as_function->ref_count <= 0) {
                    function_free(val->as.as_function);
                }
                val->as.as_function = NULL;
            }
            break;
        default:
            break;  // Primitive types don't need reference counting
    }
}

// ========== TYPE CHECKING ==========

int hml_is_null(HmlValue val) {
    return val.type == HML_VAL_NULL;
}

int hml_is_i32(HmlValue val) {
    return val.type == HML_VAL_I32;
}

int hml_is_i64(HmlValue val) {
    return val.type == HML_VAL_I64;
}

int hml_is_f64(HmlValue val) {
    return val.type == HML_VAL_F64;
}

int hml_is_bool(HmlValue val) {
    return val.type == HML_VAL_BOOL;
}

int hml_is_string(HmlValue val) {
    return val.type == HML_VAL_STRING;
}

int hml_is_array(HmlValue val) {
    return val.type == HML_VAL_ARRAY;
}

int hml_is_object(HmlValue val) {
    return val.type == HML_VAL_OBJECT;
}

int hml_is_function(HmlValue val) {
    return val.type == HML_VAL_FUNCTION || val.type == HML_VAL_BUILTIN_FN;
}

int hml_is_numeric(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:
        case HML_VAL_I16:
        case HML_VAL_I32:
        case HML_VAL_I64:
        case HML_VAL_U8:
        case HML_VAL_U16:
        case HML_VAL_U32:
        case HML_VAL_U64:
        case HML_VAL_F32:
        case HML_VAL_F64:
        case HML_VAL_RUNE:  // Runes can be used in numeric operations
            return 1;
        default:
            return 0;
    }
}

int hml_is_integer(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:
        case HML_VAL_I16:
        case HML_VAL_I32:
        case HML_VAL_I64:
        case HML_VAL_U8:
        case HML_VAL_U16:
        case HML_VAL_U32:
        case HML_VAL_U64:
        case HML_VAL_RUNE:  // Runes are 32-bit integers (codepoints)
            return 1;
        default:
            return 0;
    }
}

// ========== TYPE CONVERSION ==========

int hml_to_bool(HmlValue val) {
    switch (val.type) {
        case HML_VAL_BOOL:
            return val.as.as_bool;
        case HML_VAL_I8:
            return val.as.as_i8 != 0;
        case HML_VAL_I16:
            return val.as.as_i16 != 0;
        case HML_VAL_I32:
            return val.as.as_i32 != 0;
        case HML_VAL_I64:
            return val.as.as_i64 != 0;
        case HML_VAL_U8:
            return val.as.as_u8 != 0;
        case HML_VAL_U16:
            return val.as.as_u16 != 0;
        case HML_VAL_U32:
            return val.as.as_u32 != 0;
        case HML_VAL_U64:
            return val.as.as_u64 != 0;
        case HML_VAL_F32:
            return val.as.as_f32 != 0.0f;
        case HML_VAL_F64:
            return val.as.as_f64 != 0.0;
        case HML_VAL_STRING:
            return val.as.as_string != NULL && val.as.as_string->length > 0;
        case HML_VAL_ARRAY:
            return val.as.as_array != NULL && val.as.as_array->length > 0;
        case HML_VAL_NULL:
            return 0;
        default:
            return 1;  // Non-null objects are truthy
    }
}

int32_t hml_to_i32(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:
            return (int32_t)val.as.as_i8;
        case HML_VAL_I16:
            return (int32_t)val.as.as_i16;
        case HML_VAL_I32:
            return val.as.as_i32;
        case HML_VAL_I64:
            return (int32_t)val.as.as_i64;
        case HML_VAL_U8:
            return (int32_t)val.as.as_u8;
        case HML_VAL_U16:
            return (int32_t)val.as.as_u16;
        case HML_VAL_U32:
            return (int32_t)val.as.as_u32;
        case HML_VAL_U64:
            return (int32_t)val.as.as_u64;
        case HML_VAL_F32:
            return (int32_t)val.as.as_f32;
        case HML_VAL_F64:
            return (int32_t)val.as.as_f64;
        case HML_VAL_BOOL:
            return val.as.as_bool ? 1 : 0;
        case HML_VAL_RUNE:
            return (int32_t)val.as.as_rune;
        default:
            return 0;
    }
}

int64_t hml_to_i64(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:
            return (int64_t)val.as.as_i8;
        case HML_VAL_I16:
            return (int64_t)val.as.as_i16;
        case HML_VAL_I32:
            return (int64_t)val.as.as_i32;
        case HML_VAL_I64:
            return val.as.as_i64;
        case HML_VAL_U8:
            return (int64_t)val.as.as_u8;
        case HML_VAL_U16:
            return (int64_t)val.as.as_u16;
        case HML_VAL_U32:
            return (int64_t)val.as.as_u32;
        case HML_VAL_U64:
            return (int64_t)val.as.as_u64;
        case HML_VAL_F32:
            return (int64_t)val.as.as_f32;
        case HML_VAL_F64:
            return (int64_t)val.as.as_f64;
        case HML_VAL_BOOL:
            return val.as.as_bool ? 1 : 0;
        case HML_VAL_RUNE:
            return (int64_t)val.as.as_rune;
        default:
            return 0;
    }
}

double hml_to_f64(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:
            return (double)val.as.as_i8;
        case HML_VAL_I16:
            return (double)val.as.as_i16;
        case HML_VAL_I32:
            return (double)val.as.as_i32;
        case HML_VAL_I64:
            return (double)val.as.as_i64;
        case HML_VAL_U8:
            return (double)val.as.as_u8;
        case HML_VAL_U16:
            return (double)val.as.as_u16;
        case HML_VAL_U32:
            return (double)val.as.as_u32;
        case HML_VAL_U64:
            return (double)val.as.as_u64;
        case HML_VAL_F32:
            return (double)val.as.as_f32;
        case HML_VAL_F64:
            return val.as.as_f64;
        case HML_VAL_BOOL:
            return val.as.as_bool ? 1.0 : 0.0;
        default:
            return 0.0;
    }
}

const char* hml_to_string_ptr(HmlValue val) {
    if (val.type == HML_VAL_STRING && val.as.as_string) {
        return val.as.as_string->data;
    }
    return NULL;
}

// ========== TYPE NAME ==========

const char* hml_type_name(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8:      return "i8";
        case HML_VAL_I16:     return "i16";
        case HML_VAL_I32:     return "i32";
        case HML_VAL_I64:     return "i64";
        case HML_VAL_U8:      return "u8";
        case HML_VAL_U16:     return "u16";
        case HML_VAL_U32:     return "u32";
        case HML_VAL_U64:     return "u64";
        case HML_VAL_F32:     return "f32";
        case HML_VAL_F64:     return "f64";
        case HML_VAL_BOOL:    return "bool";
        case HML_VAL_STRING:  return "string";
        case HML_VAL_RUNE:    return "rune";
        case HML_VAL_PTR:     return "ptr";
        case HML_VAL_BUFFER:  return "buffer";
        case HML_VAL_ARRAY:   return "array";
        case HML_VAL_OBJECT:  return "object";
        case HML_VAL_FILE:    return "file";
        case HML_VAL_FUNCTION: return "function";
        case HML_VAL_BUILTIN_FN: return "builtin_fn";
        case HML_VAL_TASK:    return "task";
        case HML_VAL_CHANNEL: return "channel";
        case HML_VAL_SOCKET:  return "socket";
        case HML_VAL_NULL:    return "null";
        default:              return "unknown";
    }
}

const char* hml_typeof_str(HmlValue val) {
    // For objects with custom type names
    if (val.type == HML_VAL_OBJECT && val.as.as_object && val.as.as_object->type_name) {
        return val.as.as_object->type_name;
    }
    return hml_type_name(val.type);
}

// ========== CLOSURE ENVIRONMENT ==========

HmlClosureEnv* hml_closure_env_new(int num_vars) {
    HmlClosureEnv *env = malloc(sizeof(HmlClosureEnv));
    env->captured = calloc(num_vars, sizeof(HmlValue));
    env->num_captured = num_vars;
    env->ref_count = 1;

    // Initialize all captured values to null
    for (int i = 0; i < num_vars; i++) {
        env->captured[i] = hml_val_null();
    }

    return env;
}

void hml_closure_env_free(HmlClosureEnv *env) {
    if (env) {
        // Release all captured values
        for (int i = 0; i < env->num_captured; i++) {
            hml_release(&env->captured[i]);
        }
        free(env->captured);
        free(env);
    }
}

void hml_closure_env_retain(HmlClosureEnv *env) {
    if (env) {
        env->ref_count++;
    }
}

void hml_closure_env_release(HmlClosureEnv *env) {
    if (env) {
        env->ref_count--;
        if (env->ref_count <= 0) {
            hml_closure_env_free(env);
        }
    }
}

HmlValue hml_closure_env_get(HmlClosureEnv *env, int index) {
    if (env && index >= 0 && index < env->num_captured) {
        HmlValue val = env->captured[index];
        hml_retain(&val);
        return val;
    }
    return hml_val_null();
}

void hml_closure_env_set(HmlClosureEnv *env, int index, HmlValue val) {
    if (env && index >= 0 && index < env->num_captured) {
        hml_release(&env->captured[index]);
        env->captured[index] = val;
        hml_retain(&env->captured[index]);
    }
}
