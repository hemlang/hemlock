#include "internal.h"
#include <stdarg.h>

// ========== RUNTIME ERROR HELPER ==========

static Value throw_runtime_error(ExecutionContext *ctx, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ctx->exception_state.exception_value = val_string(buffer);
    value_retain(ctx->exception_state.exception_value);
    ctx->exception_state.is_throwing = 1;
    return val_null();
}

// ========== FILE METHOD HANDLING ==========

Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args, ExecutionContext *ctx) {
    // read(size?: i32): string - read text from file
    if (strcmp(method, "read") == 0) {
        if (file->closed) {
            return throw_runtime_error(ctx, "Cannot read from closed file '%s'", file->path);
        }

        if (num_args == 0) {
            // Read entire file from current position
            long current_pos = ftell(file->fp);
            fseek(file->fp, 0, SEEK_END);
            long end_pos = ftell(file->fp);
            fseek(file->fp, current_pos, SEEK_SET);

            long size = end_pos - current_pos;
            if (size <= 0) {
                return val_string("");
            }

            char *buffer = malloc(size + 1);
            if (!buffer) {
                return throw_runtime_error(ctx, "Memory allocation failed");
            }
            size_t read_bytes = fread(buffer, 1, size, file->fp);
            buffer[read_bytes] = '\0';

            if (ferror(file->fp)) {
                free(buffer);
                return throw_runtime_error(ctx, "Read error on file '%s': %s",
                        file->path, strerror(errno));
            }

            String *str = malloc(sizeof(String));
            str->data = buffer;
            str->length = read_bytes;
            str->char_length = -1;
            str->capacity = size + 1;
            str->ref_count = 0;

            return (Value){ .type = VAL_STRING, .as.as_string = str };
        } else if (num_args == 1) {
            // Read specified number of bytes
            if (!is_integer(args[0])) {
                return throw_runtime_error(ctx, "read() size must be integer");
            }

            int size = value_to_int(args[0]);
            if (size <= 0) {
                return val_string("");
            }

            char *buffer = malloc(size + 1);
            if (!buffer) {
                return throw_runtime_error(ctx, "Memory allocation failed");
            }
            size_t read_bytes = fread(buffer, 1, size, file->fp);
            buffer[read_bytes] = '\0';

            if (ferror(file->fp)) {
                free(buffer);
                return throw_runtime_error(ctx, "Read error on file '%s': %s",
                        file->path, strerror(errno));
            }

            String *str = malloc(sizeof(String));
            str->data = buffer;
            str->length = read_bytes;
            str->char_length = -1;
            str->capacity = size + 1;
            str->ref_count = 0;

            return (Value){ .type = VAL_STRING, .as.as_string = str };
        } else {
            return throw_runtime_error(ctx, "read() expects 0-1 arguments");
        }
    }

    // read_bytes(size: i32): buffer - read binary data
    if (strcmp(method, "read_bytes") == 0) {
        if (file->closed) {
            return throw_runtime_error(ctx, "Cannot read from closed file '%s'", file->path);
        }

        if (num_args != 1 || !is_integer(args[0])) {
            return throw_runtime_error(ctx, "read_bytes() expects 1 integer argument (size)");
        }

        int size = value_to_int(args[0]);
        if (size <= 0) {
            Buffer *buf = malloc(sizeof(Buffer));
            buf->data = malloc(1);
            buf->length = 0;
            buf->capacity = 0;
            buf->ref_count = 0;
            return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
        }

        void *data = malloc(size);
        if (!data) {
            return throw_runtime_error(ctx, "Memory allocation failed");
        }
        size_t read_bytes = fread(data, 1, size, file->fp);

        if (ferror(file->fp)) {
            free(data);
            return throw_runtime_error(ctx, "Read error on file '%s': %s",
                    file->path, strerror(errno));
        }

        Buffer *buf = malloc(sizeof(Buffer));
        buf->data = data;
        buf->length = read_bytes;
        buf->capacity = size;
        buf->ref_count = 0;

        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    // write(data: string): i32 - write string to file
    if (strcmp(method, "write") == 0) {
        if (file->closed) {
            return throw_runtime_error(ctx, "Cannot write to closed file '%s'", file->path);
        }

        if (num_args != 1) {
            return throw_runtime_error(ctx, "write() expects 1 argument (data)");
        }

        // Check if file is writable
        if (file->mode[0] == 'r' && strchr(file->mode, '+') == NULL) {
            return throw_runtime_error(ctx, "Cannot write to file '%s' opened in read-only mode", file->path);
        }

        size_t written = 0;
        if (args[0].type == VAL_STRING) {
            String *str = args[0].as.as_string;
            written = fwrite(str->data, 1, str->length, file->fp);

            if (ferror(file->fp)) {
                return throw_runtime_error(ctx, "Write error on file '%s': %s",
                        file->path, strerror(errno));
            }
        } else {
            return throw_runtime_error(ctx, "write() expects string argument");
        }

        return val_i32((int32_t)written);
    }

    // write_bytes(data: buffer): i32 - write binary data
    if (strcmp(method, "write_bytes") == 0) {
        if (file->closed) {
            return throw_runtime_error(ctx, "Cannot write to closed file '%s'", file->path);
        }

        if (num_args != 1) {
            return throw_runtime_error(ctx, "write_bytes() expects 1 argument (data)");
        }

        // Check if file is writable
        if (file->mode[0] == 'r' && strchr(file->mode, '+') == NULL) {
            return throw_runtime_error(ctx, "Cannot write to file '%s' opened in read-only mode", file->path);
        }

        size_t written = 0;
        if (args[0].type == VAL_BUFFER) {
            Buffer *buf = args[0].as.as_buffer;
            written = fwrite(buf->data, 1, buf->length, file->fp);

            if (ferror(file->fp)) {
                return throw_runtime_error(ctx, "Write error on file '%s': %s",
                        file->path, strerror(errno));
            }
        } else {
            return throw_runtime_error(ctx, "write_bytes() expects buffer argument");
        }

        return val_i32((int32_t)written);
    }

    // seek(position: i32): i32 - move file pointer
    if (strcmp(method, "seek") == 0) {
        if (file->closed) {
            return throw_runtime_error(ctx, "Cannot seek in closed file '%s'", file->path);
        }

        if (num_args != 1 || !is_integer(args[0])) {
            return throw_runtime_error(ctx, "seek() expects 1 integer argument (position)");
        }

        int position = value_to_int(args[0]);
        if (fseek(file->fp, position, SEEK_SET) != 0) {
            return throw_runtime_error(ctx, "Seek error on file '%s': %s",
                    file->path, strerror(errno));
        }

        long new_pos = ftell(file->fp);
        return val_i32((int32_t)new_pos);
    }

    // tell(): i32 - get current file position
    if (strcmp(method, "tell") == 0) {
        if (file->closed) {
            return throw_runtime_error(ctx, "Cannot tell position in closed file '%s'", file->path);
        }

        if (num_args != 0) {
            return throw_runtime_error(ctx, "tell() expects no arguments");
        }

        long pos = ftell(file->fp);
        if (pos < 0) {
            return throw_runtime_error(ctx, "Tell error on file '%s': %s",
                    file->path, strerror(errno));
        }

        return val_i32((int32_t)pos);
    }

    // close() - close file (idempotent)
    if (strcmp(method, "close") == 0) {
        if (num_args != 0) {
            return throw_runtime_error(ctx, "close() expects no arguments");
        }

        // Idempotent - safe to call multiple times
        if (!file->closed && file->fp) {
            fclose(file->fp);
            file->fp = NULL;
            file->closed = 1;
        }
        return val_null();
    }

    return throw_runtime_error(ctx, "File has no method '%s'", method);
}

// ========== I/O BUILTIN FUNCTIONS ==========

Value builtin_read_line(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    if (num_args != 0) {
        ctx->exception_state.exception_value = val_string("read_line() expects no arguments");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read = getline(&line, &len, stdin);

    if (read == -1) {
        free(line);
        return val_null();  // EOF
    }

    // Strip newline
    if (read > 0 && line[read - 1] == '\n') {
        line[read - 1] = '\0';
        read--;
    }
    if (read > 0 && line[read - 1] == '\r') {
        line[read - 1] = '\0';
        read--;
    }

    String *str = malloc(sizeof(String));
    str->data = line;
    str->length = read;
    str->capacity = len;
    str->ref_count = 0;

    return (Value){ .type = VAL_STRING, .as.as_string = str };
}

Value builtin_eprint(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        ctx->exception_state.exception_value = val_string("eprint() expects 1 argument");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    // Print to stderr
    switch (args[0].type) {
        case VAL_I8:
            fprintf(stderr, "%d", args[0].as.as_i8);
            break;
        case VAL_I16:
            fprintf(stderr, "%d", args[0].as.as_i16);
            break;
        case VAL_I32:
            fprintf(stderr, "%d", args[0].as.as_i32);
            break;
        case VAL_U8:
            fprintf(stderr, "%u", args[0].as.as_u8);
            break;
        case VAL_U16:
            fprintf(stderr, "%u", args[0].as.as_u16);
            break;
        case VAL_U32:
            fprintf(stderr, "%u", args[0].as.as_u32);
            break;
        case VAL_F32:
            fprintf(stderr, "%g", args[0].as.as_f32);
            break;
        case VAL_F64:
            fprintf(stderr, "%g", args[0].as.as_f64);
            break;
        case VAL_BOOL:
            fprintf(stderr, "%s", args[0].as.as_bool ? "true" : "false");
            break;
        case VAL_STRING:
            fprintf(stderr, "%s", args[0].as.as_string->data);
            break;
        case VAL_NULL:
            fprintf(stderr, "null");
            break;
        default:
            // For complex types, use simpler representation
            fprintf(stderr, "<value>");
            break;
    }
    fprintf(stderr, "\n");
    return val_null();
}

Value builtin_open(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        ctx->exception_state.exception_value = val_string("open() expects 1-2 arguments (path, [mode])");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        ctx->exception_state.exception_value = val_string("open() path must be a string");
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    const char *path = args[0].as.as_string->data;
    const char *mode = "r";  // Default mode

    if (num_args == 2) {
        if (args[1].type != VAL_STRING) {
            ctx->exception_state.exception_value = val_string("open() mode must be a string");
            value_retain(ctx->exception_state.exception_value);
            ctx->exception_state.is_throwing = 1;
            return val_null();
        }
        mode = args[1].as.as_string->data;
    }

    FILE *fp = fopen(path, mode);
    if (!fp) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open '%s' with mode '%s': %s",
                path, mode, strerror(errno));
        ctx->exception_state.exception_value = val_string(error_msg);
        value_retain(ctx->exception_state.exception_value);
        ctx->exception_state.is_throwing = 1;
        return val_null();
    }

    FileHandle *file = malloc(sizeof(FileHandle));
    file->fp = fp;
    file->path = strdup(path);
    file->mode = strdup(mode);
    file->closed = 0;

    return val_file(file);
}
