#include "internal.h"
#include "hemlock_limits.h"
#include <poll.h>

// Structure to hold builtin function info
typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinInfo;

static BuiltinInfo builtins[] = {
    {"print", builtin_print},
    {"write", builtin_write},
    {"alloc", builtin_alloc},
    {"talloc", builtin_talloc},
    {"realloc", builtin_realloc},
    {"free", builtin_free},
    {"memset", builtin_memset},
    {"memcpy", builtin_memcpy},
    {"sizeof", builtin_sizeof},
    {"buffer", builtin_buffer},
    {"typeof", builtin_typeof},
    {"typeid", builtin_typeid},
    {"read_line", builtin_read_line},
    {"__string_concat_many", builtin_string_concat_many},
    {"eprint", builtin_eprint},
    {"__open", builtin_open},
    {"assert", builtin_assert},
    {"panic", builtin_panic},
    {"__set_stack_limit", builtin_set_stack_limit},
    {"__get_stack_limit", builtin_get_stack_limit},
    // exec/exec_argv: use @stdlib/process for public API
    // __exec and __exec_argv already registered in env/process section below
    {"spawn", builtin_spawn},
    {"spawn_with", builtin_spawn_with},
    {"join", builtin_join},
    {"detach", builtin_detach},
    {"apply", builtin_apply},
    {"channel", builtin_channel},
    {"select", builtin_select},
    {"__task_debug_info", builtin_task_debug_info},
    {"__get_default_stack_size", builtin_get_default_stack_size},
    {"__set_default_stack_size", builtin_set_default_stack_size},
    {"__signal", builtin_signal},
    {"__raise", builtin_raise},
    // Networking functions (__ prefixed, use @stdlib/net for public API)
    {"__socket_create", builtin_socket_create},
    {"__dns_resolve", builtin_dns_resolve},
    {"__poll", builtin_poll},
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
    {"__floori", builtin_floori},
    {"__ceili", builtin_ceili},
    {"__roundi", builtin_roundi},
    {"__trunci", builtin_trunci},
    {"__div", builtin_div},
    {"__divi", builtin_divi},
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
    {"__exec_argv", builtin_exec_argv},
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
    {"__read_ptr", builtin_read_ptr},
    {"__strerror", builtin_strerror_fn},
    {"__dirent_name", builtin_dirent_name},
    {"__string_to_cstr", builtin_string_to_cstr},
    {"__cstr_to_string", builtin_cstr_to_string},
    {"__string_from_bytes", builtin_string_from_bytes},
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
    {"__lws_http_request", builtin_lws_http_request},
    // Timeout versions
    {"__lws_http_get_timeout", builtin_lws_http_get_timeout},
    {"__lws_http_post_timeout", builtin_lws_http_post_timeout},
    {"__lws_http_request_timeout", builtin_lws_http_request_timeout},
    {"__lws_response_status", builtin_lws_response_status},
    {"__lws_response_body", builtin_lws_response_body},
    {"__lws_response_body_binary", builtin_lws_response_body_binary},
    {"__lws_response_headers", builtin_lws_response_headers},
    {"__lws_response_redirect", builtin_lws_response_redirect},
    {"__lws_response_free", builtin_lws_response_free},
    // Streaming HTTP builtins
    {"__lws_http_stream_start", builtin_lws_http_stream_start},
    {"__lws_http_stream_read", builtin_lws_http_stream_read},
    {"__lws_http_stream_status", builtin_lws_http_stream_status},
    {"__lws_http_stream_headers", builtin_lws_http_stream_headers},
    {"__lws_http_stream_close", builtin_lws_http_stream_close},
    // WebSocket builtins
    {"__lws_ws_connect", builtin_lws_ws_connect},
    {"__lws_ws_send_text", builtin_lws_ws_send_text},
    {"__lws_ws_send_binary", builtin_lws_ws_send_binary},
    {"__lws_ws_recv", builtin_lws_ws_recv},
    {"__lws_msg_type", builtin_lws_msg_type},
    {"__lws_msg_text", builtin_lws_msg_text},
    {"__lws_msg_len", builtin_lws_msg_len},
    {"__lws_msg_binary", builtin_lws_msg_binary},
    {"__lws_msg_free", builtin_lws_msg_free},
    {"__lws_ws_close", builtin_lws_ws_close},
    {"__lws_ws_is_closed", builtin_lws_ws_is_closed},
    // WebSocket server builtins
    {"__lws_ws_server_create", builtin_lws_ws_server_create},
    {"__lws_ws_server_accept", builtin_lws_ws_server_accept},
    {"__lws_ws_server_close", builtin_lws_ws_server_close},
    // Compression builtins (use stdlib/compression.hml module for public API)
    {"__zlib_compress", builtin_zlib_compress},
    {"__zlib_decompress", builtin_zlib_decompress},
    {"__gzip_compress", builtin_gzip_compress},
    {"__gzip_decompress", builtin_gzip_decompress},
    {"__zlib_compress_bound", builtin_zlib_compress_bound},
    {"__crc32", builtin_crc32},
    {"__adler32", builtin_adler32},
    // Cryptographic hash builtins (use stdlib/hash.hml module for public API)
    {"__sha256", builtin_sha256},
    {"__sha512", builtin_sha512},
    {"__md5", builtin_md5},
    // ECDSA signature builtins (use stdlib/crypto.hml module for public API)
    {"__ecdsa_generate_key", builtin_ecdsa_generate_key},
    {"__ecdsa_free_key", builtin_ecdsa_free_key},
    {"__ecdsa_sign", builtin_ecdsa_sign},
    {"__ecdsa_verify", builtin_ecdsa_verify},
    // Regex builtins (use stdlib/regex.hml module for public API)
    {"__regex_compile", builtin_regex_compile},
    {"__regex_test", builtin_regex_test},
    {"__regex_match", builtin_regex_match},
    {"__regex_free", builtin_regex_free},
    {"__regex_error", builtin_regex_error},
    {"__regex_replace", builtin_regex_replace},
    {"__regex_replace_all", builtin_regex_replace_all},
    // OS information builtins (use stdlib/os.hml module for public API)
    {"__platform", builtin_platform},
    {"__arch", builtin_arch},
    {"__hostname", builtin_hostname},
    {"__username", builtin_username},
    {"__homedir", builtin_homedir},
    {"__cpu_count", builtin_cpu_count},
    {"__total_memory", builtin_total_memory},
    {"__free_memory", builtin_free_memory},
    {"__os_version", builtin_os_version},
    {"__os_name", builtin_os_name},
    {"__tmpdir", builtin_tmpdir},
    {"__uptime", builtin_uptime},
    // NOTE: Math functions (sin, cos, sqrt, etc.), environment functions
    // (getenv, setenv, etc.), and random functions (rand, seed) are no
    // longer registered as unprefixed global builtins. Users must import
    // them from @stdlib/math, @stdlib/env, or @stdlib/random respectively.
    // The __-prefixed versions remain for stdlib internal use.
    // FFI callback functions (use @stdlib/ffi for public API)
    {"__callback", builtin_callback},
    {"__callback_free", builtin_callback_free},
    {"ptr_read_i32", builtin_ptr_read_i32},
    {"ptr_deref_i32", builtin_ptr_deref_i32},
    {"ptr_write_i32", builtin_ptr_write_i32},
    {"ptr_offset", builtin_ptr_offset},
    // Additional pointer deref functions for all types
    {"ptr_deref_i8", builtin_ptr_deref_i8},
    {"ptr_deref_i16", builtin_ptr_deref_i16},
    {"ptr_deref_i64", builtin_ptr_deref_i64},
    {"ptr_deref_u8", builtin_ptr_deref_u8},
    {"ptr_deref_u16", builtin_ptr_deref_u16},
    {"ptr_deref_u32", builtin_ptr_deref_u32},
    {"ptr_deref_u64", builtin_ptr_deref_u64},
    {"ptr_deref_f32", builtin_ptr_deref_f32},
    {"ptr_deref_f64", builtin_ptr_deref_f64},
    {"ptr_deref_ptr", builtin_ptr_deref_ptr},
    // Additional pointer write functions for all types
    {"ptr_write_i8", builtin_ptr_write_i8},
    {"ptr_write_i16", builtin_ptr_write_i16},
    {"ptr_write_i64", builtin_ptr_write_i64},
    {"ptr_write_u8", builtin_ptr_write_u8},
    {"ptr_write_u16", builtin_ptr_write_u16},
    {"ptr_write_u32", builtin_ptr_write_u32},
    {"ptr_write_u64", builtin_ptr_write_u64},
    {"ptr_write_f32", builtin_ptr_write_f32},
    {"ptr_write_f64", builtin_ptr_write_f64},
    {"ptr_write_ptr", builtin_ptr_write_ptr},
    {"ptr_read_i8", builtin_ptr_read_i8},
    {"ptr_read_i16", builtin_ptr_read_i16},
    {"ptr_read_ptr", builtin_ptr_read_ptr},
    {"ptr_read_i64", builtin_ptr_read_i64},
    {"ptr_read_u8", builtin_ptr_read_u8},
    {"ptr_read_u16", builtin_ptr_read_u16},
    {"ptr_read_u32", builtin_ptr_read_u32},
    {"ptr_read_u64", builtin_ptr_read_u64},
    {"ptr_read_f32", builtin_ptr_read_f32},
    {"ptr_read_f64", builtin_ptr_read_f64},
    // FFI utility functions
    {"__ffi_sizeof", builtin_ffi_sizeof},
    {"ptr_to_buffer", builtin_ptr_to_buffer},
    {"buffer_ptr", builtin_buffer_ptr},
    {"ptr_null", builtin_ptr_null},
    // Atomic operations (use @stdlib/atomic for public API)
    {"__atomic_load_i32", builtin_atomic_load_i32},
    {"__atomic_store_i32", builtin_atomic_store_i32},
    {"__atomic_add_i32", builtin_atomic_add_i32},
    {"__atomic_sub_i32", builtin_atomic_sub_i32},
    {"__atomic_and_i32", builtin_atomic_and_i32},
    {"__atomic_or_i32", builtin_atomic_or_i32},
    {"__atomic_xor_i32", builtin_atomic_xor_i32},
    {"__atomic_cas_i32", builtin_atomic_cas_i32},
    {"__atomic_exchange_i32", builtin_atomic_exchange_i32},
    {"__atomic_load_i64", builtin_atomic_load_i64},
    {"__atomic_store_i64", builtin_atomic_store_i64},
    {"__atomic_add_i64", builtin_atomic_add_i64},
    {"__atomic_sub_i64", builtin_atomic_sub_i64},
    {"__atomic_and_i64", builtin_atomic_and_i64},
    {"__atomic_or_i64", builtin_atomic_or_i64},
    {"__atomic_xor_i64", builtin_atomic_xor_i64},
    {"__atomic_cas_i64", builtin_atomic_cas_i64},
    {"__atomic_exchange_i64", builtin_atomic_exchange_i64},
    {"__atomic_fence", builtin_atomic_fence},
    // Byte order builtins (use @stdlib/bytes for public API)
    {"__bswap16", builtin_bswap16},
    {"__bswap32", builtin_bswap32},
    {"__bswap64", builtin_bswap64},
    {"__htons", builtin_htons},
    {"__htonl", builtin_htonl},
    {"__htonll", builtin_htonll},
    {"__ntohs", builtin_ntohs},
    {"__ntohl", builtin_ntohl},
    {"__ntohll", builtin_ntohll},
    {"__is_little_endian", builtin_is_little_endian},
    {"__read_u16_be", builtin_read_u16_be},
    {"__read_u16_le", builtin_read_u16_le},
    {"__read_u32_be", builtin_read_u32_be},
    {"__read_u32_le", builtin_read_u32_le},
    {"__read_u64_be", builtin_read_u64_be},
    {"__read_u64_le", builtin_read_u64_le},
    {"__write_u16_be", builtin_write_u16_be},
    {"__write_u16_le", builtin_write_u16_le},
    {"__write_u32_be", builtin_write_u32_be},
    {"__write_u32_le", builtin_write_u32_le},
    {"__write_u64_be", builtin_write_u64_be},
    {"__write_u64_le", builtin_write_u64_le},
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
    env_set(env, "i64", val_type(TYPE_I64), ctx);
    env_set(env, "u8", val_type(TYPE_U8), ctx);
    env_set(env, "u16", val_type(TYPE_U16), ctx);
    env_set(env, "u32", val_type(TYPE_U32), ctx);
    env_set(env, "u64", val_type(TYPE_U64), ctx);
    env_set(env, "f32", val_type(TYPE_F32), ctx);
    env_set(env, "f64", val_type(TYPE_F64), ctx);
    env_set(env, "bool", val_type(TYPE_BOOL), ctx);
    env_set(env, "rune", val_type(TYPE_RUNE), ctx);
    env_set(env, "ptr", val_type(TYPE_PTR), ctx);

    // Type aliases
    env_set(env, "integer", val_type(TYPE_I32), ctx);
    env_set(env, "number", val_type(TYPE_F64), ctx);
    env_set(env, "byte", val_type(TYPE_U8), ctx);

    // TYPEID constants for use with typeid() builtin
    env_set(env, "TYPEID_I8", val_i32(HML_TYPEID_I8), ctx);
    env_set(env, "TYPEID_I16", val_i32(HML_TYPEID_I16), ctx);
    env_set(env, "TYPEID_I32", val_i32(HML_TYPEID_I32), ctx);
    env_set(env, "TYPEID_I64", val_i32(HML_TYPEID_I64), ctx);
    env_set(env, "TYPEID_U8", val_i32(HML_TYPEID_U8), ctx);
    env_set(env, "TYPEID_U16", val_i32(HML_TYPEID_U16), ctx);
    env_set(env, "TYPEID_U32", val_i32(HML_TYPEID_U32), ctx);
    env_set(env, "TYPEID_U64", val_i32(HML_TYPEID_U64), ctx);
    env_set(env, "TYPEID_F32", val_i32(HML_TYPEID_F32), ctx);
    env_set(env, "TYPEID_F64", val_i32(HML_TYPEID_F64), ctx);
    env_set(env, "TYPEID_BOOL", val_i32(HML_TYPEID_BOOL), ctx);
    env_set(env, "TYPEID_STRING", val_i32(HML_TYPEID_STRING), ctx);
    env_set(env, "TYPEID_RUNE", val_i32(HML_TYPEID_RUNE), ctx);
    env_set(env, "TYPEID_PTR", val_i32(HML_TYPEID_PTR), ctx);
    env_set(env, "TYPEID_BUFFER", val_i32(HML_TYPEID_BUFFER), ctx);
    env_set(env, "TYPEID_ARRAY", val_i32(HML_TYPEID_ARRAY), ctx);
    env_set(env, "TYPEID_OBJECT", val_i32(HML_TYPEID_OBJECT), ctx);
    env_set(env, "TYPEID_FILE", val_i32(HML_TYPEID_FILE), ctx);
    env_set(env, "TYPEID_FUNCTION", val_i32(HML_TYPEID_FUNCTION), ctx);
    env_set(env, "TYPEID_TASK", val_i32(HML_TYPEID_TASK), ctx);
    env_set(env, "TYPEID_CHANNEL", val_i32(HML_TYPEID_CHANNEL), ctx);
    env_set(env, "TYPEID_NULL", val_i32(HML_TYPEID_NULL), ctx);

    // Math constants (use stdlib/math.hml module for public API)
    env_set(env, "__PI", val_f64(M_PI), ctx);
    env_set(env, "__E", val_f64(M_E), ctx);
    env_set(env, "__TAU", val_f64(2.0 * M_PI), ctx);
    env_set(env, "__INF", val_f64(INFINITY), ctx);
    env_set(env, "__NAN", val_f64(NAN), ctx);

    // NOTE: Signal constants (SIGINT, SIGTERM, etc.) moved to @stdlib/signal
    // NOTE: Socket constants (AF_INET, SOCK_STREAM, etc.) moved to @stdlib/net
    // NOTE: Poll constants (POLLIN, POLLOUT, etc.) moved to @stdlib/net
    // Users must import them from the respective stdlib modules.

    // Internal signal constants for stdlib use (__ prefixed)
    env_set(env, "__SIGINT", val_i32(SIGINT), ctx);
    env_set(env, "__SIGTERM", val_i32(SIGTERM), ctx);
    env_set(env, "__SIGHUP", val_i32(SIGHUP), ctx);
    env_set(env, "__SIGQUIT", val_i32(SIGQUIT), ctx);
    env_set(env, "__SIGABRT", val_i32(SIGABRT), ctx);
    env_set(env, "__SIGUSR1", val_i32(SIGUSR1), ctx);
    env_set(env, "__SIGUSR2", val_i32(SIGUSR2), ctx);
    env_set(env, "__SIGALRM", val_i32(SIGALRM), ctx);
    env_set(env, "__SIGCHLD", val_i32(SIGCHLD), ctx);
    env_set(env, "__SIGPIPE", val_i32(SIGPIPE), ctx);
    env_set(env, "__SIGCONT", val_i32(SIGCONT), ctx);
    env_set(env, "__SIGSTOP", val_i32(SIGSTOP), ctx);
    env_set(env, "__SIGTSTP", val_i32(SIGTSTP), ctx);
    env_set(env, "__SIGTTIN", val_i32(SIGTTIN), ctx);
    env_set(env, "__SIGTTOU", val_i32(SIGTTOU), ctx);

    // Internal socket constants for stdlib use (__ prefixed)
    env_set(env, "__AF_INET", val_i32(AF_INET), ctx);
    env_set(env, "__AF_INET6", val_i32(AF_INET6), ctx);
    env_set(env, "__SOCK_STREAM", val_i32(SOCK_STREAM), ctx);
    env_set(env, "__SOCK_DGRAM", val_i32(SOCK_DGRAM), ctx);
    env_set(env, "__IPPROTO_TCP", val_i32(IPPROTO_TCP), ctx);
    env_set(env, "__IPPROTO_UDP", val_i32(IPPROTO_UDP), ctx);
    env_set(env, "__SOL_SOCKET", val_i32(SOL_SOCKET), ctx);
    env_set(env, "__SO_REUSEADDR", val_i32(SO_REUSEADDR), ctx);
    env_set(env, "__SO_KEEPALIVE", val_i32(SO_KEEPALIVE), ctx);
    env_set(env, "__SO_RCVTIMEO", val_i32(SO_RCVTIMEO), ctx);
    env_set(env, "__SO_SNDTIMEO", val_i32(SO_SNDTIMEO), ctx);
    env_set(env, "__POLLIN", val_i32(POLLIN), ctx);
    env_set(env, "__POLLOUT", val_i32(POLLOUT), ctx);
    env_set(env, "__POLLERR", val_i32(POLLERR), ctx);
    env_set(env, "__POLLHUP", val_i32(POLLHUP), ctx);
    env_set(env, "__POLLNVAL", val_i32(POLLNVAL), ctx);
    env_set(env, "__POLLPRI", val_i32(POLLPRI), ctx);

    // Register builtin functions (may overwrite some type names if there are conflicts)
    for (int i = 0; builtins[i].name != NULL; i++) {
        env_set(env, builtins[i].name, val_builtin_fn(builtins[i].fn), ctx);
    }

    // Register command-line arguments as 'args' array
    Array *args_array = array_new();
    for (int i = 0; i < argc; i++) {
        Value str_val = val_string(argv[i]);
        array_push(args_array, str_val);
        value_release(str_val);  // array_push retains, so release our reference
    }
    env_set(env, "args", val_array(args_array), ctx);
    // Release caller's reference - env now owns the array
    array_release(args_array);
}
