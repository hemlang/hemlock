/*
 * Hemlock Runtime Library - I/O Builtins
 *
 * This file implements:
 * - Basic I/O operations (read_line)
 * - File I/O (open, read, write, seek, close)
 * - System info (platform, arch, hostname, etc.)
 * - Filesystem operations (read_file, write_file, exists, stat)
 * - Directory operations (list_dir, mkdir, rmdir)
 */

#include "builtins_internal.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// ========== I/O OPERATIONS ==========

HmlValue hml_read_line(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t read = getline(&line, &len, stdin);
    if (read == -1) {
        free(line);
        return hml_val_null();
    }
    // Remove trailing newline
    if (read > 0 && line[read - 1] == '\n') {
        line[read - 1] = '\0';
        read--;
    }
    HmlValue result = hml_val_string(line);
    free(line);
    return result;
}

// ========== FILE I/O ==========

HmlValue hml_open(HmlValue path, HmlValue mode) {
    if (path.type != HML_VAL_STRING) {
        hml_throw(hml_val_string("open() expects string path"));
    }

    const char *path_str = path.as.as_string->data;
    const char *mode_str = "r";

    if (mode.type == HML_VAL_STRING) {
        mode_str = mode.as.as_string->data;
    }

    // SANDBOX: Check file access permissions
    // Modes with write access: w, a, r+, w+, a+
    int is_write = (strchr(mode_str, 'w') != NULL || strchr(mode_str, 'a') != NULL ||
                    strstr(mode_str, "r+") != NULL);
    if (!hml_sandbox_path_allowed(path_str, is_write)) {
        if (is_write) {
            hml_sandbox_error("file write operations");
        } else {
            hml_sandbox_error("file read outside sandbox root");
        }
    }

    FILE *fp = fopen(path_str, mode_str);
    if (!fp) {
        char err_buf[512];
        snprintf(err_buf, sizeof(err_buf), "Failed to open '%s' with mode '%s': %s",
                path_str, mode_str, strerror(errno));
        hml_throw(hml_val_string(err_buf));
    }

    HmlFileHandle *fh = malloc(sizeof(HmlFileHandle));
    fh->fp = fp;
    fh->path = strdup(path_str);
    fh->mode = strdup(mode_str);
    fh->closed = 0;

    HmlValue result;
    result.type = HML_VAL_FILE;
    result.as.as_file = fh;
    return result;
}

// Translate a fopen-style mode string to open(2) flags. Returns -1 on
// unrecognized mode.
static int hml_mode_to_open_flags(const char *mode) {
    if (!mode || !*mode) return -1;
    int has_plus = strchr(mode, '+') != NULL;
    int flags;
    switch (mode[0]) {
        case 'r': flags = has_plus ? O_RDWR : O_RDONLY; break;
        case 'w': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
        case 'a': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
        default: return -1;
    }
    return flags;
}

// open_fd(path, mode?) -> i32: open a file and return its raw POSIX fd.
HmlValue hml_open_fd(HmlValue path, HmlValue mode) {
    if (path.type != HML_VAL_STRING) {
        hml_throw(hml_val_string("open_fd() expects string path"));
    }

    const char *path_str = path.as.as_string->data;
    const char *mode_str = "r";
    if (mode.type == HML_VAL_STRING) {
        mode_str = mode.as.as_string->data;
    }

    int flags = hml_mode_to_open_flags(mode_str);
    if (flags < 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf),
                 "open_fd() unsupported mode '%s' (use r, w, a, r+, w+, a+)", mode_str);
        hml_throw(hml_val_string(err_buf));
    }

    int is_write = (strchr(mode_str, 'w') != NULL || strchr(mode_str, 'a') != NULL ||
                    strstr(mode_str, "r+") != NULL);
    if (!hml_sandbox_path_allowed(path_str, is_write)) {
        if (is_write) {
            hml_sandbox_error("file write operations");
        } else {
            hml_sandbox_error("file read outside sandbox root");
        }
    }

    int fd = open(path_str, flags, 0666);
    if (fd < 0) {
        char err_buf[512];
        snprintf(err_buf, sizeof(err_buf), "Failed to open '%s' with mode '%s': %s",
                 path_str, mode_str, strerror(errno));
        hml_throw(hml_val_string(err_buf));
    }

    return hml_val_i32(fd);
}

