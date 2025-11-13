#define _GNU_SOURCE
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

// Define math constants if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

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
        case TYPE_I64:
        case TYPE_U64:
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


static Value builtin_print(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: print() expects 1 argument\n");
        exit(1);
    }

    print_value(args[0]);
    printf("\n");
    return val_null();
}

static Value builtin_alloc(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
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

static Value builtin_free(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: free() expects 1 argument (pointer, buffer, object, or array)\n");
        exit(1);
    }

    if (args[0].type == VAL_PTR) {
        free(args[0].as.as_ptr);
        return val_null();
    } else if (args[0].type == VAL_BUFFER) {
        buffer_free(args[0].as.as_buffer);
        return val_null();
    } else if (args[0].type == VAL_OBJECT) {
        // Free object with cycle detection
        object_free(args[0].as.as_object);
        return val_null();
    } else if (args[0].type == VAL_ARRAY) {
        // Free array with cycle detection
        array_free(args[0].as.as_array);
        return val_null();
    } else {
        fprintf(stderr, "Runtime error: free() requires a pointer, buffer, object, or array\n");
        exit(1);
    }
}

static Value builtin_memset(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
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

static Value builtin_memcpy(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
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

static Value builtin_sizeof(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
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

static Value builtin_buffer(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
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

static Value builtin_talloc(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
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

static Value builtin_realloc(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
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

static Value builtin_typeof(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: typeof() expects 1 argument\n");
        exit(1);
    }

    const char *type_name;
    switch (args[0].type) {
        case VAL_I8:
            type_name = "i8";
            break;
        case VAL_I16:
            type_name = "i16";
            break;
        case VAL_I32:
            type_name = "i32";
            break;
        case VAL_I64:
            type_name = "i64";
            break;
        case VAL_U8:
            type_name = "u8";
            break;
        case VAL_U16:
            type_name = "u16";
            break;
        case VAL_U32:
            type_name = "u32";
            break;
        case VAL_U64:
            type_name = "u64";
            break;
        case VAL_F32:
            type_name = "f32";
            break;
        case VAL_F64:
            type_name = "f64";
            break;
        case VAL_BOOL:
            type_name = "bool";
            break;
        case VAL_STRING:
            type_name = "string";
            break;
        case VAL_RUNE:
            type_name = "rune";
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

// ========== I/O BUILTIN FUNCTIONS ==========
// Note: File/array method handlers are in io.c


static Value builtin_assert(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: assert() expects 1-2 arguments (condition, [message])\n");
        exit(1);
    }

    // Check if condition is truthy
    int is_truthy = 0;
    switch (args[0].type) {
        case VAL_I8:
            is_truthy = args[0].as.as_i8 != 0;
            break;
        case VAL_I16:
            is_truthy = args[0].as.as_i16 != 0;
            break;
        case VAL_I32:
            is_truthy = args[0].as.as_i32 != 0;
            break;
        case VAL_U8:
            is_truthy = args[0].as.as_u8 != 0;
            break;
        case VAL_U16:
            is_truthy = args[0].as.as_u16 != 0;
            break;
        case VAL_U32:
            is_truthy = args[0].as.as_u32 != 0;
            break;
        case VAL_F32:
            is_truthy = args[0].as.as_f32 != 0.0f;
            break;
        case VAL_F64:
            is_truthy = args[0].as.as_f64 != 0.0;
            break;
        case VAL_BOOL:
            is_truthy = args[0].as.as_bool;
            break;
        case VAL_NULL:
            is_truthy = 0;
            break;
        case VAL_STRING:
            // Non-empty string is truthy
            is_truthy = args[0].as.as_string->length > 0;
            break;
        case VAL_PTR:
            is_truthy = args[0].as.as_ptr != NULL;
            break;
        default:
            // All other types (objects, arrays, functions, etc.) are truthy
            is_truthy = 1;
            break;
    }

    // If condition is false, throw exception
    if (!is_truthy) {
        Value exception_msg;
        if (num_args == 2) {
            // Use provided message
            exception_msg = args[1];
        } else {
            // Use default message
            exception_msg = val_string("assertion failed");
        }

        ctx->exception_state.exception_value = exception_msg;
        ctx->exception_state.is_throwing = 1;
    }

    return val_null();
}

// ========== ASYNC/CONCURRENCY BUILTINS ==========

// Global task ID counter
static int next_task_id = 1;

// Thread wrapper function that executes a task
static void* task_thread_wrapper(void* arg) {
    Task *task = (Task*)arg;
    Function *fn = task->function;

    // Mark as running
    task->state = TASK_RUNNING;

    // Create new environment for function execution
    Environment *func_env = env_new(task->env);

    // Bind parameters
    for (int i = 0; i < fn->num_params && i < task->num_args; i++) {
        Value arg = task->args[i];
        // Type check if parameter has type annotation
        if (fn->param_types[i]) {
            arg = convert_to_type(arg, fn->param_types[i], func_env, task->ctx);
        }
        env_define(func_env, fn->param_names[i], arg, 0, task->ctx);
    }

    // Execute function body
    eval_stmt(fn->body, func_env, task->ctx);

    // Get return value
    Value result = val_null();
    if (task->ctx->return_state.is_returning) {
        result = task->ctx->return_state.return_value;
        task->ctx->return_state.is_returning = 0;
    }

    // Store result in task
    task->result = malloc(sizeof(Value));
    *task->result = result;
    task->state = TASK_COMPLETED;

    // Release function environment (reference counted)
    env_release(func_env);

    return NULL;
}

static Value builtin_spawn(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Not used in spawn

    if (num_args < 1) {
        fprintf(stderr, "Runtime error: spawn() expects at least 1 argument (async function)\n");
        exit(1);
    }

    Value func_val = args[0];

    if (func_val.type != VAL_FUNCTION) {
        fprintf(stderr, "Runtime error: spawn() expects an async function\n");
        exit(1);
    }

    Function *fn = func_val.as.as_function;

    if (!fn->is_async) {
        fprintf(stderr, "Runtime error: spawn() requires an async function\n");
        exit(1);
    }

    // Create task with remaining args as function arguments
    Value *task_args = NULL;
    int task_num_args = num_args - 1;

    if (task_num_args > 0) {
        task_args = malloc(sizeof(Value) * task_num_args);
        for (int i = 0; i < task_num_args; i++) {
            task_args[i] = args[i + 1];
        }
    }

    // Create task
    Task *task = task_new(next_task_id++, fn, task_args, task_num_args, fn->closure_env);

    // Allocate pthread_t
    task->thread = malloc(sizeof(pthread_t));
    if (!task->thread) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

    // Create thread to execute task
    int rc = pthread_create((pthread_t*)task->thread, NULL, task_thread_wrapper, task);
    if (rc != 0) {
        fprintf(stderr, "Runtime error: Failed to create thread: %d\n", rc);
        exit(1);
    }

    return val_task(task);
}

static Value builtin_join(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: join() expects 1 argument (task handle)\n");
        exit(1);
    }

    Value task_val = args[0];

    if (task_val.type != VAL_TASK) {
        fprintf(stderr, "Runtime error: join() expects a task handle\n");
        exit(1);
    }

    Task *task = task_val.as.as_task;

    if (task->joined) {
        fprintf(stderr, "Runtime error: task handle already joined\n");
        exit(1);
    }

    if (task->detached) {
        fprintf(stderr, "Runtime error: cannot join detached task\n");
        exit(1);
    }

    // Mark as joined
    task->joined = 1;

    // Wait for thread to complete
    if (task->thread) {
        int rc = pthread_join(*(pthread_t*)task->thread, NULL);
        if (rc != 0) {
            fprintf(stderr, "Runtime error: pthread_join failed: %d\n", rc);
            exit(1);
        }
    }

    // Check if task threw an exception
    if (task->ctx->exception_state.is_throwing) {
        // Re-throw the exception in the current context
        ctx->exception_state = task->ctx->exception_state;
        return val_null();
    }

    // Return the result
    if (task->result) {
        return *task->result;
    }

    return val_null();
}

static Value builtin_detach(Value *args, int num_args, ExecutionContext *ctx) {
    // detach() is like spawn() but detaches the thread
    Value task = builtin_spawn(args, num_args, ctx);

    // Detach the thread (fire and forget)
    if (task.type == VAL_TASK) {
        Task *t = task.as.as_task;
        t->detached = 1;

        if (t->thread) {
            int rc = pthread_detach(*(pthread_t*)t->thread);
            if (rc != 0) {
                fprintf(stderr, "Runtime error: pthread_detach failed: %d\n", rc);
                exit(1);
            }
        }

        // Note: We don't free the task here - it will clean itself up
        // when the thread completes. In a production system, we'd have
        // a cleanup mechanism.
    }

    return val_null();
}

static Value builtin_channel(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    int capacity = 0;  // unbuffered by default

    if (num_args > 0) {
        if (args[0].type != VAL_I32 && args[0].type != VAL_U32) {
            fprintf(stderr, "Runtime error: channel() capacity must be an integer\n");
            exit(1);
        }
        capacity = value_to_int(args[0]);

        if (capacity < 0) {
            fprintf(stderr, "Runtime error: channel() capacity cannot be negative\n");
            exit(1);
        }
    }

    Channel *ch = channel_new(capacity);
    return val_channel(ch);
}

// ========== INTERNAL HELPER BUILTINS ==========

static Value builtin_read_u32(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __read_u32() expects 1 argument (ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __read_u32() requires a pointer\n");
        exit(1);
    }

    uint32_t *ptr = (uint32_t*)args[0].as.as_ptr;
    return val_u32(*ptr);
}

static Value builtin_read_u64(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __read_u64() expects 1 argument (ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __read_u64() requires a pointer\n");
        exit(1);
    }

    uint64_t *ptr = (uint64_t*)args[0].as.as_ptr;
    return val_u64(*ptr);
}

static Value builtin_strerror_fn(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: __strerror() expects 0 arguments\n");
        exit(1);
    }

    return val_string(strerror(errno));
}

static Value builtin_dirent_name(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __dirent_name() expects 1 argument (dirent ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __dirent_name() requires a pointer\n");
        exit(1);
    }

    struct dirent *entry = (struct dirent*)args[0].as.as_ptr;
    return val_string(entry->d_name);
}

static Value builtin_string_to_cstr(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __string_to_cstr() expects 1 argument (string)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: __string_to_cstr() requires a string\n");
        exit(1);
    }

    String *str = args[0].as.as_string;
    char *cstr = malloc(str->length + 1);
    if (!cstr) {
        fprintf(stderr, "Runtime error: __string_to_cstr() memory allocation failed\n");
        exit(1);
    }
    memcpy(cstr, str->data, str->length);
    cstr[str->length] = '\0';
    return val_ptr(cstr);
}

static Value builtin_cstr_to_string(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __cstr_to_string() expects 1 argument (ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __cstr_to_string() requires a pointer\n");
        exit(1);
    }

    char *cstr = (char*)args[0].as.as_ptr;
    if (!cstr) {
        return val_string("");
    }
    return val_string(cstr);
}

// ========== FILE OPERATIONS ==========

static Value builtin_exists(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: exists() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: exists() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: exists() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    struct stat st;
    int exists = (stat(cpath, &st) == 0);
    free(cpath);
    return val_bool(exists);
}

static Value builtin_read_file(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: read_file() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: read_file() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: read_file() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    FILE *fp = fopen(cpath, "r");
    if (!fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Allocate buffer
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "Runtime error: read_file() memory allocation failed\n");
        fclose(fp);
        free(cpath);
        exit(1);
    }

    // Read file
    size_t read_size = fread(buffer, 1, size, fp);
    buffer[read_size] = '\0';
    fclose(fp);
    free(cpath);

    Value result = val_string_take(buffer, read_size, size + 1);
    return result;
}

static Value builtin_write_file(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: write_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: write_file() requires string arguments\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    String *content = args[1].as.as_string;

    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: write_file() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    FILE *fp = fopen(cpath, "w");
    if (!fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    fwrite(content->data, 1, content->length, fp);
    fclose(fp);
    free(cpath);
    return val_null();
}

static Value builtin_append_file(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: append_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: append_file() requires string arguments\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    String *content = args[1].as.as_string;

    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: append_file() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    FILE *fp = fopen(cpath, "a");
    if (!fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    fwrite(content->data, 1, content->length, fp);
    fclose(fp);
    free(cpath);
    return val_null();
}

// ========== DIRECTORY OPERATIONS ==========

static Value builtin_make_dir(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: make_dir() expects 1-2 arguments (path, [mode])\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: make_dir() requires a string path\n");
        exit(1);
    }

    uint32_t mode = 0755;  // Default mode
    if (num_args == 2) {
        if (args[1].type != VAL_U32) {
            fprintf(stderr, "Runtime error: make_dir() mode must be u32\n");
            exit(1);
        }
        mode = args[1].as.as_u32;
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: make_dir() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    if (mkdir(cpath, mode) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to create directory '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    free(cpath);
    return val_null();
}

static Value builtin_remove_dir(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: remove_dir() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: remove_dir() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: remove_dir() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    if (rmdir(cpath) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to remove directory '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    free(cpath);
    return val_null();
}

static Value builtin_list_dir(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: list_dir() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: list_dir() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: list_dir() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    DIR *dir = opendir(cpath);
    if (!dir) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open directory '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    Array *entries = array_new();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        array_push(entries, val_string(entry->d_name));
    }

    closedir(dir);
    free(cpath);
    return val_array(entries);
}

// ========== FILE MANAGEMENT ==========

static Value builtin_remove_file(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: remove_file() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: remove_file() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: remove_file() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    if (unlink(cpath) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to remove file '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    free(cpath);
    return val_null();
}

static Value builtin_rename(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: rename() expects 2 arguments (old_path, new_path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: rename() requires string paths\n");
        exit(1);
    }

    String *old_path = args[0].as.as_string;
    String *new_path = args[1].as.as_string;

    char *old_cpath = malloc(old_path->length + 1);
    char *new_cpath = malloc(new_path->length + 1);
    if (!old_cpath || !new_cpath) {
        fprintf(stderr, "Runtime error: rename() memory allocation failed\n");
        exit(1);
    }
    memcpy(old_cpath, old_path->data, old_path->length);
    old_cpath[old_path->length] = '\0';
    memcpy(new_cpath, new_path->data, new_path->length);
    new_cpath[new_path->length] = '\0';

    if (rename(old_cpath, new_cpath) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to rename '%s' to '%s': %s", old_cpath, new_cpath, strerror(errno));
        free(old_cpath);
        free(new_cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    free(old_cpath);
    free(new_cpath);
    return val_null();
}

static Value builtin_copy_file(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: copy_file() expects 2 arguments (src, dest)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: copy_file() requires string paths\n");
        exit(1);
    }

    String *src_path = args[0].as.as_string;
    String *dest_path = args[1].as.as_string;

    char *src_cpath = malloc(src_path->length + 1);
    char *dest_cpath = malloc(dest_path->length + 1);
    if (!src_cpath || !dest_cpath) {
        fprintf(stderr, "Runtime error: copy_file() memory allocation failed\n");
        exit(1);
    }
    memcpy(src_cpath, src_path->data, src_path->length);
    src_cpath[src_path->length] = '\0';
    memcpy(dest_cpath, dest_path->data, dest_path->length);
    dest_cpath[dest_path->length] = '\0';

    FILE *src_fp = fopen(src_cpath, "rb");
    if (!src_fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open source file '%s': %s", src_cpath, strerror(errno));
        free(src_cpath);
        free(dest_cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    FILE *dest_fp = fopen(dest_cpath, "wb");
    if (!dest_fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open destination file '%s': %s", dest_cpath, strerror(errno));
        fclose(src_fp);
        free(src_cpath);
        free(dest_cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Copy in chunks
    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src_fp)) > 0) {
        if (fwrite(buffer, 1, n, dest_fp) != n) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Failed to write to '%s': %s", dest_cpath, strerror(errno));
            fclose(src_fp);
            fclose(dest_fp);
            free(src_cpath);
            free(dest_cpath);
            ctx->exception_state.exception_value = val_string(error_msg);
            ctx->exception_state.is_throwing = 1;
            return val_null();
        }
    }

    fclose(src_fp);
    fclose(dest_fp);
    free(src_cpath);
    free(dest_cpath);
    return val_null();
}

// ========== FILE INFO ==========

static Value builtin_is_file(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: is_file() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: is_file() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: is_file() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    struct stat st;
    if (stat(cpath, &st) != 0) {
        free(cpath);
        return val_bool(0);
    }

    free(cpath);
    return val_bool(S_ISREG(st.st_mode));
}

static Value builtin_is_dir(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: is_dir() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: is_dir() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: is_dir() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    struct stat st;
    if (stat(cpath, &st) != 0) {
        free(cpath);
        return val_bool(0);
    }

    free(cpath);
    return val_bool(S_ISDIR(st.st_mode));
}

static Value builtin_file_stat(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: file_stat() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: file_stat() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: file_stat() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    struct stat st;
    if (stat(cpath, &st) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to stat '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    free(cpath);

    // Create object with stat info
    Object *stat_obj = object_new(NULL, 8);

    // Add fields
    char *field_names[] = {"size", "atime", "mtime", "ctime", "mode", "is_file", "is_dir"};
    Value field_values[] = {
        val_i64(st.st_size),
        val_i64(st.st_atime),
        val_i64(st.st_mtime),
        val_i64(st.st_ctime),
        val_u32(st.st_mode),
        val_bool(S_ISREG(st.st_mode)),
        val_bool(S_ISDIR(st.st_mode))
    };

    for (int i = 0; i < 7; i++) {
        stat_obj->field_names[i] = strdup(field_names[i]);
        stat_obj->field_values[i] = field_values[i];
        stat_obj->num_fields++;
    }

    return val_object(stat_obj);
}

// ========== DIRECTORY NAVIGATION ==========

static Value builtin_cwd(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: cwd() expects 0 arguments\n");
        exit(1);
    }

    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        ctx->exception_state.exception_value = val_string(strerror(errno));
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_string(buffer);
}

static Value builtin_chdir(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: chdir() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: chdir() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: chdir() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    if (chdir(cpath) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to change directory to '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    free(cpath);
    return val_null();
}

static Value builtin_absolute_path(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: absolute_path() expects 1 argument (path)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: absolute_path() requires a string path\n");
        exit(1);
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: absolute_path() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    char buffer[PATH_MAX];
    if (realpath(cpath, buffer) == NULL) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to resolve path '%s': %s", cpath, strerror(errno));
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    free(cpath);
    return val_string(buffer);
}

// ========== MATH BUILTIN FUNCTIONS ==========

static Value builtin_sin(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sin() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: sin() argument must be numeric\n");
        exit(1);
    }
    return val_f64(sin(value_to_float(args[0])));
}

static Value builtin_cos(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: cos() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: cos() argument must be numeric\n");
        exit(1);
    }
    return val_f64(cos(value_to_float(args[0])));
}

static Value builtin_tan(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: tan() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: tan() argument must be numeric\n");
        exit(1);
    }
    return val_f64(tan(value_to_float(args[0])));
}

static Value builtin_asin(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: asin() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: asin() argument must be numeric\n");
        exit(1);
    }
    return val_f64(asin(value_to_float(args[0])));
}

static Value builtin_acos(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: acos() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: acos() argument must be numeric\n");
        exit(1);
    }
    return val_f64(acos(value_to_float(args[0])));
}

static Value builtin_atan(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: atan() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: atan() argument must be numeric\n");
        exit(1);
    }
    return val_f64(atan(value_to_float(args[0])));
}

static Value builtin_atan2(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: atan2() expects 2 arguments\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: atan2() arguments must be numeric\n");
        exit(1);
    }
    return val_f64(atan2(value_to_float(args[0]), value_to_float(args[1])));
}

static Value builtin_sqrt(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sqrt() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: sqrt() argument must be numeric\n");
        exit(1);
    }
    return val_f64(sqrt(value_to_float(args[0])));
}

