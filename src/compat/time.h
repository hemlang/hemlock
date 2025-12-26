/*
 * Hemlock Time Compatibility Layer
 *
 * Provides cross-platform time functions:
 * - High-resolution time (gettimeofday, clock_gettime)
 * - Sleep functions (nanosleep, usleep)
 * - Time conversion utilities
 */

#ifndef HML_COMPAT_TIME_H
#define HML_COMPAT_TIME_H

#include "platform.h"
#include <time.h>

#ifdef HML_WINDOWS

/* ========== Windows Implementation ========== */

#include <windows.h>

/* timeval structure (if not defined) */
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

/* timespec structure (if not defined) */
#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

/* Clock IDs for clock_gettime compatibility */
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

/* gettimeofday implementation */
HML_INLINE int hml_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;  /* timezone not used */

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    /* FILETIME is in 100-nanosecond intervals since Jan 1, 1601 */
    /* We need to convert to Unix epoch (Jan 1, 1970) */
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;

    /* Subtract difference between 1601 and 1970 (in 100-ns units) */
    ull.QuadPart -= 116444736000000000ULL;

    /* Convert to seconds and microseconds */
    tv->tv_sec = (long)(ull.QuadPart / 10000000ULL);
    tv->tv_usec = (long)((ull.QuadPart % 10000000ULL) / 10);

    return 0;
}

/* clock_gettime implementation */
HML_INLINE int hml_clock_gettime(int clk_id, struct timespec *ts) {
    if (clk_id == CLOCK_MONOTONIC) {
        /* Use QueryPerformanceCounter for monotonic clock */
        static LARGE_INTEGER freq = {0};
        if (freq.QuadPart == 0) {
            QueryPerformanceFrequency(&freq);
        }

        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);

        ts->tv_sec = (time_t)(count.QuadPart / freq.QuadPart);
        ts->tv_nsec = (long)(((count.QuadPart % freq.QuadPart) * 1000000000LL) / freq.QuadPart);
    } else {
        /* CLOCK_REALTIME - use system time */
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);

        ULARGE_INTEGER ull;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        ull.QuadPart -= 116444736000000000ULL;

        ts->tv_sec = (time_t)(ull.QuadPart / 10000000ULL);
        ts->tv_nsec = (long)((ull.QuadPart % 10000000ULL) * 100);
    }

    return 0;
}

/* nanosleep implementation */
HML_INLINE int hml_nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;  /* remainder not implemented */

    /* Convert to milliseconds, rounding up */
    DWORD ms = (DWORD)(req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000);
    if (ms == 0 && (req->tv_sec > 0 || req->tv_nsec > 0)) {
        ms = 1;  /* Minimum 1ms sleep */
    }

    Sleep(ms);
    return 0;
}

/* usleep implementation (microseconds) */
HML_INLINE int hml_usleep(unsigned int usec) {
    DWORD ms = (usec + 999) / 1000;  /* Round up to milliseconds */
    if (ms == 0 && usec > 0) ms = 1;
    Sleep(ms);
    return 0;
}

/* Get time in milliseconds since program start */
HML_INLINE long long hml_time_ms(void) {
    static LARGE_INTEGER freq = {0};
    static LARGE_INTEGER start = {0};

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    return ((now.QuadPart - start.QuadPart) * 1000LL) / freq.QuadPart;
}

/* Get time in microseconds since program start */
HML_INLINE long long hml_time_us(void) {
    static LARGE_INTEGER freq = {0};
    static LARGE_INTEGER start = {0};

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    return ((now.QuadPart - start.QuadPart) * 1000000LL) / freq.QuadPart;
}

/* Get current time in seconds since Unix epoch */
HML_INLINE double hml_now(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    ull.QuadPart -= 116444736000000000ULL;

    return (double)ull.QuadPart / 10000000.0;
}

#else /* POSIX Implementation */

/* ========== POSIX Implementation ========== */

#include <sys/time.h>
/* For usleep, need _DEFAULT_SOURCE or _BSD_SOURCE on some systems */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <unistd.h>

/* gettimeofday wrapper */
HML_INLINE int hml_gettimeofday(struct timeval *tv, void *tz) {
    return gettimeofday(tv, tz);
}

/* clock_gettime wrapper */
HML_INLINE int hml_clock_gettime(int clk_id, struct timespec *ts) {
    return clock_gettime(clk_id, ts);
}

/* nanosleep wrapper */
HML_INLINE int hml_nanosleep(const struct timespec *req, struct timespec *rem) {
    return nanosleep(req, rem);
}

/* usleep wrapper */
HML_INLINE int hml_usleep(unsigned int usec) {
    return usleep(usec);
}

/* Get time in milliseconds since program start */
HML_INLINE long long hml_time_ms(void) {
    static struct timespec start = {0, 0};
    static int initialized = 0;

    if (!initialized) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        initialized = 1;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long long ms = (now.tv_sec - start.tv_sec) * 1000LL;
    ms += (now.tv_nsec - start.tv_nsec) / 1000000LL;

    return ms;
}

/* Get time in microseconds since program start */
HML_INLINE long long hml_time_us(void) {
    static struct timespec start = {0, 0};
    static int initialized = 0;

    if (!initialized) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        initialized = 1;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long long us = (now.tv_sec - start.tv_sec) * 1000000LL;
    us += (now.tv_nsec - start.tv_nsec) / 1000LL;

    return us;
}

/* Get current time in seconds since Unix epoch */
HML_INLINE double hml_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

#endif /* HML_WINDOWS */

/* ========== Common Time Utilities ========== */

/* Sleep for a number of milliseconds */
HML_INLINE void hml_sleep_milliseconds(unsigned int ms) {
#ifdef HML_WINDOWS
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* Sleep for a number of seconds */
HML_INLINE void hml_sleep_seconds(unsigned int sec) {
#ifdef HML_WINDOWS
    Sleep(sec * 1000);
#else
    sleep(sec);
#endif
}

#endif /* HML_COMPAT_TIME_H */