// fileno(file) -> i32: extract the raw POSIX fd from a File handle. The
// file retains ownership of the fd; closing the file closes the fd.
HmlValue hml_fileno(HmlValue file) {
    if (file.type != HML_VAL_FILE || !file.as.as_file) {
        hml_throw(hml_val_string("fileno() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed || !fh->fp) {
        char err_buf[512];
        snprintf(err_buf, sizeof(err_buf), "fileno() called on closed file '%s'",
                 fh->path ? fh->path : "<unknown>");
        hml_throw(hml_val_string(err_buf));
    }

    int fd = fileno((FILE*)fh->fp);
    if (fd < 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "fileno() failed: %s", strerror(errno));
        hml_throw(hml_val_string(err_buf));
    }

    return hml_val_i32(fd);
}

HmlValue hml_file_read(HmlValue file, HmlValue size) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("read() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot read from closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    int32_t read_size = 0;
    if (size.type == HML_VAL_I32) {
        read_size = size.as.as_i32;
    } else if (size.type == HML_VAL_I64) {
        read_size = (int32_t)size.as.as_i64;
    }

    if (read_size == 0) { return hml_val_string(""); }
    if (read_size < 0) { return hml_file_read_all(file); }

    char *buffer = malloc(read_size + 1);
    size_t bytes_read = fread(buffer, 1, read_size, (FILE*)fh->fp);
    buffer[bytes_read] = '\0';

    HmlValue result = hml_val_string(buffer);
    free(buffer);
    return result;
}

HmlValue hml_file_read_all(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("read() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot read from closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    FILE *fp = (FILE*)fh->fp;
    long start_pos = ftell(fp);

    // Try the seek/size fast path first, but if it reports size 0 fall
    // through to chunked reading. /proc and /sys pseudo-files appear
    // seekable and have st_size == 0 yet contain real data, so trusting the
    // seek-based size silently returns "" for them. Real empty files just
    // read 0 bytes via the chunked path -- same result, no harm.
    long size = -1;
    int is_seekable = (start_pos != -1 && fseek(fp, 0, SEEK_END) == 0);
    if (is_seekable) {
        long end_pos = ftell(fp);
        fseek(fp, start_pos, SEEK_SET);
        if (end_pos >= 0) size = end_pos - start_pos;
    }

    if (size > 0) {
        char *buffer = malloc(size + 1);
        if (!buffer) {
            hml_throw(hml_val_string("Memory allocation failed"));
        }
        size_t bytes_read = fread(buffer, 1, size, fp);
        buffer[bytes_read] = '\0';

        HmlValue result = hml_val_string(buffer);
        free(buffer);
        return result;
    } else {
        // Non-seekable stream (stdin, pipe, socket): read in chunks
        size_t capacity = 4096;
        size_t total_read = 0;
        char *buffer = malloc(capacity);
        if (!buffer) {
            hml_throw(hml_val_string("Memory allocation failed"));
        }

        while (1) {
            // Ensure we have room to read
            if (total_read + 4096 > capacity) {
                capacity *= 2;
                char *new_buffer = realloc(buffer, capacity);
                if (!new_buffer) {
                    free(buffer);
                    hml_throw(hml_val_string("Memory allocation failed"));
                }
                buffer = new_buffer;
            }

            size_t bytes = fread(buffer + total_read, 1, 4096, fp);
            total_read += bytes;

            if (bytes < 4096) {
                // EOF or error
                if (ferror(fp)) {
                    free(buffer);
                    char _err_buf[512];
                    snprintf(_err_buf, sizeof(_err_buf), "Read error on file '%s'", fh->path);
                    hml_throw(hml_val_string(_err_buf));
                }
                break;  // EOF reached
            }
        }

        buffer[total_read] = '\0';

        HmlValue result = hml_val_string(buffer);
        free(buffer);
        return result;
    }
}

HmlValue hml_file_write(HmlValue file, HmlValue data) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("write() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot write to closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    const char *str = "";
    if (data.type == HML_VAL_STRING) {
        str = data.as.as_string->data;
    }

    size_t bytes_written = fwrite(str, 1, strlen(str), (FILE*)fh->fp);
    return hml_val_i32((int32_t)bytes_written);
}

HmlValue hml_file_seek(HmlValue file, HmlValue position) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("seek() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot seek in closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    long pos = 0;
    if (position.type == HML_VAL_I32) {
        pos = position.as.as_i32;
    } else if (position.type == HML_VAL_I64) {
        pos = (long)position.as.as_i64;
    }

    fseek((FILE*)fh->fp, pos, SEEK_SET);
    return hml_val_i32((int32_t)ftell((FILE*)fh->fp));
}

HmlValue hml_file_tell(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("tell() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot tell position in closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    return hml_val_i32((int32_t)ftell((FILE*)fh->fp));
}

void hml_file_close(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        return;
    }

    HmlFileHandle *fh = file.as.as_file;
    if (!fh->closed) {
        fclose((FILE*)fh->fp);
        fh->closed = 1;
    }
}

HmlValue hml_file_read_bytes(HmlValue file, HmlValue size) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("read_bytes() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot read from closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    int32_t read_size = 0;
    if (size.type == HML_VAL_I32) {
        read_size = size.as.as_i32;
    } else if (size.type == HML_VAL_I64) {
        read_size = (int32_t)size.as.as_i64;
    } else {
        hml_throw(hml_val_string("read_bytes() expects integer size argument"));
    }

    if (read_size <= 0) {
        // Return empty buffer
        HmlBuffer *buf = calloc(1, sizeof(HmlBuffer));
        buf->data = malloc(1);
        buf->length = 0;
        buf->capacity = 0;
        buf->ref_count = 1;
        atomic_store(&buf->freed, 0);
        buf->parent = NULL;
        return (HmlValue){ .type = HML_VAL_BUFFER, .as.as_buffer = buf };
    }

    void *data = malloc(read_size);
    if (!data) {
        hml_throw(hml_val_string("Memory allocation failed in read_bytes()"));
    }

    size_t bytes_read = fread(data, 1, read_size, (FILE*)fh->fp);

    if (ferror((FILE*)fh->fp)) {
        free(data);
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Read error on file '%s': %s", fh->path, strerror(errno));
        hml_throw(hml_val_string(_err_buf));
    }

    HmlBuffer *buf = calloc(1, sizeof(HmlBuffer));
    buf->data = data;
    buf->length = (int)bytes_read;
    buf->capacity = read_size;
    buf->ref_count = 1;
    atomic_store(&buf->freed, 0);
    buf->parent = NULL;

    return (HmlValue){ .type = HML_VAL_BUFFER, .as.as_buffer = buf };
}

HmlValue hml_file_read_binary_all(HmlValue file) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("read_binary() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot read from closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    FILE *fp = (FILE*)fh->fp;
    long start_pos = ftell(fp);
    long size = -1;
    int is_seekable = (start_pos != -1 && fseek(fp, 0, SEEK_END) == 0);
    if (is_seekable) {
        long end_pos = ftell(fp);
        fseek(fp, start_pos, SEEK_SET);
        if (end_pos >= 0) size = end_pos - start_pos;
    }

    void *data;
    size_t total_read;

    if (size > 0) {
        data = malloc(size);
        if (!data) hml_throw(hml_val_string("Memory allocation failed"));
        total_read = fread(data, 1, size, fp);
        if (ferror(fp)) {
            free(data);
            char _err_buf[512];
            snprintf(_err_buf, sizeof(_err_buf), "Read error on file '%s'", fh->path);
            hml_throw(hml_val_string(_err_buf));
        }
    } else {
        size_t capacity = 4096;
        total_read = 0;
        data = malloc(capacity);
        if (!data) hml_throw(hml_val_string("Memory allocation failed"));

        while (1) {
            if (total_read + 4096 > capacity) {
                capacity *= 2;
                void *new_data = realloc(data, capacity);
                if (!new_data) {
                    free(data);
                    hml_throw(hml_val_string("Memory allocation failed"));
                }
                data = new_data;
            }
            size_t bytes = fread((char *)data + total_read, 1, 4096, fp);
            total_read += bytes;
            if (bytes < 4096) {
                if (ferror(fp)) {
                    free(data);
                    char _err_buf[512];
                    snprintf(_err_buf, sizeof(_err_buf), "Read error on file '%s'", fh->path);
                    hml_throw(hml_val_string(_err_buf));
                }
                break;
            }
        }
    }

    HmlBuffer *buf = calloc(1, sizeof(HmlBuffer));
    buf->data = data;
    buf->length = (int)total_read;
    buf->capacity = (int)(total_read > 0 ? total_read : 1);
    buf->ref_count = 1;
    atomic_store(&buf->freed, 0);
    buf->parent = NULL;
    return (HmlValue){ .type = HML_VAL_BUFFER, .as.as_buffer = buf };
}

HmlValue hml_file_read_binary(HmlValue file, HmlValue size) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("read_binary() expects file object"));
    }

    int32_t read_size = 0;
    if (size.type == HML_VAL_I32) {
        read_size = size.as.as_i32;
    } else if (size.type == HML_VAL_I64) {
        read_size = (int32_t)size.as.as_i64;
    }

    if (read_size < 0) { return hml_file_read_binary_all(file); }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot read from closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    if (read_size == 0) {
        HmlBuffer *buf = calloc(1, sizeof(HmlBuffer));
        buf->data = malloc(1);
        buf->length = 0;
        buf->capacity = 0;
        buf->ref_count = 1;
        atomic_store(&buf->freed, 0);
        buf->parent = NULL;
        return (HmlValue){ .type = HML_VAL_BUFFER, .as.as_buffer = buf };
    }

    void *data = malloc(read_size);
    if (!data) hml_throw(hml_val_string("Memory allocation failed in read_binary()"));

    size_t bytes_read = fread(data, 1, read_size, (FILE*)fh->fp);
    if (ferror((FILE*)fh->fp)) {
        free(data);
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Read error on file '%s': %s", fh->path, strerror(errno));
        hml_throw(hml_val_string(_err_buf));
    }

    HmlBuffer *buf = calloc(1, sizeof(HmlBuffer));
    buf->data = data;
    buf->length = (int)bytes_read;
    buf->capacity = read_size;
    buf->ref_count = 1;
    atomic_store(&buf->freed, 0);
    buf->parent = NULL;
    return (HmlValue){ .type = HML_VAL_BUFFER, .as.as_buffer = buf };
}

