// Lightweight declarations for POSIX functions missing from MinGW.
//
// Unlike hemlock_platform.h this header does NOT pull in windows.h, so it
// is safe to include from frontend/tool sources (winnt.h declares a
// TokenType enumerator that collides with the lexer's TokenType).
// Implementations live in src/shared/platform_win32.c (also compiled into
// the runtime library).
#ifndef HEMLOCK_COMPAT_H
#define HEMLOCK_COMPAT_H

#ifdef _WIN32

#include <stddef.h>

// Resolves to an absolute path with forward slashes; requires the path to
// exist (like POSIX realpath). resolved == NULL mallocs the result.
char *realpath(const char *path, char *resolved);

char *strndup(const char *s, size_t n);

// GetModuleFileName wrapper (the /proc/self/exe equivalent).
// Returns 1 on success; buf gets forward slashes.
int hml_get_executable_path(char *buf, size_t size);

#include <stdio.h>
typedef long long hml_ssize_t_compat;
hml_ssize_t_compat getline(char **lineptr, size_t *n, FILE *stream);

int mkstemps(char *template_path, int suffixlen);

// tmpfile() replacement: MSVCRT's tmpfile() creates in the drive root,
// which fails for non-admin users. This one uses %TEMP% and is deleted
// on close.
FILE *hml_win_tmpfile(void);

// CNG-backed hashing (bcrypt.dll) for the hash builtins. alg is one of
// "sha1", "sha256", "sha512", "md5"; writes the raw digest to out and
// returns its length, or -1 (unknown alg, out_cap too small, CNG error).
#define HML_WIN32_DIGEST_MAX 64  // SHA-512
int hml_win32_hash(const char *alg, const void *data, size_t len,
                   unsigned char *out, size_t out_cap);

// CNG-backed CSPRNG (BCryptGenRandom with the system-preferred RNG).
// Fills out with len random bytes; returns 0 on success, -1 on failure.
int hml_win32_random(void *out, size_t len);

// Process execution (CreateProcess + pipes) for the exec builtins.
// hml_win32_build_cmdline: argv -> command line with CommandLineToArgvW
// quoting rules. hml_win32_shell_cmdline: raw command -> cmd.exe /S /C
// invocation. Both return malloc'd strings (NULL on alloc failure).
char *hml_win32_build_cmdline(const char *const *argv, int argc);
char *hml_win32_shell_cmdline(const char *command);

// Runs cmdline, optionally feeding stdin_data and capturing stderr
// (otherwise the child inherits this process's stderr). On success
// returns 0 and hands over malloc'd NUL-terminated out/err buffers plus
// the exit code; on failure returns -1 with a message in errmsg.
int hml_win32_run_capture(const char *cmdline,
                          const char *stdin_data, size_t stdin_len,
                          int capture_stderr,
                          char **out_buf, size_t *out_len,
                          char **err_buf, size_t *err_len,
                          int *exit_code,
                          char *errmsg, size_t errmsg_cap);

#endif // _WIN32

#endif // HEMLOCK_COMPAT_H
