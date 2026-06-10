/*
 * Hemlock Interpreter - Memory-Mapped File I/O Builtins
 *
 * Provides mmap/munmap operations for memory-mapped file I/O:
 * - mmap_open: Map a file into memory (read-only or read-write)
 * - mmap_open_anon: Create an anonymous (non-file-backed) mapping
 * - mmap_sync: Flush changes to disk (msync)
 * - mmap_close: Unmap memory region (munmap)
 * - mmap_size: Get the size of a mapped region
 *
 * Memory-mapped regions are returned as raw pointers (ptr) that can be
 * read/written with ptr_read and ptr_write builtins.
 */

#include "internal.h"
#include <sys/mman.h>

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

// mmap_open(path: string, mode: string): ptr
// Modes: "r" (read-only), "rw" (read-write)
// Returns a pointer to the mapped region, or null on failure.
Value builtin_mmap_open(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || num_args > 2) {
        runtime_error(ctx, "mmap_open() expects 1-2 arguments (path, mode?), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "mmap_open() expects a string path, got %s", value_type_name(args[0].type));
        return val_null();
    }

    // Default mode is "r" (read-only)
    const char *mode = "r";
    if (num_args == 2) {
        if (args[1].type != VAL_STRING) {
            runtime_error(ctx, "mmap_open() expects a string mode, got %s", value_type_name(args[1].type));
            return val_null();
        }
        String *mode_str = args[1].as.as_string;
        if (mode_str->length == 1 && mode_str->data[0] == 'r') {
            mode = "r";
        } else if (mode_str->length == 2 && mode_str->data[0] == 'r' && mode_str->data[1] == 'w') {
            mode = "rw";
        } else {
            runtime_error(ctx, "mmap_open() mode must be \"r\" or \"rw\"");
            return val_null();
        }
    }

    String *path = args[0].as.as_string;
    char *cpath = malloc(path->length + 1);
    if (!cpath) {
        runtime_error(ctx, "mmap_open() memory allocation failed");
        return val_null();
    }
    memcpy(cpath, path->data, path->length);
    cpath[path->length] = '\0';

    int is_write = (strcmp(mode, "rw") == 0);

    // SANDBOX: mmap exposes file contents for reading (and, in "rw" mode,
    // writing). Enforce the same policy as read_file/write_file, which this
    // path previously bypassed entirely.
    if (!sandbox_path_allowed(ctx, cpath, is_write)) {
        free(cpath);
        sandbox_error(ctx, is_write ? "file write operations" : "file read operations");
        return val_null();
    }

    // SECURITY: O_NOFOLLOW to prevent symlink redirection (matches read_file).
    int open_flags = (is_write ? O_RDWR : O_RDONLY) | O_NOFOLLOW;
    int fd = open(cpath, open_flags);
    if (fd < 0) {
        runtime_error(ctx, "mmap_open() failed to open '%s': %s", cpath, strerror(errno));
        free(cpath);
        return val_null();
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        runtime_error(ctx, "mmap_open() failed to stat '%s': %s", cpath, strerror(errno));
        close(fd);
        free(cpath);
        return val_null();
    }

    if (st.st_size == 0) {
        runtime_error(ctx, "mmap_open() cannot map empty file '%s'", cpath);
        close(fd);
        free(cpath);
        return val_null();
    }

    int prot = PROT_READ;
    int flags = MAP_SHARED;
    if (strcmp(mode, "rw") == 0) {
        prot = PROT_READ | PROT_WRITE;
    }

    void *addr = mmap(NULL, st.st_size, prot, flags, fd, 0);
    if (addr == MAP_FAILED) {
        runtime_error(ctx, "mmap_open() mmap failed for '%s': %s", cpath, strerror(errno));
        close(fd);
        free(cpath);
        return val_null();
    }

    free(cpath);

    // Keep fd open for msync/munmap; track metadata
    mmap_register(addr, st.st_size, fd);

    return val_ptr(addr);
}

// mmap_open_anon(size: i32|i64): ptr
// Creates an anonymous memory mapping (not backed by a file).
Value builtin_mmap_open_anon(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "mmap_open_anon() expects 1 argument (size), got %d", num_args);
        return val_null();
    }

    if (!is_integer(args[0])) {
        runtime_error(ctx, "mmap_open_anon() expects an integer size, got %s", value_type_name(args[0].type));
        return val_null();
    }

    int64_t size = value_to_int64(args[0]);
    if (size <= 0) {
        runtime_error(ctx, "mmap_open_anon() size must be positive, got %lld", (long long)size);
        return val_null();
    }

    void *addr = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        runtime_error(ctx, "mmap_open_anon() mmap failed: %s", strerror(errno));
        return val_null();
    }

    mmap_register(addr, (size_t)size, -1);

    return val_ptr(addr);
}

