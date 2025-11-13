#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

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
    if (!str) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    str->length = len;
    str->char_length = -1;  // Cache not yet computed
    str->capacity = len + 1;
    str->data = malloc(str->capacity);
    if (!str->data) {
        free(str);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    memcpy(str->data, cstr, len);
    str->data[len] = '\0';
    return str;
}

String* string_copy(String *str) {
    String *copy = malloc(sizeof(String));
    if (!copy) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    copy->length = str->length;
    copy->char_length = str->char_length;  // Copy cached value
    copy->capacity = str->capacity;
    copy->data = malloc(copy->capacity);
    if (!copy->data) {
        free(copy);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    memcpy(copy->data, str->data, str->length + 1);
    return copy;
}

String* string_concat(String *a, String *b) {
    int new_len = a->length + b->length;
    String *result = malloc(sizeof(String));
    if (!result) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    result->length = new_len;
    result->char_length = -1;  // Cache invalidated after concatenation
    result->capacity = new_len + 1;
    result->data = malloc(result->capacity);
    if (!result->data) {
        free(result);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

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
    if (!str) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    str->data = data;
    str->length = length;
    str->char_length = -1;  // Cache not yet computed
    str->capacity = capacity;
    v.as.as_string = str;
    return v;
}

Value val_rune(uint32_t codepoint) {
    if (codepoint > 0x10FFFF) {
        fprintf(stderr, "Runtime error: Invalid Unicode codepoint: 0x%X (max is 0x10FFFF)\n", codepoint);
        exit(1);
    }
    Value v;
    v.type = VAL_RUNE;
    v.as.as_rune = codepoint;
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
    if (!buf) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    buf->data = malloc(size);
    if (buf->data == NULL) {
        free(buf);
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
    if (!arr) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    arr->capacity = 8;
    arr->length = 0;
    arr->elements = malloc(sizeof(Value) * arr->capacity);
    if (!arr->elements) {
        free(arr);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
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
    Value *new_elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
    if (!new_elements) {
        fprintf(stderr, "Runtime error: Memory allocation failed during array growth\n");
        exit(1);
    }
    arr->elements = new_elements;
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
            // Recursively free field values
            // WARNING: This will cause infinite recursion on circular references
            value_free(obj->field_values[i]);
        }
        free(obj->field_names);
        free(obj->field_values);
        free(obj);
    }
}

Object* object_new(char *type_name, int initial_capacity) {
    Object *obj = malloc(sizeof(Object));
    if (!obj) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    obj->type_name = type_name ? strdup(type_name) : NULL;
    obj->field_names = malloc(sizeof(char*) * initial_capacity);
    if (!obj->field_names) {
        free(obj);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    obj->field_values = malloc(sizeof(Value) * initial_capacity);
    if (!obj->field_values) {
        free(obj->field_names);
        free(obj);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
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

// ========== TASK OPERATIONS ==========

Task* task_new(int id, Function *function, Value *args, int num_args, Environment *env) {
    Task *task = malloc(sizeof(Task));
    if (!task) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    task->id = id;
    task->state = TASK_READY;
    task->function = function;
    task->args = args;
    task->num_args = num_args;
    task->result = NULL;
    task->joined = 0;
    task->env = env;
    task->ctx = exec_context_new();
    task->waiting_on = NULL;
    task->thread = NULL;
    task->detached = 0;
    return task;
}

void task_free(Task *task) {
    if (task) {
        if (task->args) {
            free(task->args);
        }
        if (task->result) {
            free(task->result);
        }
        if (task->ctx) {
            exec_context_free(task->ctx);
        }
        if (task->thread) {
            free(task->thread);
        }
        free(task);
    }
}

Value val_task(Task *task) {
    Value v;
    v.type = VAL_TASK;
    v.as.as_task = task;
    return v;
}

// ========== CHANNEL OPERATIONS ==========

Channel* channel_new(int capacity) {
    Channel *ch = malloc(sizeof(Channel));
    if (!ch) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;

    if (capacity > 0) {
        ch->buffer = malloc(sizeof(Value) * capacity);
        if (!ch->buffer) {
            free(ch);
            fprintf(stderr, "Runtime error: Memory allocation failed\n");
            exit(1);
        }
    } else {
        ch->buffer = NULL;
    }

    // Initialize pthread mutex and condition variables
    ch->mutex = malloc(sizeof(pthread_mutex_t));
    ch->not_empty = malloc(sizeof(pthread_cond_t));
    ch->not_full = malloc(sizeof(pthread_cond_t));

    if (!ch->mutex || !ch->not_empty || !ch->not_full) {
        if (ch->buffer) free(ch->buffer);
        if (ch->mutex) free(ch->mutex);
        if (ch->not_empty) free(ch->not_empty);
        if (ch->not_full) free(ch->not_full);
        free(ch);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

    pthread_mutex_init((pthread_mutex_t*)ch->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_empty, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_full, NULL);

    return ch;
}

void channel_free(Channel *ch) {
    if (ch) {
        if (ch->buffer) {
            free(ch->buffer);
        }
        if (ch->mutex) {
            pthread_mutex_destroy((pthread_mutex_t*)ch->mutex);
            free(ch->mutex);
        }
        if (ch->not_empty) {
            pthread_cond_destroy((pthread_cond_t*)ch->not_empty);
            free(ch->not_empty);
        }
        if (ch->not_full) {
            pthread_cond_destroy((pthread_cond_t*)ch->not_full);
            free(ch->not_full);
        }
        free(ch);
    }
}

Value val_channel(Channel *channel) {
    Value v;
    v.type = VAL_CHANNEL;
    v.as.as_channel = channel;
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

Value val_i64(int64_t value) {
    Value v;
    v.type = VAL_I64;
    v.as.as_i64 = value;
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

Value val_u64(uint64_t value) {
    Value v;
    v.type = VAL_U64;
    v.as.as_u64 = value;
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
        case VAL_I64:
            printf("%ld", val.as.as_i64);
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
        case VAL_U64:
            printf("%lu", val.as.as_u64);
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
        case VAL_RUNE: {
            // Print rune as character if printable, otherwise as U+XXXX
            uint32_t r = val.as.as_rune;
            if (r >= 32 && r < 127) {
                printf("'%c'", (char)r);
            } else {
                printf("U+%04X", r);
            }
            break;
        }
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
        case VAL_FFI_FUNCTION:
            printf("<ffi function>");
            break;
        case VAL_TASK:
            printf("<task id=%d state=%d>", val.as.as_task->id, val.as.as_task->state);
            break;
        case VAL_CHANNEL:
            printf("<channel capacity=%d count=%d%s>",
                   val.as.as_channel->capacity,
                   val.as.as_channel->count,
                   val.as.as_channel->closed ? " closed" : "");
            break;
        case VAL_NULL:
            printf("null");
            break;
    }
}

// ========== VALUE CLEANUP ==========

// Recursively free a Value and all its heap-allocated contents
// WARNING: Does not handle circular references - will cause infinite recursion
void value_free(Value val) {
    switch (val.type) {
        case VAL_STRING:
            if (val.as.as_string) {
                string_free(val.as.as_string);
            }
            break;
        case VAL_RUNE:
            // No cleanup needed for rune (primitive value)
            break;
        case VAL_BUFFER:
            if (val.as.as_buffer) {
                buffer_free(val.as.as_buffer);
            }
            break;
        case VAL_ARRAY:
            if (val.as.as_array) {
                Array *arr = val.as.as_array;
                // Recursively free each element
                for (int i = 0; i < arr->length; i++) {
                    value_free(arr->elements[i]);
                }
                array_free(arr);
            }
            break;
        case VAL_FILE:
            if (val.as.as_file) {
                file_free(val.as.as_file);
            }
            break;
        case VAL_OBJECT:
            if (val.as.as_object) {
                // object_free will handle field values recursively
                object_free(val.as.as_object);
            }
            break;
        case VAL_FUNCTION:
            if (val.as.as_function) {
                Function *fn = val.as.as_function;
                // Free parameter names and types
                if (fn->param_names) {
                    for (int i = 0; i < fn->num_params; i++) {
                        if (fn->param_names[i]) free(fn->param_names[i]);
                    }
                    free(fn->param_names);
                }
                if (fn->param_types) {
                    // Note: Type structs are not freed (shared/owned by AST)
                    free(fn->param_types);
                }
                // Release closure environment (reference counted)
                if (fn->closure_env) {
                    env_release(fn->closure_env);
                }
                // Note: body is not freed (shared/owned by AST)
                free(fn);
            }
            break;
        case VAL_FFI_FUNCTION:
            if (val.as.as_ffi_function) {
                // FFI functions are managed by the FFI module
                // Don't free them here
            }
            break;
        case VAL_TASK:
            if (val.as.as_task) {
                task_free(val.as.as_task);
            }
            break;
        case VAL_CHANNEL:
            if (val.as.as_channel) {
                channel_free(val.as.as_channel);
            }
            break;
        case VAL_PTR:
            // Raw pointers are user-managed - do not free
            break;
        case VAL_I8:
        case VAL_I16:
        case VAL_I32:
        case VAL_I64:
        case VAL_U8:
        case VAL_U16:
        case VAL_U32:
        case VAL_U64:
        case VAL_F32:
        case VAL_F64:
        case VAL_BOOL:
        case VAL_NULL:
        case VAL_TYPE:
        case VAL_BUILTIN_FN:
            // These types have no heap allocation
            break;
    }
}
