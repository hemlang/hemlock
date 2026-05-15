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
#include <stdatomic.h>
#include <pthread.h>

// ========== OBJECT POOL ==========
// Pre-allocate Object structs + field storage to avoid malloc/free churn
// in hot loops. Uses a free list for O(1) alloc/free.

// Must match HML_OBJECT_POOL_SIZE and HML_OBJECT_POOL_FIELDS_CAPACITY
// in include/hemlock_limits.h
#ifndef HML_OBJECT_POOL_SIZE
#define HML_OBJECT_POOL_SIZE 512
#endif
#ifndef HML_OBJECT_POOL_FIELDS_CAPACITY
#define HML_OBJECT_POOL_FIELDS_CAPACITY 8
#endif
#define OBJ_POOL_SIZE HML_OBJECT_POOL_SIZE
#define OBJ_POOL_FIELDS_CAP HML_OBJECT_POOL_FIELDS_CAPACITY

typedef struct {
    HmlObject objects[OBJ_POOL_SIZE];
    HmlFieldEntry fields_storage[OBJ_POOL_SIZE][OBJ_POOL_FIELDS_CAP];
    int free_list[OBJ_POOL_SIZE];
    int free_count;
    pthread_mutex_t mutex;
} HmlObjectPool;

static HmlObjectPool obj_pool = {0};
static pthread_once_t obj_pool_once = PTHREAD_ONCE_INIT;

static void obj_pool_init_internal(void) {
    pthread_mutex_init(&obj_pool.mutex, NULL);
    for (int i = 0; i < OBJ_POOL_SIZE; i++) {
        obj_pool.free_list[i] = OBJ_POOL_SIZE - 1 - i;
    }
    obj_pool.free_count = OBJ_POOL_SIZE;
}

static void obj_pool_init(void) {
    pthread_once(&obj_pool_once, obj_pool_init_internal);
}

static HmlObject* obj_pool_alloc(void) {
    obj_pool_init();
    pthread_mutex_lock(&obj_pool.mutex);
    if (obj_pool.free_count > 0) {
        int idx = obj_pool.free_list[--obj_pool.free_count];
        pthread_mutex_unlock(&obj_pool.mutex);

        HmlObject *obj = &obj_pool.objects[idx];
        obj->type_name = NULL;
        obj->fields = obj_pool.fields_storage[idx];
        obj->num_fields = 0;
        obj->capacity = OBJ_POOL_FIELDS_CAP;
        obj->ref_count = 1;
        atomic_store(&obj->freed, 0);
        obj->is_pooled = 1;
        obj->hash_table = NULL;
        obj->hash_capacity = 0;
        return obj;
    }
    pthread_mutex_unlock(&obj_pool.mutex);
    return NULL;
}

static int obj_is_pooled(HmlObject *obj) {
    return obj >= &obj_pool.objects[0] && obj < &obj_pool.objects[OBJ_POOL_SIZE];
}

static void obj_pool_free(HmlObject *obj) {
    if (!obj_is_pooled(obj)) return;
    int idx = (int)(obj - &obj_pool.objects[0]);

    // If fields were grown beyond pooled storage, free the grown array
    if (obj->fields != obj_pool.fields_storage[idx]) {
        free(obj->fields);
    }
    free(obj->hash_table);
    obj->fields = obj_pool.fields_storage[idx];
    obj->hash_table = NULL;
    obj->is_pooled = 0;

    pthread_mutex_lock(&obj_pool.mutex);
    obj_pool.free_list[obj_pool.free_count++] = idx;
    pthread_mutex_unlock(&obj_pool.mutex);
}

// ========== FUNCTION POOL ==========
// Pre-allocate Function structs for closures created in hot loops.

// Must match HML_FUNCTION_POOL_SIZE in include/hemlock_limits.h
#ifndef HML_FUNCTION_POOL_SIZE
#define HML_FUNCTION_POOL_SIZE 512
#endif
#define FN_POOL_SIZE HML_FUNCTION_POOL_SIZE