static Value builtin_pow(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: pow() expects 2 arguments\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: pow() arguments must be numeric\n");
        exit(1);
    }
    return val_f64(pow(value_to_float(args[0]), value_to_float(args[1])));
}

static Value builtin_exp(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: exp() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: exp() argument must be numeric\n");
        exit(1);
    }
    return val_f64(exp(value_to_float(args[0])));
}

static Value builtin_log(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: log() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: log() argument must be numeric\n");
        exit(1);
    }
    return val_f64(log(value_to_float(args[0])));
}

static Value builtin_log10(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: log10() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: log10() argument must be numeric\n");
        exit(1);
    }
    return val_f64(log10(value_to_float(args[0])));
}

static Value builtin_log2(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: log2() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: log2() argument must be numeric\n");
        exit(1);
    }
    return val_f64(log2(value_to_float(args[0])));
}

static Value builtin_floor(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: floor() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: floor() argument must be numeric\n");
        exit(1);
    }
    return val_f64(floor(value_to_float(args[0])));
}

static Value builtin_ceil(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: ceil() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: ceil() argument must be numeric\n");
        exit(1);
    }
    return val_f64(ceil(value_to_float(args[0])));
}

static Value builtin_round(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: round() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: round() argument must be numeric\n");
        exit(1);
    }
    return val_f64(round(value_to_float(args[0])));
}

