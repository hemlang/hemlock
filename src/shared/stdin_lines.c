/*
 * Buffered line reads from standard input - see stdin_lines.h for why this
 * does not use stdio.
 */

#include "stdin_lines.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define HML_STDIN_CHUNK 4096

/* Bytes read from fd 0 but not yet handed out, as [start, end). */
static char *g_buf = NULL;
static size_t g_cap = 0;
static size_t g_start = 0;
static size_t g_end = 0;
static int g_eof = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* Make room for at least HML_STDIN_CHUNK more bytes, sliding consumed bytes
 * off the front first so a long session does not grow without bound. */
static int stdin_reserve(void) {
    if (g_start > 0) {
        memmove(g_buf, g_buf + g_start, g_end - g_start);
        g_end -= g_start;
        g_start = 0;
    }
    if (g_end + HML_STDIN_CHUNK > g_cap) {
        size_t cap = g_cap ? g_cap * 2 : HML_STDIN_CHUNK * 2;
        while (cap < g_end + HML_STDIN_CHUNK) cap *= 2;
        char *buf = realloc(g_buf, cap);
        if (!buf) return 0;
        g_buf = buf;
        g_cap = cap;
    }
    return 1;
}

/* Hand out g_buf[g_start, line_end), dropping a trailing CRLF or LF. */
static char *stdin_take(size_t line_end, size_t *out_len) {
    size_t len = line_end - g_start;
    if (len > 0 && g_buf[g_start + len - 1] == '\n') len--;
    if (len > 0 && g_buf[g_start + len - 1] == '\r') len--;

    char *line = malloc(len + 1);
    if (!line) return NULL;
    memcpy(line, g_buf + g_start, len);
    line[len] = '\0';

    g_start = line_end;
    if (g_start == g_end) {
        g_start = 0;
        g_end = 0;
    }
    if (out_len) *out_len = len;
    return line;
}

char *hml_stdin_read_line(size_t *out_len) {
    pthread_mutex_lock(&g_lock);

    for (;;) {
        for (size_t i = g_start; i < g_end; i++) {
            if (g_buf[i] == '\n') {
                char *line = stdin_take(i + 1, out_len);
                pthread_mutex_unlock(&g_lock);
                return line;
            }
        }

        if (g_eof) {
            /* No newline left. Whatever remains is the last line; after that
             * there is nothing to report. */
            if (g_start < g_end) {
                char *line = stdin_take(g_end, out_len);
                pthread_mutex_unlock(&g_lock);
                return line;
            }
            pthread_mutex_unlock(&g_lock);
            return NULL;
        }

        if (!stdin_reserve()) {
            pthread_mutex_unlock(&g_lock);
            return NULL;
        }

        ssize_t n = read(STDIN_FILENO, g_buf + g_end, HML_STDIN_CHUNK);
        if (n > 0) {
            g_end += (size_t)n;
        } else if (n == 0) {
            g_eof = 1;
        } else if (errno != EINTR) {
            /* A read error leaves nothing more to give; report what is
             * buffered, then end of input. */
            g_eof = 1;
        }
    }
}
