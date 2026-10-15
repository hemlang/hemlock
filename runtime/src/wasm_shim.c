/*
 * Hemlock Runtime Library - WASM Shim Layer
 *
 * Provides stub implementations for POSIX-dependent functions that are
 * excluded from the WASM build. Each stub either panics with a clear
 * error message or returns a sensible default.
 *
 * This file is ONLY compiled when targeting Emscripten (__EMSCRIPTEN__).
 * On native builds, the real implementations in builtins_*.c are used.
 */

#ifdef __EMSCRIPTEN__

#include "../include/hemlock_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ========== HELPER MACROS ==========

#define WASM_STUB_PANIC(name) \
    hml_runtime_error(name "() is not available in WebAssembly")

#define WASM_STUB_PANIC_RETURN(name) \
    do { hml_runtime_error(name "() is not available in WebAssembly"); return hml_val_null(); } while(0)

// ========== PROCESS BUILTINS (builtins_process.c) ==========

HmlValue hml_exec(HmlValue command) {
    (void)command;
    WASM_STUB_PANIC_RETURN("exec");
}

HmlValue hml_exec_argv(HmlValue args_array, HmlValue opts) {
    (void)args_array; (void)opts;
    WASM_STUB_PANIC_RETURN("exec_argv");
}

HmlValue hml_exec_with_args(HmlValue command, HmlValue args_array) {
    (void)command; (void)args_array;
    WASM_STUB_PANIC_RETURN("exec_with_args");
}

// Environment operations - these work via Emscripten's in-memory env
HmlValue hml_getenv(HmlValue name) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        hml_runtime_error("getenv() argument must be a string");
    }
    char *val = getenv(name.as.as_string->data);
    if (val == NULL) {
        return hml_val_null();
    }
    return hml_val_string(val);
}

void hml_setenv(HmlValue name, HmlValue value) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        hml_runtime_error("setenv() name must be a string");
    }
    if (value.type != HML_VAL_STRING || !value.as.as_string) {
        hml_runtime_error("setenv() value must be a string");
    }
    setenv(name.as.as_string->data, value.as.as_string->data, 1);
}

void hml_exit(HmlValue code) {
    exit(hml_to_i32(code));
}

HmlValue hml_get_pid(void) {
    return hml_val_i32(1);  // WASM has no PID concept
}

HmlValue hml_getppid(void) { return hml_val_i32(0); }
HmlValue hml_getuid(void) { return hml_val_i32(0); }
HmlValue hml_geteuid(void) { return hml_val_i32(0); }
HmlValue hml_getgid(void) { return hml_val_i32(0); }
HmlValue hml_getegid(void) { return hml_val_i32(0); }

HmlValue hml_unsetenv(HmlValue name) {
    if (name.type != HML_VAL_STRING || !name.as.as_string) {
        hml_runtime_error("unsetenv() argument must be a string");
    }
    unsetenv(name.as.as_string->data);
    return hml_val_null();
}

HmlValue hml_kill(HmlValue pid, HmlValue sig) {
    (void)pid; (void)sig;
    WASM_STUB_PANIC_RETURN("kill");
}

HmlValue hml_fork(void) {
    WASM_STUB_PANIC_RETURN("fork");
}

HmlValue hml_wait(void) {
    WASM_STUB_PANIC_RETURN("wait");
}

HmlValue hml_waitpid(HmlValue pid, HmlValue options) {
    (void)pid; (void)options;
    WASM_STUB_PANIC_RETURN("waitpid");
}

HmlValue hml_posix_spawn(HmlValue argv, HmlValue opts) {
    (void)argv; (void)opts;
    WASM_STUB_PANIC_RETURN("spawn");
}

void hml_abort(void) {
    fprintf(stderr, "abort() called in WASM\n");
    exit(134);  // SIGABRT exit code
}

