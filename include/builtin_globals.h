/*
 * Hemlock builtin global names - single source of truth.
 *
 * Every name the runtime installs in the global environment is listed here
 * exactly once. The interpreter expands these lists to register the globals
 * (src/backends/interpreter/builtins/registration.c); the frontend name
 * resolver expands them to decide whether a bare identifier is a builtin
 * (src/frontend/resolver/name_resolve.c). Keeping one list means a new builtin
 * cannot be visible to one and undefined to the other.
 *
 * tests/unit/test_builtin_globals_sync.sh re-derives the lists from
 * registration.c and fails if the two ever drift.
 *
 * Names are spelled as string literals, not bare tokens, so that a name which
 * happens to be a C macro (bool) or keyword (sizeof) still reaches the lists
 * intact. Only block comments may appear inside these macro bodies: a line
 * comment would swallow the line continuation that follows it.
 */
#ifndef HEMLOCK_BUILTIN_GLOBALS_H
#define HEMLOCK_BUILTIN_GLOBALS_H

/* Builtin functions: X(hemlock_name, c_implementation) */
#define HML_BUILTIN_FNS(X) \
    X("print", builtin_print) \
    X("write", builtin_write) \
    X("alloc", builtin_alloc) \
    X("talloc", builtin_talloc) \
    X("realloc", builtin_realloc) \
    X("free", builtin_free) \
    X("memset", builtin_memset) \
    X("memcpy", builtin_memcpy) \
    X("sizeof", builtin_sizeof) \
    X("buffer", builtin_buffer) \
    X("typeof", builtin_typeof) \
    X("typeid", builtin_typeid) \
    X("read_line", builtin_read_line) \
    X("__string_concat_many", builtin_string_concat_many) \
    X("eprint", builtin_eprint) \
    X("__open", builtin_open) \
    X("assert", builtin_assert) \
    X("panic", builtin_panic) \
    X("__set_stack_limit", builtin_set_stack_limit) \
    X("__get_stack_limit", builtin_get_stack_limit) \
    /* exec/exec_argv: use @stdlib/process for public API */ \
    /* __exec and __exec_argv already registered in env/process section below */ \
    X("spawn", builtin_spawn) \
    X("spawn_with", builtin_spawn_with) \
    X("join", builtin_join) \
    X("detach", builtin_detach) \
    X("apply", builtin_apply) \
    X("channel", builtin_channel) \
    X("select", builtin_select) \
    X("__task_debug_info", builtin_task_debug_info) \
    X("__get_default_stack_size", builtin_get_default_stack_size) \
    X("__set_default_stack_size", builtin_set_default_stack_size) \
    X("__signal", builtin_signal) \
    X("__raise", builtin_raise) \
    /* Networking functions (__ prefixed, use @stdlib/net for public API) */ \
    X("__socket_create", builtin_socket_create) \
    X("__dns_resolve", builtin_dns_resolve) \
    X("__poll", builtin_poll) \
    /* Math functions (use stdlib/math.hml module for public API) */ \
    X("__sin", builtin_sin) \
    X("__cos", builtin_cos) \
    X("__tan", builtin_tan) \
    X("__asin", builtin_asin) \
    X("__acos", builtin_acos) \
    X("__atan", builtin_atan) \
    X("__atan2", builtin_atan2) \
    X("__sqrt", builtin_sqrt) \
    X("__pow", builtin_pow) \
    X("__exp", builtin_exp) \
    X("__log", builtin_log) \
    X("__log10", builtin_log10) \
    X("__log2", builtin_log2) \
    X("__floor", builtin_floor) \
    X("__ceil", builtin_ceil) \
    X("__round", builtin_round) \
    X("__trunc", builtin_trunc) \
    X("__floori", builtin_floori) \
    X("__ceili", builtin_ceili) \
    X("__roundi", builtin_roundi) \
    X("__trunci", builtin_trunci) \
    X("__div", builtin_div) \
    X("__divi", builtin_divi) \
    X("__abs", builtin_abs) \
    X("__min", builtin_min) \
    X("__max", builtin_max) \
    X("__clamp", builtin_clamp) \
    X("__rand", builtin_rand) \
    X("__rand_range", builtin_rand_range) \
    X("__seed", builtin_seed) \
    /* Time functions (use stdlib/time.hml and stdlib/datetime.hml modules for public API) */ \
    X("__now", builtin_now) \
    X("__time_ms", builtin_time_ms) \
    X("__sleep", builtin_sleep) \
    X("__clock", builtin_clock) \
    X("__main_loop", builtin_main_loop) \
    X("__main_loop_stop", builtin_main_loop_stop) \
    X("__localtime", builtin_localtime) \
    X("__gmtime", builtin_gmtime) \
    X("__mktime", builtin_mktime) \
    X("__strftime", builtin_strftime) \
    /* Environment functions (use stdlib/env.hml and stdlib/process.hml modules for public API) */ \
    X("__getenv", builtin_getenv) \
    X("__setenv", builtin_setenv) \
    X("__unsetenv", builtin_unsetenv) \
    X("__exit", builtin_exit) \
    X("__get_pid", builtin_get_pid) \
    X("__exec", builtin_exec) \
    X("__exec_argv", builtin_exec_argv) \
    X("__getppid", builtin_getppid) \
    X("__getuid", builtin_getuid) \
    X("__geteuid", builtin_geteuid) \
    X("__getgid", builtin_getgid) \
    X("__getegid", builtin_getegid) \
    X("__kill", builtin_kill) \
    X("__fork", builtin_fork) \
    X("__wait", builtin_wait) \
    X("__waitpid", builtin_waitpid) \
    X("__posix_spawn", builtin_posix_spawn) \
    X("__abort", builtin_abort) \
    /* Pipe operations (use @stdlib/ipc for public API) */ \
    X("__pipe", builtin_pipe) \
    X("__close_fd", builtin_close_fd) \
    X("__read_fd", builtin_read_fd) \
    X("__write_fd", builtin_write_fd) \
    X("__dup2", builtin_dup2) \
    /* Internal helper builtins */ \
    X("__read_u32", builtin_read_u32) \
    X("__read_u64", builtin_read_u64) \
    X("__read_ptr", builtin_read_ptr) \
    X("__strerror", builtin_strerror_fn) \
    X("__dirent_name", builtin_dirent_name) \
    X("__string_to_cstr", builtin_string_to_cstr) \
    X("__cstr_to_string", builtin_cstr_to_string) \
    X("__string_from_bytes", builtin_string_from_bytes) \
    /* Internal file operations (use stdlib/fs.hml module for public API) */ \
    X("__exists", builtin_exists) \
    X("__read_file", builtin_read_file) \
    X("__write_file", builtin_write_file) \
    X("__append_file", builtin_append_file) \
    /* Internal directory operations (use stdlib/fs.hml module for public API) */ \
    X("__make_dir", builtin_make_dir) \
    X("__remove_dir", builtin_remove_dir) \
    X("__list_dir", builtin_list_dir) \
    /* Internal file management (use stdlib/fs.hml module for public API) */ \
    X("__remove_file", builtin_remove_file) \
    X("__rename", builtin_rename) \
    X("__copy_file", builtin_copy_file) \
    X("__open_fd", builtin_open_fd) \
    X("__fileno", builtin_fileno) \
    /* Internal file info (use stdlib/fs.hml module for public API) */ \
    X("__is_file", builtin_is_file) \
    X("__is_dir", builtin_is_dir) \
    X("__file_stat", builtin_file_stat) \
    /* Internal directory navigation (use stdlib/fs.hml module for public API) */ \
    X("__cwd", builtin_cwd) \
    X("__chdir", builtin_chdir) \
    X("__absolute_path", builtin_absolute_path) \
    /* libwebsockets builtins (use stdlib/http.hml and stdlib/websocket.hml modules for public API) */ \
    /* HTTP builtins */ \
    X("__lws_http_get", builtin_lws_http_get) \
    X("__lws_http_post", builtin_lws_http_post) \
    X("__lws_http_request", builtin_lws_http_request) \
    /* Timeout versions */ \
    X("__lws_http_get_timeout", builtin_lws_http_get_timeout) \
    X("__lws_http_post_timeout", builtin_lws_http_post_timeout) \
    X("__lws_http_request_timeout", builtin_lws_http_request_timeout) \
    X("__lws_response_status", builtin_lws_response_status) \
    X("__lws_response_body", builtin_lws_response_body) \
    X("__lws_response_body_binary", builtin_lws_response_body_binary) \
    X("__lws_response_headers", builtin_lws_response_headers) \
    X("__lws_response_redirect", builtin_lws_response_redirect) \
    X("__lws_response_free", builtin_lws_response_free) \
    /* Streaming HTTP builtins */ \
    X("__lws_http_stream_start", builtin_lws_http_stream_start) \
    X("__lws_http_stream_read", builtin_lws_http_stream_read) \
    X("__lws_http_stream_read_binary", builtin_lws_http_stream_read_binary) \
    X("__lws_http_stream_status", builtin_lws_http_stream_status) \
    X("__lws_http_stream_headers", builtin_lws_http_stream_headers) \
    X("__lws_http_stream_close", builtin_lws_http_stream_close) \
    /* WebSocket builtins */ \
    X("__lws_ws_connect", builtin_lws_ws_connect) \
    X("__lws_ws_send_text", builtin_lws_ws_send_text) \
    X("__lws_ws_send_binary", builtin_lws_ws_send_binary) \
    X("__lws_ws_recv", builtin_lws_ws_recv) \
    X("__lws_msg_type", builtin_lws_msg_type) \
    X("__lws_msg_text", builtin_lws_msg_text) \
    X("__lws_msg_len", builtin_lws_msg_len) \
    X("__lws_msg_binary", builtin_lws_msg_binary) \
    X("__lws_msg_free", builtin_lws_msg_free) \
    X("__lws_ws_close", builtin_lws_ws_close) \
    X("__lws_ws_is_closed", builtin_lws_ws_is_closed) \
    /* WebSocket server builtins */ \
    X("__lws_ws_server_create", builtin_lws_ws_server_create) \
    X("__lws_ws_server_accept", builtin_lws_ws_server_accept) \
    X("__lws_ws_server_close", builtin_lws_ws_server_close) \
    /* Compression builtins (use stdlib/compression.hml module for public API) */ \
    X("__zlib_compress", builtin_zlib_compress) \
    X("__zlib_decompress", builtin_zlib_decompress) \
    X("__gzip_compress", builtin_gzip_compress) \
    X("__gzip_decompress", builtin_gzip_decompress) \
    X("__zlib_compress_bound", builtin_zlib_compress_bound) \
    X("__crc32", builtin_crc32) \
    X("__adler32", builtin_adler32) \
    /* CSPRNG builtin (use stdlib/crypto.hml random_bytes for public API) */ \
    X("__random_bytes", builtin_random_bytes) \
    /* Cryptographic hash builtins (use stdlib/hash.hml module for public API) */ \
    X("__sha1", builtin_sha1) \
    X("__sha256", builtin_sha256) \
    X("__sha512", builtin_sha512) \
    X("__md5", builtin_md5) \
    /* ECDSA signature builtins (use stdlib/crypto.hml module for public API) */ \
    X("__ecdsa_generate_key", builtin_ecdsa_generate_key) \
    X("__ecdsa_free_key", builtin_ecdsa_free_key) \
    X("__ecdsa_sign", builtin_ecdsa_sign) \
    X("__ecdsa_verify", builtin_ecdsa_verify) \
    /* Regex builtins (use stdlib/regex.hml module for public API) */ \
    X("__regex_compile", builtin_regex_compile) \
    X("__regex_test", builtin_regex_test) \
    X("__regex_match", builtin_regex_match) \
    X("__regex_free", builtin_regex_free) \
    X("__regex_error", builtin_regex_error) \
    X("__regex_replace", builtin_regex_replace) \
    X("__regex_replace_all", builtin_regex_replace_all) \
    /* OS information builtins (use stdlib/os.hml module for public API) */ \
    X("__platform", builtin_platform) \
    X("__arch", builtin_arch) \
    X("__hostname", builtin_hostname) \
    X("__username", builtin_username) \
    X("__homedir", builtin_homedir) \
    X("__cpu_count", builtin_cpu_count) \
    X("__total_memory", builtin_total_memory) \
    X("__free_memory", builtin_free_memory) \
    X("__os_version", builtin_os_version) \
    X("__os_name", builtin_os_name) \
    X("__tmpdir", builtin_tmpdir) \
    X("__uptime", builtin_uptime) \
    /* Terminal control (use stdlib/termios.hml + terminal.hml for public API) */ \
    X("__term_is_tty", builtin_term_is_tty) \
    X("__term_raw", builtin_term_raw) \
    X("__term_read_byte", builtin_term_read_byte) \
    X("__term_size", builtin_term_size) \
    /* NOTE: Math functions (sin, cos, sqrt, etc.), environment functions */ \
    /* (getenv, setenv, etc.), and random functions (rand, seed) are no */ \
    /* longer registered as unprefixed global builtins. Users must import */ \
    /* them from @stdlib/math, @stdlib/env, or @stdlib/random respectively. */ \
    /* The __-prefixed versions remain for stdlib internal use. */ \
    /* FFI callback functions (use @stdlib/ffi for public API) */ \
    X("__callback", builtin_callback) \
    X("__callback_free", builtin_callback_free) \
    X("ptr_read_i32", builtin_ptr_read_i32) \
    X("ptr_deref_i32", builtin_ptr_deref_i32) \
    X("ptr_write_i32", builtin_ptr_write_i32) \
    X("ptr_offset", builtin_ptr_offset) \
    /* Additional pointer deref functions for all types */ \
    X("ptr_deref_i8", builtin_ptr_deref_i8) \
    X("ptr_deref_i16", builtin_ptr_deref_i16) \
    X("ptr_deref_i64", builtin_ptr_deref_i64) \
    X("ptr_deref_u8", builtin_ptr_deref_u8) \
    X("ptr_deref_u16", builtin_ptr_deref_u16) \
    X("ptr_deref_u32", builtin_ptr_deref_u32) \
    X("ptr_deref_u64", builtin_ptr_deref_u64) \
    X("ptr_deref_f32", builtin_ptr_deref_f32) \
    X("ptr_deref_f64", builtin_ptr_deref_f64) \
    X("ptr_deref_ptr", builtin_ptr_deref_ptr) \
    /* Additional pointer write functions for all types */ \
    X("ptr_write_i8", builtin_ptr_write_i8) \
    X("ptr_write_i16", builtin_ptr_write_i16) \
    X("ptr_write_i64", builtin_ptr_write_i64) \
    X("ptr_write_u8", builtin_ptr_write_u8) \
    X("ptr_write_u16", builtin_ptr_write_u16) \
    X("ptr_write_u32", builtin_ptr_write_u32) \
    X("ptr_write_u64", builtin_ptr_write_u64) \
    X("ptr_write_f32", builtin_ptr_write_f32) \
    X("ptr_write_f64", builtin_ptr_write_f64) \
    X("ptr_write_ptr", builtin_ptr_write_ptr) \
    X("ptr_read_i8", builtin_ptr_read_i8) \
    X("ptr_read_i16", builtin_ptr_read_i16) \
    X("ptr_read_ptr", builtin_ptr_read_ptr) \
    X("ptr_read_i64", builtin_ptr_read_i64) \
    X("ptr_read_u8", builtin_ptr_read_u8) \
    X("ptr_read_u16", builtin_ptr_read_u16) \
    X("ptr_read_u32", builtin_ptr_read_u32) \
    X("ptr_read_u64", builtin_ptr_read_u64) \
    X("ptr_read_f32", builtin_ptr_read_f32) \
    X("ptr_read_f64", builtin_ptr_read_f64) \
    /* FFI utility functions */ \
    X("__ffi_sizeof", builtin_ffi_sizeof) \
    X("ptr_to_buffer", builtin_ptr_to_buffer) \
    X("buffer_ptr", builtin_buffer_ptr) \
    X("ptr_null", builtin_ptr_null) \
    /* Atomic operations (use @stdlib/atomic for public API) */ \
    X("__atomic_load_i32", builtin_atomic_load_i32) \
    X("__atomic_store_i32", builtin_atomic_store_i32) \
    X("__atomic_add_i32", builtin_atomic_add_i32) \
    X("__atomic_sub_i32", builtin_atomic_sub_i32) \
    X("__atomic_and_i32", builtin_atomic_and_i32) \
    X("__atomic_or_i32", builtin_atomic_or_i32) \
    X("__atomic_xor_i32", builtin_atomic_xor_i32) \
    X("__atomic_cas_i32", builtin_atomic_cas_i32) \
    X("__atomic_exchange_i32", builtin_atomic_exchange_i32) \
    X("__atomic_load_i64", builtin_atomic_load_i64) \
    X("__atomic_store_i64", builtin_atomic_store_i64) \
    X("__atomic_add_i64", builtin_atomic_add_i64) \
    X("__atomic_sub_i64", builtin_atomic_sub_i64) \
    X("__atomic_and_i64", builtin_atomic_and_i64) \
    X("__atomic_or_i64", builtin_atomic_or_i64) \
    X("__atomic_xor_i64", builtin_atomic_xor_i64) \
    X("__atomic_cas_i64", builtin_atomic_cas_i64) \
    X("__atomic_exchange_i64", builtin_atomic_exchange_i64) \
    X("__atomic_fence", builtin_atomic_fence) \
    /* Byte order builtins (use @stdlib/bytes for public API) */ \
    X("__bswap16", builtin_bswap16) \
    X("__bswap32", builtin_bswap32) \
    X("__bswap64", builtin_bswap64) \
    X("__htons", builtin_htons) \
    X("__htonl", builtin_htonl) \
    X("__htonll", builtin_htonll) \
    X("__ntohs", builtin_ntohs) \
    X("__ntohl", builtin_ntohl) \
    X("__ntohll", builtin_ntohll) \
    X("__is_little_endian", builtin_is_little_endian) \
    X("__read_u16_be", builtin_read_u16_be) \
    X("__read_u16_le", builtin_read_u16_le) \
    X("__read_u32_be", builtin_read_u32_be) \
    X("__read_u32_le", builtin_read_u32_le) \
    X("__read_u64_be", builtin_read_u64_be) \
    X("__read_u64_le", builtin_read_u64_le) \
    X("__write_u16_be", builtin_write_u16_be) \
    X("__write_u16_le", builtin_write_u16_le) \
    X("__write_u32_be", builtin_write_u32_be) \
    X("__write_u32_le", builtin_write_u32_le) \
    X("__write_u64_be", builtin_write_u64_be) \
    X("__write_u64_le", builtin_write_u64_le) \
    /* Memory-mapped file I/O builtins (use @stdlib/mmap for public API) */ \
    X("__mmap_open", builtin_mmap_open) \
    X("__mmap_open_anon", builtin_mmap_open_anon) \
    X("__mmap_sync", builtin_mmap_sync) \
    X("__mmap_close", builtin_mmap_close) \
    X("__mmap_size", builtin_mmap_size) \
    X("__mmap_advise", builtin_mmap_advise) \
    X("__mmap_protect", builtin_mmap_protect)

