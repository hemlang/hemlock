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

// SECURITY WARNING: exec() uses popen() which passes commands through a shell.
// This is vulnerable to command injection if the command string contains untrusted input.
// For safe command execution, use exec_argv() or exec(cmd, args) instead which bypasses the shell.
Value builtin_exec(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if process spawning is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_PROCESS)) {
        sandbox_error(ctx, "command execution");
        return val_null();
    }

    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: exec() expects 1-2 arguments (command string, [args array])\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: exec() first argument must be a string\n");
        exit(1);
    }

    // If second argument is provided, use fork/execvp (safe, no shell)
    if (num_args == 2) {
        if (args[1].type != VAL_ARRAY) {
            fprintf(stderr, "Runtime error: exec() second argument must be an array of strings\n");
            exit(1);
        }

        String *command = args[0].as.as_string;
        Array *arr = args[1].as.as_array;

        // Build argv array: [command, ...args, NULL]
        char **argv = malloc((arr->length + 2) * sizeof(char*));
        if (!argv) {
            fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
            exit(1);
        }

        // First element is the command
        argv[0] = malloc(command->length + 1);
        if (!argv[0]) {
            fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
            free(argv);
            exit(1);
        }
        memcpy(argv[0], command->data, command->length);
        argv[0][command->length] = '\0';

        // Copy args from array
        for (int i = 0; i < arr->length; i++) {
            if (arr->elements[i].type != VAL_STRING) {
                fprintf(stderr, "Runtime error: exec() args array elements must be strings\n");
                for (int j = 0; j <= i; j++) free(argv[j]);
                free(argv);
                exit(1);
            }
            String *s = arr->elements[i].as.as_string;
            argv[i + 1] = malloc(s->length + 1);
            if (!argv[i + 1]) {
                fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
                for (int j = 0; j <= i; j++) free(argv[j]);
                free(argv);
                exit(1);
            }
            memcpy(argv[i + 1], s->data, s->length);
            argv[i + 1][s->length] = '\0';
        }
        argv[arr->length + 1] = NULL;

        // Create pipes for stdout and stderr
        int stdout_pipe[2];
        int stderr_pipe[2];
        if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "exec() pipe creation failed: %s", strerror(errno));
            for (int i = 0; i <= arr->length; i++) free(argv[i]);
            free(argv);
            ctx->exception_state.exception_value = val_string(error_msg);
            ctx->exception_state.is_throwing = 1;
            return val_null();
        }

        pid_t pid = fork();
        if (pid < 0) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "exec() fork failed: %s", strerror(errno));
            for (int i = 0; i <= arr->length; i++) free(argv[i]);
            free(argv);
            close(stdout_pipe[0]); close(stdout_pipe[1]);
            close(stderr_pipe[0]); close(stderr_pipe[1]);
            ctx->exception_state.exception_value = val_string(error_msg);
            ctx->exception_state.is_throwing = 1;
            return val_null();
        }

        if (pid == 0) {
            // Child process
            close(stdout_pipe[0]);  // Close read end
            close(stderr_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);

            execvp(argv[0], argv);
            // If execvp returns, it failed
            fprintf(stderr, "exec() failed to execute '%s': %s\n", argv[0], strerror(errno));
            _exit(127);
        }

        // Parent process
        close(stdout_pipe[1]);  // Close write end
        close(stderr_pipe[1]);

        // Free argv in parent (child has its own copy after fork)
        for (int i = 0; i <= arr->length; i++) free(argv[i]);
        free(argv);

        // Read output from child
        char *output_buffer = NULL;
        size_t output_size = 0;
        size_t output_capacity = 4096;
        output_buffer = malloc(output_capacity);
        if (!output_buffer) {
            fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            exit(1);
        }

        char chunk[4096];
        ssize_t bytes_read;
        while ((bytes_read = read(stdout_pipe[0], chunk, sizeof(chunk))) > 0) {
            // Check for overflow before doubling capacity
            while (output_size + (size_t)bytes_read > output_capacity) {
                if (output_capacity > SIZE_MAX / 2) {
                    fprintf(stderr, "Runtime error: exec() output too large\n");
                    free(output_buffer);
                    close(stdout_pipe[0]);
                    close(stderr_pipe[0]);
                    exit(1);
                }
                output_capacity *= 2;
                char *new_buffer = realloc(output_buffer, output_capacity);
                if (!new_buffer) {
                    fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
                    free(output_buffer);
                    close(stdout_pipe[0]);
                    close(stderr_pipe[0]);
                    exit(1);
                }
                output_buffer = new_buffer;
            }
            memcpy(output_buffer + output_size, chunk, (size_t)bytes_read);
            output_size += (size_t)bytes_read;
        }
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        // Wait for child
        int status;
        waitpid(pid, &status, 0);
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        // Ensure null termination
        if (output_size >= output_capacity) {
            char *new_buffer = realloc(output_buffer, output_size + 1);
            if (!new_buffer) {
                fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
                free(output_buffer);
                exit(1);
            }
            output_buffer = new_buffer;
            output_capacity = output_size + 1;
        }
        output_buffer[output_size] = '\0';

        // Create result object
        Object *result = object_new(NULL, 2);
        result->field_names[0] = strdup("output");
        result->field_values[0] = val_string_take(output_buffer, output_size, output_capacity);
        result->num_fields++;

        result->field_names[1] = strdup("exit_code");
        result->field_values[1] = val_i32(exit_code);
        result->num_fields++;

        return val_object(result);
    }

    // Single argument: use popen (shell mode)
    String *command = args[0].as.as_string;

    // SECURITY: Warn about potentially dangerous shell metacharacters
    const char *dangerous_chars = ";|&$`\\\"'<>(){}[]!#";
    for (int i = 0; i < command->length; i++) {
        for (const char *dc = dangerous_chars; *dc; dc++) {
            if (command->data[i] == *dc) {
                fprintf(stderr, "Warning: exec() command contains shell metacharacter '%c'. "
                        "Consider using exec_argv() for safer command execution.\n", *dc);
                goto done_warning;
            }
        }
    }
