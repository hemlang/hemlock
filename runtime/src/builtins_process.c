/*
 * Hemlock Runtime Library - Process Builtins
 *
 * This file implements process-related builtin functions:
 * - Command execution (exec, exec_argv, exec_with_args)
 * - Process control (fork, wait, waitpid, kill)
 * - Environment operations (getenv, setenv, unsetenv)
 * - Signal handling
 */

#include "builtins_internal.h"

// ========== WINDOWS COMPATIBILITY ==========
#ifdef HML_WINDOWS
    // Windows exit status macros
    #ifndef WIFEXITED
    #define WIFEXITED(status) (1)
    #endif
    #ifndef WEXITSTATUS
    #define WEXITSTATUS(status) (status)
    #endif

    // Windows pipe compatibility
    #define pipe(fds) _pipe(fds, 4096, _O_BINARY)

    // Windows process compatibility stubs
    #define waitpid(pid, status, options) (-1)
    #define wait(status) (-1)
    #define kill(pid, sig) (-1)
    #define fork() (-1)

    // Windows environment functions
    static inline int hml_setenv(const char *name, const char *value, int overwrite) {
        if (!overwrite) {
            char *existing = getenv(name);
            if (existing != NULL) return 0;
        }
        return _putenv_s(name, value);
    }
    static inline int hml_unsetenv(const char *name) {
        return _putenv_s(name, "");
    }
    #define setenv hml_setenv
    #define unsetenv hml_unsetenv

    // Windows signal compatibility - uses basic signal() in hml_signal()
    // sigaction() is replaced with signal() via #ifdef in the function
#endif

// ========== COMMAND EXECUTION ==========

