#include "internal.h"
#include <unistd.h>

// ========== CURRENT SOURCE FILE TRACKING ==========

static char *current_source_file = NULL;
static char *current_source_code = NULL;

void set_current_source_file(const char *file) {
    if (current_source_file) {
        free(current_source_file);
        current_source_file = NULL;
    }
    if (file) {
        current_source_file = strdup(file);
    }
}

const char* get_current_source_file(void) {
    return current_source_file;
}

void set_current_source_code(const char *source) {
    if (current_source_code) {
        free(current_source_code);
        current_source_code = NULL;
    }
    if (source) {
        current_source_code = strdup(source);
    }
}

const char* get_current_source_code(void) {
    return current_source_code;
}

// ========== SOURCE CONTEXT HELPERS ==========

// Get a specific line from the source code
const char* get_source_line(const char *source, int line_num, int *line_length) {
    if (!source || line_num <= 0) {
        *line_length = 0;
        return NULL;
    }

    const char *p = source;
    int current_line = 1;

    // Find the start of the requested line
    while (*p && current_line < line_num) {
        if (*p == '\n') {
            current_line++;
        }
        p++;
    }

    if (!*p && current_line < line_num) {
        *line_length = 0;
        return NULL;
    }

    // Find the end of the line
    const char *line_start = p;
    while (*p && *p != '\n') {
        p++;
    }

    *line_length = (int)(p - line_start);
    return line_start;
}

// Print source context with caret pointing to error position
void print_source_context(FILE *out, const char *source, int line, int column) {
    if (!source || line <= 0) return;

    int line_length;
    const char *line_text = get_source_line(source, line, &line_length);
    if (!line_text || line_length == 0) return;

    // Print line number prefix
    fprintf(out, "  %4d | ", line);

    // Print the source line (truncate if too long)
    int max_display = 120;
    int display_len = line_length < max_display ? line_length : max_display;
    fprintf(out, "%.*s", display_len, line_text);
    if (line_length > max_display) {
        fprintf(out, "...");
    }
    fprintf(out, "\n");

    // Print the caret pointing to the column
    fprintf(out, "       | ");
    if (column > 0) {
        // Account for tabs and print spaces to align caret
        for (int i = 1; i < column && i <= display_len; i++) {
            if (line_text[i-1] == '\t') {
                fprintf(out, "\t");
            } else {
                fprintf(out, " ");
            }
        }
        fprintf(out, "^\n");
    } else {
        fprintf(out, "^\n");
    }
}

// Format an error message with full location and context
char* format_error_with_context(const char *file, int line, int column, const char *message) {
    // Handle null message
    if (!message) message = "Unknown error";

    // Calculate buffer size needed
    // Format: [file:line:column] message\n  line | source\n       | ^
    size_t base_size = 1024;
    char *buffer = malloc(base_size);
    if (!buffer) return strdup(message);

    int offset = 0;

    // Format location prefix
    if (file && line > 0 && column > 0) {
        offset += snprintf(buffer + offset, base_size - offset, "[%s:%d:%d] ", file, line, column);
    } else if (file && line > 0) {
        offset += snprintf(buffer + offset, base_size - offset, "[%s:%d] ", file, line);
    } else if (line > 0 && column > 0) {
        offset += snprintf(buffer + offset, base_size - offset, "[line %d:%d] ", line, column);
    } else if (line > 0) {
        offset += snprintf(buffer + offset, base_size - offset, "[line %d] ", line);
    }

    // Add the error message
    offset += snprintf(buffer + offset, base_size - offset, "%s", message);

    // Add source context if available
    const char *source = get_current_source_code();
    if (source && line > 0) {
        int line_length;
        const char *line_text = get_source_line(source, line, &line_length);
        if (line_text && line_length > 0) {
            // Truncate line if too long
            int max_display = 80;
            int display_len = line_length < max_display ? line_length : max_display;

            // Realloc if needed
            size_t needed = offset + display_len + 100;
            if (needed > base_size) {
                char *new_buf = realloc(buffer, needed);
                if (new_buf) {
                    buffer = new_buf;
                    base_size = needed;
                }
            }

            offset += snprintf(buffer + offset, base_size - offset, "\n  %4d | %.*s",
                             line, display_len, line_text);
            if (line_length > max_display) {
                offset += snprintf(buffer + offset, base_size - offset, "...");
            }
            offset += snprintf(buffer + offset, base_size - offset, "\n       | ");

            // Add caret (with bounds checking)
            if (column > 0 && (size_t)offset + (size_t)column + 2 < base_size) {
                for (int i = 1; i < column && i <= display_len; i++) {
                    if (line_text[i-1] == '\t') {
                        buffer[offset++] = '\t';
                    } else {
                        buffer[offset++] = ' ';
                    }
                }
            }
            if ((size_t)offset + 2 < base_size) {
                buffer[offset++] = '^';
                buffer[offset] = '\0';
            }
        }
    }

    return buffer;
}