static Value builtin_trunc(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: trunc() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: trunc() argument must be numeric\n");
        exit(1);
    }
    return val_f64(trunc(value_to_float(args[0])));
}

static Value builtin_abs(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: abs() expects 1 argument\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: abs() argument must be numeric\n");
        exit(1);
    }
    return val_f64(fabs(value_to_float(args[0])));
}

static Value builtin_min(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: min() expects 2 arguments\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: min() arguments must be numeric\n");
        exit(1);
    }
    double a = value_to_float(args[0]);
    double b = value_to_float(args[1]);
    return val_f64(a < b ? a : b);
}

static Value builtin_max(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: max() expects 2 arguments\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: max() arguments must be numeric\n");
        exit(1);
    }
    double a = value_to_float(args[0]);
    double b = value_to_float(args[1]);
    return val_f64(a > b ? a : b);
}

static Value builtin_clamp(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 3) {
        fprintf(stderr, "Runtime error: clamp() expects 3 arguments (value, min, max)\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1]) || !is_numeric(args[2])) {
        fprintf(stderr, "Runtime error: clamp() arguments must be numeric\n");
        exit(1);
    }
    double value = value_to_float(args[0]);
    double min_val = value_to_float(args[1]);
    double max_val = value_to_float(args[2]);
    if (value < min_val) return val_f64(min_val);
    if (value > max_val) return val_f64(max_val);
    return val_f64(value);
}

