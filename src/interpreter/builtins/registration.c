#include "internal.h"

// Structure to hold builtin function info
typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinInfo;

static BuiltinInfo builtins[] = {
    {"print", builtin_print},
    {"alloc", builtin_alloc},
    {"talloc", builtin_talloc},
    {"realloc", builtin_realloc},
    {"free", builtin_free},
    {"memset", builtin_memset},
    {"memcpy", builtin_memcpy},
    {"sizeof", builtin_sizeof},
    {"buffer", builtin_buffer},
    {"typeof", builtin_typeof},
    {"read_line", builtin_read_line},
    {"eprint", builtin_eprint},
    {"open", builtin_open},
    {"assert", builtin_assert},
    {"panic", builtin_panic},
    {"exec", builtin_exec},
    {"spawn", builtin_spawn},
    {"join", builtin_join},
    {"detach", builtin_detach},
    {"channel", builtin_channel},
    {"task_debug_info", builtin_task_debug_info},
    {"signal", builtin_signal},
    {"raise", builtin_raise},
    // Networking functions
    {"socket_create", builtin_socket_create},
    {"dns_resolve", builtin_dns_resolve},
    // Math functions (use stdlib/math.hml module for public API)
    {"__sin", builtin_sin},
    {"__cos", builtin_cos},
    {"__tan", builtin_tan},
    {"__asin", builtin_asin},
    {"__acos", builtin_acos},
    {"__atan", builtin_atan},
    {"__atan2", builtin_atan2},
    {"__sqrt", builtin_sqrt},
    {"__pow", builtin_pow},
    {"__exp", builtin_exp},
    {"__log", builtin_log},
    {"__log10", builtin_log10},
    {"__log2", builtin_log2},
    {"__floor", builtin_floor},
    {"__ceil", builtin_ceil},
    {"__round", builtin_round},
    {"__trunc", builtin_trunc},
    {"__abs", builtin_abs},
    {"__min", builtin_min},
    {"__max", builtin_max},
    {"__clamp", builtin_clamp},
    {"__rand", builtin_rand},
    {"__rand_range", builtin_rand_range},
    {"__seed", builtin_seed},
    // Time functions (use stdlib/time.hml and stdlib/datetime.hml modules for public API)
    {"__now", builtin_now},
    {"__time_ms", builtin_time_ms},
    {"__sleep", builtin_sleep},
    {"__clock", builtin_clock},
    {"__localtime", builtin_localtime},
    {"__gmtime", builtin_gmtime},
    {"__mktime", builtin_mktime},
    {"__strftime", builtin_strftime},
    // Environment functions (use stdlib/env.hml and stdlib/process.hml modules for public API)
    {"__getenv", builtin_getenv},
    {"__setenv", builtin_setenv},
    {"__unsetenv", builtin_unsetenv},
    {"__exit", builtin_exit},
    {"__get_pid", builtin_get_pid},
    {"__exec", builtin_exec},
    {"__getppid", builtin_getppid},
    {"__getuid", builtin_getuid},
    {"__geteuid", builtin_geteuid},
    {"__getgid", builtin_getgid},
    {"__getegid", builtin_getegid},
    {"__kill", builtin_kill},
    {"__fork", builtin_fork},
    {"__wait", builtin_wait},
    {"__waitpid", builtin_waitpid},
    {"__abort", builtin_abort},
    // Internal helper builtins
    {"__read_u32", builtin_read_u32},
    {"__read_u64", builtin_read_u64},
    {"__strerror", builtin_strerror_fn},
    {"__dirent_name", builtin_dirent_name},
    {"__string_to_cstr", builtin_string_to_cstr},
    {"__cstr_to_string", builtin_cstr_to_string},
    // Internal file operations (use stdlib/fs.hml module for public API)
    {"__exists", builtin_exists},
    {"__read_file", builtin_read_file},
    {"__write_file", builtin_write_file},
    {"__append_file", builtin_append_file},
    // Internal directory operations (use stdlib/fs.hml module for public API)
    {"__make_dir", builtin_make_dir},
    {"__remove_dir", builtin_remove_dir},
    {"__list_dir", builtin_list_dir},
    // Internal file management (use stdlib/fs.hml module for public API)
    {"__remove_file", builtin_remove_file},
    {"__rename", builtin_rename},
    {"__copy_file", builtin_copy_file},
    // Internal file info (use stdlib/fs.hml module for public API)
    {"__is_file", builtin_is_file},
    {"__is_dir", builtin_is_dir},
    {"__file_stat", builtin_file_stat},
    // Internal directory navigation (use stdlib/fs.hml module for public API)
    {"__cwd", builtin_cwd},
    {"__chdir", builtin_chdir},
    {"__absolute_path", builtin_absolute_path},
    // libwebsockets builtins (use stdlib/http.hml and stdlib/websocket.hml modules for public API)
    // HTTP builtins
    {"__lws_http_get", builtin_lws_http_get},
    {"__lws_http_post", builtin_lws_http_post},
    {"__lws_response_status", builtin_lws_response_status},
    {"__lws_response_body", builtin_lws_response_body},
    {"__lws_response_headers", builtin_lws_response_headers},
    {"__lws_response_free", builtin_lws_response_free},
    // WebSocket builtins
    {"__lws_ws_connect", builtin_lws_ws_connect},
    {"__lws_ws_send_text", builtin_lws_ws_send_text},
    {"__lws_ws_recv", builtin_lws_ws_recv},
    {"__lws_msg_type", builtin_lws_msg_type},
    {"__lws_msg_text", builtin_lws_msg_text},
    {"__lws_msg_len", builtin_lws_msg_len},
    {"__lws_msg_free", builtin_lws_msg_free},
    {"__lws_ws_close", builtin_lws_ws_close},
    {"__lws_ws_is_closed", builtin_lws_ws_is_closed},
    // WebSocket server builtins
    {"__lws_ws_server_create", builtin_lws_ws_server_create},
    {"__lws_ws_server_accept", builtin_lws_ws_server_accept},
    {"__lws_ws_server_close", builtin_lws_ws_server_close},
    {NULL, NULL}  // Sentinel
};