// ========== EXECUTION CONTEXT IMPLEMENTATION ==========

ExecutionContext* exec_context_new(void) {
    ExecutionContext *ctx = malloc(sizeof(ExecutionContext));
    if (!ctx) {
        fprintf(stderr, "Fatal error: Failed to allocate execution context\n");
        exit(1);
    }
    ctx->return_state.is_returning = 0;
    ctx->return_state.return_value = val_null();
    ctx->loop_state.is_breaking = 0;
    ctx->loop_state.is_continuing = 0;
    ctx->exception_state.is_throwing = 0;
    ctx->exception_state.exception_value = val_null();
    ctx->max_stack_depth = DEFAULT_MAX_STACK_DEPTH;
    ctx->sandbox_flags = HML_SANDBOX_RESTRICT_NONE;  // No restrictions by default
    ctx->sandbox_root = NULL;                         // No root restriction
    ctx->profiler = NULL;                             // Profiler disabled by default
    ctx->current_source_file = NULL;                  // Set before builtin calls
    ctx->current_line = 0;
    call_stack_init(&ctx->call_stack);
    defer_stack_init(&ctx->defer_stack);
    return ctx;
}

void exec_context_free(ExecutionContext *ctx) {
    if (ctx) {
        call_stack_free(&ctx->call_stack);
        defer_stack_free(&ctx->defer_stack);
        if (ctx->sandbox_root) {
            free(ctx->sandbox_root);
        }
        free(ctx);
    }
}

// ========== CALL STACK IMPLEMENTATION ==========

void call_stack_init(CallStack *stack) {
    stack->capacity = 64;
    stack->count = 0;
    stack->frames = malloc(sizeof(CallFrame) * stack->capacity);
    if (!stack->frames) {
        fprintf(stderr, "Fatal error: Failed to initialize call stack\n");
        exit(1);
    }
}

void call_stack_push(CallStack *stack, const char *function_name) {
    call_stack_push_full(stack, function_name, NULL, 0);
}

// Push with line number (uses current source file)
void call_stack_push_line(CallStack *stack, const char *function_name, int line) {
    call_stack_push_full(stack, function_name, get_current_source_file(), line);
}

// Push with full info (without column)
void call_stack_push_full(CallStack *stack, const char *function_name, const char *source_file, int line) {
    call_stack_push_full_loc(stack, function_name, source_file, line, 0);
}

// Push with full info including column
// OPTIMIZATION: Store const pointers instead of strdup'ing - the strings are from AST and live long enough
void call_stack_push_full_loc(CallStack *stack, const char *function_name, const char *source_file, int line, int column) {
    if (stack->capacity == 0) {
        call_stack_init(stack);
    }

    if (stack->count >= stack->capacity) {
        stack->capacity *= 2;
        CallFrame *new_frames = realloc(stack->frames, sizeof(CallFrame) * stack->capacity);
        if (!new_frames) {
            fprintf(stderr, "Fatal error: Failed to grow call stack\n");
            exit(1);
        }
        stack->frames = new_frames;
    }

    // Store pointers directly - no need to strdup since AST strings outlive the call
    stack->frames[stack->count].function_name = (char*)function_name;
    stack->frames[stack->count].source_file = (char*)source_file;
    stack->frames[stack->count].line = line;
    stack->frames[stack->count].column = column;
    stack->count++;
}