HmlValue hml_signal(HmlValue signum, HmlValue handler) {
    (void)signum; (void)handler;
    WASM_STUB_PANIC_RETURN("signal");
}

HmlValue hml_raise(HmlValue signum) {
    (void)signum;
    WASM_STUB_PANIC_RETURN("raise");
}

// Process builtin wrappers
HmlValue hml_builtin_getenv(HmlClosureEnv *env, HmlValue name) {
    (void)env;
    return hml_getenv(name);
}

HmlValue hml_builtin_setenv(HmlClosureEnv *env, HmlValue name, HmlValue value) {
    (void)env;
    hml_setenv(name, value);
    return hml_val_null();
}

HmlValue hml_builtin_exit(HmlClosureEnv *env, HmlValue code) {
    (void)env;
    hml_exit(code);
    return hml_val_null();  // unreachable
}

HmlValue hml_builtin_get_pid(HmlClosureEnv *env) {
    (void)env;
    return hml_get_pid();
}

HmlValue hml_builtin_exec(HmlClosureEnv *env, HmlValue command) {
    (void)env;
    return hml_exec(command);
}

HmlValue hml_builtin_exec_with_args(HmlClosureEnv *env, HmlValue command, HmlValue args_array) {
    (void)env;
    return hml_exec_with_args(command, args_array);
}

HmlValue hml_builtin_exec_argv(HmlClosureEnv *env, HmlValue args_array, HmlValue opts) {
    (void)env;
    return hml_exec_argv(args_array, opts);
}

HmlValue hml_builtin_getppid(HmlClosureEnv *env) { (void)env; return hml_getppid(); }
HmlValue hml_builtin_getuid(HmlClosureEnv *env) { (void)env; return hml_getuid(); }
HmlValue hml_builtin_geteuid(HmlClosureEnv *env) { (void)env; return hml_geteuid(); }
HmlValue hml_builtin_getgid(HmlClosureEnv *env) { (void)env; return hml_getgid(); }
HmlValue hml_builtin_getegid(HmlClosureEnv *env) { (void)env; return hml_getegid(); }
HmlValue hml_builtin_unsetenv(HmlClosureEnv *env, HmlValue name) { (void)env; return hml_unsetenv(name); }
HmlValue hml_builtin_kill(HmlClosureEnv *env, HmlValue pid, HmlValue sig) { (void)env; return hml_kill(pid, sig); }
HmlValue hml_builtin_fork(HmlClosureEnv *env) { (void)env; return hml_fork(); }
HmlValue hml_builtin_wait(HmlClosureEnv *env) { (void)env; return hml_wait(); }
HmlValue hml_builtin_waitpid(HmlClosureEnv *env, HmlValue pid, HmlValue options) { (void)env; return hml_waitpid(pid, options); }
HmlValue hml_builtin_abort(HmlClosureEnv *env) { (void)env; hml_abort(); return hml_val_null(); }

// ========== SOCKET BUILTINS (builtins_socket.c) ==========

