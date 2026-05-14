// _GNU_SOURCE exposes posix_spawn_file_actions_addchdir_np (glibc 2.29+) and
// POSIX_SPAWN_SETSID (glibc 2.26+). Must be defined before any system header
// inclusion, including the chain pulled in by internal.h. Guarded so it does
// not collide with a -D_GNU_SOURCE on the build command line (e.g. WASM).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "internal.h"

Value builtin_getenv(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "getenv() expects 1 argument (variable name), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "getenv() argument must be a string, got %s", value_type_name(args[0].type));
        return val_null();
    }
    String *name = args[0].as.as_string;
    char *cname = malloc(name->length + 1);
    if (cname == NULL) {
        runtime_error(ctx, "getenv() memory allocation failed");
        return val_null();
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
    if (num_args != 2) {
        runtime_error(ctx, "setenv() expects 2 arguments (name, value), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "setenv() name must be a string, got %s", value_type_name(args[0].type));
        return val_null();
    }
    if (args[1].type != VAL_STRING) {
        runtime_error(ctx, "setenv() value must be a string, got %s", value_type_name(args[1].type));
        return val_null();
    }

    String *name = args[0].as.as_string;
    String *value = args[1].as.as_string;

    char *cname = malloc(name->length + 1);
    char *cvalue = malloc(value->length + 1);
    if (cname == NULL || cvalue == NULL) {
        free(cname);
        free(cvalue);
        runtime_error(ctx, "setenv() memory allocation failed");
        return val_null();
    }

    memcpy(cname, name->data, name->length);
    cname[name->length] = '\0';
    memcpy(cvalue, value->data, value->length);
    cvalue[value->length] = '\0';

    int result = setenv(cname, cvalue, 1);
    free(cname);
    free(cvalue);

    if (result != 0) {
        runtime_error(ctx, "setenv() failed: %s", strerror(errno));
        return val_null();
    }
    return val_null();
}

Value builtin_unsetenv(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "unsetenv() expects 1 argument (variable name), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "unsetenv() argument must be a string, got %s", value_type_name(args[0].type));
        return val_null();
    }

    String *name = args[0].as.as_string;
    char *cname = malloc(name->length + 1);
    if (cname == NULL) {
        runtime_error(ctx, "unsetenv() memory allocation failed");
        return val_null();
    }
    memcpy(cname, name->data, name->length);
    cname[name->length] = '\0';

    int result = unsetenv(cname);
    free(cname);

    if (result != 0) {
        runtime_error(ctx, "unsetenv() failed: %s", strerror(errno));
        return val_null();
    }
    return val_null();
}

Value builtin_exit(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args > 1) {
        runtime_error(ctx, "exit() expects 0 or 1 argument (exit code), got %d", num_args);
        return val_null();
    }

    int exit_code = 0;
    if (num_args == 1) {
        if (!is_integer(args[0])) {
            runtime_error(ctx, "exit() argument must be an integer, got %s", value_type_name(args[0].type));
            return val_null();
        }
        exit_code = value_to_int(args[0]);
    }

    exit(exit_code);
}

