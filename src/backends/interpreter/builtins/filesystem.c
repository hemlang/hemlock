#include "internal.h"

Value builtin_exists(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_read_file(Value *args, int num_args, ExecutionContext *ctx) {
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

    // SECURITY: Use O_NOFOLLOW to prevent symlink attacks
    int fd = open(cpath, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        char error_msg[512];
        if (errno == ELOOP) {
            snprintf(error_msg, sizeof(error_msg), "Cannot read '%s': symbolic links not allowed", cpath);
        } else {
            snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        }
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    FILE *fp = fdopen(fd, "r");
    if (!fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        close(fd);
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

Value builtin_write_file(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: write_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: write_file() requires string path\n");
        exit(1);
    }

    // Accept both string and buffer content
    if (args[1].type != VAL_STRING && args[1].type != VAL_BUFFER) {
        fprintf(stderr, "Runtime error: write_file() requires string or buffer content\n");
        exit(1);
    }

    String *path = args[0].as.as_string;

    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        fprintf(stderr, "Runtime error: write_file() memory allocation failed\n");
        exit(1);
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    // SECURITY: Use O_NOFOLLOW to prevent symlink attacks
    // O_CREAT | O_TRUNC to create or truncate the file
    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) {
        char error_msg[512];
        if (errno == ELOOP) {
            snprintf(error_msg, sizeof(error_msg), "Cannot write '%s': symbolic links not allowed", cpath);
        } else {
            snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        }
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Use binary mode to preserve all bytes
    FILE *fp = fdopen(fd, "wb");
    if (!fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        close(fd);
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    if (args[1].type == VAL_STRING) {
        String *content = args[1].as.as_string;
        fwrite(content->data, 1, content->length, fp);
    } else {
        Buffer *content = args[1].as.as_buffer;
        fwrite(content->data, 1, content->length, fp);
    }

    fclose(fp);
    free(cpath);
    return val_null();
}

Value builtin_append_file(Value *args, int num_args, ExecutionContext *ctx) {
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

    // SECURITY: Use O_NOFOLLOW to prevent symlink attacks
    // O_CREAT | O_APPEND to create or append to the file
    int fd = open(cpath, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0644);
    if (fd < 0) {
        char error_msg[512];
        if (errno == ELOOP) {
            snprintf(error_msg, sizeof(error_msg), "Cannot append '%s': symbolic links not allowed", cpath);
        } else {
            snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        }
        free(cpath);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    FILE *fp = fdopen(fd, "a");
    if (!fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open '%s': %s", cpath, strerror(errno));
        close(fd);
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

Value builtin_remove_file(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_rename(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_copy_file(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_is_file(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_is_dir(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_file_stat(Value *args, int num_args, ExecutionContext *ctx) {
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