done_warning:
    ; // Empty statement required after label before declaration in C

    char *ccmd = malloc(command->length + 1);
    if (!ccmd) {
        fprintf(stderr, "Runtime error: exec() memory allocation failed\n");
        exit(1);
    }
    memcpy(ccmd, command->data, command->length);
    ccmd[command->length] = '\0';

    // Open pipe to read command output (uses shell - vulnerable to injection)
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
            // SECURITY: Check for overflow before doubling capacity
            if (output_capacity > SIZE_MAX / 2) {
                fprintf(stderr, "Runtime error: exec() output too large\n");
                free(output_buffer);
                pclose(pipe);
                free(ccmd);
                exit(1);
            }
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

// exec_argv() - Safe command execution without shell interpretation
// Takes an array of strings: [program, arg1, arg2, ...]
// Uses fork/execvp directly, preventing shell injection attacks
Value builtin_exec_argv(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if process spawning is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_PROCESS)) {
        sandbox_error(ctx, "command execution");
        return val_null();
    }

    if (num_args != 1) {
        fprintf(stderr, "Runtime error: exec_argv() expects 1 argument (array of strings)\n");
        exit(1);
    }

    if (args[0].type != VAL_ARRAY) {
        fprintf(stderr, "Runtime error: exec_argv() argument must be an array of strings\n");
        exit(1);
    }

    Array *arr = args[0].as.as_array;
    if (arr->length == 0) {
        fprintf(stderr, "Runtime error: exec_argv() array must not be empty\n");
        exit(1);
    }

    // Build argv array for execvp
    char **argv = malloc((arr->length + 1) * sizeof(char*));
    if (!argv) {
        fprintf(stderr, "Runtime error: exec_argv() memory allocation failed\n");
        exit(1);
    }

    for (int i = 0; i < arr->length; i++) {
        if (arr->elements[i].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: exec_argv() array elements must be strings\n");
            for (int j = 0; j < i; j++) free(argv[j]);
            free(argv);
            exit(1);
        }
        String *s = arr->elements[i].as.as_string;
        argv[i] = malloc(s->length + 1);
        if (!argv[i]) {
            fprintf(stderr, "Runtime error: exec_argv() memory allocation failed\n");
            for (int j = 0; j < i; j++) free(argv[j]);
            free(argv);
            exit(1);
        }
        memcpy(argv[i], s->data, s->length);
        argv[i][s->length] = '\0';
    }
    argv[arr->length] = NULL;

    // Create pipes for stdout and stderr
    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "exec_argv() pipe creation failed: %s", strerror(errno));
        for (int i = 0; i < arr->length; i++) free(argv[i]);
        free(argv);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    pid_t pid = fork();
    if (pid < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "exec_argv() fork failed: %s", strerror(errno));
        for (int i = 0; i < arr->length; i++) free(argv[i]);
        free(argv);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);  // Close read end
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execvp(argv[0], argv);
        // If execvp returns, it failed
        fprintf(stderr, "exec_argv() failed to execute '%s': %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    // Parent process
    close(stdout_pipe[1]);  // Close write end
    close(stderr_pipe[1]);

    // Free argv in parent (child has its own copy after fork)
    for (int i = 0; i < arr->length; i++) free(argv[i]);
    free(argv);

    // Read output from child
    char *output_buffer = NULL;
    size_t output_size = 0;
    size_t output_capacity = 4096;
    output_buffer = malloc(output_capacity);
    if (!output_buffer) {
        fprintf(stderr, "Runtime error: exec_argv() memory allocation failed\n");
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        exit(1);
    }

    char chunk[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(stdout_pipe[0], chunk, sizeof(chunk))) > 0) {
        // Check for overflow before doubling capacity
        while (output_size + (size_t)bytes_read > output_capacity) {
            if (output_capacity > SIZE_MAX / 2) {
                fprintf(stderr, "Runtime error: exec_argv() output too large\n");
                free(output_buffer);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                exit(1);
            }
            output_capacity *= 2;
            char *new_buffer = realloc(output_buffer, output_capacity);
            if (!new_buffer) {
                fprintf(stderr, "Runtime error: exec_argv() memory allocation failed\n");
                free(output_buffer);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                exit(1);
            }
            output_buffer = new_buffer;
        }
        memcpy(output_buffer + output_size, chunk, (size_t)bytes_read);
        output_size += (size_t)bytes_read;
    }
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Wait for child
    int status;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    // Ensure null termination
    if (output_size >= output_capacity) {
        char *new_buffer = realloc(output_buffer, output_size + 1);
        if (!new_buffer) {
            fprintf(stderr, "Runtime error: exec_argv() memory allocation failed\n");
            free(output_buffer);
            exit(1);
        }
        output_buffer = new_buffer;
        output_capacity = output_size + 1;
    }
    output_buffer[output_size] = '\0';

    // Create result object
    Object *result = object_new(NULL, 2);
    result->field_names[0] = strdup("output");
    result->field_values[0] = val_string_take(output_buffer, output_size, output_capacity);
    result->num_fields++;

    result->field_names[1] = strdup("exit_code");
    result->field_values[1] = val_i32(exit_code);
    result->num_fields++;

    return val_object(result);
}