Value builtin_get_pid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "get_pid() expects no arguments, got %d", num_args);
        return val_null();
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
        runtime_error(ctx, "exec() expects 1-2 arguments (command string, [args array]), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "exec() first argument must be a string, got %s", value_type_name(args[0].type));
        return val_null();
    }

    // If second argument is provided, use fork/execvp (safe, no shell)
    if (num_args == 2) {
        if (args[1].type != VAL_ARRAY) {
            runtime_error(ctx, "exec() second argument must be an array of strings, got %s", value_type_name(args[1].type));
            return val_null();
        }

        String *command = args[0].as.as_string;
        Array *arr = args[1].as.as_array;

        // Build argv array: [command, ...args, NULL]
        char **argv = malloc((arr->length + 2) * sizeof(char*));
        if (!argv) {
            runtime_error(ctx, "exec() memory allocation failed");
            return val_null();
        }

        // First element is the command
        argv[0] = malloc(command->length + 1);
        if (!argv[0]) {
            free(argv);
            runtime_error(ctx, "exec() memory allocation failed");
            return val_null();
        }
        memcpy(argv[0], command->data, command->length);
        argv[0][command->length] = '\0';

        // Copy args from array
        for (int i = 0; i < arr->length; i++) {
            if (arr->elements[i].type != VAL_STRING) {
                for (int j = 0; j <= i; j++) free(argv[j]);
                free(argv);
                runtime_error(ctx, "exec() args array element %d must be a string, got %s", i, value_type_name(arr->elements[i].type));
                return val_null();
            }
            String *s = arr->elements[i].as.as_string;
            argv[i + 1] = malloc(s->length + 1);
            if (!argv[i + 1]) {
                for (int j = 0; j <= i; j++) free(argv[j]);
                free(argv);
                runtime_error(ctx, "exec() memory allocation failed");
                return val_null();
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

        // Read output from child (both stdout and stderr using poll to avoid deadlock)
        char *output_buffer = NULL;
        size_t output_size = 0;
        size_t output_capacity = 4096;
        output_buffer = malloc(output_capacity);

        char *stderr_buffer = NULL;
        size_t stderr_size = 0;
        size_t stderr_capacity = 4096;
        stderr_buffer = malloc(stderr_capacity);

        if (!output_buffer || !stderr_buffer) {
            free(output_buffer);
            free(stderr_buffer);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            runtime_error(ctx, "exec() memory allocation failed");
            return val_null();
        }

        struct pollfd fds[2];
        fds[0].fd = stdout_pipe[0];
        fds[0].events = POLLIN;
        fds[1].fd = stderr_pipe[0];
        fds[1].events = POLLIN;

        int open_fds = 2;
        char chunk[4096];

        while (open_fds > 0) {
            int ret = poll(fds, 2, -1);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }

            // Read from stdout if available
            if (fds[0].revents & (POLLIN | POLLHUP)) {
                ssize_t bytes_read = read(stdout_pipe[0], chunk, sizeof(chunk));
                if (bytes_read > 0) {
                    while (output_size + (size_t)bytes_read > output_capacity) {
                        if (output_capacity > SIZE_MAX / 2) {
                            free(output_buffer);
                            free(stderr_buffer);
                            close(stdout_pipe[0]);
                            close(stderr_pipe[0]);
                            runtime_error(ctx, "exec() output too large");
                            return val_null();
                        }
                        output_capacity *= 2;
                        char *new_buffer = realloc(output_buffer, output_capacity);
                        if (!new_buffer) {
                            free(output_buffer);
                            free(stderr_buffer);
                            close(stdout_pipe[0]);
                            close(stderr_pipe[0]);
                            runtime_error(ctx, "exec() memory allocation failed");
                            return val_null();
                        }
                        output_buffer = new_buffer;
                    }
                    memcpy(output_buffer + output_size, chunk, (size_t)bytes_read);
                    output_size += (size_t)bytes_read;
                } else if (bytes_read == 0 || (bytes_read < 0 && errno != EINTR)) {
                    fds[0].fd = -1;  // Stop polling this fd
                    open_fds--;
                }
            }

            // Read from stderr if available
            if (fds[1].revents & (POLLIN | POLLHUP)) {
                ssize_t bytes_read = read(stderr_pipe[0], chunk, sizeof(chunk));
                if (bytes_read > 0) {
                    while (stderr_size + (size_t)bytes_read > stderr_capacity) {
                        if (stderr_capacity > SIZE_MAX / 2) {
                            free(output_buffer);
                            free(stderr_buffer);
                            close(stdout_pipe[0]);
                            close(stderr_pipe[0]);
                            runtime_error(ctx, "exec() stderr output too large");
                            return val_null();
                        }
                        stderr_capacity *= 2;
                        char *new_buffer = realloc(stderr_buffer, stderr_capacity);
                        if (!new_buffer) {
                            free(output_buffer);
                            free(stderr_buffer);
                            close(stdout_pipe[0]);
                            close(stderr_pipe[0]);
                            runtime_error(ctx, "exec() memory allocation failed");
                            return val_null();
                        }
                        stderr_buffer = new_buffer;
                    }
                    memcpy(stderr_buffer + stderr_size, chunk, (size_t)bytes_read);
                    stderr_size += (size_t)bytes_read;
                } else if (bytes_read == 0 || (bytes_read < 0 && errno != EINTR)) {
                    fds[1].fd = -1;  // Stop polling this fd
                    open_fds--;
                }
            }
        }
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        // Wait for child
        int status;
        waitpid(pid, &status, 0);
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        // Ensure null termination for stdout
        if (output_size >= output_capacity) {
            char *new_buffer = realloc(output_buffer, output_size + 1);
            if (!new_buffer) {
                free(output_buffer);
                free(stderr_buffer);
                runtime_error(ctx, "exec() memory allocation failed");
                return val_null();
            }
            output_buffer = new_buffer;
            output_capacity = output_size + 1;
        }
        output_buffer[output_size] = '\0';

        // Ensure null termination for stderr
        if (stderr_size >= stderr_capacity) {
            char *new_buffer = realloc(stderr_buffer, stderr_size + 1);
            if (!new_buffer) {
                free(output_buffer);
                free(stderr_buffer);
                runtime_error(ctx, "exec() memory allocation failed");
                return val_null();
            }
            stderr_buffer = new_buffer;
            stderr_capacity = stderr_size + 1;
        }
        stderr_buffer[stderr_size] = '\0';

        // Create result object
        Object *result = object_new(NULL, 3);
        if (!result) {
            free(output_buffer);
            free(stderr_buffer);
            runtime_error(ctx, "exec() memory allocation failed");
            return val_null();
        }
        result->fields[0].name = strdup("output");
        if (!result->fields[0].name) {
            free(output_buffer);
            free(stderr_buffer);
            object_free(result);
            runtime_error(ctx, "exec() memory allocation failed");
            return val_null();
        }
        result->fields[0].value = val_string_take(output_buffer, output_size, output_capacity);
        result->num_fields++;

        result->fields[1].name = strdup("stderr");
        if (!result->fields[1].name) {
            free(stderr_buffer);
            object_free(result);
            runtime_error(ctx, "exec() memory allocation failed");
            return val_null();
        }
        result->fields[1].value = val_string_take(stderr_buffer, stderr_size, stderr_capacity);
        result->num_fields++;

        result->fields[2].name = strdup("exit_code");
        if (!result->fields[2].name) {
            object_free(result);
            runtime_error(ctx, "exec() memory allocation failed");
            return val_null();
        }
        result->fields[2].value = val_i32(exit_code);
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
        runtime_error(ctx, "exec() memory allocation failed");
        return val_null();
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
        pclose(pipe);
        free(ccmd);
        runtime_error(ctx, "exec() memory allocation failed");
        return val_null();
    }

    char chunk[4096];
    size_t bytes_read;
    while ((bytes_read = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        // Grow buffer if needed
        while (output_size + bytes_read > output_capacity) {
            // SECURITY: Check for overflow before doubling capacity
            if (output_capacity > SIZE_MAX / 2) {
                free(output_buffer);
                pclose(pipe);
                free(ccmd);
                runtime_error(ctx, "exec() output too large");
                return val_null();
            }
            output_capacity *= 2;
            char *new_buffer = realloc(output_buffer, output_capacity);
            if (!new_buffer) {
                free(output_buffer);
                pclose(pipe);
                free(ccmd);
                runtime_error(ctx, "exec() memory allocation failed");
                return val_null();
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
            free(output_buffer);
            runtime_error(ctx, "exec() memory allocation failed");
            return val_null();
        }
        output_buffer = new_buffer;
    }
    output_buffer[output_size] = '\0';

    // Create result object with output, stderr, and exit_code
    // Note: popen() can't capture stderr separately, so we return empty string for it
    Object *result = object_new(NULL, 3);
    if (!result) {
        free(output_buffer);
        runtime_error(ctx, "exec() memory allocation failed");
        return val_null();
    }
    result->fields[0].name = strdup("output");
    if (!result->fields[0].name) {
        free(output_buffer);
        object_free(result);
        runtime_error(ctx, "exec() memory allocation failed");
        return val_null();
    }
    result->fields[0].value = val_string_take(output_buffer, output_size, output_capacity);
    result->num_fields++;

    result->fields[1].name = strdup("stderr");
    if (!result->fields[1].name) {
        object_free(result);
        runtime_error(ctx, "exec() memory allocation failed");
        return val_null();
    }
    result->fields[1].value = val_string("");
    result->num_fields++;

    result->fields[2].name = strdup("exit_code");
    if (!result->fields[2].name) {
        object_free(result);
        runtime_error(ctx, "exec() memory allocation failed");
        return val_null();
    }
    result->fields[2].value = val_i32(exit_code);
    result->num_fields++;

    return val_object(result);
}

// exec_argv() - Safe command execution without shell interpretation
// Takes an array of strings: [program, arg1, arg2, ...]
// Optional second argument: options object with optional fields:
//   stdin: string or buffer to pipe into the child's stdin
// Uses fork/execvp directly, preventing shell injection attacks
Value builtin_exec_argv(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if process spawning is allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_PROCESS)) {
        sandbox_error(ctx, "command execution");
        return val_null();
    }

    if (num_args < 1 || num_args > 2) {
        runtime_error(ctx, "exec_argv() expects 1-2 arguments, got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_ARRAY) {
        runtime_error(ctx, "exec_argv() argument must be an array of strings, got %s", value_type_name(args[0].type));
        return val_null();
    }

    // Extract stdin data from opts (string or buffer)
    const char *stdin_data = NULL;
    size_t stdin_len = 0;
    if (num_args == 2 && args[1].type == VAL_OBJECT && args[1].as.as_object) {
        Object *opts = args[1].as.as_object;
        int idx = object_lookup_field(opts, "stdin");
        if (idx >= 0) {
            Value stdin_val = opts->fields[idx].value;
            if (stdin_val.type == VAL_STRING && stdin_val.as.as_string) {
                stdin_data = stdin_val.as.as_string->data;
                stdin_len = (size_t)stdin_val.as.as_string->length;
            } else if (stdin_val.type == VAL_BUFFER && stdin_val.as.as_buffer) {
                stdin_data = (const char *)stdin_val.as.as_buffer->data;
                stdin_len = (size_t)stdin_val.as.as_buffer->length;
            }
        }
    }

    Array *arr = args[0].as.as_array;
    if (arr->length == 0) {
        runtime_error(ctx, "exec_argv() array must not be empty");
        return val_null();
    }

    // Build argv array for execvp
    char **argv = malloc((arr->length + 1) * sizeof(char*));
    if (!argv) {
        runtime_error(ctx, "exec_argv() memory allocation failed");
        return val_null();
    }

    for (int i = 0; i < arr->length; i++) {
        if (arr->elements[i].type != VAL_STRING) {
            for (int j = 0; j < i; j++) free(argv[j]);
            free(argv);
            runtime_error(ctx, "exec_argv() array element %d must be a string, got %s", i, value_type_name(arr->elements[i].type));
            return val_null();
        }
        String *s = arr->elements[i].as.as_string;
        argv[i] = malloc(s->length + 1);
        if (!argv[i]) {
            for (int j = 0; j < i; j++) free(argv[j]);
            free(argv);
            runtime_error(ctx, "exec_argv() memory allocation failed");
            return val_null();
        }
        memcpy(argv[i], s->data, s->length);
        argv[i][s->length] = '\0';
    }
    argv[arr->length] = NULL;

    // Create pipes for stdout, stderr, and optionally stdin
    int stdout_pipe[2];
    int stderr_pipe[2];
    int stdin_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "exec_argv() pipe creation failed: %s", strerror(errno));
        for (int i = 0; i < arr->length; i++) free(argv[i]);
        free(argv);
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }
    if (stdin_data != NULL && pipe(stdin_pipe) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "exec_argv() stdin pipe creation failed: %s", strerror(errno));
        for (int i = 0; i < arr->length; i++) free(argv[i]);
        free(argv);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
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
        if (stdin_pipe[0] != -1) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    if (pid == 0) {
        // Child process
        if (stdin_pipe[0] != -1) {
            close(stdin_pipe[1]);
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
        }
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
    if (stdin_pipe[0] != -1) close(stdin_pipe[0]);  // Close read end in parent
    close(stdout_pipe[1]);  // Close write end
    close(stderr_pipe[1]);

    // Free argv in parent (child has its own copy after fork)
    for (int i = 0; i < arr->length; i++) free(argv[i]);
    free(argv);

    // Read output from child (both stdout and stderr using poll to avoid deadlock)
    // Also write stdin data if provided.
    char *output_buffer = NULL;
    size_t output_size = 0;
    size_t output_capacity = 4096;
    output_buffer = malloc(output_capacity);

    char *stderr_buffer = NULL;
    size_t stderr_size = 0;
    size_t stderr_capacity = 4096;
    stderr_buffer = malloc(stderr_capacity);

    if (!output_buffer || !stderr_buffer) {
        free(output_buffer);
        free(stderr_buffer);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        if (stdin_pipe[1] != -1) close(stdin_pipe[1]);
        runtime_error(ctx, "exec_argv() memory allocation failed");
        return val_null();
    }

    // fds[0]=stdout_read, fds[1]=stderr_read, fds[2]=stdin_write (optional)
    struct pollfd fds[3];
    fds[0].fd = stdout_pipe[0];
    fds[0].events = POLLIN;
    fds[1].fd = stderr_pipe[0];
    fds[1].events = POLLIN;
    fds[2].fd = stdin_pipe[1];
    fds[2].events = (stdin_pipe[1] != -1) ? POLLOUT : 0;
    int num_poll_fds = (stdin_pipe[1] != -1) ? 3 : 2;

    size_t stdin_written = 0;
    int open_fds = 2;
    char chunk[4096];

    while (open_fds > 0 || fds[2].fd != -1) {
        int ret = poll(fds, num_poll_fds, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Write stdin data if the write end is ready
        if (fds[2].fd != -1 && (fds[2].revents & (POLLOUT | POLLERR | POLLHUP))) {
            if (stdin_written < stdin_len) {
                size_t remaining = stdin_len - stdin_written;
                size_t to_write = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
                ssize_t written = write(stdin_pipe[1], stdin_data + stdin_written, to_write);
                if (written > 0) {
                    stdin_written += (size_t)written;
                }
            }
            if (stdin_written >= stdin_len || (fds[2].revents & (POLLERR | POLLHUP))) {
                close(stdin_pipe[1]);
                stdin_pipe[1] = -1;
                fds[2].fd = -1;
                fds[2].events = 0;
            }
        }

        // Read from stdout if available
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            ssize_t bytes_read = read(stdout_pipe[0], chunk, sizeof(chunk));
            if (bytes_read > 0) {
                while (output_size + (size_t)bytes_read > output_capacity) {
                    if (output_capacity > SIZE_MAX / 2) {
                        free(output_buffer);
                        free(stderr_buffer);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        if (stdin_pipe[1] != -1) close(stdin_pipe[1]);
                        runtime_error(ctx, "exec_argv() output too large");
                        return val_null();
                    }
                    output_capacity *= 2;
                    char *new_buffer = realloc(output_buffer, output_capacity);
                    if (!new_buffer) {
                        free(output_buffer);
                        free(stderr_buffer);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        if (stdin_pipe[1] != -1) close(stdin_pipe[1]);
                        runtime_error(ctx, "exec_argv() memory allocation failed");
                        return val_null();
                    }
                    output_buffer = new_buffer;
                }
                memcpy(output_buffer + output_size, chunk, (size_t)bytes_read);
                output_size += (size_t)bytes_read;
            } else if (bytes_read == 0 || (bytes_read < 0 && errno != EINTR)) {
                fds[0].fd = -1;  // Stop polling this fd
                open_fds--;
            }
        }

        // Read from stderr if available
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t bytes_read = read(stderr_pipe[0], chunk, sizeof(chunk));
            if (bytes_read > 0) {
                while (stderr_size + (size_t)bytes_read > stderr_capacity) {
                    if (stderr_capacity > SIZE_MAX / 2) {
                        free(output_buffer);
                        free(stderr_buffer);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        if (stdin_pipe[1] != -1) close(stdin_pipe[1]);
                        runtime_error(ctx, "exec_argv() stderr output too large");
                        return val_null();
                    }
                    stderr_capacity *= 2;
                    char *new_buffer = realloc(stderr_buffer, stderr_capacity);
                    if (!new_buffer) {
                        free(output_buffer);
                        free(stderr_buffer);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        if (stdin_pipe[1] != -1) close(stdin_pipe[1]);
                        runtime_error(ctx, "exec_argv() memory allocation failed");
                        return val_null();
                    }
                    stderr_buffer = new_buffer;
                }
                memcpy(stderr_buffer + stderr_size, chunk, (size_t)bytes_read);
                stderr_size += (size_t)bytes_read;
            } else if (bytes_read == 0 || (bytes_read < 0 && errno != EINTR)) {
                fds[1].fd = -1;  // Stop polling this fd
                open_fds--;
            }
        }
    }
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    if (stdin_pipe[1] != -1) close(stdin_pipe[1]);

    // Wait for child
    int status;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    // Ensure null termination for stdout
    if (output_size >= output_capacity) {
        char *new_buffer = realloc(output_buffer, output_size + 1);
        if (!new_buffer) {
            free(output_buffer);
            free(stderr_buffer);
            runtime_error(ctx, "exec_argv() memory allocation failed");
            return val_null();
        }
        output_buffer = new_buffer;
        output_capacity = output_size + 1;
    }
    output_buffer[output_size] = '\0';

    // Ensure null termination for stderr
    if (stderr_size >= stderr_capacity) {
        char *new_buffer = realloc(stderr_buffer, stderr_size + 1);
        if (!new_buffer) {
            free(output_buffer);
            free(stderr_buffer);
            runtime_error(ctx, "exec_argv() memory allocation failed");
            return val_null();
        }
        stderr_buffer = new_buffer;
        stderr_capacity = stderr_size + 1;
    }
    stderr_buffer[stderr_size] = '\0';

    // Create result object
    Object *result = object_new(NULL, 3);
    if (!result) {
        free(output_buffer);
        free(stderr_buffer);
        runtime_error(ctx, "exec_argv() memory allocation failed");
        return val_null();
    }
    result->fields[0].name = strdup("output");
    if (!result->fields[0].name) {
        free(output_buffer);
        free(stderr_buffer);
        object_free(result);
        runtime_error(ctx, "exec_argv() memory allocation failed");
        return val_null();
    }
    result->fields[0].value = val_string_take(output_buffer, output_size, output_capacity);
    result->num_fields++;

    result->fields[1].name = strdup("stderr");
    if (!result->fields[1].name) {
        free(stderr_buffer);
        object_free(result);
        runtime_error(ctx, "exec_argv() memory allocation failed");
        return val_null();
    }
    result->fields[1].value = val_string_take(stderr_buffer, stderr_size, stderr_capacity);
    result->num_fields++;

    result->fields[2].name = strdup("exit_code");
    if (!result->fields[2].name) {
        object_free(result);
        runtime_error(ctx, "exec_argv() memory allocation failed");
        return val_null();
    }
    result->fields[2].value = val_i32(exit_code);
    result->num_fields++;

    return val_object(result);
}

