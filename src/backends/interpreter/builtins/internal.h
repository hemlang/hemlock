#ifndef BUILTINS_INTERNAL_H
#define BUILTINS_INTERNAL_H

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #ifndef HML_WINDOWS
    #define HML_WINDOWS 1
    #endif
#endif

// Define feature test macros before including system headers (POSIX only)
#ifndef HML_WINDOWS
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#endif

#include "../internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>
#include <math.h>
#include <time.h>

// ========== WINDOWS COMPATIBILITY ==========
// Note: winsock2.h and windows.h are included by ../internal.h
#ifdef HML_WINDOWS
    #include <io.h>
    #include <process.h>
    #include <direct.h>
    #include <fcntl.h>  // For O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC, O_APPEND
    #include <sys/stat.h>
    #include <sys/types.h>

    // Windows doesn't have unistd.h - provide compatibility
    #ifndef F_OK
    #define F_OK 0
    #define R_OK 4
    #define W_OK 2
    #define X_OK 1
    #endif

    // Windows doesn't have O_NOFOLLOW
    #ifndef O_NOFOLLOW
    #define O_NOFOLLOW 0
    #endif

    // Windows directory functions
    #define getcwd _getcwd
    #define chdir _chdir
    #define mkdir(path, mode) _mkdir(path)
    #define rmdir _rmdir
    #define access _access

    // Windows file descriptor functions
    #define open _open
    #define close _close
    #define read _read
    #define write _write
    #define dup _dup
    #define dup2 _dup2
    #define fileno _fileno
    #define isatty _isatty
    #define lseek _lseek

    // Windows process functions
    #define getpid _getpid
    #define getppid() 0
    #define getuid() 0
    #define getgid() 0
    #define geteuid() 0
    #define getegid() 0
    #define fork() (-1)

    // Windows doesn't have realpath - provide a simple implementation
    static inline char* realpath(const char *path, char *resolved) {
        if (!resolved) {
            resolved = malloc(MAX_PATH);
            if (!resolved) return NULL;
        }
        if (GetFullPathNameA(path, MAX_PATH, resolved, NULL) == 0) {
            return NULL;
        }
        return resolved;
    }

    // poll() compatibility - use WSAPoll on Windows
    #define poll WSAPoll
    #ifndef POLLIN
    #define POLLIN POLLRDNORM
    #define POLLOUT POLLWRNORM
    #endif

    // ssize_t is provided by sys/types.h on MinGW
    // No need to define it ourselves

    // Directory compatibility types
    typedef struct hml_dirent {
        char d_name[260];  // MAX_PATH
    } hml_dirent_t;

    typedef struct hml_dir {
        HANDLE hFind;
        WIN32_FIND_DATAA data;
        int first;
        hml_dirent_t entry;
    } hml_dir_t;

    static inline hml_dir_t* hml_opendir(const char *path) {
        hml_dir_t *dir = malloc(sizeof(hml_dir_t));
        if (!dir) return NULL;

        char search_path[MAX_PATH];
        snprintf(search_path, MAX_PATH, "%s\\*", path);

        dir->hFind = FindFirstFileA(search_path, &dir->data);
        if (dir->hFind == INVALID_HANDLE_VALUE) {
            free(dir);
            return NULL;
        }
        dir->first = 1;
        return dir;
    }

    static inline hml_dirent_t* hml_readdir(hml_dir_t *dir) {
        if (!dir) return NULL;
        if (dir->first) {
            dir->first = 0;
            strncpy(dir->entry.d_name, dir->data.cFileName, 260);
            return &dir->entry;
        }
        if (FindNextFileA(dir->hFind, &dir->data)) {
            strncpy(dir->entry.d_name, dir->data.cFileName, 260);
            return &dir->entry;
        }
        return NULL;
    }

    static inline void hml_closedir(hml_dir_t *dir) {
        if (dir) {
            FindClose(dir->hFind);
            free(dir);
        }
    }

    // Socket compatibility
    typedef SOCKET hml_socket_t;
    #define HML_INVALID_SOCKET INVALID_SOCKET
    #define hml_closesocket(s) closesocket(s)
    #define hml_socket_error() WSAGetLastError()

    // strndup compatibility for Windows
    static inline char* hml_strndup(const char *s, size_t n) {
        size_t len = strnlen(s, n);
        char *result = malloc(len + 1);
        if (result) {
            memcpy(result, s, len);
            result[len] = '\0';
        }
        return result;
    }
    #ifndef strndup
    #define strndup hml_strndup
    #endif

    // getline compatibility is provided by ../internal.h (hml_getline)

    // MinGW provides nanosleep and usleep via time.h and unistd.h

    // Signal compatibility - limited on Windows
    #include <signal.h>

