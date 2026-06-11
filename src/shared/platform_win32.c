// Windows (MinGW-w64) implementations for the platform compatibility layer.
// See include/hemlock_platform.h. This file is empty on non-Windows builds.
#ifdef _WIN32

#include "hemlock_platform.h"
#include "hemlock_compat.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

// ---- dlfcn shim over LoadLibrary/GetProcAddress ----

static char hml_dl_error_buf[256];
static int hml_dl_error_set = 0;

static void hml_dl_set_error(const char *what, const char *name) {
    DWORD code = GetLastError();
    snprintf(hml_dl_error_buf, sizeof(hml_dl_error_buf),
             "%s '%s' failed (error %lu)", what, name ? name : "?", (unsigned long)code);
    hml_dl_error_set = 1;
}

void *dlopen(const char *filename, int flags) {
    (void)flags;
    HMODULE handle;
    if (filename == NULL) {
        handle = GetModuleHandle(NULL);
    } else {
        handle = LoadLibraryA(filename);
    }
    if (!handle) {
        hml_dl_set_error("LoadLibrary", filename);
    }
    return (void *)handle;
}

void *dlsym(void *handle, const char *symbol) {
    FARPROC addr = GetProcAddress((HMODULE)handle, symbol);
    if (!addr) {
        hml_dl_set_error("GetProcAddress", symbol);
    }
    // Function pointer to object pointer: required by the dlsym contract
    union { FARPROC fp; void *p; } cast;
    cast.fp = addr;
    return cast.p;
}

int dlclose(void *handle) {
    if (handle == GetModuleHandle(NULL)) {
        return 0;
    }
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}

char *dlerror(void) {
    if (!hml_dl_error_set) {
        return NULL;
    }
    hml_dl_error_set = 0;
    return hml_dl_error_buf;
}

// ---- mmap emulation (mman-win32 style) ----
//
// File-backed mappings use CreateFileMapping/MapViewOfFile; anonymous
// mappings use VirtualAlloc. A small registry remembers which strategy
// (and which mapping handle) each address used so munmap can undo it.

#include <pthread.h>
#include <stdlib.h>

typedef struct HmlMapping {
    void *addr;
    HANDLE mapping;          // NULL for VirtualAlloc-backed (anonymous)
    struct HmlMapping *next;
} HmlMapping;

static HmlMapping *hml_mappings = NULL;
static pthread_mutex_t hml_mappings_mutex = PTHREAD_MUTEX_INITIALIZER;

static void hml_mapping_register(void *addr, HANDLE mapping) {
    HmlMapping *m = malloc(sizeof(HmlMapping));
    if (!m) return;
    m->addr = addr;
    m->mapping = mapping;
    pthread_mutex_lock(&hml_mappings_mutex);
    m->next = hml_mappings;
    hml_mappings = m;
    pthread_mutex_unlock(&hml_mappings_mutex);
}

// Removes the entry for addr; returns 1 if found (with *mapping set)
static int hml_mapping_take(void *addr, HANDLE *mapping) {
    pthread_mutex_lock(&hml_mappings_mutex);
    HmlMapping **pp = &hml_mappings;
    while (*pp) {
        if ((*pp)->addr == addr) {
            HmlMapping *m = *pp;
            *pp = m->next;
            *mapping = m->mapping;
            free(m);
            pthread_mutex_unlock(&hml_mappings_mutex);
            return 1;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&hml_mappings_mutex);
    return 0;
}

static DWORD hml_prot_to_page(int prot) {
    if (prot & PROT_EXEC) {
        return (prot & PROT_WRITE) ? PAGE_EXECUTE_READWRITE
             : (prot & PROT_READ)  ? PAGE_EXECUTE_READ
                                   : PAGE_EXECUTE;
    }
    if (prot & PROT_WRITE) return PAGE_READWRITE;
    if (prot & PROT_READ)  return PAGE_READONLY;
    return PAGE_NOACCESS;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long long offset) {
    (void)addr;
    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (flags & MAP_ANONYMOUS) {
        void *mem = VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE,
                                 hml_prot_to_page(prot));
        if (!mem) {
            errno = ENOMEM;
            return MAP_FAILED;
        }
        hml_mapping_register(mem, NULL);
        return mem;
    }

    HANDLE file = (HANDLE)_get_osfhandle(fd);
    if (file == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return MAP_FAILED;
    }

    DWORD page_prot;
    DWORD view_access;
    if (prot & PROT_WRITE) {
        if (flags & MAP_PRIVATE) {
            page_prot = PAGE_WRITECOPY;
            view_access = FILE_MAP_COPY;
        } else {
            page_prot = PAGE_READWRITE;
            view_access = FILE_MAP_READ | FILE_MAP_WRITE;
        }
    } else {
        page_prot = PAGE_READONLY;
        view_access = FILE_MAP_READ;
    }

    unsigned long long end = (unsigned long long)offset + length;
    HANDLE mapping = CreateFileMappingA(file, NULL, page_prot,
                                        (DWORD)(end >> 32), (DWORD)end, NULL);
    if (!mapping) {
        errno = EACCES;
        return MAP_FAILED;
    }

    void *mem = MapViewOfFile(mapping, view_access,
                              (DWORD)((unsigned long long)offset >> 32),
                              (DWORD)offset, length);
    if (!mem) {
        CloseHandle(mapping);
        errno = EACCES;
        return MAP_FAILED;
    }

    hml_mapping_register(mem, mapping);
    return mem;
}