// mmap_sync(ptr: ptr): bool
// Flushes changes in a read-write mapped region to disk.
Value builtin_mmap_sync(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "mmap_sync() expects 1 argument (pointer), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "mmap_sync() expects a pointer argument, got %s", value_type_name(args[0].type));
        return val_null();
    }

    void *addr = args[0].as.as_ptr;
    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        runtime_error(ctx, "mmap_sync() pointer is not a known mmap region");
        return val_bool(0);
    }

    int result = msync(addr, entry->length, MS_SYNC);
    if (result < 0) {
        runtime_error(ctx, "mmap_sync() msync failed: %s", strerror(errno));
        return val_bool(0);
    }

    return val_bool(1);
}

// mmap_close(ptr: ptr): bool
// Unmaps a memory-mapped region. Returns true on success.
Value builtin_mmap_close(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "mmap_close() expects 1 argument (pointer), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "mmap_close() expects a pointer argument, got %s", value_type_name(args[0].type));
        return val_null();
    }

    void *addr = args[0].as.as_ptr;
    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        runtime_error(ctx, "mmap_close() pointer is not a known mmap region");
        return val_bool(0);
    }

    size_t length = entry->length;
    int fd = entry->fd;

    int result = munmap(addr, length);
    if (result < 0) {
        runtime_error(ctx, "mmap_close() munmap failed: %s", strerror(errno));
        return val_bool(0);
    }

    // Close the file descriptor if file-backed
    if (fd >= 0) {
        close(fd);
    }

    mmap_unregister(addr);

    return val_bool(1);
}

// mmap_size(ptr: ptr): i64
// Returns the size of a mapped region.
Value builtin_mmap_size(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "mmap_size() expects 1 argument (pointer), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "mmap_size() expects a pointer argument, got %s", value_type_name(args[0].type));
        return val_null();
    }

    void *addr = args[0].as.as_ptr;
    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        runtime_error(ctx, "mmap_size() pointer is not a known mmap region");
        return val_null();
    }

    return val_i64((int64_t)entry->length);
}

// mmap_advise(ptr: ptr, advice: i32): bool
// Provides usage hints to the kernel via madvise().
// Advice constants: MADV_NORMAL, MADV_SEQUENTIAL, MADV_RANDOM, MADV_WILLNEED, MADV_DONTNEED
Value builtin_mmap_advise(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "mmap_advise() expects 2 arguments (pointer, advice), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "mmap_advise() expects a pointer as first argument, got %s", value_type_name(args[0].type));
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "mmap_advise() expects an integer advice, got %s", value_type_name(args[1].type));
        return val_null();
    }

    void *addr = args[0].as.as_ptr;
    int advice = value_to_int(args[1]);

    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        runtime_error(ctx, "mmap_advise() pointer is not a known mmap region");
        return val_bool(0);
    }

    int result = madvise(addr, entry->length, advice);
    if (result < 0) {
        runtime_error(ctx, "mmap_advise() madvise failed: %s", strerror(errno));
        return val_bool(0);
    }

    return val_bool(1);
}

// mmap_protect(ptr: ptr, prot: i32): bool
// Changes protection on a mapped region via mprotect().
// Protection flags: PROT_NONE, PROT_READ, PROT_WRITE, PROT_EXEC
Value builtin_mmap_protect(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "mmap_protect() expects 2 arguments (pointer, protection), got %d", num_args);
        return val_null();
    }

    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "mmap_protect() expects a pointer as first argument, got %s", value_type_name(args[0].type));
        return val_null();
    }

    if (!is_integer(args[1])) {
        runtime_error(ctx, "mmap_protect() expects an integer protection flag, got %s", value_type_name(args[1].type));
        return val_null();
    }

    void *addr = args[0].as.as_ptr;
    int prot = value_to_int(args[1]);

    MmapEntry *entry = mmap_find(addr);
    if (!entry) {
        runtime_error(ctx, "mmap_protect() pointer is not a known mmap region");
        return val_bool(0);
    }

    int result = mprotect(addr, entry->length, prot);
    if (result < 0) {
        runtime_error(ctx, "mmap_protect() mprotect failed: %s", strerror(errno));
        return val_bool(0);
    }

    return val_bool(1);
}