Value builtin_getppid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "getppid() expects no arguments, got %d", num_args);
        return val_null();
    }
    return val_i32((int32_t)getppid());
}

Value builtin_getuid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "getuid() expects no arguments, got %d", num_args);
        return val_null();
    }
    return val_i32((int32_t)getuid());
}

Value builtin_geteuid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "geteuid() expects no arguments, got %d", num_args);
        return val_null();
    }
    return val_i32((int32_t)geteuid());
}

Value builtin_getgid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "getgid() expects no arguments, got %d", num_args);
        return val_null();
    }
    return val_i32((int32_t)getgid());
}

Value builtin_getegid(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        runtime_error(ctx, "getegid() expects no arguments, got %d", num_args);
        return val_null();
    }
    return val_i32((int32_t)getegid());
}

Value builtin_kill(Value *args, int num_args, ExecutionContext *ctx) {
    // SANDBOX: Check if signal operations are allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_SIGNALS)) {
        sandbox_error(ctx, "kill() signal operation");
        return val_null();
    }

    if (num_args != 2) {
        runtime_error(ctx, "kill() expects 2 arguments (pid, signal), got %d", num_args);
        return val_null();
    }
    if (!is_integer(args[0])) {
        runtime_error(ctx, "kill() pid must be an integer, got %s", value_type_name(args[0].type));
        return val_null();
    }
    if (!is_integer(args[1])) {
        runtime_error(ctx, "kill() signal must be an integer, got %s", value_type_name(args[1].type));
        return val_null();
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
        runtime_error(ctx, "fork() expects no arguments, got %d", num_args);
        return val_null();
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
        runtime_error(ctx, "wait() expects no arguments, got %d", num_args);
        return val_null();
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
    if (!result) {
        runtime_error(ctx, "wait() memory allocation failed");
        return val_null();
    }
    result->fields[0].name = strdup("pid");
    if (!result->fields[0].name) {
        object_free(result);
        runtime_error(ctx, "wait() memory allocation failed");
        return val_null();
    }
    result->fields[0].value = val_i32((int32_t)pid);
    result->num_fields++;

    result->fields[1].name = strdup("status");
    if (!result->fields[1].name) {
        object_free(result);
        runtime_error(ctx, "wait() memory allocation failed");
        return val_null();
    }
    result->fields[1].value = val_i32(status);
    result->num_fields++;

    return val_object(result);
}

