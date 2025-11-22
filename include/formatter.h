#ifndef HEMLOCK_FORMATTER_H
#define HEMLOCK_FORMATTER_H

#include "ast.h"
#include <stdio.h>

// Format a list of statements to a string
char* format_statements(Stmt **statements, int count);

// Format a single statement to a string
char* format_statement(Stmt *stmt, int indent);

// Format an expression to a string
char* format_expression(Expr *expr);

// Format a type annotation to a string
char* format_type(Type *type);

// Check if two files have the same content (for --check mode)
int files_are_equal(const char *file1, const char *file2);

#endif // HEMLOCK_FORMATTER_H
