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
            exit(1);
    }
}

Value builtin_alloc(Value *args, int num_args, ExecutionContext *ctx) {
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
        return val_null();
    }

    // Record allocation for profiler and track pointer size
    PROFILER_ALLOC(ctx, ctx->current_source_file, ctx->current_line, (uint64_t)size);
    PROFILER_TRACK_PTR(ctx, ptr, (uint64_t)size, ctx->current_source_file, ctx->current_line);

    return val_ptr(ptr);
}

Value builtin_free(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: free() expects 1 argument (pointer, buffer, object, or array)\n");
        exit(1);
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
            fprintf(stderr, "Runtime error: cannot free() a buffer slice view\n");
            fprintf(stderr, "  Buffer views share data with the parent buffer.\n");
            fprintf(stderr, "  Let the view go out of scope, or set it to null.\n");
            exit(1);
        }

        // Atomically check and set the freed flag to detect double-free
        int expected = 0;
        if (!atomic_compare_exchange_strong(&buf->freed, &expected, 1)) {
            fprintf(stderr, "Runtime error: double free detected on buffer\n");
            exit(1);
        }

        // Safety check: don't allow free on buffers shared outside the environment
        // Note: we check BEFORE calling value_release since that would skip due to freed flag
        if (buf->ref_count > 3) {  // 3 = creation + env + env_get
            int active_refs = buf->ref_count - 3;
            fprintf(stderr, "Runtime error: Cannot free buffer with %d active reference%s\n\n",
                    active_refs, active_refs == 1 ? "" : "s");
            fprintf(stderr, "  Hemlock prevents freeing buffers that have active references\n");
            fprintf(stderr, "  to avoid use-after-free memory corruption.\n\n");
            fprintf(stderr, "  Options to resolve this:\n");
            fprintf(stderr, "    1. Let the buffer go out of scope (automatic cleanup)\n");
            fprintf(stderr, "    2. Copy data from buffer before freeing: let copy = buf.to_array();\n");
            fprintf(stderr, "    3. Set references to null before freeing: ref = null; free(buf);\n");
            fprintf(stderr, "    4. Remove the explicit free() call - Hemlock manages buffer lifecycle\n\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: double free detected on object\n");
            exit(1);
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
            fprintf(stderr, "Runtime error: double free detected on array\n");
            exit(1);
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
        fprintf(stderr, "Runtime error: free() requires a pointer, buffer, object, or array\n");
        exit(1);
    }
}

Value builtin_memset(Value *args, int num_args, ExecutionContext *ctx) {
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

    // SECURITY: Validate size is non-negative to prevent undefined behavior
    if (size < 0) {
        fprintf(stderr, "Runtime error: memset() size cannot be negative\n");
        exit(1);
    }

    memset(ptr, byte, size);
    return val_null();
}

Value builtin_memcpy(Value *args, int num_args, ExecutionContext *ctx) {
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

    // SECURITY: Validate size is non-negative to prevent undefined behavior
    if (size < 0) {
        fprintf(stderr, "Runtime error: memcpy() size cannot be negative\n");
        exit(1);
    }

    memcpy(dest, src, size);
    return val_null();
}

Value builtin_sizeof(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sizeof() expects 1 argument (type)\n");
        exit(1);
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
        fprintf(stderr, "Runtime error: sizeof() requires a type argument\n");
        exit(1);
    }
}

Value builtin_buffer(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: buffer() expects 1 argument (size in bytes)\n");
        exit(1);
    }

    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: buffer() size must be an integer\n");
        exit(1);
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

    // SECURITY: Check for multiplication overflow before allocating
    if ((size_t)count > SIZE_MAX / (size_t)elem_size) {
        fprintf(stderr, "Runtime error: talloc() size overflow - allocation too large\n");
        exit(1);
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