Value builtin_waitpid(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        runtime_error(ctx, "waitpid() expects 1-2 arguments (pid, [options]), got %d", num_args);
        return val_null();
    }
    if (!is_integer(args[0])) {
        runtime_error(ctx, "waitpid() pid must be an integer, got %s", value_type_name(args[0].type));
        return val_null();
    }
    if (num_args == 2 && !is_integer(args[1])) {
        runtime_error(ctx, "waitpid() options must be an integer, got %s", value_type_name(args[1].type));
        return val_null();
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
    if (!result) {
        runtime_error(ctx, "waitpid() memory allocation failed");
        return val_null();
    }
    result->fields[0].name = strdup("pid");
    if (!result->fields[0].name) {
        object_free(result);
        runtime_error(ctx, "waitpid() memory allocation failed");
        return val_null();
    }
    result->fields[0].value = val_i32((int32_t)result_pid);
    result->num_fields++;

    result->fields[1].name = strdup("status");
    if (!result->fields[1].name) {
        object_free(result);
        runtime_error(ctx, "waitpid() memory allocation failed");
        return val_null();
    }
    result->fields[1].value = val_i32(status);
    result->num_fields++;

    return val_object(result);
}

// ========== POSIX_SPAWN PRIMITIVE ==========
//
// posix_spawn(argv, opts?) -> { pid }
//
// Detached spawn primitive backed by posix_spawnp(3). Unlike exec_argv()
// which forks, redirects stdout/stderr through pipes, and waits for the
// child, this primitive returns immediately after the child is created.
// The caller owns the pid and must waitpid() (or arrange SIGCHLD/SA_NOCLDWAIT)
// to reap it.
//
// argv: array of strings; argv[0] is the program (PATH-resolved via
//   posix_spawnp unless it contains '/').
// opts (optional object):
//   env:    array of "KEY=value" strings (default: inherit parent environ)
//   stdin:  i32 fd to dup2 onto STDIN_FILENO  in the child (default: inherit)
//   stdout: i32 fd to dup2 onto STDOUT_FILENO in the child (default: inherit)
//   stderr: i32 fd to dup2 onto STDERR_FILENO in the child (default: inherit)
//   cwd:    string; chdir before exec (requires glibc 2.29+ / macOS 10.15+)
//   setsid: bool; if true, child becomes session leader (detaches from
//           controlling terminal). Requires POSIX_SPAWN_SETSID.
extern char **environ;