HmlValue hml_socket_create(HmlValue d, HmlValue t, HmlValue p) { (void)d;(void)t;(void)p; WASM_STUB_PANIC_RETURN("socket_create"); }
void hml_socket_bind(HmlValue s, HmlValue a, HmlValue p) { (void)s;(void)a;(void)p; WASM_STUB_PANIC("socket_bind"); }
void hml_socket_listen(HmlValue s, HmlValue b) { (void)s;(void)b; WASM_STUB_PANIC("socket_listen"); }
HmlValue hml_socket_accept(HmlValue s) { (void)s; WASM_STUB_PANIC_RETURN("socket_accept"); }
void hml_socket_connect(HmlValue s, HmlValue a, HmlValue p) { (void)s;(void)a;(void)p; WASM_STUB_PANIC("socket_connect"); }
void hml_socket_close(HmlValue s) { (void)s; WASM_STUB_PANIC("socket_close"); }
// Sockets can never be created on WASM (socket_create panics), but hml_release
// dispatches here for HML_VAL_SOCKET values; free defensively without closing.
void hml_socket_destroy(HmlSocket *sock) { if (sock) { free(sock->address); free(sock); } }
HmlValue hml_socket_send(HmlValue s, HmlValue d) { (void)s;(void)d; WASM_STUB_PANIC_RETURN("socket_send"); }
HmlValue hml_socket_recv(HmlValue s, HmlValue sz) { (void)s;(void)sz; WASM_STUB_PANIC_RETURN("socket_recv"); }
HmlValue hml_socket_sendto(HmlValue s, HmlValue a, HmlValue p, HmlValue d) { (void)s;(void)a;(void)p;(void)d; WASM_STUB_PANIC_RETURN("socket_sendto"); }
HmlValue hml_socket_recvfrom(HmlValue s, HmlValue sz) { (void)s;(void)sz; WASM_STUB_PANIC_RETURN("socket_recvfrom"); }
void hml_socket_setsockopt(HmlValue s, HmlValue l, HmlValue o, HmlValue v) { (void)s;(void)l;(void)o;(void)v; WASM_STUB_PANIC("socket_setsockopt"); }
void hml_socket_set_timeout(HmlValue s, HmlValue t) { (void)s;(void)t; WASM_STUB_PANIC("socket_set_timeout"); }
void hml_socket_set_nonblocking(HmlValue s, HmlValue e) { (void)s;(void)e; WASM_STUB_PANIC("socket_set_nonblocking"); }
HmlValue hml_socket_get_fd(HmlValue s) { (void)s; WASM_STUB_PANIC_RETURN("socket_get_fd"); }
HmlValue hml_socket_get_address(HmlValue s) { (void)s; WASM_STUB_PANIC_RETURN("socket_get_address"); }
HmlValue hml_socket_get_port(HmlValue s) { (void)s; WASM_STUB_PANIC_RETURN("socket_get_port"); }
HmlValue hml_socket_get_closed(HmlValue s) { (void)s; WASM_STUB_PANIC_RETURN("socket_get_closed"); }
HmlValue hml_dns_resolve(HmlValue h) { (void)h; WASM_STUB_PANIC_RETURN("dns_resolve"); }

// Socket builtin wrappers
HmlValue hml_builtin_dns_resolve(HmlClosureEnv *env, HmlValue h) { (void)env; return hml_dns_resolve(h); }
HmlValue hml_builtin_socket_create(HmlClosureEnv *env, HmlValue d, HmlValue t, HmlValue p) { (void)env; return hml_socket_create(d,t,p); }
HmlValue hml_builtin_socket_bind(HmlClosureEnv *env, HmlValue s, HmlValue a, HmlValue p) { (void)env; hml_socket_bind(s,a,p); return hml_val_null(); }
HmlValue hml_builtin_socket_listen(HmlClosureEnv *env, HmlValue s, HmlValue b) { (void)env; hml_socket_listen(s,b); return hml_val_null(); }
HmlValue hml_builtin_socket_accept(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_socket_accept(s); }
HmlValue hml_builtin_socket_connect(HmlClosureEnv *env, HmlValue s, HmlValue a, HmlValue p) { (void)env; hml_socket_connect(s,a,p); return hml_val_null(); }
HmlValue hml_builtin_socket_close(HmlClosureEnv *env, HmlValue s) { (void)env; hml_socket_close(s); return hml_val_null(); }
HmlValue hml_builtin_socket_send(HmlClosureEnv *env, HmlValue s, HmlValue d) { (void)env; return hml_socket_send(s,d); }
HmlValue hml_builtin_socket_recv(HmlClosureEnv *env, HmlValue s, HmlValue sz) { (void)env; return hml_socket_recv(s,sz); }
HmlValue hml_builtin_socket_sendto(HmlClosureEnv *env, HmlValue s, HmlValue a, HmlValue p, HmlValue d) { (void)env; return hml_socket_sendto(s,a,p,d); }
HmlValue hml_builtin_socket_recvfrom(HmlClosureEnv *env, HmlValue s, HmlValue sz) { (void)env; return hml_socket_recvfrom(s,sz); }
HmlValue hml_builtin_socket_setsockopt(HmlClosureEnv *env, HmlValue s, HmlValue l, HmlValue o, HmlValue v) { (void)env; hml_socket_setsockopt(s,l,o,v); return hml_val_null(); }
HmlValue hml_builtin_socket_get_fd(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_socket_get_fd(s); }
HmlValue hml_builtin_socket_get_address(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_socket_get_address(s); }
HmlValue hml_builtin_socket_get_port(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_socket_get_port(s); }
HmlValue hml_builtin_socket_get_closed(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_socket_get_closed(s); }