HmlValue hml_file_write_bytes(HmlValue file, HmlValue data) {
    if (file.type != HML_VAL_FILE) {
        hml_throw(hml_val_string("write_bytes() expects file object"));
    }

    HmlFileHandle *fh = file.as.as_file;
    if (fh->closed) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot write to closed file '%s'", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    // Check if file is writable
    if (fh->mode[0] == 'r' && strchr(fh->mode, '+') == NULL) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Cannot write to file '%s' opened in read-only mode", fh->path);
        hml_throw(hml_val_string(_err_buf));
    }

    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_throw(hml_val_string("write_bytes() expects buffer argument"));
    }

    HmlBuffer *buf = data.as.as_buffer;
    size_t written = fwrite(buf->data, 1, buf->length, (FILE*)fh->fp);

    if (ferror((FILE*)fh->fp)) {
        char _err_buf[512];
        snprintf(_err_buf, sizeof(_err_buf), "Write error on file '%s': %s", fh->path, strerror(errno));
        hml_throw(hml_val_string(_err_buf));
    }

    return hml_val_i32((int32_t)written);
}

// ========== SYSTEM INFO OPERATIONS ==========

HmlValue hml_platform(void) {
#ifdef __EMSCRIPTEN__
    return hml_val_string("wasm");
#elif defined(__linux__)
    return hml_val_string("linux");
#elif defined(__APPLE__)
    return hml_val_string("macos");
#elif defined(_WIN32) || defined(_WIN64)
    return hml_val_string("windows");
#else
    return hml_val_string("unknown");
#endif
}