static Value builtin_rand(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: rand() expects no arguments\n");
        exit(1);
    }
    // Return random float between 0.0 and 1.0
    return val_f64((double)rand() / RAND_MAX);
}

static Value builtin_rand_range(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: rand_range() expects 2 arguments (min, max)\n");
        exit(1);
    }
    if (!is_numeric(args[0]) || !is_numeric(args[1])) {
        fprintf(stderr, "Runtime error: rand_range() arguments must be numeric\n");
        exit(1);
    }
    double min_val = value_to_float(args[0]);
    double max_val = value_to_float(args[1]);
    double range = max_val - min_val;
    return val_f64(min_val + (range * rand() / RAND_MAX));
}

static Value builtin_seed(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: seed() expects 1 argument\n");
        exit(1);
    }
    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: seed() argument must be an integer\n");
        exit(1);
    }
    srand((unsigned int)value_to_int(args[0]));
    return val_null();
}

// ========== TIME BUILTIN FUNCTIONS ==========

static Value builtin_now(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: now() expects no arguments\n");
        exit(1);
    }
    return val_i64((int64_t)time(NULL));
}

static Value builtin_time_ms(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: time_ms() expects no arguments\n");
        exit(1);
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return val_i64(ms);
}

static Value builtin_sleep(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sleep() expects 1 argument (seconds)\n");
        exit(1);
    }
    if (!is_numeric(args[0])) {
        fprintf(stderr, "Runtime error: sleep() argument must be numeric\n");
        exit(1);
    }
    double seconds = value_to_float(args[0]);
    if (seconds < 0) {
        fprintf(stderr, "Runtime error: sleep() argument must be non-negative\n");
        exit(1);
    }
    // Use nanosleep for more precise sleep
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - req.tv_sec) * 1000000000);
    nanosleep(&req, NULL);
    return val_null();
}

