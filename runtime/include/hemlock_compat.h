/*
 * Hemlock Runtime Library - Platform Compatibility Header
 *
 * This header MUST be included FIRST before any other headers.
 * It sets up Windows compatibility macros and includes headers
 * in the correct order to avoid conflicts.
 *
 * Windows Limitations (documented here):
 * - poll() uses WSAPoll which requires Windows Vista+
 * - fork() is not supported (always returns -1)
 * - getppid(), getuid(), getgid() return 0
 * - ECDSA crypto requires OpenSSL 3.0+
 * - Some signal handlers are limited
 * - Terminal raw mode (termios) is not available
 */

#ifndef HEMLOCK_COMPAT_H
#define HEMLOCK_COMPAT_H

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #ifndef HML_WINDOWS
    #define HML_WINDOWS 1
    #endif
#endif

#ifdef HML_WINDOWS
    // Set minimum Windows version to Vista BEFORE any Windows headers
    // This enables WSAPoll, GetTickCount64, and other Vista+ APIs
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
    #endif
    #ifndef WINVER
    #define WINVER 0x0600
    #endif

    // WIN32_LEAN_AND_MEAN reduces Windows header bloat
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif

    // CRITICAL: winsock2.h MUST be included BEFORE windows.h
    // Many winsock functions conflict with windows.h definitions
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>

    // Define PROCESSOR_ARCHITECTURE_ARM64 if not available (older SDK)
    #ifndef PROCESSOR_ARCHITECTURE_ARM64
    #define PROCESSOR_ARCHITECTURE_ARM64 12
    #endif

#endif // HML_WINDOWS

#endif // HEMLOCK_COMPAT_H