typedef struct {
    HmlFunction functions[FN_POOL_SIZE];
    int free_list[FN_POOL_SIZE];
    int free_count;
    pthread_mutex_t mutex;
} HmlFunctionPool;

static HmlFunctionPool fn_pool = {0};
static pthread_once_t fn_pool_once = PTHREAD_ONCE_INIT;

static void fn_pool_init_internal(void) {
    pthread_mutex_init(&fn_pool.mutex, NULL);
    for (int i = 0; i < FN_POOL_SIZE; i++) {
        fn_pool.free_list[i] = FN_POOL_SIZE - 1 - i;
    }
    fn_pool.free_count = FN_POOL_SIZE;
}

static void fn_pool_init(void) {
    pthread_once(&fn_pool_once, fn_pool_init_internal);
}

static HmlFunction* fn_pool_alloc(void) {
    fn_pool_init();
    pthread_mutex_lock(&fn_pool.mutex);
    if (fn_pool.free_count > 0) {
        int idx = fn_pool.free_list[--fn_pool.free_count];
        pthread_mutex_unlock(&fn_pool.mutex);
        return &fn_pool.functions[idx];
    }
    pthread_mutex_unlock(&fn_pool.mutex);
    return NULL;
}

static int fn_is_pooled(HmlFunction *fn) {
    return fn >= &fn_pool.functions[0] && fn < &fn_pool.functions[FN_POOL_SIZE];
}

static void fn_pool_return(HmlFunction *fn) {
    if (!fn_is_pooled(fn)) return;
    int idx = (int)(fn - &fn_pool.functions[0]);

    pthread_mutex_lock(&fn_pool.mutex);
    fn_pool.free_list[fn_pool.free_count++] = idx;
    pthread_mutex_unlock(&fn_pool.mutex);
}

// Helper: allocate a function struct (pool first, fallback to malloc)
static HmlFunction* fn_alloc(void) {
    HmlFunction *f = fn_pool_alloc();
    if (f) return f;
    f = malloc(sizeof(HmlFunction));
    if (!f) {
        fprintf(stderr, "Fatal: out of memory allocating function\n");
        abort();
    }
    return f;
}

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

// Pre-allocated single-character ASCII strings (immortal, never freed)
// Uses SSO - data stored inline in each HmlString struct
static HmlString *ascii_strings[128] = {0};

static void init_ascii_strings(void) {
    static int initialized = 0;
    if (initialized) return;
    for (int i = 0; i < 128; i++) {
        ascii_strings[i] = malloc(sizeof(HmlString));
        if (!ascii_strings[i]) {
            // Fatal error during initialization - cannot continue
            fprintf(stderr, "Fatal: Failed to allocate ASCII string pool\n");
            exit(1);
        }
        // Use SSO for ASCII cache - store data inline
        ascii_strings[i]->inline_data[0] = (char)i;
        ascii_strings[i]->inline_data[1] = '\0';
        ascii_strings[i]->data = ascii_strings[i]->inline_data;
        ascii_strings[i]->length = 1;
        ascii_strings[i]->char_length = 1;
        ascii_strings[i]->capacity = HML_SSO_THRESHOLD + 1;
        ascii_strings[i]->is_sso = 1;
        ascii_strings[i]->ref_count = HML_REFCOUNT_IMMORTAL;  // frozen: retain/release never touch it
    }
    initialized = 1;
}

