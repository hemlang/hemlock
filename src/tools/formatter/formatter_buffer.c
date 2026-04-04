#include "formatter_internal.h"

// ========== STRING BUFFER ==========

void buf_init(StrBuf *buf) {
    buf->cap = 256;
    buf->data = malloc(buf->cap);
    buf->data[0] = '\0';
    buf->len = 0;
}

void buf_free(StrBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = buf->cap = 0;
}

void buf_grow(StrBuf *buf, size_t needed) {
    if (buf->len + needed + 1 > buf->cap) {
        size_t new_cap = buf->cap;
        while (buf->len + needed + 1 > new_cap) {
            new_cap *= 2;
        }
        char *new_data = realloc(buf->data, new_cap);
        if (!new_data) {
            fprintf(stderr, "Formatter error: Failed to expand buffer\n");
            exit(1);
        }
        buf->data = new_data;
        buf->cap = new_cap;
    }
}

void buf_append(StrBuf *buf, const char *s) {
    size_t slen = strlen(s);
    buf_grow(buf, slen);
    memcpy(buf->data + buf->len, s, slen);
    buf->len += slen;
    buf->data[buf->len] = '\0';
}

void buf_append_char(StrBuf *buf, char c) {
    buf_grow(buf, 1);
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

void buf_printf(StrBuf *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    // First, determine size needed
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    buf_grow(buf, needed);
    vsnprintf(buf->data + buf->len, needed + 1, fmt, args);
    buf->len += needed;

    va_end(args);
}