#ifdef __EMSCRIPTEN__
HmlValue hml_arch(void) {
    return hml_val_string("wasm32");
}
#else
HmlValue hml_arch(void) {
    struct utsname info;
    if (uname(&info) != 0) {
        fprintf(stderr, "Error: arch() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(info.machine);
}
#endif

HmlValue hml_hostname(void) {
#ifdef __EMSCRIPTEN__
    return hml_val_string("wasm-host");
#else
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        fprintf(stderr, "Error: hostname() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(hostname);
#endif
}

HmlValue hml_username(void) {
#ifdef __EMSCRIPTEN__
    char *env_user = getenv("USER");
    if (env_user != NULL) {
        return hml_val_string(env_user);
    }
    return hml_val_string("wasm-user");
#else
    // Try getlogin_r first
    char username[256];
    if (getlogin_r(username, sizeof(username)) == 0) {
        return hml_val_string(username);
    }

    // Fall back to getpwuid
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_name != NULL) {
        return hml_val_string(pw->pw_name);
    }

    // Fall back to environment variable
    char *env_user = getenv("USER");
    if (env_user != NULL) {
        return hml_val_string(env_user);
    }

    fprintf(stderr, "Error: username() failed: could not determine username\n");
    exit(1);
#endif
}

HmlValue hml_homedir(void) {
    // Try HOME environment variable first
    char *home = getenv("HOME");
    if (home != NULL) {
        return hml_val_string(home);
    }

#ifndef __EMSCRIPTEN__
    // Fall back to getpwuid (not available in WASM)
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL) {
        return hml_val_string(pw->pw_dir);
    }
#endif

    return hml_val_string("/home");
}

HmlValue hml_cpu_count(void) {
#ifdef __EMSCRIPTEN__
    return hml_val_i32(1);  // WASM runs single-threaded
#else
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs < 1) {
        nprocs = 1;  // Default to 1 if we can't determine
    }
    return hml_val_i32((int32_t)nprocs);
#endif
}