HmlValue hml_val_string(const char *str) {
    HmlValue v;
    v.type = HML_VAL_STRING;

    int len = (str != NULL) ? strlen(str) : 0;

    // Fast path: single ASCII character - return pre-allocated string
    if (len == 1 && (unsigned char)str[0] < 128) {
        init_ascii_strings();
        v.as.as_string = ascii_strings[(unsigned char)str[0]];
        return v;
    }

    HmlString *s = malloc(sizeof(HmlString));
    if (!s) {
        hml_runtime_error("Out of memory allocating string");
    }

    // Small String Optimization: store small strings inline
    if (len <= HML_SSO_THRESHOLD) {
        // Use inline storage - no separate heap allocation
        if (str != NULL) {
            memcpy(s->inline_data, str, len);
        }
        s->inline_data[len] = '\0';
        s->data = s->inline_data;
        s->capacity = HML_SSO_THRESHOLD + 1;
        s->is_sso = 1;
    } else {
        // Large string - allocate on heap
        int capacity = len + 1;
        s->data = malloc(capacity);
        if (!s->data) {
            free(s);
            hml_runtime_error("Out of memory allocating string data");
        }
        if (str != NULL) {
            memcpy(s->data, str, len);
        }
        s->data[len] = '\0';
        s->capacity = capacity;
        s->is_sso = 0;
    }

    s->length = len;
    s->char_length = -1;  // Uncalculated
    s->ref_count = 1;

    v.as.as_string = s;
    return v;
}

HmlValue hml_val_string_owned(char *str, int length, int capacity) {
    HmlValue v;
    v.type = HML_VAL_STRING;

    HmlString *s = malloc(sizeof(HmlString));
    if (!s) {
        hml_runtime_error("Out of memory allocating string");
    }

    // Small String Optimization: if string is small, copy to inline storage
    // and free the original heap allocation to reduce fragmentation
    if (length <= HML_SSO_THRESHOLD) {
        // Copy to inline storage
        memcpy(s->inline_data, str, length);
        s->inline_data[length] = '\0';
        s->data = s->inline_data;
        s->capacity = HML_SSO_THRESHOLD + 1;
        s->is_sso = 1;
        // Free the original heap allocation
        free(str);
    } else {
        // Use the provided heap allocation
        s->data = str;
        s->capacity = capacity;
        s->is_sso = 0;
    }

    s->length = length;
    s->char_length = -1;
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
    HmlBuffer *b = calloc(1, sizeof(HmlBuffer));
    if (!b) {
        return hml_val_null();
    }
    b->data = calloc(size, 1);  // Zero-initialized
    if (!b->data) {
        free(b);
        return hml_val_null();
    }
    b->length = size;
    b->capacity = size;
    b->ref_count = 1;
    atomic_store(&b->freed, 0);  // Not freed
    b->parent = NULL;            // Not a view

    HmlValue v;
    v.type = HML_VAL_BUFFER;
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
    atomic_store(&a->freed, 0);  // Not freed

    v.as.as_array = a;
    return v;
}

HmlValue hml_val_object(void) {
    HmlValue v;
    v.type = HML_VAL_OBJECT;

    // Try pool first for O(1) allocation with pre-allocated field storage
    HmlObject *o = obj_pool_alloc();
    if (!o) {
        // Pool exhausted — fallback to malloc
        o = malloc(sizeof(HmlObject));
        o->type_name = NULL;
        o->fields = NULL;
        o->num_fields = 0;
        o->capacity = 0;
        o->ref_count = 1;
        atomic_store(&o->freed, 0);
        o->is_pooled = 0;
        o->hash_table = NULL;
        o->hash_capacity = 0;
    }

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
    return hml_val_function_named(fn_ptr, num_params, num_required, is_async, NULL);
}

HmlValue hml_val_function_named(void *fn_ptr, int num_params, int num_required, int is_async, const char *name) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = fn_alloc();
    f->fn_ptr = fn_ptr;
    f->closure_env = NULL;
    f->name = name ? strdup(name) : NULL;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = 0;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_function_rest(void *fn_ptr, int num_params, int num_required, int is_async, int has_rest_param) {
    return hml_val_function_rest_named(fn_ptr, num_params, num_required, is_async, has_rest_param, NULL);
}