Value val_builtin_fn(BuiltinFn fn) {
    Value v;
    v.type = VAL_BUILTIN_FN;
    v.as.as_builtin_fn = fn;
    return v;
}

void register_builtins(Environment *env, int argc, char **argv, ExecutionContext *ctx) {
    // Register type constants FIRST for use with sizeof() and talloc()
    // These must be registered before builtin functions to avoid conflicts
    env_set(env, "i8", val_type(TYPE_I8), ctx);
    env_set(env, "i16", val_type(TYPE_I16), ctx);
    env_set(env, "i32", val_type(TYPE_I32), ctx);
    env_set(env, "u8", val_type(TYPE_U8), ctx);
    env_set(env, "u16", val_type(TYPE_U16), ctx);
    env_set(env, "u32", val_type(TYPE_U32), ctx);
    env_set(env, "f32", val_type(TYPE_F32), ctx);
    env_set(env, "f64", val_type(TYPE_F64), ctx);
    env_set(env, "ptr", val_type(TYPE_PTR), ctx);

    // Type aliases
    env_set(env, "integer", val_type(TYPE_I32), ctx);
    env_set(env, "number", val_type(TYPE_F64), ctx);
    env_set(env, "byte", val_type(TYPE_U8), ctx);

    // Math constants (use stdlib/math.hml module for public API)
    env_set(env, "__PI", val_f64(M_PI), ctx);
    env_set(env, "__E", val_f64(M_E), ctx);
    env_set(env, "__TAU", val_f64(2.0 * M_PI), ctx);
    env_set(env, "__INF", val_f64(INFINITY), ctx);
    env_set(env, "__NAN", val_f64(NAN), ctx);

    // Signal constants
    env_set(env, "SIGINT", val_i32(SIGINT), ctx);      // Interrupt (Ctrl+C)
    env_set(env, "SIGTERM", val_i32(SIGTERM), ctx);    // Termination request
    env_set(env, "SIGHUP", val_i32(SIGHUP), ctx);      // Hangup
    env_set(env, "SIGQUIT", val_i32(SIGQUIT), ctx);    // Quit (Ctrl+\)
    env_set(env, "SIGABRT", val_i32(SIGABRT), ctx);    // Abort
    env_set(env, "SIGUSR1", val_i32(SIGUSR1), ctx);    // User-defined signal 1
    env_set(env, "SIGUSR2", val_i32(SIGUSR2), ctx);    // User-defined signal 2
    env_set(env, "SIGALRM", val_i32(SIGALRM), ctx);    // Alarm clock
    env_set(env, "SIGCHLD", val_i32(SIGCHLD), ctx);    // Child process status change
    env_set(env, "SIGPIPE", val_i32(SIGPIPE), ctx);    // Broken pipe
    env_set(env, "SIGCONT", val_i32(SIGCONT), ctx);    // Continue if stopped
    env_set(env, "SIGSTOP", val_i32(SIGSTOP), ctx);    // Stop process (cannot be caught)
    env_set(env, "SIGTSTP", val_i32(SIGTSTP), ctx);    // Terminal stop (Ctrl+Z)
    env_set(env, "SIGTTIN", val_i32(SIGTTIN), ctx);    // Background read from terminal
    env_set(env, "SIGTTOU", val_i32(SIGTTOU), ctx);    // Background write to terminal

    // Socket constants - Domain (address family)
    env_set(env, "AF_INET", val_i32(AF_INET), ctx);    // IPv4
    env_set(env, "AF_INET6", val_i32(AF_INET6), ctx);  // IPv6

    // Socket constants - Type
    env_set(env, "SOCK_STREAM", val_i32(SOCK_STREAM), ctx);  // TCP
    env_set(env, "SOCK_DGRAM", val_i32(SOCK_DGRAM), ctx);    // UDP

    // Socket constants - Socket options level
    env_set(env, "SOL_SOCKET", val_i32(SOL_SOCKET), ctx);

    // Socket constants - Socket options
    env_set(env, "SO_REUSEADDR", val_i32(SO_REUSEADDR), ctx);  // Reuse address
    env_set(env, "SO_KEEPALIVE", val_i32(SO_KEEPALIVE), ctx);  // Keep connections alive
    env_set(env, "SO_RCVTIMEO", val_i32(SO_RCVTIMEO), ctx);    // Receive timeout
    env_set(env, "SO_SNDTIMEO", val_i32(SO_SNDTIMEO), ctx);    // Send timeout

    // Register builtin functions (may overwrite some type names if there are conflicts)
    for (int i = 0; builtins[i].name != NULL; i++) {
        env_set(env, builtins[i].name, val_builtin_fn(builtins[i].fn), ctx);
    }

    // Register command-line arguments as 'args' array
    Array *args_array = array_new();
    for (int i = 0; i < argc; i++) {
        array_push(args_array, val_string(argv[i]));
    }
    env_set(env, "args", val_array(args_array), ctx);
}