HmlValue hml_total_memory(void) {
#ifdef __EMSCRIPTEN__
    // Query actual WASM heap size via Emscripten
    size_t heap_size = (size_t)EM_ASM_INT({ return HEAP8.length; });
    return hml_val_i64((int64_t)heap_size);
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        fprintf(stderr, "Error: total_memory() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_i64((int64_t)info.totalram * (int64_t)info.mem_unit);
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    int64_t memsize;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) != 0) {
        fprintf(stderr, "Error: total_memory() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_i64(memsize);
#else
    // Fallback: use sysconf
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages < 0 || page_size < 0) {
        fprintf(stderr, "Error: total_memory() failed: could not determine memory\n");
        exit(1);
    }
    return hml_val_i64((int64_t)pages * (int64_t)page_size);
#endif
}

HmlValue hml_free_memory(void) {
#ifdef __EMSCRIPTEN__
    // Estimate free memory as total heap minus dynamically used
    size_t heap_size = (size_t)EM_ASM_INT({ return HEAP8.length; });
    // sbrk(0) returns current break — approximate used heap
    size_t used = (size_t)sbrk(0);
    int64_t free_mem = (int64_t)heap_size - (int64_t)used;
    if (free_mem < 0) free_mem = 0;
    return hml_val_i64(free_mem);
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        fprintf(stderr, "Error: free_memory() failed: %s\n", strerror(errno));
        exit(1);
    }
    int64_t free_mem = (int64_t)info.freeram * (int64_t)info.mem_unit;
    int64_t buffers = (int64_t)info.bufferram * (int64_t)info.mem_unit;
    return hml_val_i64(free_mem + buffers);
#elif defined(__APPLE__)
    // Use vm_statistics to get free memory on macOS
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

    host_page_size(mach_host_self(), &page_size);
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         (host_info64_t)&vm_stat, &count) != KERN_SUCCESS) {
        // Fallback: return 10% of total memory
        int mib[2] = {CTL_HW, HW_MEMSIZE};
        int64_t memsize;
        size_t len = sizeof(memsize);
        sysctl(mib, 2, &memsize, &len, NULL, 0);
        return hml_val_i64(memsize / 10);
    }
    // Free + inactive pages
    int64_t free_mem = (int64_t)(vm_stat.free_count + vm_stat.inactive_count) * (int64_t)page_size;
    return hml_val_i64(free_mem);
#else
    long avail_pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (avail_pages < 0 || page_size < 0) {
        fprintf(stderr, "Error: free_memory() failed: could not determine free memory\n");
        exit(1);
    }
    return hml_val_i64((int64_t)avail_pages * (int64_t)page_size);
#endif
}

