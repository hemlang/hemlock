/*
 * Hemlock Filesystem Compatibility Layer
 *
 * Provides cross-platform filesystem operations:
 * - Directory operations (opendir/readdir, FindFirstFile/FindNextFile)
 * - File operations (stat, access, etc.)
 * - Path manipulation
 */

#ifndef HML_COMPAT_FILESYSTEM_H
#define HML_COMPAT_FILESYSTEM_H

#include "platform.h"

#ifdef HML_WINDOWS

/* ========== Windows Implementation ========== */

#include <windows.h>
#include <direct.h>
#include <io.h>
#include <sys/stat.h>

/* Directory entry structure */
typedef struct {
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    int first_read;
    char path[MAX_PATH];
} hml_dir_t;

/* Directory entry */
typedef struct {
    char d_name[MAX_PATH];
} hml_dirent_t;

/* Open a directory */
HML_INLINE hml_dir_t *hml_opendir(const char *path) {
    hml_dir_t *dir = (hml_dir_t *)malloc(sizeof(hml_dir_t));
    if (!dir) return NULL;

    /* Build search pattern */
    size_t len = strlen(path);
    if (len + 3 > MAX_PATH) {
        free(dir);
        return NULL;
    }

    strcpy(dir->path, path);

    /* Append \* for search pattern */
    char pattern[MAX_PATH];
    strcpy(pattern, path);
    if (len > 0 && path[len-1] != '\\' && path[len-1] != '/') {
        strcat(pattern, "\\");
    }
    strcat(pattern, "*");

    dir->hFind = FindFirstFileA(pattern, &dir->find_data);
    if (dir->hFind == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }

    dir->first_read = 1;
    return dir;
}

/* Read next directory entry */
HML_INLINE hml_dirent_t *hml_readdir(hml_dir_t *dir) {
    static HML_THREAD_LOCAL hml_dirent_t entry;

    if (dir->first_read) {
        dir->first_read = 0;
        strcpy(entry.d_name, dir->find_data.cFileName);
        return &entry;
    }

    if (FindNextFileA(dir->hFind, &dir->find_data)) {
        strcpy(entry.d_name, dir->find_data.cFileName);
        return &entry;
    }

    return NULL;
}

/* Close directory */
HML_INLINE int hml_closedir(hml_dir_t *dir) {
    if (dir) {
        FindClose(dir->hFind);
        free(dir);
    }
    return 0;
}

/* Create directory */
HML_INLINE int hml_mkdir(const char *path, int mode) {
    (void)mode;  /* Windows doesn't use POSIX mode */
    return CreateDirectoryA(path, NULL) ? 0 : -1;
}

/* Remove directory */
HML_INLINE int hml_rmdir(const char *path) {
    return RemoveDirectoryA(path) ? 0 : -1;
}

/* Get current working directory */
HML_INLINE char *hml_getcwd(char *buf, size_t size) {
    DWORD result = GetCurrentDirectoryA((DWORD)size, buf);
    if (result == 0 || result > size) {
        return NULL;
    }
    return buf;
}

/* Change working directory */
HML_INLINE int hml_chdir(const char *path) {
    return SetCurrentDirectoryA(path) ? 0 : -1;
}

/* File stat structure - use hml_ prefix to avoid macro conflicts */
typedef struct {
    unsigned int hml_mode;
    size_t hml_size;
    time_t hml_mtime;
    time_t hml_atime;
    time_t hml_ctime;
    int is_directory;
    int is_file;
    int is_symlink;
} hml_stat_t;

/* Stat a file */
HML_INLINE int hml_stat(const char *path, hml_stat_t *buf) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr)) {
        return -1;
    }

    buf->hml_mode = 0;
    if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        buf->hml_mode |= 0040000;  /* S_IFDIR */
        buf->is_directory = 1;
        buf->is_file = 0;
    } else {
        buf->hml_mode |= 0100000;  /* S_IFREG */
        buf->is_directory = 0;
        buf->is_file = 1;
    }

    buf->is_symlink = (attr.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0;

    /* File size */
    buf->hml_size = ((size_t)attr.nFileSizeHigh << 32) | attr.nFileSizeLow;

    /* Convert FILETIME to time_t */
    ULARGE_INTEGER ull;
    ull.LowPart = attr.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    buf->hml_mtime = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);

    ull.LowPart = attr.ftLastAccessTime.dwLowDateTime;
    ull.HighPart = attr.ftLastAccessTime.dwHighDateTime;
    buf->hml_atime = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);

    ull.LowPart = attr.ftCreationTime.dwLowDateTime;
    ull.HighPart = attr.ftCreationTime.dwHighDateTime;
    buf->hml_ctime = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);

    return 0;
}