void call_stack_pop(CallStack *stack) {
    if (stack->count > 0) {
        stack->count--;
        // No need to free - we're storing const pointers now
    }
}

void call_stack_print(CallStack *stack) {
    if (stack->count == 0) {
        return;
    }

    fprintf(stderr, "\nStack trace (most recent call first):\n");
    for (int i = stack->count - 1; i >= 0; i--) {
        CallFrame *frame = &stack->frames[i];

        // Print frame number for easier reference
        int frame_num = stack->count - 1 - i;

        if (frame->source_file && frame->line > 0) {
            // Full info: file:line:column
            if (frame->column > 0) {
                fprintf(stderr, "  #%d  %s (%s:%d:%d)\n",
                        frame_num,
                        frame->function_name,
                        frame->source_file,
                        frame->line,
                        frame->column);
            } else {
                fprintf(stderr, "  #%d  %s (%s:%d)\n",
                        frame_num,
                        frame->function_name,
                        frame->source_file,
                        frame->line);
            }
        } else if (frame->line > 0) {
            // Line only (with optional column)
            if (frame->column > 0) {
                fprintf(stderr, "  #%d  %s (line %d:%d)\n",
                        frame_num,
                        frame->function_name,
                        frame->line,
                        frame->column);
            } else {
                fprintf(stderr, "  #%d  %s (line %d)\n",
                        frame_num,
                        frame->function_name,
                        frame->line);
            }
        } else if (frame->source_file) {
            // File only
            fprintf(stderr, "  #%d  %s (%s)\n",
                    frame_num,
                    frame->function_name,
                    frame->source_file);
        } else {
            // No location info
            fprintf(stderr, "  #%d  %s\n",
                    frame_num,
                    frame->function_name);
        }
    }
}

// Enhanced stack trace with source code snippets
void call_stack_print_with_source(CallStack *stack, const char *source) {
    if (stack->count == 0) {
        return;
    }

    fprintf(stderr, "\nStack trace (most recent call first):\n");
    for (int i = stack->count - 1; i >= 0; i--) {
        CallFrame *frame = &stack->frames[i];
        int frame_num = stack->count - 1 - i;

        // Print frame header
        fprintf(stderr, "  #%d  ", frame_num);
        fprintf(stderr, "%s", frame->function_name);

        if (frame->source_file && frame->line > 0) {
            if (frame->column > 0) {
                fprintf(stderr, " (%s:%d:%d)", frame->source_file, frame->line, frame->column);
            } else {
                fprintf(stderr, " (%s:%d)", frame->source_file, frame->line);
            }
        } else if (frame->line > 0) {
            if (frame->column > 0) {
                fprintf(stderr, " (line %d:%d)", frame->line, frame->column);
            } else {
                fprintf(stderr, " (line %d)", frame->line);
            }
        } else if (frame->source_file) {
            fprintf(stderr, " (%s)", frame->source_file);
        }
        fprintf(stderr, "\n");

        // Show source context for the top frame only (to avoid clutter)
        if (frame_num == 0 && source && frame->line > 0) {
            int line_length;
            const char *line_text = get_source_line(source, frame->line, &line_length);
            if (line_text && line_length > 0) {
                // Truncate if too long
                int max_display = 80;
                int display_len = line_length < max_display ? line_length : max_display;

                fprintf(stderr, "       %4d | %.*s", frame->line, display_len, line_text);
                if (line_length > max_display) {
                    fprintf(stderr, "...");
                }
                fprintf(stderr, "\n");

                // Print caret
                if (frame->column > 0) {
                    fprintf(stderr, "            | ");
                    for (int j = 1; j < frame->column && j <= display_len; j++) {
                        if (line_text[j-1] == '\t') {
                            fprintf(stderr, "\t");
                        } else {
                            fprintf(stderr, " ");
                        }
                    }
                    fprintf(stderr, "^\n");
                }
            }
        }
    }
}