/* Type-value constants (i32, byte, ptr, ...): X(hemlock_name, TypeKind) */
#define HML_BUILTIN_TYPE_CONSTS(X) \
    X("i8", TYPE_I8) \
    X("i16", TYPE_I16) \
    X("i32", TYPE_I32) \
    X("i64", TYPE_I64) \
    X("u8", TYPE_U8) \
    X("u16", TYPE_U16) \
    X("u32", TYPE_U32) \
    X("u64", TYPE_U64) \
    X("f32", TYPE_F32) \
    X("f64", TYPE_F64) \
    X("bool", TYPE_BOOL) \
    X("rune", TYPE_RUNE) \
    X("ptr", TYPE_PTR) \
    X("integer", TYPE_I32) \
    X("number", TYPE_F64) \
    X("byte", TYPE_U8)

/* i32 constants (TYPEID_*, __SIGINT, ...): X(hemlock_name, c_value) */
#define HML_BUILTIN_I32_CONSTS(X) \
    X("TYPEID_I8", HML_TYPEID_I8) \
    X("TYPEID_I16", HML_TYPEID_I16) \
    X("TYPEID_I32", HML_TYPEID_I32) \
    X("TYPEID_I64", HML_TYPEID_I64) \
    X("TYPEID_U8", HML_TYPEID_U8) \
    X("TYPEID_U16", HML_TYPEID_U16) \
    X("TYPEID_U32", HML_TYPEID_U32) \
    X("TYPEID_U64", HML_TYPEID_U64) \
    X("TYPEID_F32", HML_TYPEID_F32) \
    X("TYPEID_F64", HML_TYPEID_F64) \
    X("TYPEID_BOOL", HML_TYPEID_BOOL) \
    X("TYPEID_STRING", HML_TYPEID_STRING) \
    X("TYPEID_RUNE", HML_TYPEID_RUNE) \
    X("TYPEID_PTR", HML_TYPEID_PTR) \
    X("TYPEID_BUFFER", HML_TYPEID_BUFFER) \
    X("TYPEID_ARRAY", HML_TYPEID_ARRAY) \
    X("TYPEID_OBJECT", HML_TYPEID_OBJECT) \
    X("TYPEID_FILE", HML_TYPEID_FILE) \
    X("TYPEID_FUNCTION", HML_TYPEID_FUNCTION) \
    X("TYPEID_TASK", HML_TYPEID_TASK) \
    X("TYPEID_CHANNEL", HML_TYPEID_CHANNEL) \
    X("TYPEID_NULL", HML_TYPEID_NULL) \
    X("__SIGINT", SIGINT) \
    X("__SIGTERM", SIGTERM) \
    X("__SIGHUP", SIGHUP) \
    X("__SIGQUIT", SIGQUIT) \
    X("__SIGABRT", SIGABRT) \
    X("__SIGUSR1", SIGUSR1) \
    X("__SIGUSR2", SIGUSR2) \
    X("__SIGALRM", SIGALRM) \
    X("__SIGCHLD", SIGCHLD) \
    X("__SIGPIPE", SIGPIPE) \
    X("__SIGCONT", SIGCONT) \
    X("__SIGSTOP", SIGSTOP) \
    X("__SIGTSTP", SIGTSTP) \
    X("__SIGTTIN", SIGTTIN) \
    X("__SIGTTOU", SIGTTOU) \
    X("__AF_INET", AF_INET) \
    X("__AF_INET6", AF_INET6) \
    X("__AF_UNIX", AF_UNIX) \
    X("__SOCK_STREAM", SOCK_STREAM) \
    X("__SOCK_DGRAM", SOCK_DGRAM) \
    X("__IPPROTO_TCP", IPPROTO_TCP) \
    X("__IPPROTO_UDP", IPPROTO_UDP) \
    X("__SOL_SOCKET", SOL_SOCKET) \
    X("__SO_REUSEADDR", SO_REUSEADDR) \
    X("__SO_KEEPALIVE", SO_KEEPALIVE) \
    X("__SO_RCVTIMEO", SO_RCVTIMEO) \
    X("__SO_SNDTIMEO", SO_SNDTIMEO) \
    X("__POLLIN", POLLIN) \
    X("__POLLOUT", POLLOUT) \
    X("__POLLERR", POLLERR) \
    X("__POLLHUP", POLLHUP) \
    X("__POLLNVAL", POLLNVAL) \
    X("__POLLPRI", POLLPRI) \
    X("__STDIN_FILENO", STDIN_FILENO) \
    X("__STDOUT_FILENO", STDOUT_FILENO) \
    X("__STDERR_FILENO", STDERR_FILENO) \
    X("__PROT_NONE", PROT_NONE) \
    X("__PROT_READ", PROT_READ) \
    X("__PROT_WRITE", PROT_WRITE) \
    X("__PROT_EXEC", PROT_EXEC) \
    X("__MADV_NORMAL", MADV_NORMAL) \
    X("__MADV_SEQUENTIAL", MADV_SEQUENTIAL) \
    X("__MADV_RANDOM", MADV_RANDOM) \
    X("__MADV_WILLNEED", MADV_WILLNEED) \
    X("__MADV_DONTNEED", MADV_DONTNEED)