/* Check file access */
HML_INLINE int hml_access(const char *path, int mode) {
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return -1;
    }

    /* F_OK (0) - file exists */
    if (mode == 0) {
        return 0;
    }

    /* R_OK (4) - readable */
    if (mode & 4) {
        /* Windows files are always readable if they exist */
    }

    /* W_OK (2) - writable */
    if (mode & 2) {
        if (attr & FILE_ATTRIBUTE_READONLY) {
            return -1;
        }
    }

    /* X_OK (1) - executable */
    if (mode & 1) {
        /* Check file extension for executability */
        const char *ext = strrchr(path, '.');
        if (!ext) return -1;
        if (_stricmp(ext, ".exe") != 0 &&
            _stricmp(ext, ".cmd") != 0 &&
            _stricmp(ext, ".bat") != 0 &&
            _stricmp(ext, ".com") != 0) {
            return -1;
        }
    }

    return 0;
}

/* Delete a file */
HML_INLINE int hml_unlink(const char *path) {
    return DeleteFileA(path) ? 0 : -1;
}

/* Rename a file */
HML_INLINE int hml_rename(const char *oldpath, const char *newpath) {
    return MoveFileA(oldpath, newpath) ? 0 : -1;
}

/* getline is not available on Windows - provide a compatible implementation */
#ifndef ssize_t
typedef long long ssize_t;
#endif

HML_INLINE ssize_t hml_getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) {
        return -1;
    }

    size_t pos = 0;
    int c;

    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = (char *)malloc(*n);
        if (!*lineptr) {
            return -1;
        }
    }

    while ((c = fgetc(stream)) != EOF) {
        /* Ensure buffer has room for char + null terminator */
        if (pos + 2 > *n) {
            size_t new_size = *n * 2;
            char *new_ptr = (char *)realloc(*lineptr, new_size);
            if (!new_ptr) {
                return -1;
            }
            *lineptr = new_ptr;
            *n = new_size;
        }

        (*lineptr)[pos++] = (char)c;

        if (c == '\n') {
            break;
        }
    }

    if (pos == 0 && c == EOF) {
        return -1;  /* EOF with no data read */
    }

    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

/* Map getline to our implementation */
#define getline hml_getline

/* strndup is not available on Windows - provide a compatible implementation */
HML_INLINE char *hml_strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len] != '\0') {
        len++;
    }
    char *result = (char *)malloc(len + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}

/* Map strndup to our implementation */
#define strndup hml_strndup

/* Access mode constants */
#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#endif

/* Stat mode constants */
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#define S_ISREG(m) (((m) & 0170000) == 0100000)
#define S_ISLNK(m) (0)  /* Simplified - would need to check reparse points */
#endif

/* Resolve a path to an absolute canonical path (Windows) */
HML_INLINE char *hml_realpath(const char *path, char *resolved) {
    char buf[MAX_PATH];

    /* Get full path name */
    DWORD len = GetFullPathNameA(path, MAX_PATH, buf, NULL);
    if (len == 0 || len > MAX_PATH) {
        return NULL;
    }

    /* Check if the file exists */
    DWORD attr = GetFileAttributesA(buf);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return NULL;
    }

    if (resolved) {
        strcpy(resolved, buf);
        return resolved;
    } else {
        return _strdup(buf);
    }
}

/* Get the path to the current executable (Windows) */
HML_INLINE int hml_get_executable_path(char *buf, size_t size) {
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)size);
    if (len == 0 || len >= size) {
        return -1;
    }
    return (int)len;
}

