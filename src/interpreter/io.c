#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// ========== FILE METHOD HANDLING ==========

Value call_file_method(FileHandle *file, const char *method, Value *args, int num_args) {
    if (file->closed) {
        fprintf(stderr, "Runtime error: Cannot call method on closed file\n");
        exit(1);
    }

    if (strcmp(method, "read_text") == 0) {
        // Read up to N bytes as string
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: read_text() expects 1 integer argument (size)\n");
            exit(1);
        }

        int size = value_to_int(args[0]);
        char *buffer = malloc(size + 1);
        size_t read = fread(buffer, 1, size, file->fp);
        buffer[read] = '\0';

        String *str = malloc(sizeof(String));
        str->data = buffer;
        str->length = read;
        str->capacity = size + 1;

        return (Value){ .type = VAL_STRING, .as.as_string = str };
    }

    if (strcmp(method, "read_bytes") == 0) {
        // Read up to N bytes as buffer
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: read_bytes() expects 1 integer argument (size)\n");
            exit(1);
        }

        int size = value_to_int(args[0]);
        void *data = malloc(size);
        size_t read = fread(data, 1, size, file->fp);

        Buffer *buf = malloc(sizeof(Buffer));
        buf->data = data;
        buf->length = read;
        buf->capacity = size;

        return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
    }

    if (strcmp(method, "write") == 0) {
        // Write string or buffer
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: write() expects 1 argument (data)\n");
            exit(1);
        }

        size_t written = 0;
        if (args[0].type == VAL_STRING) {
            String *str = args[0].as.as_string;
            written = fwrite(str->data, 1, str->length, file->fp);
        } else if (args[0].type == VAL_BUFFER) {
            Buffer *buf = args[0].as.as_buffer;
            written = fwrite(buf->data, 1, buf->length, file->fp);
        } else {
            fprintf(stderr, "Runtime error: write() expects string or buffer\n");
            exit(1);
        }

        return val_i32((int32_t)written);
    }

    if (strcmp(method, "seek") == 0) {
        if (num_args != 1 || !is_integer(args[0])) {
            fprintf(stderr, "Runtime error: seek() expects 1 integer argument (offset)\n");
            exit(1);
        }

        int offset = value_to_int(args[0]);
        fseek(file->fp, offset, SEEK_SET);
        return val_null();
    }

    if (strcmp(method, "tell") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: tell() expects no arguments\n");
            exit(1);
        }

        long pos = ftell(file->fp);
        return val_i32((int32_t)pos);
    }

    if (strcmp(method, "close") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: close() expects no arguments\n");
            exit(1);
        }

        if (file->fp) {
            fclose(file->fp);
            file->fp = NULL;
        }
        file->closed = 1;
        return val_null();
    }

    fprintf(stderr, "Runtime error: File has no method '%s'\n", method);
    exit(1);
}

// ========== ARRAY METHOD HANDLING ==========

Value call_array_method(Array *arr, const char *method, Value *args, int num_args) {
    if (strcmp(method, "push") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "Runtime error: push() expects 1 argument\n");
            exit(1);
        }
        array_push(arr, args[0]);
        return val_null();
    }

    if (strcmp(method, "pop") == 0) {
        if (num_args != 0) {
            fprintf(stderr, "Runtime error: pop() expects no arguments\n");
            exit(1);
        }
        return array_pop(arr);
    }

    fprintf(stderr, "Runtime error: Array has no method '%s'\n", method);
    exit(1);
}

// ========== I/O BUILTIN FUNCTIONS ==========

Value builtin_read_file(Value *args, int num_args) {
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: read_file() expects 1 string argument (path)\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    // Read entire file
    char *buffer = malloc(size + 1);
    size_t read = fread(buffer, 1, size, fp);
    buffer[read] = '\0';
    fclose(fp);

    String *str = malloc(sizeof(String));
    str->data = buffer;
    str->length = read;
    str->capacity = size + 1;

    return (Value){ .type = VAL_STRING, .as.as_string = str };
}

