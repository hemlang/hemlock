/*
 * Hemlock Runtime Library - Memory-Mapped File I/O
 *
 * Implements mmap/munmap/msync/madvise/mprotect operations for compiled code.
 */

#include "../include/hemlock_runtime.h"
#include "builtins_internal.h"
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

// Track mmap metadata so we can munmap and msync correctly
typedef struct MmapEntry {
    void *addr;
    size_t length;
    int fd;          // -1 for anonymous mappings
    struct MmapEntry *next;
} MmapEntry;

static MmapEntry *mmap_list = NULL;
static pthread_mutex_t mmap_mutex = PTHREAD_MUTEX_INITIALIZER;

static void mmap_register(void *addr, size_t length, int fd) {
    MmapEntry *entry = malloc(sizeof(MmapEntry));
    if (!entry) return;
    entry->addr = addr;
    entry->length = length;
    entry->fd = fd;
    pthread_mutex_lock(&mmap_mutex);
    entry->next = mmap_list;
    mmap_list = entry;
    pthread_mutex_unlock(&mmap_mutex);
}

static MmapEntry *mmap_find(void *addr) {
    pthread_mutex_lock(&mmap_mutex);
    MmapEntry *entry = mmap_list;
    while (entry) {
        if (entry->addr == addr) {
            pthread_mutex_unlock(&mmap_mutex);
            return entry;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&mmap_mutex);
    return NULL;
}

static void mmap_unregister(void *addr) {
    pthread_mutex_lock(&mmap_mutex);
    MmapEntry **pp = &mmap_list;
    while (*pp) {
        if ((*pp)->addr == addr) {
            MmapEntry *entry = *pp;
            *pp = entry->next;
            free(entry);
            pthread_mutex_unlock(&mmap_mutex);
            return;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&mmap_mutex);
}

HmlValue hml_builtin_mmap_open(HmlClosureEnv *env, HmlValue path, HmlValue mode) {
    (void)env;

    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        hml_runtime_error("mmap_open() expects a string path");
    }

    const char *mode_str = "r";
    if (mode.type == HML_VAL_STRING && mode.as.as_string) {
        HmlString *ms = mode.as.as_string;
        if (ms->length == 2 && ms->data[0] == 'r' && ms->data[1] == 'w') {
            mode_str = "rw";
        } else if (ms->length == 1 && ms->data[0] == 'r') {
            mode_str = "r";
        } else {
            hml_runtime_error("mmap_open() mode must be \"r\" or \"rw\"");
        }
    }

    HmlString *path_str = path.as.as_string;
    char *cpath = malloc(path_str->length + 1);
    if (!cpath) {
        hml_runtime_error("mmap_open() memory allocation failed");
    }
    memcpy(cpath, path_str->data, path_str->length);
    cpath[path_str->length] = '\0';

    int open_flags = (strcmp(mode_str, "rw") == 0) ? O_RDWR : O_RDONLY;
    int fd = open(cpath, open_flags);
    if (fd < 0) {
        fprintf(stderr, "Error: mmap_open() failed to open '%s': %s\n", cpath, strerror(errno));
        free(cpath);
        return hml_val_null();
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "Error: mmap_open() failed to stat '%s': %s\n", cpath, strerror(errno));
        close(fd);
        free(cpath);
        return hml_val_null();
    }

    if (st.st_size == 0) {
        fprintf(stderr, "Error: mmap_open() cannot map empty file '%s'\n", cpath);
        close(fd);
        free(cpath);
        return hml_val_null();
    }

    int prot = PROT_READ;
    int flags = MAP_SHARED;
    if (strcmp(mode_str, "rw") == 0) {
        prot = PROT_READ | PROT_WRITE;
    }

    void *addr = mmap(NULL, st.st_size, prot, flags, fd, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "Error: mmap_open() mmap failed for '%s': %s\n", cpath, strerror(errno));
        close(fd);
        free(cpath);
        return hml_val_null();
    }

    free(cpath);
    mmap_register(addr, st.st_size, fd);
    return hml_val_ptr(addr);
}

HmlValue hml_builtin_mmap_open_anon(HmlClosureEnv *env, HmlValue size) {
    (void)env;

    if (!hml_is_integer(size)) {
        hml_runtime_error("mmap_open_anon() expects an integer size");
    }

    int64_t sz = hml_val_to_int64(size);
    if (sz <= 0) {
        hml_runtime_error("mmap_open_anon() size must be positive");
    }

    void *addr = mmap(NULL, (size_t)sz, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "Error: mmap_open_anon() mmap failed: %s\n", strerror(errno));
        return hml_val_null();
    }

    mmap_register(addr, (size_t)sz, -1);
    return hml_val_ptr(addr);
}

HmlValue hml_builtin_mmap_sync(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("mmap_sync() expects a pointer argument");
    }

    void *addr = ptr.as.as_ptr;
    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        hml_runtime_error("mmap_sync() pointer is not a known mmap region");
    }

    int result = msync(addr, entry->length, MS_SYNC);
    return hml_val_bool(result == 0);
}

HmlValue hml_builtin_mmap_close(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("mmap_close() expects a pointer argument");
    }

    void *addr = ptr.as.as_ptr;
    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        hml_runtime_error("mmap_close() pointer is not a known mmap region");
    }

    size_t length = entry->length;
    int fd = entry->fd;

    int result = munmap(addr, length);
    if (result < 0) {
        fprintf(stderr, "Error: mmap_close() munmap failed: %s\n", strerror(errno));
        return hml_val_bool(0);
    }

    if (fd >= 0) {
        close(fd);
    }

    mmap_unregister(addr);
    return hml_val_bool(1);
}

HmlValue hml_builtin_mmap_size(HmlClosureEnv *env, HmlValue ptr) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("mmap_size() expects a pointer argument");
    }

    void *addr = ptr.as.as_ptr;
    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        hml_runtime_error("mmap_size() pointer is not a known mmap region");
    }

    return hml_val_i64((int64_t)entry->length);
}

HmlValue hml_builtin_mmap_advise(HmlClosureEnv *env, HmlValue ptr, HmlValue advice) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("mmap_advise() expects a pointer as first argument");
    }

    if (!hml_is_integer(advice)) {
        hml_runtime_error("mmap_advise() expects an integer advice");
    }

    void *addr = ptr.as.as_ptr;
    int adv = (int)hml_val_to_int64(advice);

    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        hml_runtime_error("mmap_advise() pointer is not a known mmap region");
    }

    int result = madvise(addr, entry->length, adv);
    return hml_val_bool(result == 0);
}

HmlValue hml_builtin_mmap_protect(HmlClosureEnv *env, HmlValue ptr, HmlValue prot) {
    (void)env;

    if (ptr.type != HML_VAL_PTR) {
        hml_runtime_error("mmap_protect() expects a pointer as first argument");
    }

    if (!hml_is_integer(prot)) {
        hml_runtime_error("mmap_protect() expects an integer protection flag");
    }

    void *addr = ptr.as.as_ptr;
    int protection = (int)hml_val_to_int64(prot);

    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        hml_runtime_error("mmap_protect() pointer is not a known mmap region");
    }

    int result = mprotect(addr, entry->length, protection);
    return hml_val_bool(result == 0);
}
