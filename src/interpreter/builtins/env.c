#include "internal.h"

Value builtin_getenv(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_setenv(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_unsetenv(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_exit(Value *args, int num_args, ExecutionContext *ctx) {
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

Value builtin_get_pid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: get_pid() expects no arguments\n");
        exit(1);
    }
    return val_i32((int32_t)getpid());
}

Value builtin_exec(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: exec() expects 1 argument (command string)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: exec() argument must be a string\n");
        exit(1);
    }

    String *command = args[0].as.as_string;
    char *ccmd = malloc(command->length + 1);
    if (!ccmd) {
        fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
        exit(1);
    }
    memcpy(ccmd, command->data, command->length);
    ccmd[command->length] = '\0';

    // Open pipe to read command output
    FILE *pipe = popen(ccmd, "r");
    if (!pipe) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to execute command '%s': %s", ccmd, strerror(errno));
        free(ccmd);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Read output into buffer
    char *output_buffer = NULL;
    size_t output_size = 0;
    size_t output_capacity = 4096;
    output_buffer = malloc(output_capacity);
    if (!output_buffer) {
        fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
        pclose(pipe);
        free(ccmd);
        exit(1);
    }

    char chunk[4096];
    size_t bytes_read;
    while ((bytes_read = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        // Grow buffer if needed
        while (output_size + bytes_read > output_capacity) {
            output_capacity *= 2;
            char *new_buffer = realloc(output_buffer, output_capacity);
            if (!new_buffer) {
                fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
                free(output_buffer);
                pclose(pipe);
                free(ccmd);
                exit(1);
            }
            output_buffer = new_buffer;
        }
        memcpy(output_buffer + output_size, chunk, bytes_read);
        output_size += bytes_read;
    }

    // Get exit code
    int status = pclose(pipe);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    free(ccmd);

    // Ensure string is null-terminated
    if (output_size >= output_capacity) {
        output_capacity = output_size + 1;
        char *new_buffer = realloc(output_buffer, output_capacity);
        if (!new_buffer) {
            fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
            free(output_buffer);
            exit(1);
        }
        output_buffer = new_buffer;
    }
    output_buffer[output_size] = '\0';

    // Create result object with output and exit_code
    Object *result = object_new(NULL, 2);
    result->field_names[0] = strdup("output");
    result->field_values[0] = val_string_take(output_buffer, output_size, output_capacity);
    result->num_fields++;

    result->field_names[1] = strdup("exit_code");
    result->field_values[1] = val_i32(exit_code);
    result->num_fields++;

    return val_object(result);
}