// ========== FFI BUILTINS (builtins_ffi.c) ==========

HmlValue hml_ffi_load(const char *path) { (void)path; WASM_STUB_PANIC_RETURN("ffi_open"); }
HmlValue hml_ffi_load_import(const char *path) {
    fprintf(stderr, "Warning: FFI import '%s' ignored in WASM build\n", path);
    return hml_val_null();
}
void hml_ffi_close(HmlValue lib) { (void)lib; WASM_STUB_PANIC("ffi_close"); }
void* hml_ffi_sym(HmlValue lib, const char *name) { (void)lib;(void)name; WASM_STUB_PANIC("ffi_sym"); return NULL; }
HmlValue hml_ffi_call(void *fp, HmlValue *args, int n, HmlFFIType *types) { (void)fp;(void)args;(void)n;(void)types; WASM_STUB_PANIC_RETURN("ffi_call"); }
HmlValue hml_ffi_call_with_structs(void *fp, HmlValue *args, int n, HmlFFIType *types, const char **sn) { (void)fp;(void)args;(void)n;(void)types;(void)sn; WASM_STUB_PANIC_RETURN("ffi_call"); }
HmlFFIStructType* hml_ffi_register_struct(const char *name, const char **fn, HmlFFIType *ft, int nf) { (void)name;(void)fn;(void)ft;(void)nf; WASM_STUB_PANIC("ffi_register_struct"); return NULL; }
HmlFFIStructType* hml_ffi_lookup_struct(const char *name) { (void)name; return NULL; }
void* hml_ffi_object_to_struct(HmlValue obj, HmlFFIStructType *st) { (void)obj;(void)st; WASM_STUB_PANIC("ffi_object_to_struct"); return NULL; }
HmlValue hml_ffi_struct_to_object(void *sp, HmlFFIStructType *st) { (void)sp;(void)st; WASM_STUB_PANIC_RETURN("ffi_struct_to_object"); }
void hml_ffi_struct_cleanup(void) { /* no-op */ }
HmlFFICallback* hml_ffi_callback_create(HmlValue fn, HmlFFIType *pt, int np, HmlFFIType rt) { (void)fn;(void)pt;(void)np;(void)rt; WASM_STUB_PANIC("ffi_callback_create"); return NULL; }
void* hml_ffi_callback_ptr(HmlFFICallback *cb) { (void)cb; return NULL; }
void hml_ffi_callback_free(HmlFFICallback *cb) { (void)cb; }
int hml_ffi_callback_free_by_ptr(void *ptr) { (void)ptr; return 0; }

HmlValue hml_builtin_callback(HmlClosureEnv *env, HmlValue fn, HmlValue pt, HmlValue rt) { (void)env;(void)fn;(void)pt;(void)rt; WASM_STUB_PANIC_RETURN("callback"); }
HmlValue hml_builtin_callback_free(HmlClosureEnv *env, HmlValue ptr) { (void)env;(void)ptr; WASM_STUB_PANIC_RETURN("callback_free"); }