Value builtin_getppid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: getppid() expects no arguments\n");
        exit(1);
    }
    return val_i32((int32_t)getppid());
}

Value builtin_getuid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: getuid() expects no arguments\n");
        exit(1);
    }
    return val_i32((int32_t)getuid());
}

Value builtin_geteuid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: geteuid() expects no arguments\n");
        exit(1);
    }
    return val_i32((int32_t)geteuid());
}

Value builtin_getgid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: getgid() expects no arguments\n");
        exit(1);
    }
    return val_i32((int32_t)getgid());
}

Value builtin_getegid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: getegid() expects no arguments\n");
        exit(1);
    }
    return val_i32((int32_t)getegid());
}

Value builtin_kill(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: kill() expects 2 arguments (pid, signal)\n");
        exit(1);
    }
    if (!is_integer(args[0]) || !is_integer(args[1])) {
        fprintf(stderr, "Runtime error: kill() arguments must be integers\n");
        exit(1);
    }

    int pid = value_to_int(args[0]);
    int sig = value_to_int(args[1]);

    if (kill(pid, sig) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "kill(%d, %d) failed: %s", pid, sig, strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_null();
}

Value builtin_fork(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;

    // SANDBOX: Check if process spawning is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_PROCESS)) {
        sandbox_error(ctx, "process forking");
        return val_null();
    }

    if (num_args != 0) {
        fprintf(stderr, "Runtime error: fork() expects no arguments\n");
        exit(1);
    }

    pid_t pid = fork();
    if (pid < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "fork() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_i32((int32_t)pid);
}

Value builtin_wait(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: wait() expects no arguments\n");
        exit(1);
    }

    int status;
    pid_t pid = wait(&status);
    if (pid < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "wait() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Create result object with pid and status
    Object *result = object_new(NULL, 2);
    result->field_names[0] = strdup("pid");
    result->field_values[0] = val_i32((int32_t)pid);
    result->num_fields++;

    result->field_names[1] = strdup("status");
    result->field_values[1] = val_i32(status);
    result->num_fields++;

    return val_object(result);
}

Value builtin_waitpid(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: waitpid() expects 1-2 arguments (pid, [options])\n");
        exit(1);
    }
    if (!is_integer(args[0])) {
        fprintf(stderr, "Runtime error: waitpid() pid must be an integer\n");
        exit(1);
    }
    if (num_args == 2 && !is_integer(args[1])) {
        fprintf(stderr, "Runtime error: waitpid() options must be an integer\n");
        exit(1);
    }

    pid_t pid = (pid_t)value_to_int(args[0]);
    int options = (num_args == 2) ? value_to_int(args[1]) : 0;

    int status;
    pid_t result_pid = waitpid(pid, &status, options);
    if (result_pid < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "waitpid(%d, %d) failed: %s", pid, options, strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Create result object with pid and status
    Object *result = object_new(NULL, 2);
    result->field_names[0] = strdup("pid");
    result->field_values[0] = val_i32((int32_t)result_pid);
    result->num_fields++;

    result->field_names[1] = strdup("status");
    result->field_values[1] = val_i32(status);
    result->num_fields++;

    return val_object(result);
}

Value builtin_abort(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: abort() expects no arguments\n");
        exit(1);
    }
    abort();
    return val_null();  // Never reached
}
