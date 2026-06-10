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

#endif // _WIN32

#endif // HEMLOCK_COMPAT_H
