/*
 * Hemlock Runtime Library - Process Builtins
 *
 * This file implements process-related builtin functions:
 * - Command execution (exec, exec_argv, exec_with_args)
 * - Process control (fork, wait, waitpid, kill)
 * - Environment operations (getenv, setenv, unsetenv)
 * - Signal handling
 *
 * WASM: This entire file is excluded when building for Emscripten.
 * Stub implementations are provided in wasm_shim.c.
 */

#ifndef __EMSCRIPTEN__

// _GNU_SOURCE exposes posix_spawn_file_actions_addchdir_np (glibc 2.29+) and
// POSIX_SPAWN_SETSID (glibc 2.26+). Must be defined before any system header
// inclusion. Guarded so it does not collide with -D_GNU_SOURCE on the build
// command line.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "builtins_internal.h"

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

// posix_spawn(argv, opts?) -> { pid }
// Detached spawn primitive backed by posix_spawnp(3).
// argv: array of strings (argv[0] PATH-resolved unless it contains '/').
// opts (optional object):
//   env:    array of "KEY=value" strings (default: inherit parent environ)
//   stdin:  i32 fd to dup2 onto STDIN_FILENO  in child (default: inherit)
//   stdout: i32 fd to dup2 onto STDOUT_FILENO in child (default: inherit)
//   stderr: i32 fd to dup2 onto STDERR_FILENO in child (default: inherit)
//   cwd:    string; chdir before exec (requires glibc 2.29+ / macOS 10.15+)
//   setsid: bool; child becomes session leader (requires POSIX_SPAWN_SETSID)
extern char **environ;

// Feature gates: hemlock targets modern systems but we still gate so a
// build on an older libc fails cleanly at runtime when the option is used.
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

