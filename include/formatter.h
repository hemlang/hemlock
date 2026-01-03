#ifndef HEMLOCK_FORMATTER_H
#define HEMLOCK_FORMATTER_H

#include "ast.h"

// Formatter configuration (opinionated, not configurable by user)
#define FMT_INDENT_CHAR '\t'
#define FMT_INDENT_SIZE 1
#define FMT_MAX_LINE_WIDTH 100

// Format a file in place
// Returns 0 on success, non-zero on error
int format_file(const char *path);

// Check if a file is already formatted
// Returns 0 if formatted, 1 if not formatted, -1 on error
int format_check(const char *path);

// Format source code and return the formatted string
// Caller must free the returned string
char *format_source(const char *source);

#endif // HEMLOCK_FORMATTER_H