// NOTE: ptr_offset / ptr_deref_* / ptr_read_* / ptr_write_* are deliberately
// NOT stubbed here. They are pure pointer arithmetic over linear memory and
// are compiled for WASM from builtins_ffi.c like every other target.

// ========== ASYNC BUILTINS (builtins_async.c) ==========
// When compiled with -pthread (__EMSCRIPTEN_PTHREADS__), builtins_async.c provides
// real implementations using pthreads mapped to Web Workers. These stubs are only
// needed for non-threaded WASM builds.

#if !defined(__EMSCRIPTEN_PTHREADS__)

HmlValue hml_spawn(HmlValue fn, HmlValue *args, int num_args) {
    (void)fn; (void)args; (void)num_args;
    WASM_STUB_PANIC_RETURN("spawn");
}

HmlValue hml_join(HmlValue task_val) {
    (void)task_val;
    WASM_STUB_PANIC_RETURN("join");
}

void hml_detach(HmlValue task_val) {
    (void)task_val;
    WASM_STUB_PANIC("detach");
}

void hml_task_debug_info(HmlValue task_val) {
    (void)task_val;
    fprintf(stderr, "task_debug_info() not available in WASM (compile with --threads)\n");
}

HmlValue hml_apply(HmlValue fn, HmlValue args_array) {
    (void)fn; (void)args_array;
    WASM_STUB_PANIC_RETURN("apply");
}

HmlValue hml_channel(int32_t capacity) {
    (void)capacity;
    WASM_STUB_PANIC_RETURN("channel");
}

void hml_channel_send(HmlValue channel, HmlValue value) {
    (void)channel; (void)value;
    WASM_STUB_PANIC("channel_send");
}

HmlValue hml_channel_recv(HmlValue channel) {
    (void)channel;
    WASM_STUB_PANIC_RETURN("channel_recv");
}

HmlValue hml_channel_recv_timeout(HmlValue channel, HmlValue timeout_val) {
    (void)channel; (void)timeout_val;
    WASM_STUB_PANIC_RETURN("channel_recv_timeout");
}

HmlValue hml_channel_send_timeout(HmlValue channel, HmlValue value, HmlValue timeout_val) {
    (void)channel; (void)value; (void)timeout_val;
    WASM_STUB_PANIC_RETURN("channel_send_timeout");
}

void hml_channel_close(HmlValue channel) {
    (void)channel;
    WASM_STUB_PANIC("channel_close");
}

HmlValue hml_select(HmlValue channels, HmlValue timeout) {
    (void)channels; (void)timeout;
    WASM_STUB_PANIC_RETURN("select");
}

#endif // !__EMSCRIPTEN_PTHREADS__

// poll() stub is always needed for WASM - POSIX poll not available even with pthreads
HmlValue hml_poll(HmlValue fds, HmlValue timeout) {
    (void)fds; (void)timeout;
    WASM_STUB_PANIC_RETURN("poll");
}

// ========== HTTP BUILTINS (builtins_http.c) ==========
// NOTE: HTTP GET/POST/request and response accessors are now implemented
// in wasm_bridge.c using synchronous XMLHttpRequest via EM_JS.
// Only WebSocket stubs remain here (not yet bridged to browser WebSocket API).