static Value builtin_clock(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: clock() expects no arguments\n");
        exit(1);
    }
    // Returns CPU time in seconds as f64
    return val_f64((double)clock() / CLOCKS_PER_SEC);
}

// ========== ENV BUILTIN FUNCTIONS ==========

static Value builtin_getenv(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: getenv() expects 1 argument (variable name)\n");
        exit(1);
    }
    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: getenv() argument must be a string\n");
        exit(1);
    }
    String *name = args[0].as.as_string;
    char *cname = malloc(name->length + 1);
    if (cname == NULL) {
        fprintf(stderr, "Runtime error: getenv() memory allocation failed\n");
        exit(1);
    }
    memcpy(cname, name->data, name->length);
    cname[name->length] = '\0';

    char *value = getenv(cname);
    free(cname);

    if (value == NULL) {
        return val_null();
    }
    return val_string(value);
}

static Value builtin_setenv(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: setenv() expects 2 arguments (name, value)\n");
        exit(1);
    }
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: setenv() arguments must be strings\n");
        exit(1);
    }

    String *name = args[0].as.as_string;
    String *value = args[1].as.as_string;

    char *cname = malloc(name->length + 1);
    char *cvalue = malloc(value->length + 1);
    if (cname == NULL || cvalue == NULL) {
        fprintf(stderr, "Runtime error: setenv() memory allocation failed\n");
        exit(1);
    }

    memcpy(cname, name->data, name->length);
    cname[name->length] = '\0';
    memcpy(cvalue, value->data, value->length);
    cvalue[value->length] = '\0';

    int result = setenv(cname, cvalue, 1);
    free(cname);
    free(cvalue);

    if (result != 0) {
        fprintf(stderr, "Runtime error: setenv() failed: %s\n", strerror(errno));
        exit(1);
    }
    return val_null();
}