// Feature detection for posix_spawn extensions. Hemlock targets modern
// systems, but we still gate so a build on an older libc fails cleanly
// at runtime (when the option is requested) rather than at link time.
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#  if __GLIBC_PREREQ(2, 29)
#    define HML_SPAWN_HAS_CHDIR 1
#  endif
#endif
#if defined(__APPLE__)
#  include <Availability.h>
#  if defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 101500
#    define HML_SPAWN_HAS_CHDIR 1
#  endif
#endif

#ifdef __EMSCRIPTEN__
// WASM has no posix_spawn family. Provide a stub that throws at runtime so
// the interpreter still links — process spawning is meaningless in-browser.
Value builtin_posix_spawn(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args; (void)num_args;
    runtime_error(ctx, "spawn() not supported in WASM build");
    return val_null();
}
#else

static int posix_spawn_lookup_field(Object *obj, const char *name) {
    return obj ? object_lookup_field(obj, name) : -1;
}

Value builtin_posix_spawn(Value *args, int num_args, ExecutionContext *ctx) {
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_PROCESS)) {
        sandbox_error(ctx, "process spawning");
        return val_null();
    }

    if (num_args < 1 || num_args > 2) {
        runtime_error(ctx, "spawn() expects 1-2 arguments (argv, [opts]), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_ARRAY) {
        runtime_error(ctx, "spawn() argv must be an array of strings, got %s", value_type_name(args[0].type));
        return val_null();
    }
    Array *arr = args[0].as.as_array;
    if (arr->length == 0) {
        runtime_error(ctx, "spawn() argv must not be empty");
        return val_null();
    }

    Object *opts = NULL;
    if (num_args == 2 && args[1].type != VAL_NULL) {
        if (args[1].type != VAL_OBJECT) {
            runtime_error(ctx, "spawn() opts must be an object or null, got %s", value_type_name(args[1].type));
            return val_null();
        }
        opts = args[1].as.as_object;
    }

    char **cargv = malloc((arr->length + 1) * sizeof(char*));
    if (!cargv) {
        runtime_error(ctx, "spawn() memory allocation failed");
        return val_null();
    }
    for (int i = 0; i < arr->length; i++) {
        if (arr->elements[i].type != VAL_STRING) {
            for (int j = 0; j < i; j++) free(cargv[j]);
            free(cargv);
            runtime_error(ctx, "spawn() argv[%d] must be a string, got %s", i, value_type_name(arr->elements[i].type));
            return val_null();
        }
        String *s = arr->elements[i].as.as_string;
        cargv[i] = malloc(s->length + 1);
        if (!cargv[i]) {
            for (int j = 0; j < i; j++) free(cargv[j]);
            free(cargv);
            runtime_error(ctx, "spawn() memory allocation failed");
            return val_null();
        }
        memcpy(cargv[i], s->data, s->length);
        cargv[i][s->length] = '\0';
    }
    cargv[arr->length] = NULL;

    char **cenv = NULL;
    int cenv_count = 0;
    int env_idx = posix_spawn_lookup_field(opts, "env");
    if (env_idx >= 0 && opts->fields[env_idx].value.type != VAL_NULL) {
        Value env_val = opts->fields[env_idx].value;
        if (env_val.type != VAL_ARRAY) {
            for (int i = 0; i < arr->length; i++) free(cargv[i]);
            free(cargv);
            runtime_error(ctx, "spawn() opts.env must be an array of strings, got %s", value_type_name(env_val.type));
            return val_null();
        }
        Array *envarr = env_val.as.as_array;
        cenv = malloc((envarr->length + 1) * sizeof(char*));
        if (!cenv) {
            for (int i = 0; i < arr->length; i++) free(cargv[i]);
            free(cargv);
            runtime_error(ctx, "spawn() memory allocation failed");
            return val_null();
        }
        for (int i = 0; i < envarr->length; i++) {
            if (envarr->elements[i].type != VAL_STRING) {
                for (int j = 0; j < i; j++) free(cenv[j]);
                free(cenv);
                for (int j = 0; j < arr->length; j++) free(cargv[j]);
                free(cargv);
                runtime_error(ctx, "spawn() opts.env[%d] must be a string, got %s", i, value_type_name(envarr->elements[i].type));
                return val_null();
            }
            String *s = envarr->elements[i].as.as_string;
            cenv[i] = malloc(s->length + 1);
            if (!cenv[i]) {
                for (int j = 0; j < i; j++) free(cenv[j]);
                free(cenv);
                for (int j = 0; j < arr->length; j++) free(cargv[j]);
                free(cargv);
                runtime_error(ctx, "spawn() memory allocation failed");
                return val_null();
            }
            memcpy(cenv[i], s->data, s->length);
            cenv[i][s->length] = '\0';
            cenv_count = i + 1;
        }
        cenv[envarr->length] = NULL;
    }

    posix_spawn_file_actions_t fa;
    int fa_inited = (posix_spawn_file_actions_init(&fa) == 0);
    posix_spawnattr_t sa;
    int sa_inited = (posix_spawnattr_init(&sa) == 0);
    int attr_flags = 0;

    #define HML_SPAWN_CLEANUP() do { \
        if (cenv) { for (int j = 0; j < cenv_count; j++) free(cenv[j]); free(cenv); } \
        for (int j = 0; j < arr->length; j++) free(cargv[j]); \
        free(cargv); \
        if (fa_inited) posix_spawn_file_actions_destroy(&fa); \
        if (sa_inited) posix_spawnattr_destroy(&sa); \
    } while (0)

    if (fa_inited && opts) {
        const char *names[3] = {"stdin", "stdout", "stderr"};
        int targets[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
        for (int i = 0; i < 3; i++) {
            int fi = object_lookup_field(opts, names[i]);
            if (fi >= 0 && opts->fields[fi].value.type != VAL_NULL) {
                if (!is_integer(opts->fields[fi].value)) {
                    HML_SPAWN_CLEANUP();
                    runtime_error(ctx, "spawn() opts.%s must be an integer fd, got %s", names[i], value_type_name(opts->fields[fi].value.type));
                    return val_null();
                }
                int fd = value_to_int(opts->fields[fi].value);
                posix_spawn_file_actions_adddup2(&fa, fd, targets[i]);
            }
        }
    }

    if (fa_inited && opts) {
        int cwd_idx = object_lookup_field(opts, "cwd");
        if (cwd_idx >= 0 && opts->fields[cwd_idx].value.type != VAL_NULL) {
            Value cwd_val = opts->fields[cwd_idx].value;
            if (cwd_val.type != VAL_STRING) {
                HML_SPAWN_CLEANUP();
                runtime_error(ctx, "spawn() opts.cwd must be a string, got %s", value_type_name(cwd_val.type));
                return val_null();
            }
            #ifdef HML_SPAWN_HAS_CHDIR
            String *cwd_str = cwd_val.as.as_string;
            char *ccwd = malloc(cwd_str->length + 1);
            if (!ccwd) {
                HML_SPAWN_CLEANUP();
                runtime_error(ctx, "spawn() memory allocation failed");
                return val_null();
            }
            memcpy(ccwd, cwd_str->data, cwd_str->length);
            ccwd[cwd_str->length] = '\0';
            int chdir_rc = posix_spawn_file_actions_addchdir_np(&fa, ccwd);
            free(ccwd);
            if (chdir_rc != 0) {
                HML_SPAWN_CLEANUP();
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "spawn() addchdir failed: %s", strerror(chdir_rc));
                ctx->exception_state.exception_value = val_string(error_msg);
                ctx->exception_state.is_throwing = 1;
                return val_null();
            }
            #else
            HML_SPAWN_CLEANUP();
            runtime_error(ctx, "spawn() opts.cwd not supported on this platform (requires glibc 2.29+ or macOS 10.15+)");
            return val_null();
            #endif
        }
    }

    if (sa_inited && opts) {
        int sid_idx = object_lookup_field(opts, "setsid");
        if (sid_idx >= 0 && opts->fields[sid_idx].value.type != VAL_NULL) {
            Value sid_val = opts->fields[sid_idx].value;
            if (sid_val.type != VAL_BOOL) {
                HML_SPAWN_CLEANUP();
                runtime_error(ctx, "spawn() opts.setsid must be a bool, got %s", value_type_name(sid_val.type));
                return val_null();
            }
            if (sid_val.as.as_bool) {
                #ifdef POSIX_SPAWN_SETSID
                attr_flags |= POSIX_SPAWN_SETSID;
                #else
                HML_SPAWN_CLEANUP();
                runtime_error(ctx, "spawn() opts.setsid not supported on this platform (POSIX_SPAWN_SETSID unavailable)");
                return val_null();
                #endif
            }
        }
        if (attr_flags) {
            posix_spawnattr_setflags(&sa, attr_flags);
        }
    }

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, cargv[0],
                          fa_inited ? &fa : NULL,
                          (sa_inited && attr_flags) ? &sa : NULL,
                          cargv, cenv ? cenv : environ);

    if (fa_inited) posix_spawn_file_actions_destroy(&fa);
    if (sa_inited) posix_spawnattr_destroy(&sa);
    for (int i = 0; i < arr->length; i++) free(cargv[i]);
    free(cargv);
    if (cenv) {
        for (int i = 0; i < cenv_count; i++) free(cenv[i]);
        free(cenv);
    }
    #undef HML_SPAWN_CLEANUP

    if (rc != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "spawn() failed: %s", strerror(rc));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    Object *result = object_new(NULL, 1);
    if (!result) {
        runtime_error(ctx, "spawn() memory allocation failed");
        return val_null();
    }
    result->fields[0].name = strdup("pid");
    if (!result->fields[0].name) {
        object_free(result);
        runtime_error(ctx, "spawn() memory allocation failed");
        return val_null();
    }
    result->fields[0].value = val_i32((int32_t)pid);
    result->num_fields++;

    return val_object(result);
}
#endif // !__EMSCRIPTEN__