int munmap(void *addr, size_t length) {
    (void)length;
    HANDLE mapping = NULL;
    if (!hml_mapping_take(addr, &mapping)) {
        errno = EINVAL;
        return -1;
    }
    if (mapping == NULL) {
        // Anonymous: VirtualFree releases the whole allocation
        if (!VirtualFree(addr, 0, MEM_RELEASE)) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }
    BOOL ok = UnmapViewOfFile(addr);
    CloseHandle(mapping);
    if (!ok) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int msync(void *addr, size_t length, int flags) {
    (void)flags;
    if (!FlushViewOfFile(addr, length)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int madvise(void *addr, size_t length, int advice) {
    // Windows has no direct madvise equivalent; accept and ignore hints
    (void)addr; (void)length; (void)advice;
    return 0;
}

int mprotect(void *addr, size_t length, int prot) {
    DWORD old_protect;
    if (!VirtualProtect(addr, length, hml_prot_to_page(prot), &old_protect)) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

// ---- POSIX path/string functions missing from MinGW ----

static void hml_forward_slashes(char *s) {
    for (; *s; s++) {
        if (*s == '\\') *s = '/';
    }
}

char *realpath(const char *path, char *resolved) {
    char buf[MAX_PATH];
    if (_fullpath(buf, path, sizeof(buf)) == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (_access(buf, 0) != 0) {
        errno = ENOENT;
        return NULL;
    }
    hml_forward_slashes(buf);
    if (resolved) {
        // POSIX contract: caller's buffer holds at least PATH_MAX bytes
        strcpy(resolved, buf);
        return resolved;
    }
    return _strdup(buf);
}

char *strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

int hml_get_executable_path(char *buf, size_t size) {
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)size);
    if (n == 0 || n >= size) {
        return 0;
    }
    hml_forward_slashes(buf);
    return 1;
}

hml_ssize_t_compat getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) {
        errno = EINVAL;
        return -1;
    }
    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) return -1;
    }
    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 2 > *n) {
            size_t new_size = *n * 2;
            char *new_buf = realloc(*lineptr, new_size);
            if (!new_buf) return -1;
            *lineptr = new_buf;
            *n = new_size;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n') break;
    }
    if (pos == 0 && c == EOF) {
        return -1;
    }
    (*lineptr)[pos] = '\0';
    return (hml_ssize_t_compat)pos;
}

int mkstemps(char *template_path, int suffixlen) {
    size_t len = strlen(template_path);
    if (suffixlen < 0 || (size_t)suffixlen + 6 > len) {
        errno = EINVAL;
        return -1;
    }
    char *xs = template_path + len - suffixlen - 6;
    if (strncmp(xs, "XXXXXX", 6) != 0) {
        errno = EINVAL;
        return -1;
    }

    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int attempt = 0; attempt < 100; attempt++) {
        unsigned int seed = (unsigned int)GetTickCount64()
                          ^ (unsigned int)GetCurrentProcessId()
                          ^ (unsigned int)(attempt * 2654435761u);
        for (int i = 0; i < 6; i++) {
            seed = seed * 1103515245 + 12345;
            xs[i] = chars[(seed >> 16) % (sizeof(chars) - 1)];
        }
        int fd = _open(template_path,
                       _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                       _S_IREAD | _S_IWRITE);
        if (fd >= 0) {
            return fd;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    errno = EEXIST;
    return -1;
}

FILE *hml_win_tmpfile(void) {
    char dir[MAX_PATH];
    char path[MAX_PATH];
    DWORD n = GetTempPathA(sizeof(dir), dir);
    if (n == 0 || n >= sizeof(dir)) {
        strcpy(dir, ".");
    }
    if (GetTempFileNameA(dir, "hml", 0, path) == 0) {
        return NULL;
    }
    // _O_TEMPORARY: removed automatically when the descriptor closes
    int fd = _open(path, _O_RDWR | _O_BINARY | _O_TEMPORARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) {
        return NULL;
    }
    return _fdopen(fd, "w+b");
}

// ---- setenv/unsetenv over _putenv_s ----

int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name) != NULL) {
        return 0;
    }
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

