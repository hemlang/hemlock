/*
 * Hemlock Dynamic Loading Compatibility Layer
 *
 * Provides cross-platform dynamic library loading:
 * - dlopen/dlsym/dlclose for POSIX
 * - LoadLibrary/GetProcAddress/FreeLibrary for Windows
 */

#ifndef HML_COMPAT_DLFCN_H
#define HML_COMPAT_DLFCN_H

#include "platform.h"

#ifdef HML_WINDOWS

/* ========== Windows Implementation ========== */

#include <windows.h>

/* Library handle type */
typedef HMODULE hml_lib_t;
#define HML_LIB_INVALID NULL

/* dlopen flags (not used on Windows, but defined for compatibility) */
#define RTLD_LAZY    0x00001
#define RTLD_NOW     0x00002
#define RTLD_GLOBAL  0x00100
#define RTLD_LOCAL   0x00000

/* Thread-local error message buffer */
static HML_THREAD_LOCAL char hml_dlerror_buf[512] = {0};
static HML_THREAD_LOCAL int hml_dlerror_set = 0;

/* Clear and get error message */
HML_INLINE void hml_dlerror_clear(void) {
    hml_dlerror_buf[0] = '\0';
    hml_dlerror_set = 0;
}

HML_INLINE void hml_dlerror_set_message(void) {
    DWORD err = GetLastError();
    if (err != 0) {
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       hml_dlerror_buf, sizeof(hml_dlerror_buf), NULL);
        /* Remove trailing newline */
        size_t len = strlen(hml_dlerror_buf);
        while (len > 0 && (hml_dlerror_buf[len-1] == '\n' || hml_dlerror_buf[len-1] == '\r')) {
            hml_dlerror_buf[--len] = '\0';
        }
        hml_dlerror_set = 1;
    }
}

/* Open a dynamic library */
HML_INLINE hml_lib_t hml_dlopen(const char *filename, int flags) {
    (void)flags;  /* Windows doesn't use flags */
    hml_dlerror_clear();

    if (filename == NULL) {
        /* Return handle to the main executable */
        return GetModuleHandle(NULL);
    }

    /* Try loading the library */
    hml_lib_t handle = LoadLibraryA(filename);
    if (handle == NULL) {
        hml_dlerror_set_message();

        /* If failed, try with .dll extension if not already present */
        size_t len = strlen(filename);
        if (len < 4 || strcmp(filename + len - 4, ".dll") != 0) {
            char *dll_path = (char *)malloc(len + 5);
            if (dll_path) {
                strcpy(dll_path, filename);
                strcat(dll_path, ".dll");
                handle = LoadLibraryA(dll_path);
                free(dll_path);
                if (handle != NULL) {
                    hml_dlerror_clear();
                }
            }
        }
    }

    return handle;
}

/* Get a symbol from a library */
HML_INLINE void *hml_dlsym(hml_lib_t handle, const char *symbol) {
    hml_dlerror_clear();
    void *ptr = (void *)GetProcAddress(handle, symbol);
    if (ptr == NULL) {
        hml_dlerror_set_message();
    }
    return ptr;
}

/* Close a dynamic library */
HML_INLINE int hml_dlclose(hml_lib_t handle) {
    hml_dlerror_clear();
    if (handle == NULL) {
        return 0;
    }
    if (!FreeLibrary(handle)) {
        hml_dlerror_set_message();
        return -1;
    }
    return 0;
}

/* Get error message */
HML_INLINE char *hml_dlerror(void) {
    if (hml_dlerror_set) {
        hml_dlerror_set = 0;
        return hml_dlerror_buf;
    }
    return NULL;
}

#else /* POSIX Implementation */

/* ========== POSIX Implementation ========== */

#include <dlfcn.h>

/* Library handle type */
typedef void *hml_lib_t;
#define HML_LIB_INVALID NULL

/* Open a dynamic library */
HML_INLINE hml_lib_t hml_dlopen(const char *filename, int flags) {
    return dlopen(filename, flags);
}

/* Get a symbol from a library */
HML_INLINE void *hml_dlsym(hml_lib_t handle, const char *symbol) {
    return dlsym(handle, symbol);
}

/* Close a dynamic library */
HML_INLINE int hml_dlclose(hml_lib_t handle) {
    return dlclose(handle);
}

/* Get error message */
HML_INLINE char *hml_dlerror(void) {
    return dlerror();
}

#endif /* HML_WINDOWS */

#endif /* HML_COMPAT_DLFCN_H */
