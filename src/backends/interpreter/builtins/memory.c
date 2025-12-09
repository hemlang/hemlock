#include "internal.h"
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
        return val_null();
    }

    return val_ptr(ptr);
}

Value builtin_free(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: free() expects 1 argument (pointer, buffer, object, or array)\n");
        exit(1);
    }

    if (args[0].type == VAL_PTR) {
        free(args[0].as.as_ptr);
        return val_null();
    } else if (args[0].type == VAL_BUFFER) {
        Buffer *buf = args[0].as.as_buffer;

        // Atomically check and set the freed flag to detect double-free
        int expected = 0;
        if (!atomic_compare_exchange_strong(&buf->freed, &expected, 1)) {
            fprintf(stderr, "Runtime error: double free detected on buffer\n");
            exit(1);
        }

        // Safety check: don't allow free on buffers shared outside the environment
        // Note: we check BEFORE calling value_release since that would skip due to freed flag
        if (buf->ref_count > 3) {  // 3 = creation + env + env_get
            fprintf(stderr, "Runtime error: Cannot free buffer with %d active references. "
                    "Ensure exclusive ownership before calling free().\n", buf->ref_count);
            exit(1);
        }

        // Free the internal data but keep the struct alive for cleanup to check freed flag
        free(buf->data);
        buf->data = NULL;
        buf->length = 0;
        buf->capacity = 0;
        // Note: We don't free(buf) here - the struct must remain accessible so cleanup
        // can check the freed flag. The struct will leak, but this is manual memory
        // management and the user has explicitly freed the buffer's data.
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
            value_release(obj->field_values[i]);
            free(obj->field_names[i]);
        }
        // Free internal data but keep struct alive for cleanup to check freed flag
        free(obj->field_names);
        free(obj->field_values);
        if (obj->type_name) free(obj->type_name);
        obj->field_names = NULL;
        obj->field_values = NULL;
        obj->type_name = NULL;
        obj->num_fields = 0;
        obj->capacity = 0;
        // Note: We don't free(obj) here - struct must remain for freed flag check
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
        arr->elements = NULL;
        arr->length = 0;
        arr->capacity = 0;
        // Note: We don't free(arr) here - struct must remain for freed flag check
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

    memcpy(dest, src, size);
    return val_null();
}

Value builtin_sizeof(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: sizeof() expects 1 argument (type)\n");
        exit(1);
    }

    TypeKind kind;

    if (args[0].type == VAL_TYPE) {
        kind = args[0].as.as_type;
    } else if (args[0].type == VAL_STRING) {
        // Also accept string arguments for parity with compiler
        const char *type_name = args[0].as.as_string->data;
        if (strcmp(type_name, "i8") == 0) kind = TYPE_I8;
        else if (strcmp(type_name, "i16") == 0) kind = TYPE_I16;
        else if (strcmp(type_name, "i32") == 0 || strcmp(type_name, "integer") == 0) kind = TYPE_I32;
        else if (strcmp(type_name, "i64") == 0) kind = TYPE_I64;
        else if (strcmp(type_name, "u8") == 0 || strcmp(type_name, "byte") == 0) kind = TYPE_U8;
        else if (strcmp(type_name, "u16") == 0) kind = TYPE_U16;
        else if (strcmp(type_name, "u32") == 0) kind = TYPE_U32;
        else if (strcmp(type_name, "u64") == 0) kind = TYPE_U64;
        else if (strcmp(type_name, "f32") == 0) kind = TYPE_F32;
        else if (strcmp(type_name, "f64") == 0 || strcmp(type_name, "number") == 0) kind = TYPE_F64;
        else if (strcmp(type_name, "bool") == 0) kind = TYPE_BOOL;
        else if (strcmp(type_name, "ptr") == 0) kind = TYPE_PTR;
        else if (strcmp(type_name, "rune") == 0) kind = TYPE_RUNE;
        else {
            // Unknown type - return 0 for parity with compiler
            return val_i32(0);
        }
    } else {
        fprintf(stderr, "Runtime error: sizeof() requires a type argument\n");
        exit(1);
    }

    int size = get_type_size(kind);
    return val_i32(size);
}

Value builtin_buffer(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_talloc(Value *args, int num_args, ExecutionContext *ctx) {
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
        return val_null();
    }

    return val_ptr(ptr);
}

Value builtin_realloc(Value *args, int num_args, ExecutionContext *ctx) {
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
        return val_null();
    }

    return val_ptr(new_ptr);
}