#ifdef __EMSCRIPTEN__
HmlValue hml_os_version(void) {
    return hml_val_string("1.0");
}
HmlValue hml_os_name(void) {
    return hml_val_string("Emscripten");
}
#else
HmlValue hml_os_version(void) {
    struct utsname info;
    if (uname(&info) != 0) {
        fprintf(stderr, "Error: os_version() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(info.release);
}

HmlValue hml_os_name(void) {
    struct utsname info;
    if (uname(&info) != 0) {
        fprintf(stderr, "Error: os_name() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(info.sysname);
}
#endif

HmlValue hml_tmpdir(void) {
#ifdef __EMSCRIPTEN__
    // Emscripten MEMFS provides /tmp by default
    return hml_val_string("/tmp");
#else
    char *tmpdir = getenv("TMPDIR");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return hml_val_string(tmpdir);
    }
    tmpdir = getenv("TMP");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return hml_val_string(tmpdir);
    }
    tmpdir = getenv("TEMP");
    if (tmpdir != NULL && tmpdir[0] != '\0') {
        return hml_val_string(tmpdir);
    }
    return hml_val_string("/tmp");
#endif
}

HmlValue hml_uptime(void) {
#ifdef __EMSCRIPTEN__
    // Return time since page load in seconds (via emscripten_get_now in ms)
    double ms = emscripten_get_now();
    return hml_val_i64((int64_t)(ms / 1000.0));
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        fprintf(stderr, "Error: uptime() failed: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_i64((int64_t)info.uptime);
#elif defined(__APPLE__)
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(mib, 2, &boottime, &len, NULL, 0) != 0) {
        fprintf(stderr, "Error: uptime() failed: %s\n", strerror(errno));
        exit(1);
    }
    time_t now = time(NULL);
    return hml_val_i64((int64_t)(now - boottime.tv_sec));
#else
    fprintf(stderr, "Error: uptime() not supported on this platform\n");
    exit(1);
#endif
}

// File I/O builtin wrappers (for indirect calls through HmlValue function refs)
HmlValue hml_builtin_open_fd(HmlClosureEnv *env, HmlValue path, HmlValue mode) {
    (void)env;
    return hml_open_fd(path, mode);
}

HmlValue hml_builtin_fileno(HmlClosureEnv *env, HmlValue file) {
    (void)env;
    return hml_fileno(file);
}

// System info builtin wrappers
HmlValue hml_builtin_platform(HmlClosureEnv *env) {
    (void)env;
    return hml_platform();
}

HmlValue hml_builtin_arch(HmlClosureEnv *env) {
    (void)env;
    return hml_arch();
}

HmlValue hml_builtin_hostname(HmlClosureEnv *env) {
    (void)env;
    return hml_hostname();
}

HmlValue hml_builtin_username(HmlClosureEnv *env) {
    (void)env;
    return hml_username();
}

HmlValue hml_builtin_homedir(HmlClosureEnv *env) {
    (void)env;
    return hml_homedir();
}

HmlValue hml_builtin_cpu_count(HmlClosureEnv *env) {
    (void)env;
    return hml_cpu_count();
}

HmlValue hml_builtin_total_memory(HmlClosureEnv *env) {
    (void)env;
    return hml_total_memory();
}

HmlValue hml_builtin_free_memory(HmlClosureEnv *env) {
    (void)env;
    return hml_free_memory();
}

HmlValue hml_builtin_os_version(HmlClosureEnv *env) {
    (void)env;
    return hml_os_version();
}

HmlValue hml_builtin_os_name(HmlClosureEnv *env) {
    (void)env;
    return hml_os_name();
}

HmlValue hml_builtin_tmpdir(HmlClosureEnv *env) {
    (void)env;
    return hml_tmpdir();
}

HmlValue hml_builtin_uptime(HmlClosureEnv *env) {
    (void)env;
    return hml_uptime();
}

// ========== FILESYSTEM OPERATIONS ==========

HmlValue hml_exists(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        return hml_val_bool(0);
    }
    struct stat st;
    return hml_val_bool(stat(path.as.as_string->data, &st) == 0);
}

HmlValue hml_read_file(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: read_file() requires a string path\n");
        exit(1);
    }

    // SANDBOX: Check file read permission
    if (!hml_sandbox_path_allowed(path.as.as_string->data, 0)) {
        hml_sandbox_error("file read outside sandbox root");
    }

    FILE *fp = fopen(path.as.as_string->data, "r");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open '%s': %s\n", path.as.as_string->data, strerror(errno));
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        fprintf(stderr, "Error: read_file() memory allocation failed\n");
        exit(1);
    }

    size_t read_size = fread(buffer, 1, size, fp);
    buffer[read_size] = '\0';
    fclose(fp);

    HmlValue result = hml_val_string(buffer);
    free(buffer);
    return result;
}

HmlValue hml_write_file(HmlValue path, HmlValue content) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: write_file() requires a string path\n");
        exit(1);
    }

    const char *data;
    size_t length;
    if (content.type == HML_VAL_STRING && content.as.as_string) {
        data = content.as.as_string->data;
        length = content.as.as_string->length;
    } else if (content.type == HML_VAL_BUFFER && content.as.as_buffer) {
        data = (const char *)content.as.as_buffer->data;
        length = content.as.as_buffer->length;
    } else {
        fprintf(stderr, "Error: write_file() requires string or buffer content\n");
        exit(1);
    }

    // SANDBOX: Check file write permission
    if (!hml_sandbox_path_allowed(path.as.as_string->data, 1)) {
        hml_sandbox_error("file write operations");
    }

    // Binary mode preserves all bytes regardless of content type
    FILE *fp = fopen(path.as.as_string->data, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open '%s': %s\n", path.as.as_string->data, strerror(errno));
        exit(1);
    }

    fwrite(data, 1, length, fp);
    fclose(fp);
    return hml_val_null();
}