Value builtin_abort(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;

    // SANDBOX: Check if signal operations are allowed (abort sends SIGABRT)
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_SIGNALS)) {
        sandbox_error(ctx, "abort() operation");
        return val_null();
    }

    if (num_args != 0) {
        runtime_error(ctx, "abort() expects no arguments, got %d", num_args);
        return val_null();
    }
    abort();
    return val_null();  // Never reached
}

// ========== PIPE OPERATIONS ==========

Value builtin_pipe(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;

    // SANDBOX: Check if process operations are allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_PROCESS)) {
        sandbox_error(ctx, "pipe creation");
        return val_null();
    }

    if (num_args != 0) {
        runtime_error(ctx, "pipe() expects no arguments, got %d", num_args);
        return val_null();
    }

    int fds[2];
    if (pipe(fds) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "pipe() failed: %s", strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    Object *result = object_new(NULL, 2);
    if (!result) {
        close(fds[0]);
        close(fds[1]);
        runtime_error(ctx, "pipe() memory allocation failed");
        return val_null();
    }
    result->fields[0].name = strdup("read_fd");
    result->fields[0].value = val_i32(fds[0]);
    result->num_fields++;
    result->fields[1].name = strdup("write_fd");
    result->fields[1].value = val_i32(fds[1]);
    result->num_fields++;

    return val_object(result);
}