/* dirname implementation for Windows */
HML_INLINE char *hml_dirname(char *path) {
    static HML_THREAD_LOCAL char buf[MAX_PATH];
    if (!path || !*path) {
        strcpy(buf, ".");
        return buf;
    }

    strncpy(buf, path, MAX_PATH - 1);
    buf[MAX_PATH - 1] = '\0';

    /* Find last separator */
    char *last_sep = NULL;
    for (char *p = buf; *p; p++) {
        if (*p == '\\' || *p == '/') {
            last_sep = p;
        }
    }

    if (last_sep) {
        if (last_sep == buf) {
            /* Root directory */
            buf[1] = '\0';
        } else {
            *last_sep = '\0';
        }
    } else {
        /* No separator - current directory */
        strcpy(buf, ".");
        return buf;
    }

    return buf;
}

#else /* POSIX Implementation */

/* ========== POSIX Implementation ========== */

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* Directory types (use POSIX directly) */
typedef DIR hml_dir_t;
typedef struct dirent hml_dirent_t;

/* Directory operations */
HML_INLINE hml_dir_t *hml_opendir(const char *path) {
    return opendir(path);
}

HML_INLINE hml_dirent_t *hml_readdir(hml_dir_t *dir) {
    return readdir(dir);
}

HML_INLINE int hml_closedir(hml_dir_t *dir) {
    return closedir(dir);
}

HML_INLINE int hml_mkdir(const char *path, int mode) {
    return mkdir(path, (mode_t)mode);
}

HML_INLINE int hml_rmdir(const char *path) {
    return rmdir(path);
}

HML_INLINE char *hml_getcwd(char *buf, size_t size) {
    return getcwd(buf, size);
}

HML_INLINE int hml_chdir(const char *path) {
    return chdir(path);
}

/* Stat structure wrapper - use hml_ prefix to avoid macro conflicts */
typedef struct {
    unsigned int hml_mode;
    size_t hml_size;
    time_t hml_mtime;
    time_t hml_atime;
    time_t hml_ctime;
    int is_directory;
    int is_file;
    int is_symlink;
} hml_stat_t;

/* Stat a file */
HML_INLINE int hml_stat(const char *path, hml_stat_t *buf) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    buf->hml_mode = st.st_mode;
    buf->hml_size = st.st_size;
    buf->hml_mtime = st.st_mtime;
    buf->hml_atime = st.st_atime;
    buf->hml_ctime = st.st_ctime;
    buf->is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
    buf->is_file = S_ISREG(st.st_mode) ? 1 : 0;
    buf->is_symlink = S_ISLNK(st.st_mode) ? 1 : 0;

    return 0;
}

/* Check file access */
HML_INLINE int hml_access(const char *path, int mode) {
    return access(path, mode);
}

/* Delete a file */
HML_INLINE int hml_unlink(const char *path) {
    return unlink(path);
}

/* Rename a file */
HML_INLINE int hml_rename(const char *oldpath, const char *newpath) {
    return rename(oldpath, newpath);
}

/* Resolve a path to an absolute canonical path (POSIX) */
HML_INLINE char *hml_realpath(const char *path, char *resolved) {
    return realpath(path, resolved);
}

/* Get the path to the current executable (POSIX) */
HML_INLINE int hml_get_executable_path(char *buf, size_t size) {
#ifdef __APPLE__
    uint32_t bufsize = (uint32_t)size;
    if (_NSGetExecutablePath(buf, &bufsize) == 0) {
        return (int)strlen(buf);
    }
    return -1;
#else
    /* Linux: use /proc/self/exe */
    ssize_t len = readlink("/proc/self/exe", buf, size - 1);
    if (len != -1) {
        buf[len] = '\0';
        return (int)len;
    }
    return -1;
#endif
}

/* dirname wrapper (POSIX) */
HML_INLINE char *hml_dirname(char *path) {
    return dirname(path);
}

#endif /* HML_WINDOWS */

/* ========== Common Path Utilities ========== */

/* Normalize path separators to platform-native */
HML_INLINE void hml_normalize_path(char *path) {
#ifdef HML_WINDOWS
    for (char *p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#else
    for (char *p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
#endif
}

/* Check if path is absolute */
HML_INLINE int hml_path_is_absolute(const char *path) {
#ifdef HML_WINDOWS
    /* Windows: starts with drive letter or UNC path */
    if (path[0] && path[1] == ':') return 1;
    if (path[0] == '\\' && path[1] == '\\') return 1;
    return 0;
#else
    return path[0] == '/';
#endif
}

#endif /* HML_COMPAT_FILESYSTEM_H */
