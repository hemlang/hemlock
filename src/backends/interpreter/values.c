#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <limits.h>

// ========== FORWARD DECLARATIONS FOR CYCLE DETECTION ==========

// VisitedSet typedef is in internal.h
static void object_free_internal(Object *obj, VisitedSet *visited);
static void array_free_internal(Array *arr, VisitedSet *visited);

// ========== STRING OPERATIONS ==========

void string_free(String *str) {
    if (str) {
        free(str->data);
        free(str);
    }
}

void string_retain(String *str) {
    if (str) {
        __atomic_add_fetch(&str->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

void string_release(String *str) {
    if (str) {
        int old_count = __atomic_sub_fetch(&str->ref_count, 1, __ATOMIC_SEQ_CST);
        if (old_count == 0) {
            string_free(str);
        }
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
    str->ref_count = 1;  // Start with 1 - caller owns the first reference
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
    copy->ref_count = 1;  // Start with 1 - caller owns the first reference
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
    // SECURITY: Check for integer overflow before adding lengths
    if (a->length > INT_MAX - b->length) {
        fprintf(stderr, "Runtime error: String concatenation overflow - result too large\n");
        exit(1);
    }
    int new_len = a->length + b->length;
    String *result = malloc(sizeof(String));
    if (!result) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    result->length = new_len;
    result->char_length = -1;  // Cache invalidated after concatenation
    result->capacity = new_len + 1;
    result->ref_count = 1;  // Start with 1 - caller owns the first reference
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

String* string_concat_many(String **strings, int count) {
    if (count == 0) {
        return string_new("");
    }
    if (count == 1) {
        return string_copy(strings[0]);
    }

    // Calculate total length in one pass with overflow checking
    int total_len = 0;
    for (int i = 0; i < count; i++) {
        // SECURITY: Check for integer overflow before adding lengths
        if (strings[i]->length > INT_MAX - total_len) {
            fprintf(stderr, "Runtime error: String concatenation overflow - result too large\n");
            exit(1);
        }
        total_len += strings[i]->length;
    }

    // Allocate result string
    String *result = malloc(sizeof(String));
    if (!result) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    result->length = total_len;
    result->char_length = -1;  // Cache invalidated after concatenation
    result->capacity = total_len + 1;
    result->ref_count = 1;  // Start with 1 - caller owns the first reference
    result->data = malloc(result->capacity);
    if (!result->data) {
        free(result);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

    // Copy all strings in one pass
    int offset = 0;
    for (int i = 0; i < count; i++) {
        memcpy(result->data + offset, strings[i]->data, strings[i]->length);
        offset += strings[i]->length;
    }
    result->data[total_len] = '\0';

    return result;
}

Value val_string(const char *str) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_STRING;
    v.as.as_string = string_new(str);
    return v;
}

Value val_string_take(char *data, int length, int capacity) {
    Value v = {0};  // Zero-initialize entire struct
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
    str->ref_count = 1;  // Start with 1 - caller owns the first reference
    v.as.as_string = str;
    return v;
}

Value val_rune(uint32_t codepoint) {
    if (codepoint > 0x10FFFF) {
        fprintf(stderr, "Runtime error: Invalid Unicode codepoint: 0x%X (max is 0x10FFFF)\n", codepoint);
        exit(1);
    }
    Value v = {0};  // Zero-initialize entire struct
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

void buffer_retain(Buffer *buf) {
    if (buf) {
        __atomic_add_fetch(&buf->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

void buffer_release(Buffer *buf) {
    if (!buf) return;
    // Skip if already manually freed via builtin_free()
    if (atomic_load(&buf->freed)) return;
    int old_count = __atomic_sub_fetch(&buf->ref_count, 1, __ATOMIC_SEQ_CST);
    if (old_count == 0) {
        buffer_free(buf);
    }
}

Value val_buffer(int size) {
    if (size <= 0) {
        fprintf(stderr, "Runtime error: buffer size must be positive\n");
        exit(1);
    }

    Buffer *buf = malloc(sizeof(Buffer));
    if (!buf) {
        return val_null();
    }
    buf->data = malloc(size);
    if (buf->data == NULL) {
        free(buf);
        return val_null();
    }

    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_BUFFER;
    buf->length = size;
    buf->capacity = size;
    buf->ref_count = 1;  // Start with 1 - caller owns the first reference
    atomic_store(&buf->freed, 0);  // Not freed
    v.as.as_buffer = buf;
    return v;
}

Value val_file(FileHandle *file) {
    Value v = {0};  // Zero-initialize entire struct
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
    arr->capacity = HML_INITIAL_ARRAY_CAPACITY;
    arr->length = 0;
    arr->ref_count = 1;  // Start with 1 - caller owns the first reference
    arr->element_type = NULL;  // Untyped array
    atomic_store(&arr->freed, 0);  // Not freed
    arr->elements = malloc(sizeof(Value) * arr->capacity);
    if (!arr->elements) {
        free(arr);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }
    return arr;
}

// Public API for array_free - handles circular references
void array_free(Array *arr) {
    if (!arr) return;

    // Create visited set for cycle detection
    VisitedSet *visited = visited_set_new();
    if (!visited) {
        fprintf(stderr, "Runtime error: Failed to allocate visited set for array_free\n");
        exit(1);
    }

    array_free_internal(arr, visited);
    visited_set_free(visited);
}

void array_retain(Array *arr) {
    if (arr) {
        __atomic_add_fetch(&arr->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

void array_release(Array *arr) {
    if (!arr) return;
    // Skip if already manually freed via builtin_free()
    if (atomic_load(&arr->freed)) return;
    int old_count = __atomic_sub_fetch(&arr->ref_count, 1, __ATOMIC_SEQ_CST);
    if (old_count == 0) {
        array_free(arr);
    }
}

static void array_grow(Array *arr) {
    // SECURITY: Check for integer overflow before doubling capacity
    if (arr->capacity > INT_MAX / 2) {
        fprintf(stderr, "Runtime error: Array capacity overflow - array too large\n");
        exit(1);
    }
    int new_capacity = arr->capacity * 2;

    // Also check allocation size doesn't overflow
    if ((size_t)new_capacity > SIZE_MAX / sizeof(Value)) {
        fprintf(stderr, "Runtime error: Array allocation size overflow\n");
        exit(1);
    }

    Value *new_elements = realloc(arr->elements, sizeof(Value) * (size_t)new_capacity);
    if (!new_elements) {
        fprintf(stderr, "Runtime error: Memory allocation failed during array growth\n");
        exit(1);
    }
    arr->capacity = new_capacity;
    arr->elements = new_elements;
}

// Helper function to check if value matches array element type
static void check_array_element_type(Array *arr, Value val) {
    if (!arr->element_type) {
        return;  // Untyped array, allow any value
    }

    TypeKind expected = arr->element_type->kind;
    int type_matches = 0;

    switch (expected) {
        case TYPE_I8:   type_matches = (val.type == VAL_I8); break;
        case TYPE_I16:  type_matches = (val.type == VAL_I16); break;
        case TYPE_I32:  type_matches = (val.type == VAL_I32); break;
        case TYPE_I64:  type_matches = (val.type == VAL_I64); break;
        case TYPE_U8:   type_matches = (val.type == VAL_U8); break;
        case TYPE_U16:  type_matches = (val.type == VAL_U16); break;
        case TYPE_U32:  type_matches = (val.type == VAL_U32); break;
        case TYPE_U64:  type_matches = (val.type == VAL_U64); break;
        case TYPE_F32:  type_matches = (val.type == VAL_F32); break;
        case TYPE_F64:  type_matches = (val.type == VAL_F64); break;
        case TYPE_BOOL: type_matches = (val.type == VAL_BOOL); break;
        case TYPE_STRING: type_matches = (val.type == VAL_STRING); break;
        case TYPE_RUNE: type_matches = (val.type == VAL_RUNE); break;
        case TYPE_PTR:  type_matches = (val.type == VAL_PTR); break;
        case TYPE_BUFFER: type_matches = (val.type == VAL_BUFFER); break;
        default:
            fprintf(stderr, "Runtime error: Unsupported array element type constraint\n");
            exit(1);
    }

    if (!type_matches) {
        fprintf(stderr, "Runtime error: Type mismatch in typed array - expected element of specific type\n");
        exit(1);
    }
}

void array_push(Array *arr, Value val) {
    // Fast path: untyped array with capacity available
    if (!arr->element_type && arr->length < arr->capacity) {
        VALUE_RETAIN(val);
        arr->elements[arr->length++] = val;
        return;
    }

    // Check type constraint for typed arrays
    check_array_element_type(arr, val);

    if (arr->length >= arr->capacity) {
        array_grow(arr);
    }
    // Retain value being stored in array (reference counting)
    VALUE_RETAIN(val);
    arr->elements[arr->length++] = val;
}

Value array_pop(Array *arr) {
    if (arr->length == 0) {
        return val_null();
    }
    return arr->elements[--arr->length];
}

Value array_get(Array *arr, int index, ExecutionContext *ctx) {
    if (index < 0 || index >= arr->length) {
        runtime_error(ctx, "Array index %d out of bounds (length %d)", index, arr->length);
    }
    return arr->elements[index];
}

void array_set(Array *arr, int index, Value val, ExecutionContext *ctx) {
    if (index < 0) {
        runtime_error(ctx, "Negative array index not supported");
    }

    // Fast path: in-bounds assignment to untyped array
    if (index < arr->length && !arr->element_type) {
        VALUE_RELEASE(arr->elements[index]);
        VALUE_RETAIN(val);
        arr->elements[index] = val;
        return;
    }

    // Check type constraint for typed arrays
    check_array_element_type(arr, val);

    // Extend array if needed, filling with nulls
    while (index >= arr->length) {
        array_push(arr, val_null());
    }

    // Release old value, retain new value (reference counting)
    VALUE_RELEASE(arr->elements[index]);
    VALUE_RETAIN(val);
    arr->elements[index] = val;
}

Value val_array(Array *arr) {
    Value v = {0};  // Zero-initialize entire struct
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

// Public API for object_free - handles circular references
void object_free(Object *obj) {
    if (!obj) return;

    // Create visited set for cycle detection
    VisitedSet *visited = visited_set_new();
    if (!visited) {
        fprintf(stderr, "Runtime error: Failed to allocate visited set for object_free\n");
        exit(1);
    }

    object_free_internal(obj, visited);
    visited_set_free(visited);
}

void object_retain(Object *obj) {
    if (obj) {
        __atomic_add_fetch(&obj->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

void object_release(Object *obj) {
    if (!obj) return;
    // Skip if already manually freed via builtin_free()
    if (atomic_load(&obj->freed)) return;
    int old_count = __atomic_sub_fetch(&obj->ref_count, 1, __ATOMIC_SEQ_CST);
    if (old_count == 0) {
        object_free(obj);
    }
}

// ========== FUNCTION OPERATIONS ==========

void function_free(Function *fn) {
    if (!fn) return;

    // Bound methods share param arrays with their original function, don't free them
    if (!fn->is_bound) {
        // Free parameter names
        if (fn->param_names) {
            for (int i = 0; i < fn->num_params; i++) {
                if (fn->param_names[i]) free(fn->param_names[i]);
            }
            free(fn->param_names);
        }

        // Free parameter types (Type structs are owned by AST, just free the array)
        if (fn->param_types) {
            free(fn->param_types);
        }

        // Free parameter defaults array (Expr* pointers are owned by AST, just free the array)
        if (fn->param_defaults) {
            free(fn->param_defaults);
        }

        // Free param_is_ref array
        if (fn->param_is_ref) {
            free(fn->param_is_ref);
        }

        // Free pre-computed param hashes
        if (fn->param_hashes) {
            free(fn->param_hashes);
        }
    }

    // Release closure environment (reference counted)
    if (fn->closure_env) {
        env_release(fn->closure_env);
    }

    // Note: body (Stmt*) is not freed - owned by AST
    // Note: return_type (Type*) is not freed - owned by AST
    // Note: param_defaults (Expr**) expressions are not freed - owned by AST

    free(fn);
}

void function_retain(Function *fn) {
    if (fn) {
        __atomic_add_fetch(&fn->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

void function_release(Function *fn) {
    if (fn) {
        int old_count = __atomic_sub_fetch(&fn->ref_count, 1, __ATOMIC_SEQ_CST);
        if (old_count == 0) {
            function_free(fn);
        }
    }
}

// DJB2 hash function for strings (fast and good distribution)
static uint32_t djb2_hash(const char *str) {
    uint32_t hash = HML_DJB2_HASH_SEED;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    return hash;
}

// Rebuild hash table (called when fields are added and hash table needs rehashing)
static void object_hash_rebuild(Object *obj) {
    // Use 2x num_fields as hash table size for good performance
    int new_capacity = obj->num_fields < 4 ? 8 : obj->num_fields * 2;

    free(obj->hash_table);
    obj->hash_capacity = new_capacity;
    obj->hash_table = malloc(sizeof(int) * new_capacity);
    if (!obj->hash_table) {
        fprintf(stderr, "Runtime error: Memory allocation failed for hash table\n");
        exit(1);
    }

    // Initialize all slots to -1 (empty)
    for (int i = 0; i < new_capacity; i++) {
        obj->hash_table[i] = -1;
    }

    // Rehash all existing fields
    for (int i = 0; i < obj->num_fields; i++) {
        uint32_t hash = djb2_hash(obj->field_names[i]);
        int slot = hash % new_capacity;

        // Linear probing to find empty slot
        while (obj->hash_table[slot] != -1) {
            slot = (slot + 1) % new_capacity;
        }
        obj->hash_table[slot] = i;
    }
}

// Look up field index by name, returns -1 if not found
int object_lookup_field(Object *obj, const char *name) {
    // Lazy hash table creation: if no hash table and we have fields, build it
    if ((!obj->hash_table || obj->hash_capacity == 0) && obj->num_fields > 0) {
        object_hash_rebuild(obj);
    }

    if (!obj->hash_table || obj->hash_capacity == 0) {
        // Still no hash table (empty object) - return not found
        return -1;
    }

    uint32_t hash = djb2_hash(name);
    int slot = hash % obj->hash_capacity;
    int start_slot = slot;

    // Linear probing
    while (obj->hash_table[slot] != -1) {
        int idx = obj->hash_table[slot];
        if (strcmp(obj->field_names[idx], name) == 0) {
            return idx;  // Found
        }
        slot = (slot + 1) % obj->hash_capacity;
        if (slot == start_slot) {
            break;  // Full circle, not found
        }
    }
    return -1;  // Not found
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
    obj->ref_count = 1;  // Start with 1 - caller owns the first reference
    atomic_store(&obj->freed, 0);  // Not freed

    // Hash table is built lazily on first lookup for efficiency
    obj->hash_table = NULL;
    obj->hash_capacity = 0;

    return obj;
}

Value val_object(Object *obj) {
    Value v = {0};  // Zero-initialize entire struct
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
    function_retain(function);  // Retain function since task holds a reference
    task->args = args;
    task->num_args = num_args;
    task->result = NULL;
    task->joined = 0;
    task->env = env;
    // Note: Don't retain env - it's owned by the function which we already retained
    task->ctx = exec_context_new();
    task->waiting_on = NULL;
    task->thread = NULL;
    task->detached = 0;
    task->ref_count = 1;  // Start with 1 - caller owns the first reference

    // Initialize task mutex for thread-safe state access
    task->task_mutex = malloc(sizeof(pthread_mutex_t));
    if (!task->task_mutex) {
        free(task);
        fprintf(stderr, "Runtime error: Memory allocation failed for task mutex\n");
        exit(1);
    }
    pthread_mutex_init((pthread_mutex_t*)task->task_mutex, NULL);

    return task;
}

void task_free(Task *task) {
    if (task) {
        // Release function (reference counted)
        // This will indirectly release the environment since function owns it
        if (task->function) {
            function_release(task->function);
        }
        // Note: Don't release env - it's owned by the function
        if (task->args) {
            // Release each argument value before freeing the array
            for (int i = 0; i < task->num_args; i++) {
                value_release(task->args[i]);
            }
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
        if (task->task_mutex) {
            pthread_mutex_destroy((pthread_mutex_t*)task->task_mutex);
            free(task->task_mutex);
        }
        free(task);
    }
}

// Increment task reference count (thread-safe using atomic operations)
void task_retain(Task *task) {
    if (task) {
        __atomic_add_fetch(&task->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

// Decrement task reference count and free if it reaches 0 (thread-safe using atomic operations)
void task_release(Task *task) {
    if (task) {
        int old_count = __atomic_sub_fetch(&task->ref_count, 1, __ATOMIC_SEQ_CST);
        if (old_count == 0) {
            task_free(task);
        }
    }
}

Value val_task(Task *task) {
    Value v = {0};  // Zero-initialize entire struct
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
    ch->ref_count = 1;  // Start with 1 - caller owns the first reference

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

    // For unbuffered channels, also allocate the rendezvous condvar
    ch->rendezvous = malloc(sizeof(pthread_cond_t));

    if (!ch->mutex || !ch->not_empty || !ch->not_full || !ch->rendezvous) {
        if (ch->buffer) free(ch->buffer);
        if (ch->mutex) free(ch->mutex);
        if (ch->not_empty) free(ch->not_empty);
        if (ch->not_full) free(ch->not_full);
        if (ch->rendezvous) free(ch->rendezvous);
        free(ch);
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

    pthread_mutex_init((pthread_mutex_t*)ch->mutex, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_empty, NULL);
    pthread_cond_init((pthread_cond_t*)ch->not_full, NULL);
    pthread_cond_init((pthread_cond_t*)ch->rendezvous, NULL);

    // Initialize unbuffered channel fields
    ch->unbuffered_value = malloc(sizeof(Value));
    if (ch->unbuffered_value) {
        *(ch->unbuffered_value) = val_null();
    }
    ch->sender_waiting = 0;
    ch->receiver_waiting = 0;

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
        if (ch->rendezvous) {
            pthread_cond_destroy((pthread_cond_t*)ch->rendezvous);
            free(ch->rendezvous);
        }
        if (ch->unbuffered_value) {
            free(ch->unbuffered_value);
        }
        free(ch);
    }
}

// Increment channel reference count (thread-safe using atomic operations)
void channel_retain(Channel *ch) {
    if (ch) {
        __atomic_add_fetch(&ch->ref_count, 1, __ATOMIC_SEQ_CST);
    }
}

// Decrement channel reference count and free if it reaches 0 (thread-safe using atomic operations)
void channel_release(Channel *ch) {
    if (ch) {
        int old_count = __atomic_sub_fetch(&ch->ref_count, 1, __ATOMIC_SEQ_CST);
        if (old_count == 0) {
            channel_free(ch);
        }
    }
}

Value val_channel(Channel *channel) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_CHANNEL;
    v.as.as_channel = channel;
    return v;
}

// ========== VALUE OPERATIONS ==========

Value val_i8(int8_t value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_I8;
    v.as.as_i8 = value;
    return v;
}

Value val_i16(int16_t value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_I16;
    v.as.as_i16 = value;
    return v;
}

Value val_i32(int32_t value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_I32;
    v.as.as_i32 = value;
    return v;
}

Value val_i64(int64_t value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_I64;
    v.as.as_i64 = value;
    return v;
}

Value val_u8(uint8_t value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_U8;
    v.as.as_u8 = value;
    return v;
}

Value val_u16(uint16_t value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_U16;
    v.as.as_u16 = value;
    return v;
}

Value val_u32(uint32_t value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_U32;
    v.as.as_u32 = value;
    return v;
}

Value val_u64(uint64_t value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_U64;
    v.as.as_u64 = value;
    return v;
}

Value val_f32(float value) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_F32;
    v.as.as_f32 = value;
    return v;
}

Value val_f64(double value) {
    Value v = {0};  // Zero-initialize entire struct
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
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_BOOL;
    v.as.as_bool = value ? 1 : 0;
    return v;
}

Value val_ptr(void *ptr) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_PTR;
    v.as.as_ptr = ptr;
    return v;
}

Value val_type(TypeKind kind) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_TYPE;
    v.as.as_type = kind;
    return v;
}

Value val_function(Function *fn) {
    Value v = {0};  // Zero-initialize entire struct
    v.type = VAL_FUNCTION;
    v.as.as_function = fn;
    return v;
}

Value val_null(void) {
    Value v = {0};  // Zero-initialize entire struct
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
            printf("%" PRId64, val.as.as_i64);
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
            printf("%" PRIu64, val.as.as_u64);
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
            if (r >= HML_ASCII_PRINTABLE_START && r < HML_ASCII_PRINTABLE_END) {
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
        case VAL_SOCKET: {
            SocketHandle *sock = val.as.as_socket;
            if (sock->closed) {
                printf("<socket (closed)>");
            } else if (sock->address) {
                printf("<socket %s:%d fd=%d%s>",
                       sock->address, sock->port, sock->fd,
                       sock->listening ? " listening" : "");
            } else {
                printf("<socket fd=%d>", sock->fd);
            }
            break;
        }
        case VAL_WEBSOCKET: {
            WebSocketHandle *ws = val.as.as_websocket;
            if (ws->closed) {
                printf("<websocket (closed)>");
            } else if (ws->is_server) {
                printf("<websocket-server %s:%d>", ws->host ? ws->host : "0.0.0.0", ws->port);
            } else {
                printf("<websocket %s>", ws->url ? ws->url : "?");
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
        case VAL_REF:
            printf("<ref>");
            break;
        case VAL_NULL:
            printf("null");
            break;
    }
}

// Convert value to string (caller must free the result)
char* value_to_string(Value val) {
    char buffer[1024];  // Temporary buffer for formatting

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
        case VAL_I64:
            snprintf(buffer, sizeof(buffer), "%" PRId64, val.as.as_i64);
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
        case VAL_U64:
            snprintf(buffer, sizeof(buffer), "%" PRIu64, val.as.as_u64);
            return strdup(buffer);
        case VAL_F32:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f32);
            return strdup(buffer);
        case VAL_F64:
            snprintf(buffer, sizeof(buffer), "%g", val.as.as_f64);
            return strdup(buffer);
        case VAL_BOOL:
            return strdup(val.as.as_bool ? "true" : "false");
        case VAL_STRING:
            return strdup(val.as.as_string->data);
        case VAL_RUNE: {
            // Convert rune to UTF-8 string
            uint32_t r = val.as.as_rune;
            char utf8[5] = {0};

            if (r < 0x80) {
                utf8[0] = (char)r;
            } else if (r < 0x800) {
                utf8[0] = 0xC0 | (r >> 6);
                utf8[1] = 0x80 | (r & 0x3F);
            } else if (r < 0x10000) {
                utf8[0] = 0xE0 | (r >> 12);
                utf8[1] = 0x80 | ((r >> 6) & 0x3F);
                utf8[2] = 0x80 | (r & 0x3F);
            } else {
                utf8[0] = 0xF0 | (r >> 18);
                utf8[1] = 0x80 | ((r >> 12) & 0x3F);
                utf8[2] = 0x80 | ((r >> 6) & 0x3F);
                utf8[3] = 0x80 | (r & 0x3F);
            }

            return strdup(utf8);
        }
        case VAL_PTR:
            snprintf(buffer, sizeof(buffer), "%p", val.as.as_ptr);
            return strdup(buffer);
        case VAL_BUFFER:
            snprintf(buffer, sizeof(buffer), "<buffer %p length=%d capacity=%d>",
                   val.as.as_buffer->data,
                   val.as.as_buffer->length,
                   val.as.as_buffer->capacity);
            return strdup(buffer);
        case VAL_ARRAY: {
            // Simple array representation
            Array *arr = val.as.as_array;
            size_t total_len = 2;  // [ and ]
            char **parts = malloc(sizeof(char*) * (arr->length > 0 ? arr->length : 1));
            size_t *part_lens = malloc(sizeof(size_t) * (arr->length > 0 ? arr->length : 1));
            for (int i = 0; i < arr->length; i++) {
                parts[i] = value_to_string(arr->elements[i]);
                part_lens[i] = strlen(parts[i]);
                total_len += part_lens[i];
                if (i > 0) total_len += 2;  // ", "
            }

            // SECURITY: Use memcpy with explicit position tracking instead of strcat
            char *result = malloc(total_len + 1);
            size_t pos = 0;
            result[pos++] = '[';
            for (int i = 0; i < arr->length; i++) {
                if (i > 0) {
                    memcpy(result + pos, ", ", 2);
                    pos += 2;
                }
                memcpy(result + pos, parts[i], part_lens[i]);
                pos += part_lens[i];
                free(parts[i]);
            }
            result[pos++] = ']';
            result[pos] = '\0';
            free(parts);
            free(part_lens);
            return result;
        }
        case VAL_FILE: {
            FileHandle *file = val.as.as_file;
            if (file->closed) {
                return strdup("<file (closed)>");
            } else {
                snprintf(buffer, sizeof(buffer), "<file '%s' mode='%s'>", file->path, file->mode);
                return strdup(buffer);
            }
        }
        case VAL_SOCKET: {
            SocketHandle *sock = val.as.as_socket;
            if (sock->closed) {
                return strdup("<socket (closed)>");
            } else if (sock->address) {
                snprintf(buffer, sizeof(buffer), "<socket %s:%d fd=%d%s>",
                       sock->address, sock->port, sock->fd,
                       sock->listening ? " listening" : "");
                return strdup(buffer);
            } else {
                snprintf(buffer, sizeof(buffer), "<socket fd=%d>", sock->fd);
                return strdup(buffer);
            }
        }
        case VAL_WEBSOCKET: {
            WebSocketHandle *ws = val.as.as_websocket;
            if (ws->closed) {
                return strdup("<websocket (closed)>");
            } else if (ws->is_server) {
                snprintf(buffer, sizeof(buffer), "<websocket-server %s:%d>",
                       ws->host ? ws->host : "0.0.0.0", ws->port);
                return strdup(buffer);
            } else {
                snprintf(buffer, sizeof(buffer), "<websocket %s>",
                       ws->url ? ws->url : "?");
                return strdup(buffer);
            }
        }
        case VAL_OBJECT:
            if (val.as.as_object->type_name) {
                snprintf(buffer, sizeof(buffer), "<object:%s>", val.as.as_object->type_name);
                return strdup(buffer);
            } else {
                return strdup("<object>");
            }
        case VAL_TYPE:
            return strdup("<type>");
        case VAL_BUILTIN_FN:
            return strdup("<builtin function>");
        case VAL_FUNCTION:
            return strdup("<function>");
        case VAL_FFI_FUNCTION:
            return strdup("<ffi function>");
        case VAL_TASK:
            snprintf(buffer, sizeof(buffer), "<task %d>", val.as.as_task->id);
            return strdup(buffer);
        case VAL_CHANNEL:
            snprintf(buffer, sizeof(buffer), "<channel capacity=%d count=%d%s>",
                   val.as.as_channel->capacity,
                   val.as.as_channel->count,
                   val.as.as_channel->closed ? " closed" : "");
            return strdup(buffer);
        case VAL_REF:
            return strdup("<ref>");
        case VAL_NULL:
            return strdup("null");
    }

    return strdup("<unknown>");
}

// ========== VALUE CLEANUP ==========

// ========== CYCLE DETECTION FOR DEALLOCATION ==========

// Shared VisitedSet implementation (declared in internal.h)
// Used by both values.c and environment.c for cycle detection
VisitedSet* visited_set_new(void) {
    VisitedSet *set = malloc(sizeof(VisitedSet));
    if (!set) return NULL;
    set->capacity = HML_INITIAL_VISITED_SET_CAPACITY;
    set->count = 0;
    set->pointers = malloc(sizeof(void*) * set->capacity);
    if (!set->pointers) {
        free(set);
        return NULL;
    }
    return set;
}

void visited_set_free(VisitedSet *set) {
    if (set) {
        free(set->pointers);
        free(set);
    }
}

int visited_set_contains(VisitedSet *set, void *ptr) {
    for (int i = 0; i < set->count; i++) {
        if (set->pointers[i] == ptr) {
            return 1;
        }
    }
    return 0;
}

void visited_set_add(VisitedSet *set, void *ptr) {
    if (set->count >= set->capacity) {
        // SECURITY: Check for integer overflow before doubling
        if (set->capacity > INT_MAX / 2) {
            fprintf(stderr, "Runtime error: Visited set capacity overflow\n");
            exit(1);
        }
        int new_capacity = set->capacity * 2;
        if ((size_t)new_capacity > SIZE_MAX / sizeof(void*)) {
            fprintf(stderr, "Runtime error: Visited set allocation overflow\n");
            exit(1);
        }
        void **new_pointers = realloc(set->pointers, sizeof(void*) * (size_t)new_capacity);
        if (!new_pointers) {
            fprintf(stderr, "Runtime error: Memory allocation failed during visited set growth\n");
            exit(1);
        }
        set->capacity = new_capacity;
        set->pointers = new_pointers;
    }
    set->pointers[set->count++] = ptr;
}

// Forward declarations for internal versions
static void value_free_internal(Value val, VisitedSet *visited);

// Internal version of object_free with cycle detection
static void object_free_internal(Object *obj, VisitedSet *visited) {
    if (!obj) return;

    // Check if manually freed via builtin_free()
    if (atomic_load(&obj->freed)) return;

    // Check if already visited (cycle detected)
    if (visited_set_contains(visited, obj)) {
        return;  // Already freeing this object - skip to prevent infinite recursion
    }

    // Mark as visited
    visited_set_add(visited, obj);

    // Free object contents
    if (obj->type_name) free(obj->type_name);
    for (int i = 0; i < obj->num_fields; i++) {
        free(obj->field_names[i]);
        // Release field values (decrements ref_counts)
        value_release(obj->field_values[i]);
    }
    free(obj->field_names);
    free(obj->field_values);
    // Free hash table
    if (obj->hash_table) free(obj->hash_table);
    free(obj);
}

// Internal version of array cleanup with cycle detection
static void array_free_internal(Array *arr, VisitedSet *visited) {
    if (!arr) return;

    // Check if manually freed via builtin_free()
    if (atomic_load(&arr->freed)) return;

    // Check if already visited (cycle detected)
    if (visited_set_contains(visited, arr)) {
        return;  // Already freeing this array - skip to prevent infinite recursion
    }

    // Mark as visited
    visited_set_add(visited, arr);

    // Release each element (decrements ref_counts)
    for (int i = 0; i < arr->length; i++) {
        value_release(arr->elements[i]);
    }
    free(arr->elements);

    // Free element type annotation if present
    if (arr->element_type) {
        type_free(arr->element_type);
    }

    free(arr);
}

// Internal version of value_free with cycle detection
static void value_free_internal(Value val, VisitedSet *visited) {
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
                array_free_internal(val.as.as_array, visited);
            }
            break;
        case VAL_FILE:
            if (val.as.as_file) {
                file_free(val.as.as_file);
            }
            break;
        case VAL_SOCKET:
            if (val.as.as_socket) {
                socket_free(val.as.as_socket);
            }
            break;
        case VAL_WEBSOCKET:
            if (val.as.as_websocket) {
                websocket_free(val.as.as_websocket);
            }
            break;
        case VAL_OBJECT:
            if (val.as.as_object) {
                object_free_internal(val.as.as_object, visited);
            }
            break;
        case VAL_FUNCTION:
            if (val.as.as_function) {
                // Use function_release to respect reference counting
                // This is critical for async tasks that may retain function references
                function_release(val.as.as_function);
            }
            break;
        case VAL_FFI_FUNCTION:
            if (val.as.as_ffi_function) {
                ffi_free_function((FFIFunction*)val.as.as_ffi_function);
            }
            break;
        case VAL_TASK:
            if (val.as.as_task) {
                task_release(val.as.as_task);
            }
            break;
        case VAL_CHANNEL:
            if (val.as.as_channel) {
                channel_free(val.as.as_channel);
            }
            break;
        case VAL_REF:
            if (val.as.as_ref) {
                reference_free(val.as.as_ref);
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

// Public API - recursively free a Value and all its heap-allocated contents
// Now handles circular references safely
void value_free(Value val) {
    VisitedSet *visited = visited_set_new();
    if (!visited) {
        fprintf(stderr, "Runtime error: Failed to allocate visited set for value_free\n");
        exit(1);
    }
    value_free_internal(val, visited);
    visited_set_free(visited);
}

// Public API - increment reference count for heap-allocated values
void value_retain(Value val) {
    switch (val.type) {
        case VAL_STRING:
            if (val.as.as_string) {
                string_retain(val.as.as_string);
            }
            break;
        case VAL_BUFFER:
            if (val.as.as_buffer) {
                buffer_retain(val.as.as_buffer);
            }
            break;
        case VAL_ARRAY:
            if (val.as.as_array) {
                array_retain(val.as.as_array);
            }
            break;
        case VAL_OBJECT:
            if (val.as.as_object) {
                object_retain(val.as.as_object);
            }
            break;
        case VAL_FUNCTION:
            if (val.as.as_function) {
                function_retain(val.as.as_function);
            }
            break;
        case VAL_TASK:
            if (val.as.as_task) {
                task_retain(val.as.as_task);
            }
            break;
        case VAL_CHANNEL:
            if (val.as.as_channel) {
                channel_retain(val.as.as_channel);
            }
            break;
        case VAL_REF:
            if (val.as.as_ref) {
                val.as.as_ref->ref_count++;
            }
            break;
        // Other types don't need reference counting
        default:
            break;
    }
}

// Public API - decrement reference count and free if reaches 0
void value_release(Value val) {
    switch (val.type) {
        case VAL_STRING:
            if (val.as.as_string) {
                string_release(val.as.as_string);
            }
            break;
        case VAL_BUFFER:
            if (val.as.as_buffer) {
                buffer_release(val.as.as_buffer);
            }
            break;
        case VAL_ARRAY:
            if (val.as.as_array) {
                array_release(val.as.as_array);
            }
            break;
        case VAL_OBJECT:
            if (val.as.as_object) {
                object_release(val.as.as_object);
            }
            break;
        case VAL_FUNCTION:
            if (val.as.as_function) {
                function_release(val.as.as_function);
            }
            break;
        case VAL_TASK:
            if (val.as.as_task) {
                task_release(val.as.as_task);
            }
            break;
        case VAL_CHANNEL:
            if (val.as.as_channel) {
                channel_release(val.as.as_channel);
            }
            break;
        case VAL_REF:
            if (val.as.as_ref) {
                reference_free(val.as.as_ref);
            }
            break;
        // Other types don't need reference counting
        default:
            break;
    }
}

// ========== VALUE DEEP COPY (for thread isolation) ==========

// Deep copy a value for passing to spawned tasks
// This ensures tasks don't share mutable state with the parent thread
Value value_deep_copy(Value val) {
    Value result = {0};

    switch (val.type) {
        // Primitive types - just copy the value
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
        case VAL_RUNE:
        case VAL_NULL:
        case VAL_TYPE:
        case VAL_BUILTIN_FN:
            return val;  // Immutable, safe to share

        case VAL_STRING:
            if (val.as.as_string) {
                // Create a new string with copied data
                result.type = VAL_STRING;
                result.as.as_string = string_copy(val.as.as_string);
            } else {
                result = val_null();
            }
            break;

        case VAL_BUFFER:
            if (val.as.as_buffer) {
                Buffer *src = val.as.as_buffer;
                result = val_buffer(src->length);
                memcpy(result.as.as_buffer->data, src->data, src->length);
            } else {
                result = val_null();
            }
            break;

        case VAL_ARRAY:
            if (val.as.as_array) {
                Array *src = val.as.as_array;
                Array *dst = array_new();
                // Copy element type if present
                if (src->element_type) {
                    dst->element_type = malloc(sizeof(Type));
                    dst->element_type->kind = src->element_type->kind;
                    dst->element_type->type_name = src->element_type->type_name ? strdup(src->element_type->type_name) : NULL;
                    dst->element_type->element_type = NULL;  // Don't support nested typed arrays
                }
                // Deep copy each element
                for (int i = 0; i < src->length; i++) {
                    Value elem_copy = value_deep_copy(src->elements[i]);
                    array_push(dst, elem_copy);
                    value_release(elem_copy);  // array_push retains
                }
                result.type = VAL_ARRAY;
                result.as.as_array = dst;
            } else {
                result = val_null();
            }
            break;

        case VAL_OBJECT:
            if (val.as.as_object) {
                Object *src = val.as.as_object;
                Object *dst = object_new(src->type_name, src->capacity);
                // Deep copy each field
                for (int i = 0; i < src->num_fields; i++) {
                    // Grow if needed
                    if (dst->num_fields >= dst->capacity) {
                        // SECURITY: Check for integer overflow before doubling
                        if (dst->capacity > INT_MAX / 2) {
                            fprintf(stderr, "Runtime error: Object field capacity overflow\n");
                            exit(1);
                        }
                        int new_capacity = dst->capacity * 2;
                        char **new_names = realloc(dst->field_names, sizeof(char*) * (size_t)new_capacity);
                        Value *new_values = realloc(dst->field_values, sizeof(Value) * (size_t)new_capacity);
                        if (!new_names || !new_values) {
                            fprintf(stderr, "Runtime error: Memory allocation failed during object growth\n");
                            exit(1);
                        }
                        dst->capacity = new_capacity;
                        dst->field_names = new_names;
                        dst->field_values = new_values;
                    }
                    dst->field_names[dst->num_fields] = strdup(src->field_names[i]);
                    Value field_copy = value_deep_copy(src->field_values[i]);
                    value_retain(field_copy);
                    dst->field_values[dst->num_fields] = field_copy;
                    dst->num_fields++;
                }
                result.type = VAL_OBJECT;
                result.as.as_object = dst;
            } else {
                result = val_null();
            }
            break;

        case VAL_PTR:
            // Raw pointers cannot be deep copied safely - this is intentional
            // Tasks should not share raw pointers; use channels or buffers instead
            fprintf(stderr, "Runtime error: Cannot pass raw pointer to spawned task (use buffer or channel instead)\n");
            exit(1);

        case VAL_FILE:
            // File handles are OS resources - kernel handles concurrent access
            // We share by reference (no deep copy needed)
            return val;

        case VAL_SOCKET:
            // Socket handles are OS resources - kernel handles concurrent access
            // We share by reference (no deep copy needed)
            return val;

        case VAL_WEBSOCKET:
            // WebSocket handles are OS resources - kernel handles concurrent access
            // We share by reference (no deep copy needed)
            return val;

        case VAL_FUNCTION:
            // Functions are retained (shared) but their closure env is isolated
            // The spawn function will handle this specially
            if (val.as.as_function) {
                function_retain(val.as.as_function);
            }
            return val;

        case VAL_FFI_FUNCTION:
            // FFI functions are stateless, safe to share
            return val;

        case VAL_TASK:
            // Task handles are retained (shared) - they're used for coordination
            if (val.as.as_task) {
                task_retain(val.as.as_task);
            }
            return val;

        case VAL_CHANNEL:
            // Channels are retained (shared) - they're the communication mechanism
            if (val.as.as_channel) {
                channel_retain(val.as.as_channel);
            }
            return val;

        case VAL_REF:
            // References point to locations - need special handling
            // For now, don't allow refs to be deep copied across threads
            return val_null();
    }

    return result;
}

// Reference operations (for pass-by-reference parameters)

Reference* reference_new_variable(Environment *env, const char *name) {
    Reference *ref = malloc(sizeof(Reference));
    ref->ref_type = REF_VARIABLE;
    ref->as.variable.env = env;
    ref->as.variable.name = strdup(name);
    ref->ref_count = 1;
    env_retain(env);
    return ref;
}

Reference* reference_new_array_index(Array *array, int index) {
    Reference *ref = malloc(sizeof(Reference));
    ref->ref_type = REF_ARRAY_INDEX;
    ref->as.array_index.array = array;
    ref->as.array_index.index = index;
    ref->ref_count = 1;
    array->ref_count++;
    return ref;
}

Reference* reference_new_object_property(Object *object, const char *property) {
    Reference *ref = malloc(sizeof(Reference));
    ref->ref_type = REF_OBJECT_PROPERTY;
    ref->as.object_property.object = object;
    ref->as.object_property.property = strdup(property);
    ref->ref_count = 1;
    object->ref_count++;
    return ref;
}

void reference_free(Reference *ref) {
    if (!ref) return;
    ref->ref_count--;
    if (ref->ref_count > 0) return;

    switch (ref->ref_type) {
        case REF_VARIABLE:
            env_release(ref->as.variable.env);
            free(ref->as.variable.name);
            break;
        case REF_ARRAY_INDEX:
            ref->as.array_index.array->ref_count--;
            if (ref->as.array_index.array->ref_count == 0) {
                array_free(ref->as.array_index.array);
            }
            break;
        case REF_OBJECT_PROPERTY:
            ref->as.object_property.object->ref_count--;
            if (ref->as.object_property.object->ref_count == 0) {
                object_free(ref->as.object_property.object);
            }
            free(ref->as.object_property.property);
            break;
    }
    free(ref);
}

Value ref_deref(Reference *ref, ExecutionContext *ctx) {
    switch (ref->ref_type) {
        case REF_VARIABLE: {
            Value val = env_get(ref->as.variable.env, ref->as.variable.name, ctx);
            VALUE_RETAIN(val);
            return val;
        }
        case REF_ARRAY_INDEX: {
            Value val = array_get(ref->as.array_index.array, ref->as.array_index.index, ctx);
            VALUE_RETAIN(val);
            return val;
        }
        case REF_OBJECT_PROPERTY: {
            Object *obj = ref->as.object_property.object;
            int idx = object_lookup_field(obj, ref->as.object_property.property);
            if (idx < 0) {
                runtime_error(ctx, "Object has no property '%s'", ref->as.object_property.property);
                return val_null();
            }
            Value val = obj->field_values[idx];
            VALUE_RETAIN(val);
            return val;
        }
    }
    return val_null();
}

void ref_assign(Reference *ref, Value value, ExecutionContext *ctx) {
    switch (ref->ref_type) {
        case REF_VARIABLE:
            env_set(ref->as.variable.env, ref->as.variable.name, value, ctx);
            break;
        case REF_ARRAY_INDEX:
            array_set(ref->as.array_index.array, ref->as.array_index.index, value, ctx);
            break;
        case REF_OBJECT_PROPERTY: {
            Object *obj = ref->as.object_property.object;
            int idx = object_lookup_field(obj, ref->as.object_property.property);
            if (idx >= 0) {
                VALUE_RELEASE(obj->field_values[idx]);
                VALUE_RETAIN(value);
                obj->field_values[idx] = value;
            } else {
                // Property doesn't exist - this shouldn't happen for refs
                // since we created the ref from an existing property
                runtime_error(ctx, "Object property '%s' no longer exists", ref->as.object_property.property);
            }
            break;
        }
    }
}

Value val_ref(Reference *ref) {
    Value v = {0};
    v.type = VAL_REF;
    v.as.as_ref = ref;
    return v;
}