Value builtin_close_fd(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "close_fd() expects 1 argument, got %d", num_args);
        return val_null();
    }

    int fd = value_to_int(args[0]);
    if (close(fd) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "close_fd(%d) failed: %s", fd, strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
    }
    return val_null();
}

Value builtin_read_fd(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "read_fd() expects 2 arguments (fd, size), got %d", num_args);
        return val_null();
    }

    int fd = value_to_int(args[0]);
    int buf_size = value_to_int(args[1]);

    if (buf_size <= 0) {
        runtime_error(ctx, "read_fd() size must be positive, got %d", buf_size);
        return val_null();
    }

    char *buf = malloc((size_t)buf_size + 1);
    if (!buf) {
        runtime_error(ctx, "read_fd() memory allocation failed");
        return val_null();
    }

    ssize_t bytes_read = read(fd, buf, (size_t)buf_size);
    if (bytes_read < 0) {
        free(buf);
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return val_null();
        }
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "read_fd(%d) failed: %s", fd, strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    if (bytes_read == 0) {
        free(buf);
        return val_null();
    }

    buf[bytes_read] = '\0';
    Value result = val_string(buf);
    free(buf);
    return result;
}

Value builtin_write_fd(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "write_fd() expects 2 arguments (fd, data), got %d", num_args);
        return val_null();
    }

    int fd = value_to_int(args[0]);

    if (args[1].type != VAL_STRING || !args[1].as.as_string) {
        runtime_error(ctx, "write_fd() data must be a string");
        return val_null();
    }

    String *str = args[1].as.as_string;
    ssize_t written = write(fd, str->data, (size_t)str->length);
    if (written < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "write_fd(%d) failed: %s", fd, strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_i32((int32_t)written);
}

Value builtin_dup2(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "dup2() expects 2 arguments (oldfd, newfd), got %d", num_args);
        return val_null();
    }

    // SANDBOX: Check if process operations are allowed
    if (sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_PROCESS)) {
        sandbox_error(ctx, "dup2() operation");
        return val_null();
    }

    int old_fd = value_to_int(args[0]);
    int new_fd = value_to_int(args[1]);

    int result = dup2(old_fd, new_fd);
    if (result < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "dup2(%d, %d) failed: %s", old_fd, new_fd, strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    return val_i32(result);
}