// SECURITY WARNING: exec() uses popen() which passes commands through a shell.
// This is vulnerable to command injection if the command string contains untrusted input.
// For safe command execution, use exec_argv() instead which bypasses the shell.
HmlValue hml_exec(HmlValue command) {
    // SANDBOX: Check if process spawning is allowed
    if (hml_sandbox_check(HML_SANDBOX_RESTRICT_PROCESS)) {
        hml_sandbox_error("command execution");
    }

    if (command.type != HML_VAL_STRING || !command.as.as_string) {
        hml_runtime_error("exec() argument must be a string");
    }

    HmlString *cmd_str = command.as.as_string;

    // SECURITY: Warn about potentially dangerous shell metacharacters
    const char *dangerous_chars = ";|&$`\\\"'<>(){}[]!#";
    for (int64_t i = 0; i < cmd_str->length; i++) {
        for (const char *dc = dangerous_chars; *dc; dc++) {
            if (cmd_str->data[i] == *dc) {
                fprintf(stderr, "Warning: exec() command contains shell metacharacter '%c'. "
                        "Consider using exec_argv() for safer command execution.\n", *dc);
                goto done_warning;
            }
        }
    }
done_warning:
    ;  // Empty statement required after label (C99/C11)

    char *ccmd = malloc(cmd_str->length + 1);
    if (!ccmd) {
        hml_runtime_error("exec() memory allocation failed");
    }
    memcpy(ccmd, cmd_str->data, cmd_str->length);
    ccmd[cmd_str->length] = '\0';

    // Open pipe to read command output (uses shell - vulnerable to injection)
    FILE *pipe = popen(ccmd, "r");
    if (!pipe) {
        fprintf(stderr, "Runtime error: Failed to execute command '%s': %s\n", ccmd, strerror(errno));
        free(ccmd);
        exit(1);
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

    // Create result object with output, stderr, and exit_code
    // Note: popen() can't capture stderr separately, so we return empty string for it
    HmlValue result = hml_val_object();
    hml_object_set_field(result, "output", hml_val_string(output_buffer));
    hml_object_set_field(result, "stderr", hml_val_string(""));
    hml_object_set_field(result, "exit_code", hml_val_i32(exit_code));
    free(output_buffer);

    return result;
}

// exec_argv() - Safe command execution without shell interpretation
// Takes an array of strings: [program, arg1, arg2, ...]
// Uses fork/execvp directly, preventing shell injection attacks
HmlValue hml_exec_argv(HmlValue args_array) {
    // SANDBOX: Check if process spawning is allowed
    if (hml_sandbox_check(HML_SANDBOX_RESTRICT_PROCESS)) {
        hml_sandbox_error("command execution");
    }

    if (args_array.type != HML_VAL_ARRAY || !args_array.as.as_array) {
        hml_runtime_error("exec_argv() argument must be an array of strings");
    }

    HmlArray *arr = args_array.as.as_array;
    if (arr->length == 0) {
        hml_runtime_error("exec_argv() array must not be empty");
    }

    // Build argv array for execvp
    char **argv = malloc((arr->length + 1) * sizeof(char*));
    if (!argv) {
        hml_runtime_error("exec_argv() memory allocation failed");
    }

    for (int64_t i = 0; i < arr->length; i++) {
        HmlValue elem = arr->elements[i];
        if (elem.type != HML_VAL_STRING || !elem.as.as_string) {
            for (int64_t j = 0; j < i; j++) free(argv[j]);
            free(argv);
            hml_runtime_error("exec_argv() array elements must be strings");
        }
        HmlString *s = elem.as.as_string;
        argv[i] = malloc(s->length + 1);
        if (!argv[i]) {
            for (int64_t j = 0; j < i; j++) free(argv[j]);
            free(argv);
            hml_runtime_error("exec_argv() memory allocation failed");
        }
        memcpy(argv[i], s->data, s->length);
        argv[i][s->length] = '\0';
    }
    argv[arr->length] = NULL;

    // Create pipes for stdout and stderr
    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        for (int64_t i = 0; i < arr->length; i++) free(argv[i]);
        free(argv);
        hml_runtime_error("exec_argv() pipe creation failed: %s", strerror(errno));
    }

    pid_t pid = fork();
    if (pid < 0) {
        for (int64_t i = 0; i < arr->length; i++) free(argv[i]);
        free(argv);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        hml_runtime_error("exec_argv() fork failed: %s", strerror(errno));
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
    for (int64_t i = 0; i < arr->length; i++) free(argv[i]);
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
        hml_runtime_error("exec_argv() memory allocation failed");
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
                        hml_runtime_error("exec_argv() output too large");
                    }
                    output_capacity *= 2;
                    char *new_buffer = realloc(output_buffer, output_capacity);
                    if (!new_buffer) {
                        free(output_buffer);
                        free(stderr_buffer);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        hml_runtime_error("exec_argv() memory allocation failed");
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
                        hml_runtime_error("exec_argv() stderr too large");
                    }
                    stderr_capacity *= 2;
                    char *new_buffer = realloc(stderr_buffer, stderr_capacity);
                    if (!new_buffer) {
                        free(output_buffer);
                        free(stderr_buffer);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        hml_runtime_error("exec_argv() memory allocation failed");
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
            hml_runtime_error("exec_argv() memory allocation failed");
        }
        output_buffer = new_buffer;
    }
    output_buffer[output_size] = '\0';

    // Ensure null termination for stderr
    if (stderr_size >= stderr_capacity) {
        char *new_buffer = realloc(stderr_buffer, stderr_size + 1);
        if (!new_buffer) {
            free(output_buffer);
            free(stderr_buffer);
            hml_runtime_error("exec_argv() memory allocation failed");
        }
        stderr_buffer = new_buffer;
    }
    stderr_buffer[stderr_size] = '\0';

    // Create result object
    HmlValue result = hml_val_object();
    hml_object_set_field(result, "output", hml_val_string(output_buffer));
    hml_object_set_field(result, "stderr", hml_val_string(stderr_buffer));
    hml_object_set_field(result, "exit_code", hml_val_i32(exit_code));
    free(output_buffer);
    free(stderr_buffer);

    return result;
}

