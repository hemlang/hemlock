/*
 * Hemlock Process Management Compatibility Layer
 *
 * Provides cross-platform process management:
 * - Process creation (fork/exec on POSIX, CreateProcess on Windows)
 * - Process I/O (pipes)
 * - Process control (kill, wait)
 * - Environment variables
 * - User/group IDs (limited on Windows)
 */

#ifndef HML_COMPAT_PROCESS_H
#define HML_COMPAT_PROCESS_H

#include "platform.h"

#ifdef HML_WINDOWS

/* ========== Windows Implementation ========== */

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>

/* Process ID type */
typedef DWORD hml_pid_t;

/* Process handle for spawn operations */
typedef struct {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    HANDLE hStdoutRead;
    HANDLE hStderrRead;
} hml_process_t;

/* Get current process ID */
HML_INLINE hml_pid_t hml_getpid(void) {
    return GetCurrentProcessId();
}

/* Get parent process ID (complex on Windows) */
HML_INLINE hml_pid_t hml_getppid(void) {
    /* Windows doesn't have a simple getppid() - return 0 for now */
    /* A full implementation would use NtQueryInformationProcess or CreateToolhelp32Snapshot */
    return 0;
}

/* User/Group IDs - Windows doesn't have these concepts */
/* Return placeholder values for compatibility */
HML_INLINE int hml_getuid(void) { return 0; }
HML_INLINE int hml_geteuid(void) { return 0; }
HML_INLINE int hml_getgid(void) { return 0; }
HML_INLINE int hml_getegid(void) { return 0; }

/* Environment variables */
HML_INLINE int hml_setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite) {
        char buf[1];
        if (GetEnvironmentVariableA(name, buf, 1) > 0) {
            return 0;  /* Variable exists and we don't overwrite */
        }
    }
    return SetEnvironmentVariableA(name, value) ? 0 : -1;
}

HML_INLINE int hml_unsetenv(const char *name) {
    return SetEnvironmentVariableA(name, NULL) ? 0 : -1;
}

/* popen/pclose compatibility using CreateProcess */
typedef struct {
    HANDLE hProcess;
    FILE *stream;
    HANDLE hPipe;
} hml_popen_t;

HML_INLINE FILE *hml_popen(const char *command, const char *mode) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return NULL;
    }

    /* Determine if we're reading or writing */
    int reading = (mode[0] == 'r');

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    if (reading) {
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
    } else {
        si.hStdInput = hReadPipe;
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        SetHandleInformation(hWritePipe, HANDLE_FLAG_INHERIT, 0);
    }

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    /* Build command line: cmd /c "command" */
    size_t cmd_len = strlen(command) + 32;
    char *cmd_line = (char *)malloc(cmd_len);
    if (!cmd_line) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return NULL;
    }
    snprintf(cmd_line, cmd_len, "cmd.exe /c \"%s\"", command);

    BOOL success = CreateProcessA(
        NULL,           /* Application name */
        cmd_line,       /* Command line */
        NULL,           /* Process security attributes */
        NULL,           /* Thread security attributes */
        TRUE,           /* Inherit handles */
        0,              /* Creation flags */
        NULL,           /* Environment */
        NULL,           /* Current directory */
        &si,            /* Startup info */
        &pi             /* Process information */
    );

    free(cmd_line);

    if (!success) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return NULL;
    }

    /* Close the end of the pipe we don't need */
    if (reading) {
        CloseHandle(hWritePipe);
    } else {
        CloseHandle(hReadPipe);
    }

    /* Convert handle to FILE* */
    int fd = _open_osfhandle((intptr_t)(reading ? hReadPipe : hWritePipe),
                              reading ? _O_RDONLY : _O_WRONLY);
    if (fd == -1) {
        CloseHandle(reading ? hReadPipe : hWritePipe);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return NULL;
    }

    FILE *fp = _fdopen(fd, mode);
    if (!fp) {
        _close(fd);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return NULL;
    }

    /* Store process handle for pclose - we use a global map for simplicity */
    /* In production, you'd want a proper handle-to-process mapping */
    CloseHandle(pi.hThread);

    /* Store the process handle in thread-local storage for pclose */
    /* This is a simplified implementation - a hash map would be better */
    /* Note: We store for potential future use but don't use it in pclose currently */
    static HML_THREAD_LOCAL HANDLE last_popen_process HML_UNUSED = NULL;
    last_popen_process = pi.hProcess;
    (void)last_popen_process;  /* Suppress unused warning */

    return fp;
}