HmlValue hml_lws_ws_connect(HmlValue url) { (void)url; WASM_STUB_PANIC_RETURN("ws_connect"); }
HmlValue hml_lws_ws_send_text(HmlValue c, HmlValue t) { (void)c;(void)t; WASM_STUB_PANIC_RETURN("ws_send_text"); }
HmlValue hml_lws_ws_send_binary(HmlValue c, HmlValue b) { (void)c;(void)b; WASM_STUB_PANIC_RETURN("ws_send_binary"); }
HmlValue hml_lws_ws_recv(HmlValue c, HmlValue t) { (void)c;(void)t; WASM_STUB_PANIC_RETURN("ws_recv"); }
HmlValue hml_lws_ws_close(HmlValue c) { (void)c; WASM_STUB_PANIC_RETURN("ws_close"); }
HmlValue hml_lws_ws_is_closed(HmlValue c) { (void)c; WASM_STUB_PANIC_RETURN("ws_is_closed"); }
HmlValue hml_lws_msg_type(HmlValue m) { (void)m; WASM_STUB_PANIC_RETURN("msg_type"); }
HmlValue hml_lws_msg_text(HmlValue m) { (void)m; WASM_STUB_PANIC_RETURN("msg_text"); }
HmlValue hml_lws_msg_len(HmlValue m) { (void)m; WASM_STUB_PANIC_RETURN("msg_len"); }
HmlValue hml_lws_msg_binary(HmlValue m) { (void)m; WASM_STUB_PANIC_RETURN("msg_binary"); }
HmlValue hml_lws_msg_free(HmlValue m) { (void)m; return hml_val_null(); }
HmlValue hml_lws_ws_server_create(HmlValue h, HmlValue p) { (void)h;(void)p; WASM_STUB_PANIC_RETURN("ws_server_create"); }
HmlValue hml_lws_ws_server_accept(HmlValue s, HmlValue t) { (void)s;(void)t; WASM_STUB_PANIC_RETURN("ws_server_accept"); }
HmlValue hml_lws_ws_server_close(HmlValue s) { (void)s; WASM_STUB_PANIC_RETURN("ws_server_close"); }

// WebSocket builtin wrappers (stubs)
HmlValue hml_builtin_lws_ws_connect(HmlClosureEnv *env, HmlValue url) { (void)env; return hml_lws_ws_connect(url); }
HmlValue hml_builtin_lws_ws_send_text(HmlClosureEnv *env, HmlValue c, HmlValue t) { (void)env; return hml_lws_ws_send_text(c,t); }
HmlValue hml_builtin_lws_ws_send_binary(HmlClosureEnv *env, HmlValue c, HmlValue b) { (void)env; return hml_lws_ws_send_binary(c,b); }
HmlValue hml_builtin_lws_ws_recv(HmlClosureEnv *env, HmlValue c, HmlValue t) { (void)env; return hml_lws_ws_recv(c,t); }
HmlValue hml_builtin_lws_ws_close(HmlClosureEnv *env, HmlValue c) { (void)env; return hml_lws_ws_close(c); }
HmlValue hml_builtin_lws_ws_is_closed(HmlClosureEnv *env, HmlValue c) { (void)env; return hml_lws_ws_is_closed(c); }
HmlValue hml_builtin_lws_msg_type(HmlClosureEnv *env, HmlValue m) { (void)env; return hml_lws_msg_type(m); }
HmlValue hml_builtin_lws_msg_text(HmlClosureEnv *env, HmlValue m) { (void)env; return hml_lws_msg_text(m); }
HmlValue hml_builtin_lws_msg_len(HmlClosureEnv *env, HmlValue m) { (void)env; return hml_lws_msg_len(m); }
HmlValue hml_builtin_lws_msg_binary(HmlClosureEnv *env, HmlValue m) { (void)env; return hml_lws_msg_binary(m); }
HmlValue hml_builtin_lws_msg_free(HmlClosureEnv *env, HmlValue m) { (void)env; return hml_lws_msg_free(m); }
HmlValue hml_builtin_lws_ws_server_create(HmlClosureEnv *env, HmlValue h, HmlValue p) { (void)env; return hml_lws_ws_server_create(h,p); }
HmlValue hml_builtin_lws_ws_server_accept(HmlClosureEnv *env, HmlValue s, HmlValue t) { (void)env; return hml_lws_ws_server_accept(s,t); }
HmlValue hml_builtin_lws_ws_server_close(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_lws_ws_server_close(s); }