// exec_with_args() - Safe command execution with separate command and args
// Takes command string and array of string arguments
// Uses fork/execvp directly, preventing shell injection attacks
HmlValue hml_exec_with_args(HmlValue command, HmlValue args_array) {
    if (command.type != HML_VAL_STRING || !command.as.as_string) {
        hml_runtime_error("exec() first argument must be a string");
    }
    if (args_array.type != HML_VAL_ARRAY || !args_array.as.as_array) {
        hml_runtime_error("exec() second argument must be an array of strings");
    }

    HmlString *cmd_str = command.as.as_string;
    HmlArray *arr = args_array.as.as_array;

    // Build argv array: [command, ...args, NULL]
    char **argv = malloc((arr->length + 2) * sizeof(char*));
    if (!argv) {
        hml_runtime_error("exec() memory allocation failed");
    }

    // First element is the command
    argv[0] = malloc(cmd_str->length + 1);
    if (!argv[0]) {
        free(argv);
        hml_runtime_error("exec() memory allocation failed");
    }
    memcpy(argv[0], cmd_str->data, cmd_str->length);
    argv[0][cmd_str->length] = '\0';

    // Copy args from array
    for (int64_t i = 0; i < arr->length; i++) {
        HmlValue elem = arr->elements[i];
        if (elem.type != HML_VAL_STRING || !elem.as.as_string) {
            for (int64_t j = 0; j <= i; j++) free(argv[j]);
            free(argv);
            hml_runtime_error("exec() args array elements must be strings");
        }
        HmlString *s = elem.as.as_string;
        argv[i + 1] = malloc(s->length + 1);
        if (!argv[i + 1]) {
            for (int64_t j = 0; j <= i; j++) free(argv[j]);
            free(argv);
            hml_runtime_error("exec() memory allocation failed");
        }
        memcpy(argv[i + 1], s->data, s->length);
        argv[i + 1][s->length] = '\0';
    }
    argv[arr->length + 1] = NULL;

    // Create pipes for stdout and stderr
    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        for (int64_t i = 0; i <= arr->length; i++) free(argv[i]);
        free(argv);
        hml_runtime_error("exec() pipe creation failed: %s", strerror(errno));
    }

    pid_t pid = fork();
    if (pid < 0) {
        for (int64_t i = 0; i <= arr->length; i++) free(argv[i]);
        free(argv);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        hml_runtime_error("exec() fork failed: %s", strerror(errno));
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
    for (int64_t i = 0; i <= arr->length; i++) free(argv[i]);
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
        hml_runtime_error("exec() memory allocation failed");
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
                        hml_runtime_error("exec() output too large");
                    }
                    output_capacity *= 2;
                    char *new_buffer = realloc(output_buffer, output_capacity);
                    if (!new_buffer) {
                        free(output_buffer);
                        free(stderr_buffer);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        hml_runtime_error("exec() memory allocation failed");
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
                        hml_runtime_error("exec() stderr too large");
                    }
                    stderr_capacity *= 2;
                    char *new_buffer = realloc(stderr_buffer, stderr_capacity);
                    if (!new_buffer) {
                        free(output_buffer);
                        free(stderr_buffer);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        hml_runtime_error("exec() memory allocation failed");
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
            hml_runtime_error("exec() memory allocation failed");
        }
        output_buffer = new_buffer;
    }
    output_buffer[output_size] = '\0';

    // Ensure null termination for stderr
    if (stderr_size >= stderr_capacity) {
        char *new_buffer = realloc(stderr_buffer, stderr_size + 1);
        if (!new_buffer) {
            free(output_buffer);
            free(stderr_buffer);
            hml_runtime_error("exec() memory allocation failed");
        }
        stderr_buffer = new_buffer;
    }
    stderr_buffer[stderr_size] = '\0';

    // Create result object
    HmlValue result = hml_val_object();
    hml_object_set_field(result, "output", hml_val_string(output_buffer));
    hml_object_set_field(result, "stderr", hml_val_string(stderr_buffer));
    hml_object_set_field(result, "exit_code", hml_val_i32(exit_code));
    free(output_buffer);
    free(stderr_buffer);

    return result;
}

// Math operations moved to builtins_math.c

// Time builtin wrappers moved to builtins_time.c

// Env builtin wrappers
HmlValue hml_builtin_getenv(HmlClosureEnv *env, HmlValue name) {
    (void)env;
    return hml_getenv(name);
}

HmlValue hml_builtin_setenv(HmlClosureEnv *env, HmlValue name, HmlValue value) {
    (void)env;
    hml_setenv(name, value);
    return hml_val_null();
}

HmlValue hml_builtin_exit(HmlClosureEnv *env, HmlValue code) {
    (void)env;
    hml_exit(code);
    return hml_val_null();  // Never reached
}