Value builtin_write_file(Value *args, int num_args) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: write_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: write_file() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Write string or buffer
    if (args[1].type == VAL_STRING) {
        String *str = args[1].as.as_string;
        fwrite(str->data, 1, str->length, fp);
    } else if (args[1].type == VAL_BUFFER) {
        Buffer *buf = args[1].as.as_buffer;
        fwrite(buf->data, 1, buf->length, fp);
    } else {
        fclose(fp);
        fprintf(stderr, "Runtime error: write_file() content must be string or buffer\n");
        exit(1);
    }

    fclose(fp);
    return val_null();
}

Value builtin_append_file(Value *args, int num_args) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: append_file() expects 2 arguments (path, content)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: append_file() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "ab");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Write string or buffer
    if (args[1].type == VAL_STRING) {
        String *str = args[1].as.as_string;
        fwrite(str->data, 1, str->length, fp);
    } else if (args[1].type == VAL_BUFFER) {
        Buffer *buf = args[1].as.as_buffer;
        fwrite(buf->data, 1, buf->length, fp);
    } else {
        fclose(fp);
        fprintf(stderr, "Runtime error: append_file() content must be string or buffer\n");
        exit(1);
    }

    fclose(fp);
    return val_null();
}

Value builtin_read_bytes(Value *args, int num_args) {
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: read_bytes() expects 1 string argument (path)\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    // Read entire file into buffer
    void *data = malloc(size);
    size_t read = fread(data, 1, size, fp);
    fclose(fp);

    Buffer *buf = malloc(sizeof(Buffer));
    buf->data = data;
    buf->length = read;
    buf->capacity = size;

    return (Value){ .type = VAL_BUFFER, .as.as_buffer = buf };
}

Value builtin_write_bytes(Value *args, int num_args) {
    if (num_args != 2) {
        fprintf(stderr, "Runtime error: write_bytes() expects 2 arguments (path, data)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: write_bytes() path must be a string\n");
        exit(1);
    }

    if (args[1].type != VAL_BUFFER) {
        fprintf(stderr, "Runtime error: write_bytes() data must be a buffer\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;
    Buffer *buf = args[1].as.as_buffer;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    fwrite(buf->data, 1, buf->length, fp);
    fclose(fp);
    return val_null();
}

Value builtin_file_exists(Value *args, int num_args) {
    if (num_args != 1 || args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: file_exists() expects 1 string argument\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;

    FILE *fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        return val_bool(1);
    }
    return val_bool(0);
}

Value builtin_read_line(Value *args, int num_args) {
    (void)args;
    if (num_args != 0) {
        fprintf(stderr, "Runtime error: read_line() expects no arguments\n");
        exit(1);
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

    return (Value){ .type = VAL_STRING, .as.as_string = str };
}

Value builtin_eprint(Value *args, int num_args) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: eprint() expects 1 argument\n");
        exit(1);
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

Value builtin_open(Value *args, int num_args) {
    if (num_args < 1 || num_args > 2) {
        fprintf(stderr, "Runtime error: open() expects 1-2 arguments (path, [mode])\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: open() path must be a string\n");
        exit(1);
    }

    const char *path = args[0].as.as_string->data;
    const char *mode = "r";  // Default mode

    if (num_args == 2) {
        if (args[1].type != VAL_STRING) {
            fprintf(stderr, "Runtime error: open() mode must be a string\n");
            exit(1);
        }
        mode = args[1].as.as_string->data;
    }

    FILE *fp = fopen(path, mode);
    if (!fp) {
        fprintf(stderr, "Runtime error: Failed to open '%s' with mode '%s': %s\n",
                path, mode, strerror(errno));
        exit(1);
    }

    FileHandle *file = malloc(sizeof(FileHandle));
    file->fp = fp;
    file->path = strdup(path);
    file->mode = strdup(mode);
    file->closed = 0;

    return val_file(file);
}