HmlValue hml_posix_spawn(HmlValue argv_val, HmlValue opts_val) {
    if (hml_sandbox_check(HML_SANDBOX_RESTRICT_PROCESS)) {
        hml_sandbox_error("process spawning");
    }

    if (argv_val.type != HML_VAL_ARRAY || !argv_val.as.as_array) {
        hml_runtime_error("spawn() argv must be an array of strings");
    }
    HmlArray *arr = argv_val.as.as_array;
    if (arr->length == 0) {
        hml_runtime_error("spawn() argv must not be empty");
    }

    int has_opts = (opts_val.type == HML_VAL_OBJECT && opts_val.as.as_object);

    char **cargv = malloc((arr->length + 1) * sizeof(char*));
    if (!cargv) {
        hml_runtime_error("spawn() memory allocation failed");
    }
    for (int64_t i = 0; i < arr->length; i++) {
        HmlValue elem = arr->elements[i];
        if (elem.type != HML_VAL_STRING || !elem.as.as_string) {
            for (int64_t j = 0; j < i; j++) free(cargv[j]);
            free(cargv);
            hml_runtime_error("spawn() argv elements must be strings");
        }
        HmlString *s = elem.as.as_string;
        cargv[i] = malloc(s->length + 1);
        if (!cargv[i]) {
            for (int64_t j = 0; j < i; j++) free(cargv[j]);
            free(cargv);
            hml_runtime_error("spawn() memory allocation failed");
        }
        memcpy(cargv[i], s->data, s->length);
        cargv[i][s->length] = '\0';
    }
    cargv[arr->length] = NULL;

    char **cenv = NULL;
    int64_t cenv_count = 0;
    if (has_opts && hml_object_has_field(opts_val, "env")) {
        HmlValue env_val = hml_object_get_field(opts_val, "env");
        if (env_val.type != HML_VAL_NULL) {
            if (env_val.type != HML_VAL_ARRAY || !env_val.as.as_array) {
                for (int64_t i = 0; i < arr->length; i++) free(cargv[i]);
                free(cargv);
                hml_runtime_error("spawn() opts.env must be an array of strings");
            }
            HmlArray *envarr = env_val.as.as_array;
            cenv = malloc((envarr->length + 1) * sizeof(char*));
            if (!cenv) {
                for (int64_t i = 0; i < arr->length; i++) free(cargv[i]);
                free(cargv);
                hml_runtime_error("spawn() memory allocation failed");
            }
            for (int64_t i = 0; i < envarr->length; i++) {
                HmlValue ev = envarr->elements[i];
                if (ev.type != HML_VAL_STRING || !ev.as.as_string) {
                    for (int64_t j = 0; j < i; j++) free(cenv[j]);
                    free(cenv);
                    for (int64_t j = 0; j < arr->length; j++) free(cargv[j]);
                    free(cargv);
                    hml_runtime_error("spawn() opts.env elements must be strings");
                }
                HmlString *s = ev.as.as_string;
                cenv[i] = malloc(s->length + 1);
                if (!cenv[i]) {
                    for (int64_t j = 0; j < i; j++) free(cenv[j]);
                    free(cenv);
                    for (int64_t j = 0; j < arr->length; j++) free(cargv[j]);
                    free(cargv);
                    hml_runtime_error("spawn() memory allocation failed");
                }
                memcpy(cenv[i], s->data, s->length);
                cenv[i][s->length] = '\0';
                cenv_count = i + 1;
            }
            cenv[envarr->length] = NULL;
        }
    }

    posix_spawn_file_actions_t fa;
    int fa_inited = (posix_spawn_file_actions_init(&fa) == 0);
    posix_spawnattr_t sa;
    int sa_inited = (posix_spawnattr_init(&sa) == 0);
    int attr_flags = 0;

    #define HML_SPAWN_CLEANUP() do { \
        if (cenv) { for (int64_t j = 0; j < cenv_count; j++) free(cenv[j]); free(cenv); } \
        for (int64_t j = 0; j < arr->length; j++) free(cargv[j]); \
        free(cargv); \
        if (fa_inited) posix_spawn_file_actions_destroy(&fa); \
        if (sa_inited) posix_spawnattr_destroy(&sa); \
    } while (0)

    if (fa_inited && has_opts) {
        const char *names[3] = {"stdin", "stdout", "stderr"};
        int targets[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
        for (int i = 0; i < 3; i++) {
            if (hml_object_has_field(opts_val, names[i])) {
                HmlValue fdv = hml_object_get_field(opts_val, names[i]);
                if (fdv.type != HML_VAL_NULL) {
                    posix_spawn_file_actions_adddup2(&fa, hml_to_i32(fdv), targets[i]);
                }
            }
        }
    }

    if (fa_inited && has_opts && hml_object_has_field(opts_val, "cwd")) {
        HmlValue cwd_val = hml_object_get_field(opts_val, "cwd");
        if (cwd_val.type != HML_VAL_NULL) {
            if (cwd_val.type != HML_VAL_STRING || !cwd_val.as.as_string) {
                HML_SPAWN_CLEANUP();
                hml_runtime_error("spawn() opts.cwd must be a string");
            }
            #ifdef HML_SPAWN_HAS_CHDIR
            int chdir_rc = posix_spawn_file_actions_addchdir_np(&fa, cwd_val.as.as_string->data);
            if (chdir_rc != 0) {
                HML_SPAWN_CLEANUP();
                hml_runtime_error("spawn() addchdir failed: %s", strerror(chdir_rc));
            }
            #else
            HML_SPAWN_CLEANUP();
            hml_runtime_error("spawn() opts.cwd not supported on this platform (requires glibc 2.29+ or macOS 10.15+)");
            #endif
        }
    }

    if (sa_inited && has_opts && hml_object_has_field(opts_val, "setsid")) {
        HmlValue sid_val = hml_object_get_field(opts_val, "setsid");
        if (sid_val.type != HML_VAL_NULL) {
            if (sid_val.type != HML_VAL_BOOL) {
                HML_SPAWN_CLEANUP();
                hml_runtime_error("spawn() opts.setsid must be a bool");
            }
            if (sid_val.as.as_bool) {
                #ifdef POSIX_SPAWN_SETSID
                attr_flags |= POSIX_SPAWN_SETSID;
                #else
                HML_SPAWN_CLEANUP();
                hml_runtime_error("spawn() opts.setsid not supported on this platform (POSIX_SPAWN_SETSID unavailable)");
                #endif
            }
        }
    }
    if (sa_inited && attr_flags) {
        posix_spawnattr_setflags(&sa, attr_flags);
    }

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, cargv[0],
                          fa_inited ? &fa : NULL,
                          (sa_inited && attr_flags) ? &sa : NULL,
                          cargv, cenv ? cenv : environ);

    if (fa_inited) posix_spawn_file_actions_destroy(&fa);
    if (sa_inited) posix_spawnattr_destroy(&sa);
    for (int64_t i = 0; i < arr->length; i++) free(cargv[i]);
    free(cargv);
    if (cenv) {
        for (int64_t i = 0; i < cenv_count; i++) free(cenv[i]);
        free(cenv);
    }
    #undef HML_SPAWN_CLEANUP

    if (rc != 0) {
        hml_runtime_error("spawn() failed: %s", strerror(rc));
    }

    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "pid", hml_val_i32((int32_t)pid));
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