static Value builtin_unsetenv(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: unsetenv() expects 1 argument (variable name)\n");
        exit(1);
    }
    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: unsetenv() argument must be a string\n");
        exit(1);
    }

    String *name = args[0].as.as_string;
    char *cname = malloc(name->length + 1);
    if (cname == NULL) {
        fprintf(stderr, "Runtime error: unsetenv() memory allocation failed\n");
        exit(1);
    }
    memcpy(cname, name->data, name->length);
    cname[name->length] = '\0';

    int result = unsetenv(cname);
    free(cname);

    if (result != 0) {
        fprintf(stderr, "Runtime error: unsetenv() failed: %s\n", strerror(errno));
        exit(1);
    }
    return val_null();
}

static Value builtin_exit(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args > 1) {
        fprintf(stderr, "Runtime error: exit() expects 0 or 1 argument (exit code)\n");
        exit(1);
    }

    int exit_code = 0;
    if (num_args == 1) {
        if (!is_integer(args[0])) {
            fprintf(stderr, "Runtime error: exit() argument must be an integer\n");
            exit(1);
        }
        exit_code = value_to_int(args[0]);
    }

    exit(exit_code);
}

static Value builtin_get_pid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: get_pid() expects no arguments\n");
        exit(1);
    }
    return val_i32((int32_t)getpid());
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
    {"read_line", builtin_read_line},
    {"eprint", builtin_eprint},
    {"open", builtin_open},
    {"assert", builtin_assert},
    {"spawn", builtin_spawn},
    {"join", builtin_join},
    {"detach", builtin_detach},
    {"channel", builtin_channel},
    // Math functions (use stdlib/math.hml module for public API)
    {"__sin", builtin_sin},
    {"__cos", builtin_cos},
    {"__tan", builtin_tan},
    {"__asin", builtin_asin},
    {"__acos", builtin_acos},
    {"__atan", builtin_atan},
    {"__atan2", builtin_atan2},
    {"__sqrt", builtin_sqrt},
    {"__pow", builtin_pow},
    {"__exp", builtin_exp},
    {"__log", builtin_log},
    {"__log10", builtin_log10},
    {"__log2", builtin_log2},
    {"__floor", builtin_floor},
    {"__ceil", builtin_ceil},
    {"__round", builtin_round},
    {"__trunc", builtin_trunc},
    {"__abs", builtin_abs},
    {"__min", builtin_min},
    {"__max", builtin_max},
    {"__clamp", builtin_clamp},
    {"__rand", builtin_rand},
    {"__rand_range", builtin_rand_range},
    {"__seed", builtin_seed},
    // Time functions (use stdlib/time.hml module for public API)
    {"__now", builtin_now},
    {"__time_ms", builtin_time_ms},
    {"__sleep", builtin_sleep},
    {"__clock", builtin_clock},
    // Environment functions (use stdlib/env.hml module for public API)
    {"__getenv", builtin_getenv},
    {"__setenv", builtin_setenv},
    {"__unsetenv", builtin_unsetenv},
    {"__exit", builtin_exit},
    {"__get_pid", builtin_get_pid},
    // Internal helper builtins
    {"__read_u32", builtin_read_u32},
    {"__read_u64", builtin_read_u64},
    {"__strerror", builtin_strerror_fn},
    {"__dirent_name", builtin_dirent_name},
    {"__string_to_cstr", builtin_string_to_cstr},
    {"__cstr_to_string", builtin_cstr_to_string},
    // Internal file operations (use stdlib/fs.hml module for public API)
    {"__exists", builtin_exists},
    {"__read_file", builtin_read_file},
    {"__write_file", builtin_write_file},
    {"__append_file", builtin_append_file},
    // Internal directory operations (use stdlib/fs.hml module for public API)
    {"__make_dir", builtin_make_dir},
    {"__remove_dir", builtin_remove_dir},
    {"__list_dir", builtin_list_dir},
    // Internal file management (use stdlib/fs.hml module for public API)
    {"__remove_file", builtin_remove_file},
    {"__rename", builtin_rename},
    {"__copy_file", builtin_copy_file},
    // Internal file info (use stdlib/fs.hml module for public API)
    {"__is_file", builtin_is_file},
    {"__is_dir", builtin_is_dir},
    {"__file_stat", builtin_file_stat},
    // Internal directory navigation (use stdlib/fs.hml module for public API)
    {"__cwd", builtin_cwd},
    {"__chdir", builtin_chdir},
    {"__absolute_path", builtin_absolute_path},
    {NULL, NULL}  // Sentinel
};