HmlValue hml_val_function_rest_named(void *fn_ptr, int num_params, int num_required, int is_async, int has_rest_param, const char *name) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = fn_alloc();
    f->fn_ptr = fn_ptr;
    f->closure_env = NULL;
    f->name = name ? strdup(name) : NULL;
    f->param_names = NULL;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = has_rest_param;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_function_with_params(void *fn_ptr, int num_params, int num_required, int is_async, int has_rest_param, const char *name, const char **param_names) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = fn_alloc();
    f->fn_ptr = fn_ptr;
    f->closure_env = NULL;
    f->name = name ? strdup(name) : NULL;

    // Copy parameter names
    if (param_names && num_params > 0) {
        f->param_names = malloc(sizeof(char*) * num_params);
        for (int i = 0; i < num_params; i++) {
            f->param_names[i] = param_names[i] ? strdup(param_names[i]) : NULL;
        }
    } else {
        f->param_names = NULL;
    }

    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = has_rest_param;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_function_with_env(void *fn_ptr, void *env, int num_params, int num_required, int is_async) {
    return hml_val_function_with_env_named(fn_ptr, env, num_params, num_required, is_async, NULL);
}

HmlValue hml_val_function_with_env_named(void *fn_ptr, void *env, int num_params, int num_required, int is_async, const char *name) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = fn_alloc();
    f->fn_ptr = fn_ptr;
    f->closure_env = env;
    f->name = name ? strdup(name) : NULL;
    f->param_names = NULL;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = 0;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

HmlValue hml_val_function_with_env_rest(void *fn_ptr, void *env, int num_params, int num_required, int is_async, int has_rest_param) {
    return hml_val_function_with_env_rest_named(fn_ptr, env, num_params, num_required, is_async, has_rest_param, NULL);
}

HmlValue hml_val_function_with_env_rest_named(void *fn_ptr, void *env, int num_params, int num_required, int is_async, int has_rest_param, const char *name) {
    HmlValue v;
    v.type = HML_VAL_FUNCTION;

    HmlFunction *f = fn_alloc();
    f->fn_ptr = fn_ptr;
    f->closure_env = env;
    f->name = name ? strdup(name) : NULL;
    f->param_names = NULL;
    f->num_params = num_params;
    f->num_required = num_required;
    f->is_async = is_async;
    f->has_rest_param = has_rest_param;
    f->ref_count = 1;

    v.as.as_function = f;
    return v;
}

void hml_function_set_name(HmlValue fn, const char *name) {
    if (fn.type == HML_VAL_FUNCTION && fn.as.as_function != NULL) {
        HmlFunction *f = fn.as.as_function;
        // Free existing name if any
        free(f->name);
        f->name = name ? strdup(name) : NULL;
    }
}

void hml_function_set_param_names(HmlValue fn, const char **param_names, int num_params) {
    if (fn.type == HML_VAL_FUNCTION && fn.as.as_function != NULL && param_names != NULL && num_params > 0) {
        HmlFunction *f = fn.as.as_function;
        // Free existing param_names if any
        if (f->param_names) {
            for (int i = 0; i < f->num_params; i++) {
                free(f->param_names[i]);
            }
            free(f->param_names);
        }
        // Copy new param_names
        f->param_names = malloc(sizeof(char*) * num_params);
        for (int i = 0; i < num_params; i++) {
            f->param_names[i] = param_names[i] ? strdup(param_names[i]) : NULL;
        }
    }
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
            if (val->as.as_string &&
                atomic_load(&val->as.as_string->ref_count) < HML_REFCOUNT_IMMORTAL_MIN) {
                atomic_fetch_add(&val->as.as_string->ref_count, 1);
            }
            break;
        case HML_VAL_BUFFER:
            if (val->as.as_buffer) atomic_fetch_add(&val->as.as_buffer->ref_count, 1);
            break;
        case HML_VAL_ARRAY:
            if (val->as.as_array) atomic_fetch_add(&val->as.as_array->ref_count, 1);
            break;
        case HML_VAL_OBJECT:
            if (val->as.as_object) atomic_fetch_add(&val->as.as_object->ref_count, 1);
            break;
        case HML_VAL_FUNCTION:
            if (val->as.as_function) atomic_fetch_add(&val->as.as_function->ref_count, 1);
            break;
        case HML_VAL_CHANNEL:
            if (val->as.as_channel) atomic_fetch_add(&val->as.as_channel->ref_count, 1);
            break;
        case HML_VAL_TASK:
            if (val->as.as_task) atomic_fetch_add(&val->as.as_task->ref_count, 1);
            break;
        default:
            break;  // Primitive types don't need reference counting
    }
}

