#ifndef BUILTINS_INTERNAL_H
#define BUILTINS_INTERNAL_H

// Define feature test macros before including system headers
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "../internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

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

// Internal helper builtins (internal_helpers.c)
Value builtin_read_u32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_read_u64(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_strerror_fn(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_dirent_name(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_string_to_cstr(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_cstr_to_string(Value *args, int num_args, ExecutionContext *ctx);

// Networking builtins (net.c)
Value builtin_socket_create(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_dns_resolve(Value *args, int num_args, ExecutionContext *ctx);
Value val_socket(SocketHandle *sock);
void socket_free(SocketHandle *sock);
Value get_socket_property(SocketHandle *sock, const char *property, ExecutionContext *ctx);
Value call_socket_method(SocketHandle *sock, const char *method, Value *args, int num_args, ExecutionContext *ctx);

// libwebsockets builtins (websockets.c)
// HTTP builtins
Value builtin_lws_http_get(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_http_post(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_status(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_body(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_headers(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_response_free(Value *args, int num_args, ExecutionContext *ctx);
// WebSocket builtins
Value builtin_lws_ws_connect(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_lws_ws_send_text(Value *args, int num_args, ExecutionContext *ctx);
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

// Compression builtins (compression.c)
Value builtin_zlib_compress(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_zlib_decompress(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_gzip_compress(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_gzip_decompress(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_zlib_compress_bound(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_crc32(Value *args, int num_args, ExecutionContext *ctx);
Value builtin_adler32(Value *args, int num_args, ExecutionContext *ctx);

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

#endif // BUILTINS_INTERNAL_H