int unsetenv(const char *name) {
    return _putenv_s(name, "") == 0 ? 0 : -1;
}

// ---- Binary-mode file I/O ----
// Hemlock treats files as byte streams; MSVCRT defaults to text mode
// (CRLF translation), which corrupts byte counts and written data.
static void __attribute__((constructor)) hml_default_binary_mode(void) {
    _fmode = _O_BINARY;
}

// ---- Socket error formatting ----

const char *hml_sock_strerror(int err) {
    static __thread char buf[256];
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, (DWORD)err, 0, buf, sizeof(buf), NULL) == 0) {
        snprintf(buf, sizeof(buf), "socket error %d", err);
    } else {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
            buf[--n] = '\0';
        }
    }
    return buf;
}

// ---- CNG hashing (bcrypt.dll) ----
// Backs the sha1/sha256/sha512/md5 builtins. OpenSSL is typically not
// available for MinGW, but CNG ships with Windows, so hashing works with
// no extra DLLs. ECDSA stays stubbed (CNG signatures are raw r||s while
// the OpenSSL builtins produce DER; no compatible mapping).

#include <bcrypt.h>
#include <limits.h>

int hml_win32_hash(const char *alg, const void *data, size_t len,
                   unsigned char *out, size_t out_cap) {
    const wchar_t *alg_id;
    if (strcmp(alg, "sha256") == 0) {
        alg_id = BCRYPT_SHA256_ALGORITHM;
    } else if (strcmp(alg, "sha512") == 0) {
        alg_id = BCRYPT_SHA512_ALGORITHM;
    } else if (strcmp(alg, "sha1") == 0) {
        alg_id = BCRYPT_SHA1_ALGORITHM;
    } else if (strcmp(alg, "md5") == 0) {
        alg_id = BCRYPT_MD5_ALGORITHM;
    } else {
        return -1;
    }

    BCRYPT_ALG_HANDLE provider = NULL;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&provider, alg_id, NULL, 0))) {
        return -1;
    }

    int result = -1;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD digest_len = 0, got = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(provider, BCRYPT_HASH_LENGTH,
                                          (PUCHAR)&digest_len, sizeof(digest_len),
                                          &got, 0))
        || digest_len == 0 || (size_t)digest_len > out_cap
        || !BCRYPT_SUCCESS(BCryptCreateHash(provider, &hash, NULL, 0, NULL, 0, 0))) {
        goto done;
    }

    // BCryptHashData takes a ULONG byte count; feed huge inputs in chunks
    const unsigned char *p = data;
    size_t remaining = len;
    do {
        ULONG chunk = remaining > ULONG_MAX ? ULONG_MAX : (ULONG)remaining;
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, (PUCHAR)p, chunk, 0))) {
            goto done;
        }
        p += chunk;
        remaining -= chunk;
    } while (remaining > 0);

    if (BCRYPT_SUCCESS(BCryptFinishHash(hash, out, digest_len, 0))) {
        result = (int)digest_len;
    }

done:
    if (hash) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(provider, 0);
    return result;
}

int hml_win32_random(void *out, size_t len) {
    // BCRYPT_USE_SYSTEM_PREFERRED_RNG: no provider handle needed
    unsigned char *p = out;
    size_t remaining = len;
    while (remaining > 0) {
        ULONG chunk = remaining > ULONG_MAX ? ULONG_MAX : (ULONG)remaining;
        if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, p, chunk,
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            return -1;
        }
        p += chunk;
        remaining -= chunk;
    }
    return 0;
}

// ---- One-time platform initialization ----

void hml_platform_init(void) {
    static int initialized = 0;
    if (!initialized) {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        initialized = 1;
    }
}

#endif // _WIN32
