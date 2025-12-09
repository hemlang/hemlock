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