/* f64 constants (__PI, __NAN, ...): X(hemlock_name, c_value) */
#define HML_BUILTIN_F64_CONSTS(X) \
    X("__PI", M_PI) \
    X("__E", M_E) \
    X("__TAU", 2.0 * M_PI) \
    X("__INF", INFINITY) \
    X("__NAN", NAN)

/*
 * Globals the runtime builds from startup data rather than a constant:
 * X(hemlock_name). Registered by hand in register_builtins().
 */
#define HML_BUILTIN_DYNAMIC_GLOBALS(X) \
    X("args")

/*
 * Names hemlockc's code generator recognizes but the interpreter does not
 * register: `to_string(42)` compiles and runs, and throws "Undefined variable"
 * under the interpreter. That is a parity gap, not a design decision - see
 * tests/compiler/string_utils.hml, which exists because these only work
 * compiled. The name resolver accepts them for both backends so it never
 * rejects code that one of them runs today; when the gap is closed, the names
 * move into the lists above and this one shrinks.
 *
 * X(hemlock_name)
 */
#define HML_COMPILER_ONLY_BUILTINS(X) \
    X("__sizeof") \
    X("__string_byte_length") \
    X("__talloc") \
    X("__to_string") \
    X("atomic_add_i32") \
    X("atomic_add_i64") \
    X("atomic_and_i32") \
    X("atomic_and_i64") \
    X("atomic_cas_i32") \
    X("atomic_cas_i64") \
    X("atomic_exchange_i32") \
    X("atomic_exchange_i64") \
    X("atomic_fence") \
    X("atomic_load_i32") \
    X("atomic_load_i64") \
    X("atomic_or_i32") \
    X("atomic_or_i64") \
    X("atomic_store_i32") \
    X("atomic_store_i64") \
    X("atomic_sub_i32") \
    X("atomic_sub_i64") \
    X("atomic_xor_i32") \
    X("atomic_xor_i64") \
    X("bswap16") \
    X("bswap32") \
    X("bswap64") \
    X("cstr_to_string") \
    X("fileno") \
    X("get_stack_limit") \
    X("htonl") \
    X("htonll") \
    X("htons") \
    X("is_little_endian") \
    X("ntohl") \
    X("ntohll") \
    X("ntohs") \
    X("open") \
    X("open_fd") \
    X("println") \
    X("read_u16_be") \
    X("read_u16_le") \
    X("read_u32_be") \
    X("read_u32_le") \
    X("read_u64_be") \
    X("read_u64_le") \
    X("set_stack_limit") \
    X("strerror") \
    X("string_byte_length") \
    X("string_concat_many") \
    X("string_to_cstr") \
    X("task_debug_info") \
    X("to_string") \
    X("write_u16_be") \
    X("write_u16_le") \
    X("write_u32_be") \
    X("write_u32_le") \
    X("write_u64_be") \
    X("write_u64_le")

#endif /* HEMLOCK_BUILTIN_GLOBALS_H */