HmlValue hml_builtin_get_pid(HmlClosureEnv *env) {
    (void)env;
    return hml_get_pid();
}

HmlValue hml_builtin_exec(HmlClosureEnv *env, HmlValue command) {
    (void)env;
    return hml_exec(command);
}

HmlValue hml_builtin_exec_with_args(HmlClosureEnv *env, HmlValue command, HmlValue args_array) {
    (void)env;
    return hml_exec_with_args(command, args_array);
}

HmlValue hml_builtin_exec_argv(HmlClosureEnv *env, HmlValue args_array) {
    (void)env;
    return hml_exec_argv(args_array);
}

// Process ID builtins
HmlValue hml_getppid(void) {
    return hml_val_i32((int32_t)getppid());
}

HmlValue hml_getuid(void) {
    return hml_val_i32((int32_t)getuid());
}

HmlValue hml_geteuid(void) {
    return hml_val_i32((int32_t)geteuid());
}

HmlValue hml_getgid(void) {
    return hml_val_i32((int32_t)getgid());
}

HmlValue hml_getegid(void) {
    return hml_val_i32((int32_t)getegid());
}

HmlValue hml_unsetenv(HmlValue name) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        return hml_val_null();
    }
    unsetenv(name.as.as_string->data);
    return hml_val_null();
}

HmlValue hml_kill(HmlValue pid, HmlValue sig) {
    int p = hml_to_i32(pid);
    int s = hml_to_i32(sig);
    int result = kill(p, s);
    return hml_val_i32(result);
}

HmlValue hml_fork(void) {
    // SANDBOX: Check if process spawning is allowed
    if (hml_sandbox_check(HML_SANDBOX_RESTRICT_PROCESS)) {
        hml_sandbox_error("process forking");
    }

    pid_t pid = fork();
    return hml_val_i32((int32_t)pid);
}

HmlValue hml_wait(void) {
    int status;
    pid_t pid = wait(&status);
    // Return object with pid and status
    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "pid", hml_val_i32((int32_t)pid));
    hml_object_set_field(obj, "status", hml_val_i32(status));
    return obj;
}

HmlValue hml_waitpid(HmlValue pid, HmlValue options) {
    int status;
    pid_t result = waitpid(hml_to_i32(pid), &status, hml_to_i32(options));
    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "pid", hml_val_i32((int32_t)result));
    hml_object_set_field(obj, "status", hml_val_i32(status));
    return obj;
}

void hml_abort(void) {
    abort();
}

// Process builtin wrappers
HmlValue hml_builtin_getppid(HmlClosureEnv *env) {
    (void)env;
    return hml_getppid();
}

HmlValue hml_builtin_getuid(HmlClosureEnv *env) {
    (void)env;
    return hml_getuid();
}

HmlValue hml_builtin_geteuid(HmlClosureEnv *env) {
    (void)env;
    return hml_geteuid();
}

HmlValue hml_builtin_getgid(HmlClosureEnv *env) {
    (void)env;
    return hml_getgid();
}

HmlValue hml_builtin_getegid(HmlClosureEnv *env) {
    (void)env;
    return hml_getegid();
}

HmlValue hml_builtin_unsetenv(HmlClosureEnv *env, HmlValue name) {
    (void)env;
    return hml_unsetenv(name);
}

HmlValue hml_builtin_kill(HmlClosureEnv *env, HmlValue pid, HmlValue sig) {
    (void)env;
    return hml_kill(pid, sig);
}

HmlValue hml_builtin_fork(HmlClosureEnv *env) {
    (void)env;
    return hml_fork();
}

HmlValue hml_builtin_wait(HmlClosureEnv *env) {
    (void)env;
    return hml_wait();
}

HmlValue hml_builtin_waitpid(HmlClosureEnv *env, HmlValue pid, HmlValue options) {
    (void)env;
    return hml_waitpid(pid, options);
}

HmlValue hml_builtin_abort(HmlClosureEnv *env) {
    (void)env;
    hml_abort();
    return hml_val_null();  // Never reached
}

// Time and datetime operations moved to builtins_time.c

// ========== ENVIRONMENT OPERATIONS ==========

