/*
 * Hemlock Compiler - Type Error Reporting
 *
 * Error and warning reporting, error collection for LSP integration.
 */

#include "type_check_internal.h"

// ========== ERROR REPORTING ==========

// Helper to calculate column for a line in source
int calc_column_for_line(const char *source, int target_line) {
    if (!source || target_line <= 0) return 0;

    // For now, return 0 (start of line) - column info would need token position
    // This is a limitation that can be improved by passing token info
    return 0;
}

// Helper to add error to collection
void add_collected_error(TypeCheckContext *ctx, int line, const char *message, int is_warning) {
    TypeCheckError *err = calloc(1, sizeof(TypeCheckError));
    err->line = line;
    err->column = calc_column_for_line(ctx->source, line);
    err->end_column = err->column + 1;  // Default to 1 character width
    err->message = strdup(message);
    err->is_warning = is_warning;
    err->next = NULL;

    // Append to linked list (O(1) with tail pointer)
    if (ctx->errors_tail) {
        ctx->errors_tail->next = err;
        ctx->errors_tail = err;
    } else {
        ctx->errors = err;
        ctx->errors_tail = err;
    }
}

void type_error(TypeCheckContext *ctx, int line, const char *fmt, ...) {
    ctx->error_count++;

    // Format the message
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (ctx->collect_errors) {
        // Collect error for LSP
        add_collected_error(ctx, line, message, 0);
    } else {
        // Print to stderr (original behavior)
        fprintf(stderr, "%s:%d: error: %s\n",
                ctx->filename ? ctx->filename : "<unknown>", line, message);
    }
}

void type_warning(TypeCheckContext *ctx, int line, const char *fmt, ...) {
    ctx->warning_count++;

    // Format the message
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (ctx->collect_errors) {
        // Collect warning for LSP
        add_collected_error(ctx, line, message, 1);
    } else {
        // Print to stderr (original behavior)
        fprintf(stderr, "%s:%d: warning: %s\n",
                ctx->filename ? ctx->filename : "<unknown>", line, message);
    }
}

// ========== ERROR COLLECTION API ==========

void type_check_enable_collection(TypeCheckContext *ctx, const char *source) {
    ctx->collect_errors = 1;
    ctx->source = source;
    ctx->errors = NULL;
    ctx->errors_tail = NULL;
}

TypeCheckError* type_check_get_errors(TypeCheckContext *ctx) {
    return ctx->errors;
}

void type_check_free_errors(TypeCheckContext *ctx) {
    TypeCheckError *err = ctx->errors;
    while (err) {
        TypeCheckError *next = err->next;
        free(err->message);
        free(err);
        err = next;
    }
    ctx->errors = NULL;
    ctx->errors_tail = NULL;
}

int type_check_calc_column(const char *source, int target_line, const char *hint) {
    (void)hint;  // Reserved for future use
    return calc_column_for_line(source, target_line);
}
