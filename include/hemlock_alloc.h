/*
 * Hemlock - Checked allocation wrappers
 *
 * Internal allocations (values, environments, buffers the runtime owns) have
 * no caller that could recover from failure: continuing with a NULL result
 * means a NULL-based write or a half-built object reaching cleanup code. These
 * wrappers fail loudly and immediately instead.
 *
 * They call abort(), not exit(): exit() runs atexit handlers and static
 * destructors, which would walk the very state that was just left incomplete.
 *
 * Allocations made on behalf of user code (alloc(), realloc(), buffer(),
 * talloc(), array growth) must NOT use these: those report failure to the
 * program (null or a catchable error) and keep using plain malloc/realloc.
 */

#ifndef HEMLOCK_ALLOC_H
#define HEMLOCK_ALLOC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define HML_NORETURN __attribute__((noreturn))
#else
#define HML_NORETURN
#endif

HML_NORETURN static inline void hml_oom_abort(const char *fn, size_t bytes) {
    fprintf(stderr, "Fatal: out of memory (%s of %zu bytes)\n", fn, bytes);
    abort();
}

// A zero-byte request may legitimately return NULL; only a nonzero request
// that comes back NULL is an out-of-memory condition.
static inline void *hml_xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p && size != 0) hml_oom_abort("malloc", size);
    return p;
}

static inline void *hml_xcalloc(size_t count, size_t size) {
    void *p = calloc(count, size);
    if (!p && count != 0 && size != 0) hml_oom_abort("calloc", count * size);
    return p;
}

// On failure the original block is still valid but there is nothing sensible
// to do with it, so this aborts rather than returning NULL.
static inline void *hml_xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size != 0) hml_oom_abort("realloc", size);
    return p;
}

static inline char *hml_xstrdup(const char *s) {
    char *p = strdup(s);
    if (!p) hml_oom_abort("strdup", strlen(s) + 1);
    return p;
}

#endif // HEMLOCK_ALLOC_H
