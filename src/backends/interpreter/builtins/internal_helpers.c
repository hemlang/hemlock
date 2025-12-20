#include "internal.h"

Value builtin_read_u32(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_read_u64(Value *args, int num_args, ExecutionContext *ctx) {
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

// Read a pointer from memory (for pointer-to-pointer / double indirection FFI calls)
Value builtin_read_ptr(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __read_ptr() expects 1 argument (ptr)\n");
        exit(1);
    }

    if (args[0].type != VAL_PTR) {
        fprintf(stderr, "Runtime error: __read_ptr() requires a pointer\n");
        exit(1);
    }

    void **pptr = (void**)args[0].as.as_ptr;
    return val_ptr(*pptr);
}

Value builtin_strerror_fn(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: __strerror() expects 0 arguments\n");
        exit(1);
    }

    return val_string(strerror(errno));
}

Value builtin_dirent_name(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_string_to_cstr(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_cstr_to_string(Value *args, int num_args, ExecutionContext *ctx) {
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

// Convert an array of bytes (integers 0-255) to a UTF-8 string
// This allows proper reconstruction of multi-byte UTF-8 sequences
Value builtin_string_from_bytes(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: __string_from_bytes() expects 1 argument (array of bytes or buffer)\n");
        exit(1);
    }

    char *data = NULL;
    int length = 0;

    if (args[0].type == VAL_BUFFER) {
        // Handle buffer input
        Buffer *buf = args[0].as.as_buffer;
        if (!buf || !buf->data) {
            return val_string("");
        }
        length = buf->length;
        data = malloc(length + 1);
        if (!data) {
            fprintf(stderr, "Runtime error: __string_from_bytes() memory allocation failed\n");
            exit(1);
        }
        memcpy(data, buf->data, length);
        data[length] = '\0';
    } else if (args[0].type == VAL_ARRAY) {
        // Handle array input - each element should be an integer byte value
        Array *arr = args[0].as.as_array;
        if (!arr || arr->length == 0) {
            return val_string("");
        }
        length = arr->length;
        data = malloc(length + 1);
        if (!data) {
            fprintf(stderr, "Runtime error: __string_from_bytes() memory allocation failed\n");
            exit(1);
        }

        for (int i = 0; i < arr->length; i++) {
            Value elem = arr->elements[i];
            int byte_val;

            // Accept any integer type
            if (elem.type == VAL_I8) {
                byte_val = (unsigned char)elem.as.as_i8;
            } else if (elem.type == VAL_I16) {
                byte_val = elem.as.as_i16 & 0xFF;
            } else if (elem.type == VAL_I32) {
                byte_val = elem.as.as_i32 & 0xFF;
            } else if (elem.type == VAL_I64) {
                byte_val = (int)(elem.as.as_i64 & 0xFF);
            } else if (elem.type == VAL_U8) {
                byte_val = elem.as.as_u8;
            } else if (elem.type == VAL_U16) {
                byte_val = elem.as.as_u16 & 0xFF;
            } else if (elem.type == VAL_U32) {
                byte_val = elem.as.as_u32 & 0xFF;
            } else if (elem.type == VAL_U64) {
                byte_val = (int)(elem.as.as_u64 & 0xFF);
            } else {
                free(data);
                fprintf(stderr, "Runtime error: __string_from_bytes() array element at index %d is not an integer\n", i);
                exit(1);
            }

            data[i] = (char)byte_val;
        }
        data[length] = '\0';
    } else {
        fprintf(stderr, "Runtime error: __string_from_bytes() requires array or buffer argument\n");
        exit(1);
    }

    // Use val_string_take to avoid copying the data again
    return val_string_take(data, length, length + 1);
}