// ========== CRYPTO BUILTINS (builtins_crypto.c) ==========
// NOTE: SHA-256, SHA-512, MD5 hash functions and random_bytes are now
// implemented in wasm_bridge.c using pure C implementations.
// Only compression and ECDSA stubs remain here (not yet ported).

HmlValue hml_zlib_compress(HmlValue data, HmlValue level) { (void)data;(void)level; WASM_STUB_PANIC_RETURN("zlib_compress"); }
HmlValue hml_zlib_decompress(HmlValue data, HmlValue max_size) { (void)data;(void)max_size; WASM_STUB_PANIC_RETURN("zlib_decompress"); }
HmlValue hml_gzip_compress(HmlValue data, HmlValue level) { (void)data;(void)level; WASM_STUB_PANIC_RETURN("gzip_compress"); }
HmlValue hml_gzip_decompress(HmlValue data, HmlValue max_size) { (void)data;(void)max_size; WASM_STUB_PANIC_RETURN("gzip_decompress"); }
HmlValue hml_zlib_compress_bound(HmlValue source_len) { (void)source_len; WASM_STUB_PANIC_RETURN("zlib_compress_bound"); }
HmlValue hml_crc32_val(HmlValue data) { (void)data; WASM_STUB_PANIC_RETURN("crc32"); }
HmlValue hml_adler32_val(HmlValue data) { (void)data; WASM_STUB_PANIC_RETURN("adler32"); }

HmlValue hml_ecdsa_generate_key(HmlValue curve) { (void)curve; WASM_STUB_PANIC_RETURN("ecdsa_generate_key"); }
HmlValue hml_ecdsa_free_key(HmlValue keypair) { (void)keypair; WASM_STUB_PANIC_RETURN("ecdsa_free_key"); }
HmlValue hml_ecdsa_sign(HmlValue data, HmlValue keypair) { (void)data;(void)keypair; WASM_STUB_PANIC_RETURN("ecdsa_sign"); }
HmlValue hml_ecdsa_verify(HmlValue data, HmlValue sig, HmlValue keypair) { (void)data;(void)sig;(void)keypair; WASM_STUB_PANIC_RETURN("ecdsa_verify"); }

// Crypto builtin wrappers (compression and ECDSA stubs)
HmlValue hml_builtin_zlib_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) { (void)env; return hml_zlib_compress(data,level); }
HmlValue hml_builtin_zlib_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) { (void)env; return hml_zlib_decompress(data,max_size); }
HmlValue hml_builtin_gzip_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) { (void)env; return hml_gzip_compress(data,level); }
HmlValue hml_builtin_gzip_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) { (void)env; return hml_gzip_decompress(data,max_size); }
HmlValue hml_builtin_zlib_compress_bound(HmlClosureEnv *env, HmlValue source_len) { (void)env; return hml_zlib_compress_bound(source_len); }
HmlValue hml_builtin_crc32(HmlClosureEnv *env, HmlValue data) { (void)env; return hml_crc32_val(data); }
HmlValue hml_builtin_adler32(HmlClosureEnv *env, HmlValue data) { (void)env; return hml_adler32_val(data); }
HmlValue hml_builtin_ecdsa_generate_key(HmlClosureEnv *env, HmlValue curve) { (void)env; return hml_ecdsa_generate_key(curve); }
HmlValue hml_builtin_ecdsa_free_key(HmlClosureEnv *env, HmlValue keypair) { (void)env; return hml_ecdsa_free_key(keypair); }
HmlValue hml_builtin_ecdsa_sign(HmlClosureEnv *env, HmlValue data, HmlValue keypair) { (void)env; return hml_ecdsa_sign(data,keypair); }
HmlValue hml_builtin_ecdsa_verify(HmlClosureEnv *env, HmlValue data, HmlValue sig, HmlValue keypair) { (void)env; return hml_ecdsa_verify(data,sig,keypair); }

#endif // __EMSCRIPTEN__
