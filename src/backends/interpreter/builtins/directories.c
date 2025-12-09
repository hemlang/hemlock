#include "internal.h"

Value builtin_make_dir(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_remove_dir(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_list_dir(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_cwd(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_chdir(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_absolute_path(Value *args, int num_args, ExecutionContext *ctx) {
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