void call_stack_free(CallStack *stack) {
    // No need to free individual strings - they're const pointers to AST data
    free(stack->frames);
    stack->frames = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

// ========== DEFER STACK ==========

void defer_stack_init(DeferStack *stack) {
    stack->capacity = 8;
    stack->count = 0;
    stack->calls = malloc(sizeof(Expr*) * stack->capacity);
    stack->envs = malloc(sizeof(Environment*) * stack->capacity);
    if (!stack->calls || !stack->envs) {
        fprintf(stderr, "Fatal error: Failed to initialize defer stack\n");
        exit(1);
    }
}

void defer_stack_push(DeferStack *stack, Expr *call, Environment *env) {
    if (stack->capacity == 0) {
        defer_stack_init(stack);
    }

    if (stack->count >= stack->capacity) {
        stack->capacity *= 2;
        Expr **new_calls = realloc(stack->calls, sizeof(Expr*) * stack->capacity);
        Environment **new_envs = realloc(stack->envs, sizeof(Environment*) * stack->capacity);
        if (!new_calls || !new_envs) {
            fprintf(stderr, "Fatal error: Failed to grow defer stack\n");
            exit(1);
        }
        stack->calls = new_calls;
        stack->envs = new_envs;
    }

    // Clone the expression and retain the environment
    stack->calls[stack->count] = expr_clone(call);
    stack->envs[stack->count] = env;
    env_retain(env);  // Increment reference count
    stack->count++;
}

void defer_stack_execute(DeferStack *stack, ExecutionContext *ctx) {
    // Execute deferred calls in LIFO order (last defer executes first)
    for (int i = stack->count - 1; i >= 0; i--) {
        // Save current exception state
        int was_throwing = ctx->exception_state.is_throwing;
        Value saved_exception = ctx->exception_state.exception_value;

        // Retain saved exception to prevent it from being freed during defer execution
        VALUE_RETAIN(saved_exception);

        // Temporarily clear exception state to allow defer to run
        ctx->exception_state.is_throwing = 0;

        // Execute the deferred call
        eval_expr(stack->calls[i], stack->envs[i], ctx);

        // If defer itself threw, propagate that exception (overrides previous)
        // Otherwise, restore the saved exception state
        if (!ctx->exception_state.is_throwing) {
            ctx->exception_state.is_throwing = was_throwing;
            ctx->exception_state.exception_value = saved_exception;
            // Release our extra reference since we're restoring the value
            VALUE_RELEASE(saved_exception);
        } else {
            // Defer threw a new exception - release saved exception since it won't be used
            VALUE_RELEASE(saved_exception);
        }

        // Clean up the deferred expression and release environment
        expr_free(stack->calls[i]);
        env_release(stack->envs[i]);
    }

    // Clear the stack
    stack->count = 0;
}

void defer_stack_free(DeferStack *stack) {
    // Free any remaining deferred calls (shouldn't happen in normal execution)
    for (int i = 0; i < stack->count; i++) {
        expr_free(stack->calls[i]);
        env_release(stack->envs[i]);
    }
    free(stack->calls);
    free(stack->envs);
    stack->calls = NULL;
    stack->envs = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

// Runtime error with stack trace
// Automatically includes location info from ctx->current_line if available
void runtime_error(ExecutionContext *ctx, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Set exception state for catchable errors
    if (ctx) {

        // If we have location info in context, use it to format a better error
        if (ctx->current_line > 0) {
            const char *file = ctx->current_source_file ? ctx->current_source_file : get_current_source_file();
            char *formatted = format_error_with_context(file, ctx->current_line, 0, buffer);
            ctx->exception_state.exception_value = val_string(formatted);
            free(formatted);
        } else {
            ctx->exception_state.exception_value = val_string(buffer);
        }
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
    } else {
        // No context - print error and exit
        fprintf(stderr, "Runtime error: %s\n", buffer);
        exit(1);
    }
}

// Runtime error with line number
void runtime_error_at(ExecutionContext *ctx, int line, const char *format, ...) {
    char buffer[512];
    char full_buffer[600];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Include line number in the error message
    if (line > 0) {
        snprintf(full_buffer, sizeof(full_buffer), "[line %d] %s", line, buffer);
    } else {
        snprintf(full_buffer, sizeof(full_buffer), "%s", buffer);
    }

    // Set exception state for catchable errors
    if (ctx) {
        ctx->exception_state.exception_value = val_string(full_buffer);
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
    } else {
        // No context - print error and exit
        fprintf(stderr, "Runtime error: %s\n", full_buffer);
        exit(1);
    }
}

// Runtime error with full location (file, line, column) and source context
void runtime_error_with_context(ExecutionContext *ctx, const char *file, int line, int column, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Format error with full location and source context
    char *formatted = format_error_with_context(file, line, column, buffer);

    // Set exception state for catchable errors
    if (ctx) {
        ctx->exception_state.exception_value = val_string(formatted);
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
    } else {
        // No context - print error and exit
        fprintf(stderr, "Runtime error: %s\n", formatted);
        free(formatted);
        exit(1);
    }

    free(formatted);
}

// ========== SANDBOX HELPERS ==========

// Check if a specific sandbox restriction is active
int sandbox_is_restricted(ExecutionContext *ctx, int restriction_flag) {
    if (!ctx) return 0;  // No context = no restrictions
    return (ctx->sandbox_flags & restriction_flag) != 0;
}

// Check if a file path is allowed under sandbox rules
// Returns 1 if allowed, 0 if blocked
int sandbox_path_allowed(ExecutionContext *ctx, const char *path, int is_write) {
    if (!ctx) return 1;  // No context = allowed

    // Check file write restriction
    if (is_write && sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_FILE_WRITE)) {
        return 0;
    }

    // Check file read restriction (only when sandbox_root is set)
    if (!is_write && sandbox_is_restricted(ctx, HML_SANDBOX_RESTRICT_FILE_READ)) {
        // If no sandbox root, all reads are blocked
        if (!ctx->sandbox_root) {
            return 0;
        }
    }

    // If sandbox_root is set, validate path is within it
    if (ctx->sandbox_root) {
        char resolved_path[HML_SANDBOX_ROOT_MAX_PATH];
        char resolved_root[HML_SANDBOX_ROOT_MAX_PATH];

        // Resolve both paths to absolute form
        if (!realpath(path, resolved_path)) {
            // Path doesn't exist yet - try resolving parent directory
            char *path_copy = strdup(path);
            char *parent = path_copy;
            char *last_slash = strrchr(path_copy, '/');
            if (last_slash) {
                *last_slash = '\0';
                if (!realpath(parent, resolved_path)) {
                    free(path_copy);
                    return 0;  // Parent doesn't exist
                }
            } else {
                // Relative path without directory - use cwd
                if (!getcwd(resolved_path, sizeof(resolved_path))) {
                    free(path_copy);
                    return 0;
                }
            }
            free(path_copy);
        }

        if (!realpath(ctx->sandbox_root, resolved_root)) {
            return 0;  // Sandbox root doesn't exist
        }

        // Check if resolved path starts with resolved root
        size_t root_len = strlen(resolved_root);
        if (strncmp(resolved_path, resolved_root, root_len) != 0) {
            return 0;  // Path is outside sandbox root
        }

        // Make sure it's not just a prefix match (e.g., /foo vs /foobar)
        if (resolved_path[root_len] != '\0' && resolved_path[root_len] != '/') {
            return 0;
        }
    }

    return 1;  // Allowed
}

// Throw a sandbox violation error
void sandbox_error(ExecutionContext *ctx, const char *operation) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Sandbox violation: %s is not allowed in sandbox mode", operation);
    runtime_error(ctx, "%s", buffer);
}
