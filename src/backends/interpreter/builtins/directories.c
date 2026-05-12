#include "internal.h"

Value builtin_make_dir(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        runtime_error(ctx, "make_dir() expects 1-2 arguments (path, [mode]), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "make_dir() requires a string path, got %s", value_type_name(args[0].type));
        return val_null();
    }

    uint32_t mode = 0755;  // Default mode
    if (num_args == 2) {
        if (args[1].type != VAL_U32) {
            runtime_error(ctx, "make_dir() mode must be u32, got %s", value_type_name(args[1].type));
            return val_null();
        }
        mode = args[1].as.as_u32;
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        runtime_error(ctx, "make_dir() memory allocation failed");
        return val_null();
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    // SANDBOX: Check if directory creation is allowed (treated as write)
    if (!sandbox_path_allowed(ctx, cpath, 1)) {
        free(cpath);
        sandbox_error(ctx, "directory creation");
        return val_null();
    }

    if (mkdir(cpath, mode) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to create directory '%s': %s", cpath, strerror(errno));
        free(cpath);
        exception_set_value(ctx, val_string(error_msg));
        return val_null();
    }

    free(cpath);
    return val_null();
}

Value builtin_remove_dir(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "remove_dir() expects 1 argument (path), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "remove_dir() requires a string path, got %s", value_type_name(args[0].type));
        return val_null();
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        runtime_error(ctx, "remove_dir() memory allocation failed");
        return val_null();
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    // SANDBOX: Check if directory removal is allowed (treated as write)
    if (!sandbox_path_allowed(ctx, cpath, 1)) {
        free(cpath);
        sandbox_error(ctx, "directory removal");
        return val_null();
    }

    if (rmdir(cpath) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to remove directory '%s': %s", cpath, strerror(errno));
        free(cpath);
        exception_set_value(ctx, val_string(error_msg));
        return val_null();
    }

    free(cpath);
    return val_null();
}

Value builtin_list_dir(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "list_dir() expects 1 argument (path), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "list_dir() requires a string path, got %s", value_type_name(args[0].type));
        return val_null();
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        runtime_error(ctx, "list_dir() memory allocation failed");
        return val_null();
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    // SANDBOX: Check if directory read is allowed
    if (!sandbox_path_allowed(ctx, cpath, 0)) {
        free(cpath);
        sandbox_error(ctx, "directory listing outside sandbox root");
        return val_null();
    }

    DIR *dir = opendir(cpath);
    if (!dir) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open directory '%s': %s", cpath, strerror(errno));
        free(cpath);
        exception_set_value(ctx, val_string(error_msg));
        return val_null();
    }

    Array *entries = array_new();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        Value str_val = val_string(entry->d_name);
        array_push(entries, str_val);
        value_release(str_val);  // array_push retains, so release our reference
    }

    closedir(dir);
    free(cpath);
    // Ownership of array transfers to caller via return value
    return val_array(entries);
}

Value builtin_cwd(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "cwd() expects 0 arguments, got %d", num_args);
        return val_null();
    }

    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        exception_set_value(ctx, val_string(strerror(errno)));
        return val_null();
    }

    return val_string(buffer);
}

Value builtin_chdir(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "chdir() expects 1 argument (path), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "chdir() requires a string path, got %s", value_type_name(args[0].type));
        return val_null();
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        runtime_error(ctx, "chdir() memory allocation failed");
        return val_null();
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    if (chdir(cpath) != 0) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to change directory to '%s': %s", cpath, strerror(errno));
        free(cpath);
        exception_set_value(ctx, val_string(error_msg));
        return val_null();
    }

    free(cpath);
    return val_null();
}

Value builtin_absolute_path(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "absolute_path() expects 1 argument (path), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "absolute_path() requires a string path, got %s", value_type_name(args[0].type));
        return val_null();
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        runtime_error(ctx, "absolute_path() memory allocation failed");
        return val_null();
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    char buffer[PATH_MAX];
    if (realpath(cpath, buffer) == NULL) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to resolve path '%s': %s", cpath, strerror(errno));
        free(cpath);
        exception_set_value(ctx, val_string(error_msg));
        return val_null();
    }

    free(cpath);
    return val_string(buffer);
}
