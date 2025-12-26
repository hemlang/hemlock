/*
 * Hemlock Threading Compatibility Layer
 *
 * Provides cross-platform threading primitives:
 * - Threads (pthread_t / Windows threads)
 * - Mutexes (pthread_mutex_t / CRITICAL_SECTION)
 * - Condition variables (pthread_cond_t / CONDITION_VARIABLE)
 * - Atomic operations
 * - Sleep functions
 */

#ifndef HML_COMPAT_THREADING_H
#define HML_COMPAT_THREADING_H

#include "platform.h"

#ifdef HML_WINDOWS

/* ========== Windows Implementation ========== */

#include <windows.h>
#include <process.h>

/* Thread type and functions */
typedef HANDLE hml_thread_t;

typedef struct {
    void *(*start_routine)(void *);
    void *arg;
    void *retval;
} hml_thread_data_t;

/* Thread wrapper for Windows _beginthreadex */
static unsigned __stdcall hml_thread_wrapper(void *arg) {
    hml_thread_data_t *data = (hml_thread_data_t *)arg;
    data->retval = data->start_routine(data->arg);
    return 0;
}

HML_INLINE int hml_thread_create(hml_thread_t *thread, void *(*start_routine)(void *), void *arg) {
    hml_thread_data_t *data = (hml_thread_data_t *)malloc(sizeof(hml_thread_data_t));
    if (!data) return -1;
    data->start_routine = start_routine;
    data->arg = arg;
    data->retval = NULL;

    *thread = (HANDLE)_beginthreadex(NULL, 0, hml_thread_wrapper, data, 0, NULL);
    if (*thread == NULL) {
        free(data);
        return -1;
    }
    return 0;
}

HML_INLINE int hml_thread_join(hml_thread_t thread, void **retval) {
    DWORD result = WaitForSingleObject(thread, INFINITE);
    if (result != WAIT_OBJECT_0) return -1;

    /* Note: retval handling would require tracking thread data */
    if (retval) *retval = NULL;

    CloseHandle(thread);
    return 0;
}

HML_INLINE int hml_thread_detach(hml_thread_t thread) {
    /* On Windows, closing the handle "detaches" the thread */
    return CloseHandle(thread) ? 0 : -1;
}

/* Mutex type and functions */
typedef CRITICAL_SECTION hml_mutex_t;

HML_INLINE int hml_mutex_init(hml_mutex_t *mutex) {
    InitializeCriticalSection(mutex);
    return 0;
}

HML_INLINE int hml_mutex_destroy(hml_mutex_t *mutex) {
    DeleteCriticalSection(mutex);
    return 0;
}

HML_INLINE int hml_mutex_lock(hml_mutex_t *mutex) {
    EnterCriticalSection(mutex);
    return 0;
}

