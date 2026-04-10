#include "internal.h"
#include "type_promotion.h"
#include <stdatomic.h>

// Helper: Get the size of a type
int get_type_size(TypeKind kind) {
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
            return 0;
    }
}

Value builtin_alloc(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "alloc() expects 1 argument (size in bytes)"); return val_null();
    }

    if (!is_integer(args[0])) {
        runtime_error(ctx, "alloc() size must be an integer"); return val_null();
    }

    int32_t size = value_to_int(args[0]);

    if (size <= 0) {
        runtime_error(ctx, "alloc() size must be positive"); return val_null();
    }

    void *ptr = malloc(size);
    if (ptr == NULL) {
        return val_null();
    }

    // Record allocation for profiler and track pointer size
    PROFILER_ALLOC(ctx, ctx->current_source_file, ctx->current_line, (uint64_t)size);
    PROFILER_TRACK_PTR(ctx, ptr, (uint64_t)size, ctx->current_source_file, ctx->current_line);

    return val_ptr(ptr);
}

Value builtin_free(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "free() expects 1 argument (pointer, buffer, object, or array)"); return val_null();
    }

    if (args[0].type == VAL_PTR) {
        // Look up tracked size for this pointer
        uint64_t size = PROFILER_UNTRACK_PTR(ctx, args[0].as.as_ptr);
        PROFILER_FREE(ctx, ctx->current_source_file, ctx->current_line, size);
        free(args[0].as.as_ptr);
        return val_null();
    } else if (args[0].type == VAL_BUFFER) {
        Buffer *buf = args[0].as.as_buffer;

        // Cannot explicitly free a buffer slice view
        if (buf->parent) {
            runtime_error(ctx, "cannot free() a buffer slice view");
            return val_null();
        }

        // Atomically check and set the freed flag to detect double-free
        int expected = 0;
        if (!atomic_compare_exchange_strong(&buf->freed, &expected, 1)) {
            runtime_error(ctx, "double free detected on buffer"); return val_null();
        }

        // Safety check: don't allow free on buffers shared outside the environment
        // Note: we check BEFORE calling value_release since that would skip due to freed flag
        if (buf->ref_count > 3) {  // 3 = creation + env + env_get
            int active_refs = buf->ref_count - 3;
            runtime_error(ctx, "Cannot free buffer with %d active reference%s",
                    active_refs, active_refs == 1 ? "" : "s");
            return val_null();
        }

        // Untrack pointer to update original allocation site's current_bytes (for leak detection)
        uint64_t tracked_size = PROFILER_UNTRACK_PTR(ctx, buf->data);
        // Record free for profiler
        PROFILER_FREE(ctx, ctx->current_source_file, ctx->current_line, tracked_size);

        // Free the internal data but keep the struct alive for cleanup to check freed flag
        free(buf->data);
        buf->data = NULL;
        buf->length = 0;
        buf->capacity = 0;
        // Note: We don't free(buf) here - the struct remains until ref_count reaches zero
        // so runtime cleanup can safely detect manual frees.
        return val_null();
    } else if (args[0].type == VAL_OBJECT) {
        Object *obj = args[0].as.as_object;

        // Atomically check and set the freed flag to detect double-free
        int expected = 0;
        if (!atomic_compare_exchange_strong(&obj->freed, &expected, 1)) {
            runtime_error(ctx, "double free detected on object"); return val_null();
        }

        // Release all field values (decrements their ref_counts)
        for (int i = 0; i < obj->num_fields; i++) {
            value_release(obj->fields[i].value);
            free(obj->fields[i].name);
        }
        // Free internal data but keep struct alive for cleanup to check freed flag.
        // For pooled objects, we can't free the fields array here because it may
        // point to pool-owned storage. obj_pool_free handles this during final cleanup.
        if (!obj->is_pooled) {
            free(obj->fields);
        }
        if (obj->type_name) free(obj->type_name);
        if (obj->hash_table) free(obj->hash_table);
        obj->fields = NULL;
        obj->type_name = NULL;
        obj->hash_table = NULL;
        obj->num_fields = 0;
        obj->capacity = 0;
        // Note: We don't free(obj) here - struct remains until ref_count reaches zero
        // so runtime cleanup can safely detect manual frees.
        return val_null();
    } else if (args[0].type == VAL_ARRAY) {
        Array *arr = args[0].as.as_array;

        // Atomically check and set the freed flag to detect double-free
        int expected = 0;
        if (!atomic_compare_exchange_strong(&arr->freed, &expected, 1)) {
            runtime_error(ctx, "double free detected on array"); return val_null();
        }

        // Release all elements (decrements their ref_counts)
        for (int i = 0; i < arr->length; i++) {
            value_release(arr->elements[i]);
        }
        // Free internal data but keep struct alive for cleanup to check freed flag
        free(arr->elements);
        if (arr->element_type) type_free(arr->element_type);
        arr->elements = NULL;
        arr->element_type = NULL;
        arr->length = 0;
        arr->capacity = 0;
        // Note: We don't free(arr) here - struct remains until ref_count reaches zero
        // so runtime cleanup can safely detect manual frees.
        return val_null();
    } else if (args[0].type == VAL_NULL) {
        // free(null) is a safe no-op (like C's free(NULL))
        return val_null();
    } else {
        runtime_error(ctx, "free() requires a pointer, buffer, object, or array"); return val_null();
    }
}