Value val_builtin_fn(BuiltinFn fn) {
    Value v;
    v.type = VAL_BUILTIN_FN;
    v.as.as_builtin_fn = fn;
    return v;
}

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx) {
    // Register type constants FIRST for use with sizeof() and talloc()
    // These must be registered before builtin functions to avoid conflicts
    env_set(env, "i8", val_type(TYPE_I8), ctx);
    env_set(env, "i16", val_type(TYPE_I16), ctx);
    env_set(env, "i32", val_type(TYPE_I32), ctx);
    env_set(env, "u8", val_type(TYPE_U8), ctx);
    env_set(env, "u16", val_type(TYPE_U16), ctx);
    env_set(env, "u32", val_type(TYPE_U32), ctx);
    env_set(env, "f32", val_type(TYPE_F32), ctx);
    env_set(env, "f64", val_type(TYPE_F64), ctx);
    env_set(env, "ptr", val_type(TYPE_PTR), ctx);

    // Type aliases
    env_set(env, "integer", val_type(TYPE_I32), ctx);
    env_set(env, "number", val_type(TYPE_F64), ctx);
    env_set(env, "byte", val_type(TYPE_U8), ctx);

    // Math constants (use stdlib/math.hml module for public API)
    env_set(env, "__PI", val_f64(M_PI), ctx);
    env_set(env, "__E", val_f64(M_E), ctx);
    env_set(env, "__TAU", val_f64(2.0 * M_PI), ctx);
    env_set(env, "__INF", val_f64(INFINITY), ctx);
    env_set(env, "__NAN", val_f64(NAN), ctx);

    // Register builtin functions (may overwrite some type names if there are conflicts)
    for (int i = 0; builtins[i].name != NULL; i++) {
        env_set(env, builtins[i].name, val_builtin_fn(builtins[i].fn), ctx);
    }

    // Register command-line arguments as 'args' array
    Array *args_array = array_new();
    for (int i = 0; i < argc; i++) {
        array_push(args_array, val_string(argv[i]));
    }
    env_set(env, "args", val_array(args_array), ctx);
}