// posix_spawn(argv, opts?) wrapper. opts defaults to null.
HmlValue hml_builtin_posix_spawn(HmlClosureEnv *env, HmlValue argv_val, HmlValue opts_val) {
    (void)env;
    return hml_posix_spawn(argv_val, opts_val);
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

// ========== PIPE OPERATIONS ==========

HmlValue hml_pipe(void) {
    if (hml_sandbox_check(HML_SANDBOX_RESTRICT_PROCESS)) {
        hml_sandbox_error("pipe creation");
    }

    int fds[2];
    if (pipe(fds) != 0) {
        hml_runtime_error("pipe() failed: %s", strerror(errno));
    }

    HmlValue result = hml_val_object();
    hml_object_set_field(result, "read_fd", hml_val_i32(fds[0]));
    hml_object_set_field(result, "write_fd", hml_val_i32(fds[1]));
    return result;
}

HmlValue hml_close_fd(HmlValue fd) {
    int file_fd = hml_to_i32(fd);
    if (close(file_fd) != 0) {
        hml_runtime_error("close_fd(%d) failed: %s", file_fd, strerror(errno));
    }
    return hml_val_null();
}

HmlValue hml_read_fd(HmlValue fd, HmlValue size) {
    int file_fd = hml_to_i32(fd);
    int buf_size = hml_to_i32(size);

    if (buf_size <= 0) {
        hml_runtime_error("read_fd() size must be positive, got %d", buf_size);
    }

    char *buf = malloc((size_t)buf_size + 1);
    if (!buf) {
        hml_runtime_error("read_fd() memory allocation failed");
    }

    ssize_t bytes_read = read(file_fd, buf, (size_t)buf_size);
    if (bytes_read < 0) {
        free(buf);
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return hml_val_null();
        }
        hml_runtime_error("read_fd(%d) failed: %s", file_fd, strerror(errno));
    }

    if (bytes_read == 0) {
        free(buf);
        return hml_val_null();
    }

    buf[bytes_read] = '\0';
    HmlValue result = hml_val_string(buf);
    free(buf);
    return result;
}

HmlValue hml_write_fd(HmlValue fd, HmlValue data) {
    int file_fd = hml_to_i32(fd);

    if (data.type != HML_VAL_STRING || !data.as.as_string) {
        hml_runtime_error("write_fd() data must be a string");
    }

    HmlString *str = data.as.as_string;
    ssize_t written = write(file_fd, str->data, (size_t)str->length);
    if (written < 0) {
        hml_runtime_error("write_fd(%d) failed: %s", file_fd, strerror(errno));
    }

    return hml_val_i32((int32_t)written);
}

HmlValue hml_dup2(HmlValue oldfd, HmlValue newfd) {
    int old_fd = hml_to_i32(oldfd);
    int new_fd = hml_to_i32(newfd);

    int result = dup2(old_fd, new_fd);
    if (result < 0) {
        hml_runtime_error("dup2(%d, %d) failed: %s", old_fd, new_fd, strerror(errno));
    }

    return hml_val_i32(result);
}

// Pipe builtin wrappers
HmlValue hml_builtin_pipe(HmlClosureEnv *env) {
    (void)env;
    return hml_pipe();
}

HmlValue hml_builtin_close_fd(HmlClosureEnv *env, HmlValue fd) {
    (void)env;
    return hml_close_fd(fd);
}

HmlValue hml_builtin_read_fd(HmlClosureEnv *env, HmlValue fd, HmlValue size) {
    (void)env;
    return hml_read_fd(fd, size);
}

HmlValue hml_builtin_write_fd(HmlClosureEnv *env, HmlValue fd, HmlValue data) {
    (void)env;
    return hml_write_fd(fd, data);
}

HmlValue hml_builtin_dup2(HmlClosureEnv *env, HmlValue oldfd, HmlValue newfd) {
    (void)env;
    return hml_dup2(oldfd, newfd);
}

#endif // !__EMSCRIPTEN__