HmlValue hml_getenv(HmlValue name) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        return hml_val_null();
    }
    char *value = getenv(name.as.as_string->data);
    if (!value) {
        return hml_val_null();
    }
    return hml_val_string(value);
}

void hml_setenv(HmlValue name, HmlValue value) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        return;
    }
    if (value.type != HML_VAL_STRING || !value.as.as_string) {
        return;
    }
    setenv(name.as.as_string->data, value.as.as_string->data, 1);
}

void hml_exit(HmlValue code) {
    exit(hml_to_i32(code));
}

HmlValue hml_get_pid(void) {
    return hml_val_i32((int32_t)getpid());
}

// ========== SIGNAL HANDLING ==========

#include <signal.h>
#include <errno.h>

// Global signal handler table (signal number -> Hemlock function value)
static HmlValue g_signal_handlers[HML_MAX_SIGNAL];
static int g_signal_handlers_initialized = 0;

static void init_signal_handlers(void) {
    if (g_signal_handlers_initialized) return;
    for (int i = 0; i < HML_MAX_SIGNAL; i++) {
        g_signal_handlers[i] = hml_val_null();
    }
    g_signal_handlers_initialized = 1;
}

// C signal handler that invokes Hemlock function values
static void hml_c_signal_handler(int signum) {
    if (signum < 0 || signum >= HML_MAX_SIGNAL) return;

    HmlValue handler = g_signal_handlers[signum];
    if (handler.type == HML_VAL_NULL) return;

    if (handler.type == HML_VAL_FUNCTION) {
        // Call the function with signal number as argument
        HmlValue sig_arg = hml_val_i32(signum);
        hml_call_function(handler, &sig_arg, 1);
    }
}

HmlValue hml_signal(HmlValue signum, HmlValue handler) {
    init_signal_handlers();

    // Validate signum
    if (signum.type != HML_VAL_I32) {
        hml_runtime_error("signal() signum must be an integer");
    }

    int sig = signum.as.as_i32;
    if (sig < 0 || sig >= HML_MAX_SIGNAL) {
        hml_runtime_error("signal() signum %d out of range [0, %d)", sig, HML_MAX_SIGNAL);
    }

    // Validate handler is function or null
    if (handler.type != HML_VAL_NULL && handler.type != HML_VAL_FUNCTION) {
        hml_runtime_error("signal() handler must be a function or null");
    }

    // Get previous handler for return
    HmlValue prev = g_signal_handlers[sig];
    hml_retain(&prev);

    // Release old handler and store new one
    hml_release(&g_signal_handlers[sig]);
    g_signal_handlers[sig] = handler;
    hml_retain(&g_signal_handlers[sig]);

    // Install or reset C signal handler
#ifdef HML_WINDOWS
    // Windows uses basic signal() function with limited signal support
    if (handler.type != HML_VAL_NULL) {
        if (signal(sig, hml_c_signal_handler) == SIG_ERR) {
            hml_runtime_error("signal() failed for signal %d: %s", sig, strerror(errno));
        }
    } else {
        if (signal(sig, SIG_DFL) == SIG_ERR) {
            hml_runtime_error("signal() failed to reset signal %d: %s", sig, strerror(errno));
        }
    }
#else
    struct sigaction sa;
    if (handler.type != HML_VAL_NULL) {
        sa.sa_handler = hml_c_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        if (sigaction(sig, &sa, NULL) != 0) {
            hml_runtime_error("signal() failed for signal %d: %s", sig, strerror(errno));
        }
    } else {
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (sigaction(sig, &sa, NULL) != 0) {
            hml_runtime_error("signal() failed to reset signal %d: %s", sig, strerror(errno));
        }
    }
#endif

    return prev;
}

HmlValue hml_raise(HmlValue signum) {
    if (signum.type != HML_VAL_I32) {
        hml_runtime_error("raise() signum must be an integer");
    }

    int sig = signum.as.as_i32;
    if (sig < 0 || sig >= HML_MAX_SIGNAL) {
        hml_runtime_error("raise() signum %d out of range [0, %d)", sig, HML_MAX_SIGNAL);
    }

    if (raise(sig) != 0) {
        hml_runtime_error("raise() failed for signal %d: %s", sig, strerror(errno));
    }

    return hml_val_null();
}