HmlValue hml_append_file(HmlValue path, HmlValue content) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: append_file() requires a string path\n");
        exit(1);
    }
    if (content.type != HML_VAL_STRING || !content.as.as_string) {
        fprintf(stderr, "Error: append_file() requires string content\n");
        exit(1);
    }

    FILE *fp = fopen(path.as.as_string->data, "a");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open '%s': %s\n", path.as.as_string->data, strerror(errno));
        exit(1);
    }

    fwrite(content.as.as_string->data, 1, content.as.as_string->length, fp);
    fclose(fp);
    return hml_val_null();
}

HmlValue hml_remove_file(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: remove_file() requires a string path\n");
        exit(1);
    }

    if (unlink(path.as.as_string->data) != 0) {
        fprintf(stderr, "Error: Failed to remove '%s': %s\n", path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_rename_file(HmlValue old_path, HmlValue new_path) {
    if (old_path.type != HML_VAL_STRING || !old_path.as.as_string) {
        fprintf(stderr, "Error: rename() requires string old_path\n");
        exit(1);
    }
    if (new_path.type != HML_VAL_STRING || !new_path.as.as_string) {
        fprintf(stderr, "Error: rename() requires string new_path\n");
        exit(1);
    }

    if (rename(old_path.as.as_string->data, new_path.as.as_string->data) != 0) {
        fprintf(stderr, "Error: Failed to rename '%s' to '%s': %s\n",
            old_path.as.as_string->data, new_path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_copy_file(HmlValue src_path, HmlValue dest_path) {
    if (src_path.type != HML_VAL_STRING || !src_path.as.as_string) {
        fprintf(stderr, "Error: copy_file() requires string src_path\n");
        exit(1);
    }
    if (dest_path.type != HML_VAL_STRING || !dest_path.as.as_string) {
        fprintf(stderr, "Error: copy_file() requires string dest_path\n");
        exit(1);
    }

    FILE *src_fp = fopen(src_path.as.as_string->data, "rb");
    if (!src_fp) {
        fprintf(stderr, "Error: Failed to open source '%s': %s\n",
            src_path.as.as_string->data, strerror(errno));
        exit(1);
    }

    FILE *dest_fp = fopen(dest_path.as.as_string->data, "wb");
    if (!dest_fp) {
        fclose(src_fp);
        fprintf(stderr, "Error: Failed to open destination '%s': %s\n",
            dest_path.as.as_string->data, strerror(errno));
        exit(1);
    }

    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src_fp)) > 0) {
        if (fwrite(buffer, 1, n, dest_fp) != n) {
            fclose(src_fp);
            fclose(dest_fp);
            fprintf(stderr, "Error: Failed to write to '%s': %s\n",
                dest_path.as.as_string->data, strerror(errno));
            exit(1);
        }
    }

    fclose(src_fp);
    fclose(dest_fp);
    return hml_val_null();
}

HmlValue hml_is_file(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        return hml_val_bool(0);
    }
    struct stat st;
    if (stat(path.as.as_string->data, &st) != 0) {
        return hml_val_bool(0);
    }
    return hml_val_bool(S_ISREG(st.st_mode));
}

HmlValue hml_is_dir(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        return hml_val_bool(0);
    }
    struct stat st;
    if (stat(path.as.as_string->data, &st) != 0) {
        return hml_val_bool(0);
    }
    return hml_val_bool(S_ISDIR(st.st_mode));
}

HmlValue hml_file_stat(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        hml_throw(hml_val_string("file_stat() requires a string path"));
    }

    struct stat st;
    if (stat(path.as.as_string->data, &st) != 0) {
        char err_buf[512];
        snprintf(err_buf, sizeof(err_buf), "Failed to stat '%s': %s",
            path.as.as_string->data, strerror(errno));
        hml_throw(hml_val_string(err_buf));
    }

    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "size", hml_val_i64(st.st_size));
    hml_object_set_field(obj, "atime", hml_val_i64(st.st_atime));
    hml_object_set_field(obj, "mtime", hml_val_i64(st.st_mtime));
    hml_object_set_field(obj, "ctime", hml_val_i64(st.st_ctime));
    hml_object_set_field(obj, "mode", hml_val_u32(st.st_mode));
    hml_object_set_field(obj, "is_file", hml_val_bool(S_ISREG(st.st_mode)));
    hml_object_set_field(obj, "is_dir", hml_val_bool(S_ISDIR(st.st_mode)));
    return obj;
}

