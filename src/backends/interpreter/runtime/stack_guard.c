// stack_guard.c - how much C stack the calling thread has left
//
// The interpreter's call-depth limit (max_stack_depth, default 8000 Hemlock frames) is a
// count, but what runs out is the C stack, and a Hemlock call costs very different amounts of
// it: a call made from inside a try or a catch nests several more eval_stmt frames than a plain
// one. Measured on Linux with the 8 MB main-thread stack, recursion through a try block
// overflowed between 7,500 and 8,000 levels -- a SIGSEGV just short of the count that was
// supposed to raise a catchable error. A game server died that way: an error handler that
// sent on the connection whose send had just failed, recursing from inside its own catch.
//
// So the call path also asks this: is the current stack pointer within a margin of the
// thread's stack limit? Looked up once per thread (lazily, on its first call), then one
// comparison per call. Where the platform cannot say, it answers "no" and the count alone
// applies, as before.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE   // pthread_getattr_np
#endif

#include <stddef.h>
#include <pthread.h>
#include "hemlock_limits.h"

static __thread char *g_stack_floor = NULL;   // past this (stack grows down), stop calling
static __thread int g_stack_floor_state = 0;  // 0 not looked up, 1 known, -1 unknown here

static void stack_floor_set(char *lowest, size_t size) {
    size_t margin = HML_STACK_GUARD_MARGIN;
    if (margin > size / 4) {
        margin = size / 4;
    }
    g_stack_floor = lowest + margin;
    g_stack_floor_state = 1;
}

static void stack_floor_lookup(void) {
    g_stack_floor_state = -1;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *addr = NULL;
        size_t size = 0;
        // For the main thread glibc and musl report the stack from its mapping and
        // RLIMIT_STACK; for a spawned task, the size it was created with.
        if (pthread_attr_getstack(&attr, &addr, &size) == 0 && addr != NULL && size > 0) {
            stack_floor_set((char *)addr, size);
        }
        pthread_attr_destroy(&attr);
    }
#elif defined(__APPLE__)
    pthread_t self = pthread_self();
    char *top = (char *)pthread_get_stackaddr_np(self);
    size_t size = pthread_get_stacksize_np(self);
    if (top != NULL && size > 0) {
        stack_floor_set(top - size, size);
    }
#endif
}

// 1 when the calling thread is within HML_STACK_GUARD_MARGIN of the end of its stack.
int interp_stack_exhausted(void) {
    char probe;
    if (g_stack_floor_state == 0) {
        stack_floor_lookup();
    }
    return g_stack_floor_state == 1 && (char *)&probe < g_stack_floor;
}
