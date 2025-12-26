/*
 * Hemlock Platform Detection and Compatibility Layer
 *
 * This header provides platform detection macros and includes
 * the appropriate compatibility headers for the current platform.
 */

#ifndef HML_COMPAT_PLATFORM_H
#define HML_COMPAT_PLATFORM_H

/* Platform Detection */
#if defined(_WIN32) || defined(_WIN64)
    #define HML_WINDOWS 1
    #define HML_PLATFORM_NAME "windows"
    #ifdef _WIN64
        #define HML_64BIT 1
    #else
        #define HML_32BIT 1
    #endif
#elif defined(__APPLE__) && defined(__MACH__)
    #define HML_MACOS 1
    #define HML_POSIX 1
    #define HML_PLATFORM_NAME "darwin"
    #define HML_64BIT 1
#elif defined(__linux__)
    #define HML_LINUX 1
    #define HML_POSIX 1
    #define HML_PLATFORM_NAME "linux"
    #if defined(__x86_64__) || defined(__aarch64__)
        #define HML_64BIT 1
    #else
        #define HML_32BIT 1
    #endif
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #define HML_BSD 1
    #define HML_POSIX 1
    #define HML_PLATFORM_NAME "bsd"
#else
    #define HML_UNKNOWN 1
    #define HML_PLATFORM_NAME "unknown"
#endif

/* Architecture Detection */
#if defined(__x86_64__) || defined(_M_X64)
    #define HML_ARCH_X64 1
    #define HML_ARCH_NAME "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
    #define HML_ARCH_X86 1
    #define HML_ARCH_NAME "x86"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define HML_ARCH_ARM64 1
    #define HML_ARCH_NAME "aarch64"
#elif defined(__arm__) || defined(_M_ARM)
    #define HML_ARCH_ARM 1
    #define HML_ARCH_NAME "arm"
#else
    #define HML_ARCH_UNKNOWN 1
    #define HML_ARCH_NAME "unknown"
#endif

/* Common includes that work on all platforms */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Platform-specific base includes */
#ifdef HML_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
    #include <errno.h>
#endif

/* Compiler-specific attributes */
#if defined(_MSC_VER)
    #define HML_INLINE __forceinline
    #define HML_NOINLINE __declspec(noinline)
    #define HML_UNUSED
    #define HML_NORETURN __declspec(noreturn)
    #define HML_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
    #define HML_INLINE static inline __attribute__((always_inline))
    #define HML_NOINLINE __attribute__((noinline))
    #define HML_UNUSED __attribute__((unused))
    #define HML_NORETURN __attribute__((noreturn))
    #define HML_THREAD_LOCAL __thread
#else
    #define HML_INLINE static inline
    #define HML_NOINLINE
    #define HML_UNUSED
    #define HML_NORETURN
    #define HML_THREAD_LOCAL
#endif

/* Path separator */
#ifdef HML_WINDOWS
    #define HML_PATH_SEP '\\'
    #define HML_PATH_SEP_STR "\\"
    #define HML_PATH_LIST_SEP ';'
#else
    #define HML_PATH_SEP '/'
    #define HML_PATH_SEP_STR "/"
    #define HML_PATH_LIST_SEP ':'
#endif

/* Library extension */
#ifdef HML_WINDOWS
    #define HML_LIB_EXT ".dll"
    #define HML_EXE_EXT ".exe"
#elif defined(HML_MACOS)
    #define HML_LIB_EXT ".dylib"
    #define HML_EXE_EXT ""
#else
    #define HML_LIB_EXT ".so"
    #define HML_EXE_EXT ""
#endif

/* Error handling helpers */
#ifdef HML_WINDOWS
    #define HML_LAST_ERROR() GetLastError()
    HML_INLINE const char* hml_strerror(int err) {
        static HML_THREAD_LOCAL char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       buf, sizeof(buf), NULL);
        /* Remove trailing newline */
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }
        return buf;
    }
#else
    #define HML_LAST_ERROR() errno
    #define hml_strerror(err) strerror(err)
#endif

#endif /* HML_COMPAT_PLATFORM_H */