static void string_free(HmlString *str) {
    if (str) {
        // Only free data if it's heap-allocated (not using SSO)
        if (!str->is_sso) {
            free(str->data);
        }
        free(str);
    }
}

static void buffer_free(HmlBuffer *buf) {
    if (buf) {
        if (buf->parent) {
            // Zero-copy view: release parent instead of freeing data
            if (atomic_fetch_sub(&buf->parent->ref_count, 1) <= 1) {
                buffer_free(buf->parent);
            }
        } else {
            free(buf->data);
        }
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
        // Release field values and free field names
        for (int i = 0; i < obj->num_fields; i++) {
            free(obj->fields[i].name);
            hml_release(&obj->fields[i].value);
        }
        free(obj->type_name);
        obj->type_name = NULL;
        obj->num_fields = 0;

        // Return to pool if pooled, otherwise free everything
        if (obj->is_pooled) {
            obj_pool_free(obj);
        } else {
            free(obj->fields);
            free(obj->hash_table);
            free(obj);
        }
    }
}

static void function_free(HmlFunction *fn) {
    if (fn) {
        // Free the function name if set
        free(fn->name);
        fn->name = NULL;
        // Free parameter names if present
        if (fn->param_names) {
            for (int i = 0; i < fn->num_params; i++) {
                free(fn->param_names[i]);
            }
            free(fn->param_names);
            fn->param_names = NULL;
        }
        // Release closure environment (reference counted - handles sharing)
        if (fn->closure_env) {
            hml_closure_env_release(fn->closure_env);
            fn->closure_env = NULL;
        }
        // Return to pool if pooled, otherwise free
        if (fn_is_pooled(fn)) {
            fn_pool_return(fn);
        } else {
            free(fn);
        }
    }
}

static void task_free(HmlTask *task) {
    if (task) {
        // Release the stored function
        hml_release(&task->function);
        // Release the result
        hml_release(&task->result);
        // Free the args array
        if (task->args) {
            for (int i = 0; i < task->num_args; i++) {
                hml_release(&task->args[i]);
            }
            free(task->args);
        }
        // Free the consolidated sync structure (contains mutex, cond, thread)
        if (task->sync) {
            pthread_mutex_destroy(&task->sync->mutex);
            pthread_cond_destroy(&task->sync->cond);
            free(task->sync);
        }
        free(task->name);
        free(task);
    }
}

static void channel_free(HmlChannel *ch) {
    if (ch) {
        // Release any buffered values
        if (ch->buffer) {
            for (int i = 0; i < ch->count; i++) {
                int idx = (ch->head + i) % ch->capacity;
                hml_release(&ch->buffer[idx]);
            }
            free(ch->buffer);
        }
        // Release unbuffered value if present
        hml_release(&ch->unbuffered_value);
        // Free the consolidated sync structure
        if (ch->sync) {
            pthread_mutex_destroy(&ch->sync->mutex);
            pthread_cond_destroy(&ch->sync->not_empty);
            pthread_cond_destroy(&ch->sync->not_full);
            pthread_cond_destroy(&ch->sync->rendezvous);
            free(ch->sync);
        }
        free(ch);
    }
}