/* pclose - note: simplified implementation */
HML_INLINE int hml_pclose(FILE *stream) {
    fclose(stream);
    /* In a full implementation, we'd look up the process handle and wait for it */
    /* For now, just return 0 */
    return 0;
}

/* Spawn a process with captured stdout/stderr */
HML_INLINE int hml_spawn_capture(const char *program, char *const argv[],
                                   hml_process_t *proc) {
    (void)program;  /* Windows builds command line from argv */
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    /* Create pipes for stdout and stderr */
    HANDLE hStdoutRead, hStdoutWrite;
    HANDLE hStderrRead, hStderrWrite;

    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        return -1;
    }
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    /* Build command line from argv */
    size_t cmd_len = 0;
    for (int i = 0; argv[i] != NULL; i++) {
        cmd_len += strlen(argv[i]) + 3;  /* Space + quotes */
    }
    cmd_len += 1;  /* Null terminator */

    char *cmd_line = (char *)malloc(cmd_len);
    if (!cmd_line) {
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        CloseHandle(hStderrRead);
        CloseHandle(hStderrWrite);
        return -1;
    }

    cmd_line[0] = '\0';
    for (int i = 0; argv[i] != NULL; i++) {
        if (i > 0) strcat(cmd_line, " ");
        /* Quote arguments that contain spaces */
        if (strchr(argv[i], ' ') != NULL) {
            strcat(cmd_line, "\"");
            strcat(cmd_line, argv[i]);
            strcat(cmd_line, "\"");
        } else {
            strcat(cmd_line, argv[i]);
        }
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessA(
        NULL,           /* Application name */
        cmd_line,       /* Command line */
        NULL,           /* Process security attributes */
        NULL,           /* Thread security attributes */
        TRUE,           /* Inherit handles */
        0,              /* Creation flags */
        NULL,           /* Environment */
        NULL,           /* Current directory */
        &si,            /* Startup info */
        &pi             /* Process information */
    );

    free(cmd_line);

    /* Close write ends of pipes in parent */
    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!success) {
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        return -1;
    }

    proc->hProcess = pi.hProcess;
    proc->hThread = pi.hThread;
    proc->dwProcessId = pi.dwProcessId;
    proc->hStdoutRead = hStdoutRead;
    proc->hStderrRead = hStderrRead;

    return 0;
}

/* Wait for a spawned process and get exit code */
HML_INLINE int hml_process_wait(hml_process_t *proc, int *exit_code) {
    WaitForSingleObject(proc->hProcess, INFINITE);

    DWORD code;
    if (!GetExitCodeProcess(proc->hProcess, &code)) {
        *exit_code = -1;
        return -1;
    }

    *exit_code = (int)code;

    CloseHandle(proc->hProcess);
    CloseHandle(proc->hThread);
    if (proc->hStdoutRead) CloseHandle(proc->hStdoutRead);
    if (proc->hStderrRead) CloseHandle(proc->hStderrRead);

    return 0;
}

/* Read from process stdout */
HML_INLINE ssize_t hml_process_read_stdout(hml_process_t *proc, void *buf, size_t count) {
    DWORD bytesRead;
    if (!ReadFile(proc->hStdoutRead, buf, (DWORD)count, &bytesRead, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
            return 0;  /* EOF */
        }
        return -1;
    }
    return (ssize_t)bytesRead;
}

/* Kill a process */
HML_INLINE int hml_kill(hml_pid_t pid, int sig) {
    (void)sig;  /* Windows doesn't have POSIX signals */
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess == NULL) {
        return -1;
    }
    BOOL result = TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
    return result ? 0 : -1;
}