HML_INLINE int hml_mutex_unlock(hml_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

/* Condition variable type and functions (Windows Vista+) */
typedef CONDITION_VARIABLE hml_cond_t;

HML_INLINE int hml_cond_init(hml_cond_t *cond) {
    InitializeConditionVariable(cond);
    return 0;
}

HML_INLINE int hml_cond_destroy(hml_cond_t *cond) {
    /* Windows condition variables don't need cleanup */
    (void)cond;
    return 0;
}

HML_INLINE int hml_cond_wait(hml_cond_t *cond, hml_mutex_t *mutex) {
    return SleepConditionVariableCS(cond, mutex, INFINITE) ? 0 : -1;
}

HML_INLINE int hml_cond_timedwait_ms(hml_cond_t *cond, hml_mutex_t *mutex, unsigned int ms) {
    return SleepConditionVariableCS(cond, mutex, ms) ? 0 : -1;
}

/* Timed wait with absolute timespec deadline (Windows version) */
HML_INLINE int hml_cond_timedwait(hml_cond_t *cond, hml_mutex_t *mutex, const struct timespec *abstime) {
    /* Convert absolute time to relative milliseconds */
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER now;
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;
    now.QuadPart -= 116444736000000000ULL;  /* Convert to Unix epoch */
    long long now_ns = now.QuadPart * 100;  /* Convert 100ns units to ns */

    long long target_ns = abstime->tv_sec * 1000000000LL + abstime->tv_nsec;
    long long diff_ms = (target_ns - now_ns) / 1000000LL;

    if (diff_ms <= 0) {
        return ETIMEDOUT;  /* Already expired */
    }

    DWORD ms = (diff_ms > MAXDWORD) ? MAXDWORD : (DWORD)diff_ms;
    if (!SleepConditionVariableCS(cond, mutex, ms)) {
        if (GetLastError() == ERROR_TIMEOUT) {
            return ETIMEDOUT;
        }
        return -1;
    }
    return 0;
}

HML_INLINE int hml_cond_signal(hml_cond_t *cond) {
    WakeConditionVariable(cond);
    return 0;
}

HML_INLINE int hml_cond_broadcast(hml_cond_t *cond) {
    WakeAllConditionVariable(cond);
    return 0;
}

/* Sleep functions */
HML_INLINE void hml_sleep_ms(unsigned int ms) {
    Sleep(ms);
}

HML_INLINE void hml_sleep_ns(long long ns) {
    /* Windows Sleep only supports milliseconds, round up */
    DWORD ms = (DWORD)((ns + 999999) / 1000000);
    if (ms == 0 && ns > 0) ms = 1;
    Sleep(ms);
}

/* Atomic operations - Windows Interlocked functions */
typedef volatile LONG hml_atomic_int;

HML_INLINE int hml_atomic_load(hml_atomic_int *ptr) {
    return InterlockedCompareExchange(ptr, 0, 0);
}

HML_INLINE void hml_atomic_store(hml_atomic_int *ptr, int value) {
    InterlockedExchange(ptr, value);
}

HML_INLINE int hml_atomic_fetch_add(hml_atomic_int *ptr, int value) {
    return InterlockedExchangeAdd(ptr, value);
}

HML_INLINE int hml_atomic_fetch_sub(hml_atomic_int *ptr, int value) {
    return InterlockedExchangeAdd(ptr, -value);
}

HML_INLINE int hml_atomic_exchange(hml_atomic_int *ptr, int value) {
    return InterlockedExchange(ptr, value);
}

HML_INLINE int hml_atomic_compare_exchange(hml_atomic_int *ptr, int *expected, int desired) {
    LONG old = InterlockedCompareExchange(ptr, desired, *expected);
    if (old == *expected) {
        return 1; /* Success */
    } else {
        *expected = old;
        return 0; /* Failure */
    }
}

/* Signal masking - Windows doesn't have POSIX signals in threads */
/* These are no-ops on Windows; signals are handled differently */
/* Note: hml_sigset_t may also be defined in signals.h - use guard */
#ifndef HML_SIGSET_T_DEFINED
#define HML_SIGSET_T_DEFINED
typedef unsigned long hml_sigset_t;

HML_INLINE int hml_sigfillset(hml_sigset_t *set) {
    *set = ~0UL;
    return 0;
}

HML_INLINE int hml_sigemptyset(hml_sigset_t *set) {
    *set = 0;
    return 0;
}
#endif /* HML_SIGSET_T_DEFINED */

HML_INLINE int hml_pthread_sigmask(int how, const hml_sigset_t *set, hml_sigset_t *oldset) {
    /* No-op on Windows */
    (void)how;
    (void)set;
    if (oldset) *oldset = 0;
    return 0;
}

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* One-time initialization (Windows version) */
typedef INIT_ONCE hml_once_t;
#define HML_ONCE_INIT INIT_ONCE_STATIC_INIT

static BOOL CALLBACK hml_once_callback(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context) {
    (void)InitOnce;
    (void)Context;
    void (*init_routine)(void) = (void (*)(void))Parameter;
    init_routine();
    return TRUE;
}

HML_INLINE int hml_once(hml_once_t *once_control, void (*init_routine)(void)) {
    return InitOnceExecuteOnce(once_control, hml_once_callback, (PVOID)init_routine, NULL) ? 0 : -1;
}

#else /* POSIX Implementation */

/* ========== POSIX Implementation ========== */

#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <stdatomic.h>

/* Thread type and functions */
typedef pthread_t hml_thread_t;

HML_INLINE int hml_thread_create(hml_thread_t *thread, void *(*start_routine)(void *), void *arg) {
    return pthread_create(thread, NULL, start_routine, arg);
}

HML_INLINE int hml_thread_join(hml_thread_t thread, void **retval) {
    return pthread_join(thread, retval);
}

HML_INLINE int hml_thread_detach(hml_thread_t thread) {
    return pthread_detach(thread);
}

/* Mutex type and functions */
typedef pthread_mutex_t hml_mutex_t;

HML_INLINE int hml_mutex_init(hml_mutex_t *mutex) {
    return pthread_mutex_init(mutex, NULL);
}

HML_INLINE int hml_mutex_destroy(hml_mutex_t *mutex) {
    return pthread_mutex_destroy(mutex);
}

HML_INLINE int hml_mutex_lock(hml_mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

HML_INLINE int hml_mutex_unlock(hml_mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

/* Condition variable type and functions */
typedef pthread_cond_t hml_cond_t;

HML_INLINE int hml_cond_init(hml_cond_t *cond) {
    return pthread_cond_init(cond, NULL);
}

HML_INLINE int hml_cond_destroy(hml_cond_t *cond) {
    return pthread_cond_destroy(cond);
}

HML_INLINE int hml_cond_wait(hml_cond_t *cond, hml_mutex_t *mutex) {
    return pthread_cond_wait(cond, mutex);
}

HML_INLINE int hml_cond_timedwait_ms(hml_cond_t *cond, hml_mutex_t *mutex, unsigned int ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    return pthread_cond_timedwait(cond, mutex, &ts);
}

/* Timed wait with absolute timespec deadline (POSIX version) */
HML_INLINE int hml_cond_timedwait(hml_cond_t *cond, hml_mutex_t *mutex, const struct timespec *abstime) {
    return pthread_cond_timedwait(cond, mutex, abstime);
}

HML_INLINE int hml_cond_signal(hml_cond_t *cond) {
    return pthread_cond_signal(cond);
}

HML_INLINE int hml_cond_broadcast(hml_cond_t *cond) {
    return pthread_cond_broadcast(cond);
}

/* Sleep functions */
HML_INLINE void hml_sleep_ms(unsigned int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };
    nanosleep(&ts, NULL);
}

HML_INLINE void hml_sleep_ns(long long ns) {
    struct timespec ts = { ns / 1000000000LL, ns % 1000000000LL };
    nanosleep(&ts, NULL);
}

/* Atomic operations - use C11 stdatomic */
typedef atomic_int hml_atomic_int;

HML_INLINE int hml_atomic_load(hml_atomic_int *ptr) {
    return atomic_load(ptr);
}

HML_INLINE void hml_atomic_store(hml_atomic_int *ptr, int value) {
    atomic_store(ptr, value);
}

HML_INLINE int hml_atomic_fetch_add(hml_atomic_int *ptr, int value) {
    return atomic_fetch_add(ptr, value);
}

HML_INLINE int hml_atomic_fetch_sub(hml_atomic_int *ptr, int value) {
    return atomic_fetch_sub(ptr, value);
}

HML_INLINE int hml_atomic_exchange(hml_atomic_int *ptr, int value) {
    return atomic_exchange(ptr, value);
}

HML_INLINE int hml_atomic_compare_exchange(hml_atomic_int *ptr, int *expected, int desired) {
    return atomic_compare_exchange_strong(ptr, expected, desired);
}

/* Signal masking */
typedef sigset_t hml_sigset_t;

HML_INLINE int hml_sigfillset(hml_sigset_t *set) {
    return sigfillset(set);
}

HML_INLINE int hml_sigemptyset(hml_sigset_t *set) {
    return sigemptyset(set);
}

HML_INLINE int hml_pthread_sigmask(int how, const hml_sigset_t *set, hml_sigset_t *oldset) {
    return pthread_sigmask(how, set, oldset);
}

/* One-time initialization (POSIX version) */
typedef pthread_once_t hml_once_t;
#define HML_ONCE_INIT PTHREAD_ONCE_INIT

HML_INLINE int hml_once(hml_once_t *once_control, void (*init_routine)(void)) {
    return pthread_once(once_control, init_routine);
}

#endif /* HML_WINDOWS */

/* ========== Common Constants ========== */

/* Error codes (aligned with POSIX) */
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

#endif /* HML_COMPAT_THREADING_H */