void hml_release(HmlValue *val) {
    if (val == NULL) return;

    switch (val->type) {
        case HML_VAL_STRING:
            if (val->as.as_string) {
                // Frozen (immortal pool) strings: never decrement, never free.
                // Checked before the atomic sub so the count can't underflow.
                if (atomic_load(&val->as.as_string->ref_count) < HML_REFCOUNT_IMMORTAL_MIN) {
                    if (atomic_fetch_sub(&val->as.as_string->ref_count, 1) <= 1) {
                        string_free(val->as.as_string);
                    }
                }
                val->as.as_string = NULL;
            }
            break;
        case HML_VAL_BUFFER:
            if (val->as.as_buffer) {
                if (atomic_fetch_sub(&val->as.as_buffer->ref_count, 1) <= 1) {
                    buffer_free(val->as.as_buffer);
                }
                val->as.as_buffer = NULL;
            }
            break;
        case HML_VAL_ARRAY:
            if (val->as.as_array) {
                if (atomic_fetch_sub(&val->as.as_array->ref_count, 1) <= 1) {
                    array_free(val->as.as_array);
                }
                val->as.as_array = NULL;
            }
            break;
        case HML_VAL_OBJECT:
            if (val->as.as_object) {
                if (atomic_fetch_sub(&val->as.as_object->ref_count, 1) <= 1) {
                    object_free(val->as.as_object);
                }
                val->as.as_object = NULL;
            }
            break;
        case HML_VAL_FUNCTION:
            if (val->as.as_function) {
                if (atomic_fetch_sub(&val->as.as_function->ref_count, 1) <= 1) {
                    function_free(val->as.as_function);
                }
                val->as.as_function = NULL;
            }
            break;
        case HML_VAL_TASK:
            if (val->as.as_task) {
                if (atomic_fetch_sub(&val->as.as_task->ref_count, 1) <= 1) {
                    task_free(val->as.as_task);
                }
                val->as.as_task = NULL;
            }
            break;
        case HML_VAL_CHANNEL:
            if (val->as.as_channel) {
                if (atomic_fetch_sub(&val->as.as_channel->ref_count, 1) <= 1) {
                    channel_free(val->as.as_channel);
                }
                val->as.as_channel = NULL;
            }
            break;
        default:
            break;  // Primitive types don't need reference counting
    }
}

// ========== STATIC VARIABLE CLEANUP (for compiled binaries) ==========

// Simple visited set for cycle detection during cleanup
typedef struct {
    void **items;
    int count;
    int capacity;
} HmlCleanupVisited;

static int cleanup_visited_contains(HmlCleanupVisited *v, void *ptr) {
    for (int i = 0; i < v->count; i++) {
        if (v->items[i] == ptr) return 1;
    }
    return 0;
}

static void cleanup_visited_add(HmlCleanupVisited *v, void *ptr) {
    if (v->count >= v->capacity) {
        int new_cap = v->capacity == 0 ? 32 : v->capacity * 2;
        v->items = realloc(v->items, new_cap * sizeof(void *));
        v->capacity = new_cap;
    }
    v->items[v->count++] = ptr;
}

// Recursively break circular references by detaching closure environments
static void break_cycles_walk(HmlValue val, HmlCleanupVisited *visited) {
    switch (val.type) {
        case HML_VAL_FUNCTION:
            if (val.as.as_function) {
                HmlFunction *fn = val.as.as_function;
                if (fn->closure_env) {
                    hml_closure_env_release((HmlClosureEnv *)fn->closure_env);
                    fn->closure_env = NULL;
                }
            }
            break;

        case HML_VAL_OBJECT:
            if (val.as.as_object) {
                HmlObject *obj = val.as.as_object;
                if (atomic_load(&obj->freed)) return;
                if (cleanup_visited_contains(visited, obj)) return;
                cleanup_visited_add(visited, obj);
                for (int i = 0; i < obj->num_fields; i++) {
                    break_cycles_walk(obj->fields[i].value, visited);
                }
            }
            break;

        case HML_VAL_ARRAY:
            if (val.as.as_array) {
                HmlArray *arr = val.as.as_array;
                if (atomic_load(&arr->freed)) return;
                if (cleanup_visited_contains(visited, arr)) return;
                cleanup_visited_add(visited, arr);
                for (int i = 0; i < arr->length; i++) {
                    break_cycles_walk(arr->elements[i], visited);
                }
            }
            break;

        default:
            break;
    }
}

void hml_break_cycles(HmlValue *statics[], int count) {
    HmlCleanupVisited visited = {0};
    for (int i = 0; i < count; i++) {
        break_cycles_walk(*statics[i], &visited);
    }
    free(visited.items);
}

void hml_release_statics(HmlValue *statics[], int count) {
    for (int i = count - 1; i >= 0; i--) {
        hml_release(statics[i]);
    }
}