/* Wait status macros */
#define HML_WIFEXITED(status) (1)
#define HML_WEXITSTATUS(status) (status)
#define HML_WIFSIGNALED(status) (0)
#define HML_WTERMSIG(status) (0)

/* Pipe creation */
HML_INLINE int hml_pipe(int pipefd[2]) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return -1;
    }

    pipefd[0] = _open_osfhandle((intptr_t)hRead, _O_RDONLY);
    pipefd[1] = _open_osfhandle((intptr_t)hWrite, _O_WRONLY);

    if (pipefd[0] == -1 || pipefd[1] == -1) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }

    return 0;
}

/* dup2 */
HML_INLINE int hml_dup2(int oldfd, int newfd) {
    return _dup2(oldfd, newfd);
}

/* Standard file descriptors */
#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#else /* POSIX Implementation */

/* ========== POSIX Implementation ========== */

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

/* Process ID type */
typedef pid_t hml_pid_t;

/* Process handle for spawn operations */
typedef struct {
    pid_t pid;
    int stdout_fd;
    int stderr_fd;
} hml_process_t;

/* Get process IDs */
HML_INLINE hml_pid_t hml_getpid(void) {
    return getpid();
}

HML_INLINE hml_pid_t hml_getppid(void) {
    return getppid();
}

/* User/Group IDs */
HML_INLINE int hml_getuid(void) { return (int)getuid(); }
HML_INLINE int hml_geteuid(void) { return (int)geteuid(); }
HML_INLINE int hml_getgid(void) { return (int)getgid(); }
HML_INLINE int hml_getegid(void) { return (int)getegid(); }

/* Environment variables */
HML_INLINE int hml_setenv(const char *name, const char *value, int overwrite) {
    return setenv(name, value, overwrite);
}

HML_INLINE int hml_unsetenv(const char *name) {
    return unsetenv(name);
}

/* popen/pclose */
HML_INLINE FILE *hml_popen(const char *command, const char *mode) {
    return popen(command, mode);
}

HML_INLINE int hml_pclose(FILE *stream) {
    return pclose(stream);
}

/* Spawn a process with captured stdout/stderr */
HML_INLINE int hml_spawn_capture(const char *program, char *const argv[],
                                   hml_process_t *proc) {
    int stdout_pipe[2];
    int stderr_pipe[2];

    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execvp(program, argv);
        _exit(127);  /* execvp failed */
    }

    /* Parent process */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    proc->pid = pid;
    proc->stdout_fd = stdout_pipe[0];
    proc->stderr_fd = stderr_pipe[0];

    return 0;
}

/* Wait for a spawned process and get exit code */
HML_INLINE int hml_process_wait(hml_process_t *proc, int *exit_code) {
    int status;
    if (waitpid(proc->pid, &status, 0) < 0) {
        *exit_code = -1;
        return -1;
    }

    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else {
        *exit_code = -1;
    }

    if (proc->stdout_fd >= 0) close(proc->stdout_fd);
    if (proc->stderr_fd >= 0) close(proc->stderr_fd);

    return 0;
}

/* Read from process stdout */
HML_INLINE ssize_t hml_process_read_stdout(hml_process_t *proc, void *buf, size_t count) {
    return read(proc->stdout_fd, buf, count);
}

/* Kill a process */
HML_INLINE int hml_kill(hml_pid_t pid, int sig) {
    return kill(pid, sig);
}

/* Wait status macros */
#define HML_WIFEXITED(status) WIFEXITED(status)
#define HML_WEXITSTATUS(status) WEXITSTATUS(status)
#define HML_WIFSIGNALED(status) WIFSIGNALED(status)
#define HML_WTERMSIG(status) WTERMSIG(status)

/* Pipe creation */
HML_INLINE int hml_pipe(int pipefd[2]) {
    return pipe(pipefd);
}

/* dup2 */
HML_INLINE int hml_dup2(int oldfd, int newfd) {
    return dup2(oldfd, newfd);
}

#endif /* HML_WINDOWS */

#endif /* HML_COMPAT_PROCESS_H */