#else
    // POSIX systems
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <dirent.h>
    #include <unistd.h>
    #include <sys/time.h>
    #include <signal.h>
    #include <fcntl.h>  // For O_NOFOLLOW symlink protection
    #include <poll.h>   // For poll() in exec functions

    // POSIX compatibility types
    typedef struct dirent hml_dirent_t;
    typedef DIR hml_dir_t;
    #define hml_opendir opendir
    #define hml_readdir readdir
    #define hml_closedir closedir

    // POSIX socket compatibility
    typedef int hml_socket_t;
    #define HML_INVALID_SOCKET (-1)
    #define hml_closesocket(s) close(s)
    #define hml_socket_error() errno
#endif

// Define math constants if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

// Maximum signal number we'll support
#define MAX_SIGNAL 64

// Global signal handler table (defined in signals.c)
extern Function *signal_handlers[MAX_SIGNAL];

// Helper function to get the size of a type (defined in memory.c)
int get_type_size(TypeKind kind);

// Memory management builtins (memory.c)
Value builtin_alloc(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_free(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_memset(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_memcpy(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_sizeof(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_buffer(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_talloc(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_realloc(Value *args, int num_args, ExecutionContext *ctx);

// Debugging builtins (debugging.c)
Value builtin_typeof(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_assert(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_panic(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_set_stack_limit(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_get_stack_limit(Value *args, int num_args, ExecutionContext *ctx);

// Math builtins (math.c)
Value builtin_sin(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_cos(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_tan(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_asin(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_acos(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atan(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atan2(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_sqrt(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_pow(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_exp(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_log(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_log10(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_log2(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_floor(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ceil(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_round(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_trunc(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_floori(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ceili(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_roundi(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_trunci(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_div(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_divi(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_abs(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_min(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_max(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_clamp(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_rand(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_rand_range(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_seed(Value *args, int num_args, ExecutionContext *ctx);

// Time builtins (time.c)
Value builtin_now(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_time_ms(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_sleep(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_clock(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_localtime(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_gmtime(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_mktime(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_strftime(Value *args, int num_args, ExecutionContext *ctx);

// Environment builtins (env.c)
Value builtin_getenv(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_setenv(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_unsetenv(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_exit(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_get_pid(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_exec(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_exec_argv(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_getppid(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_getuid(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_geteuid(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_getgid(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_getegid(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_kill(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_fork(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_wait(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_waitpid(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_abort(Value *args, int num_args, ExecutionContext *ctx);

// Signal handling builtins (signals.c)
Value builtin_signal(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_raise(Value *args, int num_args, ExecutionContext *ctx);

// Concurrency builtins (concurrency.c)
Value builtin_spawn(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_join(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_detach(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_channel(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_select(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_task_debug_info(Value *args, int num_args, ExecutionContext *ctx);

// Filesystem builtins (filesystem.c)
Value builtin_exists(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_read_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_write_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_append_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_remove_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_rename(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_copy_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_is_file(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_is_dir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_file_stat(Value *args, int num_args, ExecutionContext *ctx);

// Directory builtins (directories.c)
Value builtin_make_dir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_remove_dir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_list_dir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_cwd(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_chdir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_absolute_path(Value *args, int num_args, ExecutionContext *ctx);

// I/O helper builtins (io_helpers.c)
Value builtin_print(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_string_concat_many(Value *args, int num_args, ExecutionContext *ctx);

// Internal helper builtins (internal_helpers.c)
Value builtin_read_u32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_read_u64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_read_ptr(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_strerror_fn(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_dirent_name(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_string_to_cstr(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_cstr_to_string(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_string_from_bytes(Value *args, int num_args, ExecutionContext *ctx);

// Networking builtins (net.c)
Value builtin_socket_create(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_dns_resolve(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_poll(Value *args, int num_args, ExecutionContext *ctx);
Value val_socket(SocketHandle *sock);
void socket_free(SocketHandle *sock);
Value get_socket_property(SocketHandle *sock, const char *property, ExecutionContext *ctx);
Value call_socket_method(SocketHandle *sock, const char *method, Value *args, int num_args, ExecutionContext *ctx);

// libwebsockets builtins (websockets.c)
// HTTP builtins
Value builtin_lws_http_get(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_http_post(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_http_request(Value *args, int num_args, ExecutionContext *ctx);
// Timeout versions (timeout_ms as last argument)
Value builtin_lws_http_get_timeout(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_http_post_timeout(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_http_request_timeout(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_status(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_body(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_body_binary(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_headers(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_redirect(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_free(Value *args, int num_args, ExecutionContext *ctx);
// WebSocket builtins
Value builtin_lws_ws_connect(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_ws_send_text(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_ws_send_binary(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_ws_recv(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_msg_type(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_msg_text(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_msg_len(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_msg_free(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_ws_close(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_ws_is_closed(Value *args, int num_args, ExecutionContext *ctx);
// WebSocket server builtins
Value builtin_lws_ws_server_create(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_ws_server_accept(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_ws_server_close(Value *args, int num_args, ExecutionContext *ctx);
// WebSocket handle helpers
Value val_websocket(WebSocketHandle *ws);
void websocket_free(WebSocketHandle *ws);
void websocket_retain(WebSocketHandle *ws);
void websocket_release(WebSocketHandle *ws);
Value get_websocket_property(WebSocketHandle *ws, const char *property, ExecutionContext *ctx);
Value call_websocket_method(WebSocketHandle *ws, const char *method, Value *args, int num_args, ExecutionContext *ctx);

// Compression builtins (compression.c)
Value builtin_zlib_compress(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_zlib_decompress(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_gzip_compress(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_gzip_decompress(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_zlib_compress_bound(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_crc32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_adler32(Value *args, int num_args, ExecutionContext *ctx);

// Cryptographic hash builtins (crypto.c)
Value builtin_sha256(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_sha512(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_md5(Value *args, int num_args, ExecutionContext *ctx);

// ECDSA signature builtins (crypto.c)
Value builtin_ecdsa_generate_key(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ecdsa_free_key(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ecdsa_sign(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ecdsa_verify(Value *args, int num_args, ExecutionContext *ctx);

// OS information builtins (os.c)
Value builtin_platform(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_arch(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_hostname(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_username(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_homedir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_cpu_count(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_total_memory(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_free_memory(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_os_version(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_os_name(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_tmpdir(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_uptime(Value *args, int num_args, ExecutionContext *ctx);

// FFI callback builtins (ffi_builtins.c)
Value builtin_callback(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_callback_free(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_read_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_offset(Value *args, int num_args, ExecutionContext *ctx);

// Additional pointer deref builtins for all types (ffi_builtins.c)
Value builtin_ptr_deref_i8(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_i16(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_u8(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_u16(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_u32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_u64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_f32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_f64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_deref_ptr(Value *args, int num_args, ExecutionContext *ctx);

// Additional pointer write builtins for all types (ffi_builtins.c)
Value builtin_ptr_write_i8(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_i16(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_u8(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_u16(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_u32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_u64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_f32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_f64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_write_ptr(Value *args, int num_args, ExecutionContext *ctx);

// FFI utility builtins (ffi_builtins.c)
Value builtin_ffi_sizeof(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_to_buffer(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_buffer_ptr(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_ptr_null(Value *args, int num_args, ExecutionContext *ctx);

// Atomic operations builtins (atomics.c)
// i32 atomics
Value builtin_atomic_load_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_store_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_add_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_sub_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_and_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_or_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_xor_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_cas_i32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_exchange_i32(Value *args, int num_args, ExecutionContext *ctx);
// i64 atomics
Value builtin_atomic_load_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_store_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_add_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_sub_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_and_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_or_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_xor_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_cas_i64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_atomic_exchange_i64(Value *args, int num_args, ExecutionContext *ctx);
// Memory fence
Value builtin_atomic_fence(Value *args, int num_args, ExecutionContext *ctx);

// Regex builtins (regex.c)
Value builtin_regex_compile(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_regex_test(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_regex_match(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_regex_free(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_regex_error(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_regex_replace(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_regex_replace_all(Value *args, int num_args, ExecutionContext *ctx);

#endif // BUILTINS_INTERNAL_H