// ========== VALUE DEEP COPY (for thread isolation) ==========

// Forward declarations for helper functions
extern void hml_array_push(HmlValue arr, HmlValue val);
extern void hml_object_set_field(HmlValue obj, const char *name, HmlValue val);

// Deep copy a value for passing to spawned tasks.
// This ensures tasks don't share mutable state with the parent thread.
HmlValue hml_value_deep_copy(HmlValue val) {
    HmlValue result = {0};

    switch (val.type) {
        // Primitive types - just return the value (immutable, safe to share)
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
        case HML_VAL_BOOL:
        case HML_VAL_RUNE:
        case HML_VAL_NULL:
        case HML_VAL_BUILTIN_FN:
            return val;

        case HML_VAL_STRING:
            if (val.as.as_string) {
                HmlString *src = val.as.as_string;
                // Create a new string with copied data using SSO if possible
                HmlString *dst = malloc(sizeof(HmlString));
                dst->length = src->length;
                dst->char_length = src->char_length;
                dst->ref_count = 1;

                // Use SSO for small strings
                if (src->length <= HML_SSO_THRESHOLD) {
                    memcpy(dst->inline_data, src->data, src->length + 1);
                    dst->data = dst->inline_data;
                    dst->capacity = HML_SSO_THRESHOLD + 1;
                    dst->is_sso = 1;
                } else {
                    dst->capacity = src->length + 1;
                    dst->data = malloc(dst->capacity);
                    memcpy(dst->data, src->data, src->length + 1);
                    dst->is_sso = 0;
                }

                result.type = HML_VAL_STRING;
                result.as.as_string = dst;
            } else {
                result = hml_val_null();
            }
            break;

        case HML_VAL_BUFFER:
            if (val.as.as_buffer) {
                HmlBuffer *src = val.as.as_buffer;
                // Create a new buffer with copied data
                result = hml_val_buffer(src->length);
                memcpy(result.as.as_buffer->data, src->data, src->length);
            } else {
                result = hml_val_null();
            }
            break;

        case HML_VAL_ARRAY:
            if (val.as.as_array) {
                HmlArray *src = val.as.as_array;
                // Create a new array
                result = hml_val_array();
                HmlArray *dst = result.as.as_array;

                // Copy element type
                dst->element_type = src->element_type;

                // Deep copy each element
                for (int i = 0; i < src->length; i++) {
                    HmlValue elem_copy = hml_value_deep_copy(src->elements[i]);
                    hml_array_push(result, elem_copy);
                    hml_release(&elem_copy);  // array_push retains
                }
            } else {
                result = hml_val_null();
            }
            break;

        case HML_VAL_OBJECT:
            if (val.as.as_object) {
                HmlObject *src = val.as.as_object;
                // Create a new object
                result = hml_val_object();
                HmlObject *dst = result.as.as_object;

                // Copy type name if present
                if (src->type_name) {
                    dst->type_name = strdup(src->type_name);
                }

                // Deep copy each field
                for (int i = 0; i < src->num_fields; i++) {
                    HmlValue field_copy = hml_value_deep_copy(src->fields[i].value);
                    hml_object_set_field(result, src->fields[i].name, field_copy);
                    hml_release(&field_copy);  // set_field retains
                }
            } else {
                result = hml_val_null();
            }
            break;

        case HML_VAL_PTR:
            // Share raw pointers by reference - the programmer is responsible
            // for synchronization, matching Hemlock's "unsafe is a feature" philosophy.
            // This enables FFI handles (e.g. WebSocket ptrs) inside objects passed to tasks.
            return val;

        case HML_VAL_FILE:
        case HML_VAL_SOCKET:
            // OS resources - kernel handles concurrent access
            // Share by reference (retain the value)
            hml_retain(&val);
            return val;

        case HML_VAL_FUNCTION:
            // Functions are shared by reference
            // The closure environment is already designed for this
            if (val.as.as_function) {
                atomic_fetch_add(&val.as.as_function->ref_count, 1);
            }
            return val;

        case HML_VAL_TASK:
            // Task handles are shared - they're used for coordination
            if (val.as.as_task) {
                atomic_fetch_add(&val.as.as_task->ref_count, 1);
            }
            return val;

        case HML_VAL_CHANNEL:
            // Channels are shared - they're the communication mechanism
            if (val.as.as_channel) {
                atomic_fetch_add(&val.as.as_channel->ref_count, 1);
            }
            return val;

        default:
            // Unknown type - return null for safety
            return hml_val_null();
    }

    return result;
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

int32_t hml_typeid(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:         return 0;   /* HML_TYPEID_I8 */
        case HML_VAL_I16:        return 1;   /* HML_TYPEID_I16 */
        case HML_VAL_I32:        return 2;   /* HML_TYPEID_I32 */
        case HML_VAL_I64:        return 3;   /* HML_TYPEID_I64 */
        case HML_VAL_U8:         return 4;   /* HML_TYPEID_U8 */
        case HML_VAL_U16:        return 5;   /* HML_TYPEID_U16 */
        case HML_VAL_U32:        return 6;   /* HML_TYPEID_U32 */
        case HML_VAL_U64:        return 7;   /* HML_TYPEID_U64 */
        case HML_VAL_F32:        return 8;   /* HML_TYPEID_F32 */
        case HML_VAL_F64:        return 9;   /* HML_TYPEID_F64 */
        case HML_VAL_BOOL:       return 10;  /* HML_TYPEID_BOOL */
        case HML_VAL_STRING:     return 11;  /* HML_TYPEID_STRING */
        case HML_VAL_RUNE:       return 12;  /* HML_TYPEID_RUNE */
        case HML_VAL_PTR:        return 13;  /* HML_TYPEID_PTR */
        case HML_VAL_BUFFER:     return 14;  /* HML_TYPEID_BUFFER */
        case HML_VAL_ARRAY:      return 15;  /* HML_TYPEID_ARRAY */
        case HML_VAL_OBJECT:     return 16;  /* HML_TYPEID_OBJECT */
        case HML_VAL_FILE:       return 17;  /* HML_TYPEID_FILE */
        case HML_VAL_FUNCTION:   return 18;  /* HML_TYPEID_FUNCTION */
        case HML_VAL_BUILTIN_FN: return 18;  /* HML_TYPEID_FUNCTION */
        case HML_VAL_TASK:       return 19;  /* HML_TYPEID_TASK */
        case HML_VAL_CHANNEL:    return 20;  /* HML_TYPEID_CHANNEL */
        case HML_VAL_SOCKET:     return 16;  /* HML_TYPEID_OBJECT (socket is object-like) */
        case HML_VAL_NULL:       return 21;  /* HML_TYPEID_NULL */
        default:                 return 21;  /* HML_TYPEID_NULL */
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
    atomic_store(&env->ref_count, 1);
    pthread_mutex_init(&env->mutex, NULL);

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
        pthread_mutex_destroy(&env->mutex);
        free(env->captured);
        free(env);
    }
}

void hml_closure_env_retain(HmlClosureEnv *env) {
    if (env) {
        atomic_fetch_add(&env->ref_count, 1);
    }
}

void hml_closure_env_release(HmlClosureEnv *env) {
    if (env) {
        if (atomic_fetch_sub(&env->ref_count, 1) <= 1) {
            hml_closure_env_free(env);
        }
    }
}

HmlValue hml_closure_env_get(HmlClosureEnv *env, int index) {
    if (env && index >= 0 && index < env->num_captured) {
        pthread_mutex_lock(&env->mutex);
        HmlValue val = env->captured[index];
        hml_retain(&val);
        pthread_mutex_unlock(&env->mutex);
        return val;
    }
    return hml_val_null();
}

void hml_closure_env_set(HmlClosureEnv *env, int index, HmlValue val) {
    if (env && index >= 0 && index < env->num_captured) {
        pthread_mutex_lock(&env->mutex);
        hml_release(&env->captured[index]);
        env->captured[index] = val;
        hml_retain(&env->captured[index]);
        pthread_mutex_unlock(&env->mutex);
    }
}