// ========== DIRECTORY OPERATIONS ==========

HmlValue hml_make_dir(HmlValue path, HmlValue mode) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: make_dir() requires a string path\n");
        exit(1);
    }

    uint32_t dir_mode = 0755;  // Default mode
    if (mode.type == HML_VAL_U32) {
        dir_mode = mode.as.as_u32;
    } else if (mode.type == HML_VAL_I32) {
        dir_mode = (uint32_t)mode.as.as_i32;
    }

    if (mkdir(path.as.as_string->data, dir_mode) != 0) {
        fprintf(stderr, "Error: Failed to create directory '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_remove_dir(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: remove_dir() requires a string path\n");
        exit(1);
    }

    if (rmdir(path.as.as_string->data) != 0) {
        fprintf(stderr, "Error: Failed to remove directory '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_list_dir(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: list_dir() requires a string path\n");
        exit(1);
    }

    DIR *dir = opendir(path.as.as_string->data);
    if (!dir) {
        fprintf(stderr, "Error: Failed to open directory '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }

    HmlValue arr = hml_val_array();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        hml_array_push(arr, hml_val_string(entry->d_name));
    }

    closedir(dir);
    return arr;
}

HmlValue hml_cwd(void) {
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        fprintf(stderr, "Error: Failed to get current directory: %s\n", strerror(errno));
        exit(1);
    }
    return hml_val_string(buffer);
}

HmlValue hml_chdir(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: chdir() requires a string path\n");
        exit(1);
    }

    if (chdir(path.as.as_string->data) != 0) {
        fprintf(stderr, "Error: Failed to change directory to '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_null();
}

HmlValue hml_absolute_path(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        fprintf(stderr, "Error: absolute_path() requires a string path\n");
        exit(1);
    }

    char buffer[PATH_MAX];
    if (realpath(path.as.as_string->data, buffer) == NULL) {
        fprintf(stderr, "Error: Failed to resolve path '%s': %s\n",
            path.as.as_string->data, strerror(errno));
        exit(1);
    }
    return hml_val_string(buffer);
}

// ========== FILESYSTEM BUILTIN WRAPPERS ==========

HmlValue hml_builtin_exists(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_exists(path);
}

HmlValue hml_builtin_read_file(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_read_file(path);
}

HmlValue hml_builtin_write_file(HmlClosureEnv *env, HmlValue path, HmlValue content) {
    (void)env;
    return hml_write_file(path, content);
}

HmlValue hml_builtin_append_file(HmlClosureEnv *env, HmlValue path, HmlValue content) {
    (void)env;
    return hml_append_file(path, content);
}

HmlValue hml_builtin_remove_file(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_remove_file(path);
}

HmlValue hml_builtin_rename(HmlClosureEnv *env, HmlValue old_path, HmlValue new_path) {
    (void)env;
    return hml_rename_file(old_path, new_path);
}

HmlValue hml_builtin_copy_file(HmlClosureEnv *env, HmlValue src, HmlValue dest) {
    (void)env;
    return hml_copy_file(src, dest);
}

HmlValue hml_builtin_is_file(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_is_file(path);
}

HmlValue hml_builtin_is_dir(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_is_dir(path);
}

HmlValue hml_builtin_file_stat(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_file_stat(path);
}

HmlValue hml_builtin_make_dir(HmlClosureEnv *env, HmlValue path, HmlValue mode) {
    (void)env;
    return hml_make_dir(path, mode);
}

HmlValue hml_builtin_remove_dir(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_remove_dir(path);
}

HmlValue hml_builtin_list_dir(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_list_dir(path);
}

HmlValue hml_builtin_cwd(HmlClosureEnv *env) {
    (void)env;
    return hml_cwd();
}

HmlValue hml_builtin_chdir(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_chdir(path);
}

HmlValue hml_builtin_absolute_path(HmlClosureEnv *env, HmlValue path) {
    (void)env;
    return hml_absolute_path(path);
}

// Async/concurrency operations moved to builtins_async.c