Value builtin_memset(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "memset() expects 3 arguments (ptr, byte, size)"); return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "memset() requires pointer as first argument"); return val_null();
    }

    if (!is_integer(args[1]) || !is_integer(args[2])) {
        runtime_error(ctx, "memset() byte and size must be integers"); return val_null();
    }

    void *ptr = args[0].as.as_ptr;
    int byte = value_to_int(args[1]);
    int size = value_to_int(args[2]);

    // SECURITY: Validate size is non-negative to prevent undefined behavior
    if (size < 0) {
        runtime_error(ctx, "memset() size cannot be negative"); return val_null();
    }

    if (ptr == NULL) {
        runtime_error(ctx, "memset() cannot write to null pointer"); return val_null();
    }

    memset(ptr, byte, size);
    return val_null();
}

Value builtin_memcpy(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "memcpy() expects 3 arguments (dest, src, size)"); return val_null();
    }

    if (args[0].type != VAL_PTR || args[1].type != VAL_PTR) {
        runtime_error(ctx, "memcpy() requires pointers for dest and src"); return val_null();
    }

    if (!is_integer(args[2])) {
        runtime_error(ctx, "memcpy() size must be an integer"); return val_null();
    }

    void *dest = args[0].as.as_ptr;
    void *src = args[1].as.as_ptr;
    int size = value_to_int(args[2]);

    // SECURITY: Validate size is non-negative to prevent undefined behavior
    if (size < 0) {
        runtime_error(ctx, "memcpy() size cannot be negative"); return val_null();
    }

    if (dest == NULL) {
        runtime_error(ctx, "memcpy() cannot write to null pointer"); return val_null();
    }
    if (src == NULL) {
        runtime_error(ctx, "memcpy() cannot read from null pointer"); return val_null();
    }

    memcpy(dest, src, size);
    return val_null();
}

Value builtin_sizeof(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "sizeof() expects 1 argument (type)"); return val_null();
    }

    if (args[0].type == VAL_TYPE) {
        // Internal type value - use interpreter's representation
        TypeKind kind = args[0].as.as_type;
        int size = get_type_size(kind);
        return val_i32(size);
    } else if (args[0].type == VAL_STRING) {
        // String type name - use shared type utilities for consistency with compiler
        const char *type_name = args[0].as.as_string->data;
        HmlTypeKind tk = hml_tk_from_name(type_name);
        int size = hml_tk_sizeof(tk);
        return val_i32(size);
    } else {
        runtime_error(ctx, "sizeof() requires a type argument"); return val_null();
    }
}

Value builtin_buffer(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "buffer() expects 1 argument (size in bytes)"); return val_null();
    }

    if (!is_integer(args[0])) {
        runtime_error(ctx, "buffer() size must be an integer"); return val_null();
    }

    int32_t size = value_to_int(args[0]);

    Value result = val_buffer(size);

    // Record allocation for profiler and track buffer's data pointer
    if (result.type == VAL_BUFFER) {
        uint64_t total_size = (uint64_t)size + sizeof(Buffer);
        PROFILER_ALLOC(ctx, ctx->current_source_file, ctx->current_line, total_size);
        PROFILER_TRACK_PTR(ctx, result.as.as_buffer->data, total_size,
                          ctx->current_source_file, ctx->current_line);
    }

    return result;
}

Value builtin_talloc(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "talloc() expects 2 arguments (type, count)"); return val_null();
    }

    if (args[0].type != VAL_TYPE) {
        runtime_error(ctx, "talloc() first argument must be a type"); return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "talloc() count must be an integer"); return val_null();
    }

    TypeKind type = args[0].as.as_type;
    int32_t count = value_to_int(args[1]);

    if (count <= 0) {
        runtime_error(ctx, "talloc() count must be positive"); return val_null();
    }

    int elem_size = get_type_size(type);

    // SECURITY: Check for multiplication overflow before allocating
    if ((size_t)count > SIZE_MAX / (size_t)elem_size) {
        runtime_error(ctx, "talloc() size overflow - allocation too large"); return val_null();
    }

    size_t total_size = (size_t)elem_size * (size_t)count;

    void *ptr = malloc(total_size);
    if (ptr == NULL) {
        return val_null();
    }

    // Record allocation for profiler and track pointer size
    PROFILER_ALLOC(ctx, ctx->current_source_file, ctx->current_line, (uint64_t)total_size);
    PROFILER_TRACK_PTR(ctx, ptr, (uint64_t)total_size, ctx->current_source_file, ctx->current_line);

    return val_ptr(ptr);
}

Value builtin_realloc(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "realloc() expects 2 arguments (ptr, new_size)"); return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "realloc() first argument must be a pointer"); return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "realloc() new_size must be an integer"); return val_null();
    }

    void *old_ptr = args[0].as.as_ptr;
    int32_t new_size = value_to_int(args[1]);

    if (new_size <= 0) {
        runtime_error(ctx, "realloc() new_size must be positive"); return val_null();
    }

    // Untrack old pointer to get its size (for accurate free tracking)
    uint64_t old_size = PROFILER_UNTRACK_PTR(ctx, old_ptr);

    void *new_ptr = realloc(old_ptr, new_size);
    if (new_ptr == NULL) {
        // Realloc failed - re-track the old pointer since it's still valid
        PROFILER_TRACK_PTR(ctx, old_ptr, old_size, ctx->current_source_file, ctx->current_line);
        return val_null();
    }

    // Record the size change: alloc new size at the new location
    // Note: We don't record a free since profiler_untrack_ptr already updated current_bytes
    PROFILER_ALLOC(ctx, ctx->current_source_file, ctx->current_line, (uint64_t)new_size);
    PROFILER_TRACK_PTR(ctx, new_ptr, (uint64_t)new_size, ctx->current_source_file, ctx->current_line);

    return val_ptr(new_ptr);
}
