/*
 * Hemlock Runtime Library - Core Builtins
 *
 * This file implements core builtin functions:
 * - Global state management
 * - Sandbox functions
 * - Runtime initialization
 * - Print/eprint
 * - Type checking and conversion
 * - Assertions and panic
 *
 * Uses shared UTF-8 module for encoding.
 */

#include "builtins_internal.h"
#include "utf8.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// ========== GLOBAL STATE ==========

int g_argc = 0;
char **g_argv = NULL;
__thread HmlExceptionContext *g_exception_stack = NULL;
__thread DeferEntry *g_defer_stack = NULL;

// Sandbox state
static int g_sandbox_flags = 0;
static char *g_sandbox_root = NULL;

// ========== SANDBOX FUNCTIONS ==========

void hml_sandbox_init(int flags, const char *root_path) {
    g_sandbox_flags = flags;
    if (g_sandbox_root) {
        free(g_sandbox_root);
        g_sandbox_root = NULL;
    }
    if (root_path) {
        g_sandbox_root = strdup(root_path);
    }
}

int hml_sandbox_check(int restriction_flag) {
    return (g_sandbox_flags & restriction_flag) != 0;
}

int hml_sandbox_path_allowed(const char *path, int is_write) {
    // Check file write restriction
    if (is_write && hml_sandbox_check(HML_SANDBOX_RESTRICT_FILE_WRITE)) {
        return 0;
    }

    // Check file read restriction (only when sandbox_root is set)
    if (!is_write && hml_sandbox_check(HML_SANDBOX_RESTRICT_FILE_READ)) {
        if (!g_sandbox_root) {
            return 0;  // No root = all reads blocked
        }
    }

    // If sandbox_root is set, validate path is within it
    if (g_sandbox_root) {
        char resolved_path[PATH_MAX];
        char resolved_root[PATH_MAX];

        // Resolve both paths to absolute form
        if (!realpath(path, resolved_path)) {
            // Path doesn't exist yet - try resolving parent directory
            char *path_copy = strdup(path);
            char *last_slash = strrchr(path_copy, '/');
            if (last_slash) {
                *last_slash = '\0';
                if (!realpath(path_copy, resolved_path)) {
                    free(path_copy);
                    return 0;  // Parent doesn't exist
                }
            } else {
                // Relative path without directory - use cwd
                if (!getcwd(resolved_path, sizeof(resolved_path))) {
                    free(path_copy);
                    return 0;
                }
            }
            free(path_copy);
        }

        if (!realpath(g_sandbox_root, resolved_root)) {
            return 0;  // Sandbox root doesn't exist
        }

        // Check if resolved path starts with resolved root
        size_t root_len = strlen(resolved_root);
        if (strncmp(resolved_path, resolved_root, root_len) != 0) {
            return 0;  // Path is outside sandbox root
        }

        // Make sure it's not just a prefix match (e.g., /foo vs /foobar)
        if (resolved_path[root_len] != '\0' && resolved_path[root_len] != '/') {
            return 0;
        }
    }

    return 1;  // Allowed
}

void hml_sandbox_error(const char *operation) {
    fprintf(stderr, "Sandbox violation: %s is not allowed in sandbox mode\n", operation);
    exit(1);
}

// ========== RUNTIME INITIALIZATION ==========

// Pin /dev/null to fds 0/1/2 if any of them are closed at process start.
// macOS-only motivation: libwebsockets' Homebrew build pulls in
// LWS_WITH_PLUGINS_BUILTIN, whose sshd / raw-test demo plugins close
// stdin during their first lws_create_context() init. After that, every
// open(), socket(), pipe() etc. that returns the lowest free fd lands
// on fd 0 — which any code that thinks "fd 0 is stdin, ignore it"
// (libwebsockets does this for /dev/urandom, emitting "ZERO RANDOM FD"
// and falling back to a degraded-randomness path that intermittently
// stalls SSL handshakes for minutes) silently mishandles. Worse, the
// next thing that closes "stdin" obliterates whatever real fd landed
// there. Pinning /dev/null on 0/1/2 makes the lowest free fd always be
// >= 3, so the trap never closes.
//
// No-op when fds are already open (the common case under nohup / a TTY).
// Linux unaffected — its libwebsockets is built without the demo
// plugins, so stdin stays where it is.
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>
#include <string.h>
#ifdef __APPLE__
#include <pthread.h>
#include <sys/resource.h>
#endif
void hml_runtime_pin_stdio(void) {
    for (int fd = 0; fd <= 2; fd++) {
        if (fcntl(fd, F_GETFD) != -1) continue;  // already open
        int devnull = open("/dev/null", (fd == 0) ? O_RDONLY : O_WRONLY);
        if (devnull < 0) continue;
        if (devnull != fd) {
            dup2(devnull, fd);
            close(devnull);
        }
    }
}

// Fatal-signal backtrace handler. Hemlock binaries are built with
// debug_info and not stripped, so glibc's backtrace_symbols_fd()
// resolves to real function names with zero external tooling — the
// difference between "status=11/SEGV, no idea where" and a stack on
// the very first crash, which matters for long-running daemons on
// boxes without gdb/valgrind installed.
//
// The handler is intentionally minimal and async-signal-safe-ish:
// backtrace()/backtrace_symbols_fd() are documented safe to call from
// a signal handler. After dumping, it restores the default disposition
// and re-raises so the original signal still produces the normal
// exit status / core-dump behavior (systemd still sees 11/SEGV or
// 6/ABRT, restart policy unaffected).
//
// Opt out with HML_NO_CRASH_HANDLER=1 (e.g. if a program installs its
// own SIGSEGV handler via the signal() builtin and they'd conflict).
static void hml_fatal_signal_handler(int sig) {
    static volatile sig_atomic_t in_handler = 0;
    if (in_handler) { _exit(128 + sig); }  // crashed inside the handler
    in_handler = 1;

    const char *name = "signal";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV (invalid memory access)"; break;
        case SIGABRT: name = "SIGABRT (abort / heap corruption detected)"; break;
        case SIGBUS:  name = "SIGBUS (bad memory alignment/access)"; break;
        case SIGFPE:  name = "SIGFPE (arithmetic exception)"; break;
        case SIGILL:  name = "SIGILL (illegal instruction)"; break;
        default: break;
    }
    char hdr[160];
    int n = snprintf(hdr, sizeof(hdr),
        "\n*** Hemlock runtime: fatal %s — backtrace follows ***\n", name);
    if (n > 0) { ssize_t w = write(STDERR_FILENO, hdr, (size_t)n); (void)w; }

    void *frames[64];
    int nframes = backtrace(frames, 64);
    backtrace_symbols_fd(frames, nframes, STDERR_FILENO);

    // Restore default and re-raise so the real exit status is preserved.
    signal(sig, SIG_DFL);
    raise(sig);
}

static void hml_runtime_install_crash_handler(void) {
    const char *off = getenv("HML_NO_CRASH_HANDLER");
    if (off && off[0] == '1') return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = hml_fatal_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;  // one-shot; re-raise hits default
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
}
#elif defined(_WIN32)
// Windows: no fd-0 trap (no libwebsockets plugins close stdin there) and
// no glibc backtrace machinery — both are POSIX-only concerns. Keep the
// exported symbol so callers (builtins_http.c) link unchanged.
void hml_runtime_pin_stdio(void) {}
#endif

void hml_runtime_init(int argc, char **argv) {
#ifndef __EMSCRIPTEN__
    hml_runtime_pin_stdio();
#ifndef _WIN32
    hml_runtime_install_crash_handler();
#endif
#endif
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    // macOS auto-downgrades the QoS of any process whose nice value
    // is positive — and `nohup` raises nice by 5, which silently
    // pushes a daemonized Hemlock program into a coalesced-timer
    // class where sleep(30) routinely runs 4-7 minutes long. Reset
    // nice to 0 BEFORE pinning QoS so the QoS choice actually
    // sticks. Errors ignored: setpriority() to a non-negative value
    // can't fail in practice (a positive nice can always be lowered;
    // we don't try to go negative which would need root). See the
    // task_thread_wrapper comment for why this matters for any
    // long-running heartbeat / poll loop.
    setpriority(PRIO_PROCESS, 0, 0);
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
    g_argc = argc;
    g_argv = argv;
    g_exception_stack = NULL;
    g_defer_stack = NULL;

#ifdef __EMSCRIPTEN__
    // Set up persistent filesystem via IDBFS (IndexedDB-backed)
    // Files written to /persistent/ survive page reloads in the browser
    EM_ASM(
        if (typeof window !== 'undefined') {
            FS.mkdir('/persistent');
            FS.mount(IDBFS, {}, '/persistent');
            FS.syncfs(true, function(err) {
                if (err) console.error('IDBFS load error:', err);
            });
        }
    );
#endif
}

#ifdef __EMSCRIPTEN__
// Sync IDBFS persistent filesystem to IndexedDB
void hml_wasm_fs_sync(void) {
    EM_ASM(
        if (typeof window !== 'undefined') {
            FS.syncfs(false, function(err) {
                if (err) console.error('IDBFS sync error:', err);
            });
        }
    );
}
#endif

int32_t hml_iter_length(HmlValue v) {
    switch (v.type) {
        case HML_VAL_OBJECT:
            return hml_object_num_fields(v);
        case HML_VAL_STRING:
            return hml_string_char_count(v).as.as_i32;
        default:
            return hml_array_length(v).as.as_i32;
    }
}

void hml_runtime_cleanup(void) {
#ifdef __EMSCRIPTEN__
    // Sync persistent filesystem before exit
    hml_wasm_fs_sync();
#endif

    // Execute remaining defers
    hml_defer_execute_all();

    // Clear exception stack
    while (g_exception_stack) {
        hml_exception_pop();
    }

    hml_uw_thread_cleanup();
}

HmlValue hml_get_args(void) {
    HmlValue arr = hml_val_array();

    // For compiled binaries, argv[0] is the program name which becomes args[0]
    // This matches interpreter behavior where args[0] is the script filename
    for (int i = 0; i < g_argc; i++) {
        HmlValue str = hml_val_string(g_argv[i]);
        hml_array_push(arr, str);
    }

    return arr;
}

// ========== UTF-8 ENCODING ==========
// Uses shared UTF-8 module for encoding

// Encode a Unicode codepoint to UTF-8, returns the number of bytes written
int utf8_encode_rune(uint32_t codepoint, char *out) {
    return hml_utf8_encode(codepoint, out);
}

// Alias for compatibility with string operations
int encode_utf8(uint32_t cp, char *out) {
    return hml_utf8_encode(cp, out);
}

// ========== PRINT IMPLEMENTATION ==========

// Helper to print a value to a file
void print_value_to(FILE *out, HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:
            fprintf(out, "%d", val.as.as_i8);
            break;
        case HML_VAL_I16:
            fprintf(out, "%d", val.as.as_i16);
            break;
        case HML_VAL_I32:
            fprintf(out, "%d", val.as.as_i32);
            break;
        case HML_VAL_I64:
            fprintf(out, "%" PRId64, val.as.as_i64);
            break;
        case HML_VAL_U8:
            fprintf(out, "%u", val.as.as_u8);
            break;
        case HML_VAL_U16:
            fprintf(out, "%u", val.as.as_u16);
            break;
        case HML_VAL_U32:
            fprintf(out, "%u", val.as.as_u32);
            break;
        case HML_VAL_U64:
            fprintf(out, "%" PRIu64, val.as.as_u64);
            break;
        case HML_VAL_F32:
            fprintf(out, "%g", val.as.as_f32);
            break;
        case HML_VAL_F64:
            fprintf(out, "%g", val.as.as_f64);
            break;
        case HML_VAL_BOOL:
            fprintf(out, "%s", val.as.as_bool ? "true" : "false");
            break;
        case HML_VAL_STRING:
            if (val.as.as_string) {
                fprintf(out, "%s", val.as.as_string->data);
            }
            break;
        case HML_VAL_RUNE: {
            // Print rune as character if printable, otherwise as U+XXXX (match interpreter behavior)
            uint32_t r = val.as.as_rune;
            if (r >= 32 && r < 127) {
                fprintf(out, "'%c'", (char)r);
            } else {
                fprintf(out, "U+%04X", r);
            }
            break;
        }
        case HML_VAL_NULL:
            fprintf(out, "null");
            break;
        case HML_VAL_PTR:
            // Match interpreter behavior: print 0x... instead of ptr<0x...>
            fprintf(out, "%p", val.as.as_ptr);
            break;
        case HML_VAL_BUFFER:
            if (val.as.as_buffer) {
                fprintf(out, "<buffer %p length=%d capacity=%d>",
                    (void*)val.as.as_buffer->data, val.as.as_buffer->length, val.as.as_buffer->capacity);
            } else {
                fprintf(out, "buffer[null]");
            }
            break;
        case HML_VAL_ARRAY:
            if (val.as.as_array) {
                fprintf(out, "[");
                for (int i = 0; i < val.as.as_array->length; i++) {
                    if (i > 0) fprintf(out, ", ");
                    // Print all elements consistently (no special quotes for strings)
                    print_value_to(out, val.as.as_array->elements[i]);
                }
                fprintf(out, "]");
            } else {
                fprintf(out, "[]");
            }
            break;
        case HML_VAL_OBJECT:
            // Match interpreter behavior: print <object> instead of JSON
            fprintf(out, "<object>");
            break;
        case HML_VAL_FUNCTION:
            fprintf(out, "<function>");
            break;
        case HML_VAL_BUILTIN_FN:
            fprintf(out, "<builtin>");
            break;
        case HML_VAL_TASK:
            fprintf(out, "<task>");
            break;
        case HML_VAL_CHANNEL:
            fprintf(out, "<channel>");
            break;
        case HML_VAL_FILE:
            fprintf(out, "<file>");
            break;
        default:
            fprintf(out, "<unknown>");
            break;
    }
}

void hml_print(HmlValue val) {
    print_value_to(stdout, val);
    printf("\n");
    fflush(stdout);
}

void hml_eprint(HmlValue val) {
    print_value_to(stderr, val);
    fprintf(stderr, "\n");
    fflush(stderr);
}

// Print without newline (for multi-argument print support)
void hml_print_value(HmlValue val) {
    print_value_to(stdout, val);
    fflush(stdout);
}

void hml_eprint_value(HmlValue val) {
    print_value_to(stderr, val);
    fflush(stderr);
}

// Print newline only (for multi-argument print support)
void hml_print_newline(void) {
    printf("\n");
    fflush(stdout);
}

// Flush stdout without newline (for write() builtin)
void hml_write_flush(void) {
    fflush(stdout);
}

void hml_eprint_newline(void) {
    fprintf(stderr, "\n");
    fflush(stderr);
}

// I/O builtins as first-class functions
HmlValue hml_builtin_print(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    hml_print(val);
    return hml_val_null();
}

HmlValue hml_builtin_println(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    hml_print(val);
    return hml_val_null();
}

HmlValue hml_builtin_write(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    hml_print_value(val);
    fflush(stdout);
    return hml_val_null();
}

HmlValue hml_builtin_eprint(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    hml_eprint(val);
    return hml_val_null();
}

// ========== VALUE COMPARISON ==========

// Strict per-type equality used by array search methods (contains, indexOf,
// lastIndexOf). Matches the interpreter's values_equal: values of different
// types are never equal (2 != 2.0 here), runes only equal runes, and
// arrays/objects compare by reference.
int hml_values_equal(HmlValue left, HmlValue right) {
    if (left.type != right.type) {
        return 0;
    }

    switch (left.type) {
        case HML_VAL_I8:   return left.as.as_i8 == right.as.as_i8;
        case HML_VAL_I16:  return left.as.as_i16 == right.as.as_i16;
        case HML_VAL_I32:  return left.as.as_i32 == right.as.as_i32;
        case HML_VAL_I64:  return left.as.as_i64 == right.as.as_i64;
        case HML_VAL_U8:   return left.as.as_u8 == right.as.as_u8;
        case HML_VAL_U16:  return left.as.as_u16 == right.as.as_u16;
        case HML_VAL_U32:  return left.as.as_u32 == right.as.as_u32;
        case HML_VAL_U64:  return left.as.as_u64 == right.as.as_u64;
        case HML_VAL_F32:  return left.as.as_f32 == right.as.as_f32;
        case HML_VAL_F64:  return left.as.as_f64 == right.as.as_f64;
        case HML_VAL_BOOL: return left.as.as_bool == right.as.as_bool;
        case HML_VAL_RUNE: return left.as.as_rune == right.as.as_rune;
        case HML_VAL_STRING:
            if (!left.as.as_string || !right.as.as_string) return 0;
            return strcmp(left.as.as_string->data, right.as.as_string->data) == 0;
        case HML_VAL_PTR:    return left.as.as_ptr == right.as.as_ptr;
        case HML_VAL_OBJECT: return left.as.as_object == right.as.as_object;
        case HML_VAL_ARRAY:  return left.as.as_array == right.as.as_array;
        case HML_VAL_NULL:   return 1;
        default:             return 0;  // Functions and other types never compare equal
    }
}

// ========== TYPE CHECKING ==========

const char* hml_typeof(HmlValue val) {
    return hml_typeof_str(val);
}

void hml_check_type(HmlValue val, HmlValueType expected, const char *var_name) {
    if (val.type != expected) {
        hml_runtime_error("Type mismatch for '%s': expected %s, got %s",
                var_name, hml_type_name(expected), hml_typeof_str(val));
    }
}

// Helper to check if a value is an integer type
int hml_is_integer_type(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
            return 1;
        default:
            return 0;
    }
}

// Helper to check if a value is a float type
int hml_is_float_type(HmlValue val) {
    return val.type == HML_VAL_F32 || val.type == HML_VAL_F64;
}

// Helper to extract int64 from any numeric value
int64_t hml_val_to_int64(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:  return val.as.as_i8;
        case HML_VAL_I16: return val.as.as_i16;
        case HML_VAL_I32: return val.as.as_i32;
        case HML_VAL_I64: return val.as.as_i64;
        case HML_VAL_U8:  return val.as.as_u8;
        case HML_VAL_U16: return val.as.as_u16;
        case HML_VAL_U32: return val.as.as_u32;
        case HML_VAL_U64: return (int64_t)val.as.as_u64;
        case HML_VAL_F32: {
            float fv = val.as.as_f32;
            if (isnan(fv) || isinf(fv)) return 0;
            return (int64_t)fv;
        }
        case HML_VAL_F64: {
            double dv = val.as.as_f64;
            if (isnan(dv) || isinf(dv)) return 0;
            return (int64_t)dv;
        }
        case HML_VAL_BOOL: return val.as.as_bool ? 1 : 0;
        case HML_VAL_RUNE: return val.as.as_rune;
        default: return 0;
    }
}

// Helper to extract double from any numeric value
double hml_val_to_double(HmlValue val) {
    switch (val.type) {
        case HML_VAL_I8:  return (double)val.as.as_i8;
        case HML_VAL_I16: return (double)val.as.as_i16;
        case HML_VAL_I32: return (double)val.as.as_i32;
        case HML_VAL_I64: return (double)val.as.as_i64;
        case HML_VAL_U8:  return (double)val.as.as_u8;
        case HML_VAL_U16: return (double)val.as.as_u16;
        case HML_VAL_U32: return (double)val.as.as_u32;
        case HML_VAL_U64: return (double)val.as.as_u64;
        case HML_VAL_F32: return (double)val.as.as_f32;
        case HML_VAL_F64: return val.as.as_f64;
        default: return 0.0;
    }
}

// Helper to check if a target type is an integer type
static int hml_is_integer_target_type(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
            return 1;
        default:
            return 0;
    }
}

// Helper to check if a type is numeric (for type annotations)
static int hml_is_numeric_target_type(HmlValueType type) {
    switch (type) {
        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
        case HML_VAL_F32: case HML_VAL_F64:
            return 1;
        default:
            return 0;
    }
}

HmlValue hml_convert_to_type(HmlValue val, HmlValueType target_type) {
    // If already the target type, return as-is
    if (val.type == target_type) {
        return val;
    }

    // Extract source value
    int64_t int_val = 0;
    double float_val = 0.0;
    int is_source_float = hml_is_float_type(val);

    if (hml_is_integer_type(val) || val.type == HML_VAL_BOOL || val.type == HML_VAL_RUNE) {
        int_val = hml_val_to_int64(val);
    } else if (is_source_float) {
        float_val = hml_val_to_double(val);
    } else if (val.type == HML_VAL_STRING && target_type == HML_VAL_STRING) {
        return val;
    } else if (val.type == HML_VAL_STRING && target_type == HML_VAL_BOOL) {
        // String to bool via type annotation is not allowed
        // Use explicit conversion: bool("true") or bool("false")
        hml_runtime_error("Cannot convert string to bool via type annotation. Use bool(\"...\") instead.");
        return hml_val_null();
    } else if (val.type == HML_VAL_STRING && hml_is_numeric_target_type(target_type)) {
        // String to numeric via type annotation is not allowed
        // Use explicit conversion: i32("42"), f64("3.14"), etc.
        hml_runtime_error("Cannot convert string to %s via type annotation. Use %s(\"...\") instead.",
                hml_type_name(target_type), hml_type_name(target_type));
        return hml_val_null();
    } else if (val.type == HML_VAL_NULL && target_type == HML_VAL_NULL) {
        return val;
    } else {
        hml_runtime_error("Cannot convert %s to %s",
                hml_type_name(val.type), hml_type_name(target_type));
        return hml_val_null();  // Never reached, but silences compiler warning
    }

    // Check for NaN/Inf/out-of-range before float-to-int cast (which is UB in C)
    if (is_source_float && hml_is_integer_target_type(target_type)) {
        if (isnan(float_val)) {
            hml_runtime_error("Cannot convert NaN to %s", hml_type_name(target_type));
        }
        if (isinf(float_val)) {
            hml_runtime_error("Cannot convert %sInfinity to %s",
                    float_val < 0 ? "-" : "", hml_type_name(target_type));
        }
        if (float_val > 9.223372036854775e+18 || float_val < -9.223372036854776e+18) {
            hml_runtime_error("Float value out of range for integer conversion to %s", hml_type_name(target_type));
        }
    }

    switch (target_type) {
        case HML_VAL_I8:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < -128 || int_val > 127) {
                hml_runtime_error("Value %" PRId64 " out of range for i8 [-128, 127]", int_val);
            }
            return hml_val_i8((int8_t)int_val);

        case HML_VAL_I16:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < -32768 || int_val > 32767) {
                hml_runtime_error("Value %" PRId64 " out of range for i16 [-32768, 32767]", int_val);
            }
            return hml_val_i16((int16_t)int_val);

        case HML_VAL_I32:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < -2147483648LL || int_val > 2147483647LL) {
                hml_runtime_error("Value %" PRId64 " out of range for i32 [-2147483648, 2147483647]", int_val);
            }
            return hml_val_i32((int32_t)int_val);

        case HML_VAL_I64:
            if (is_source_float) int_val = (int64_t)float_val;
            return hml_val_i64(int_val);

        case HML_VAL_U8:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0 || int_val > 255) {
                hml_runtime_error("Value %" PRId64 " out of range for u8 [0, 255]", int_val);
            }
            return hml_val_u8((uint8_t)int_val);

        case HML_VAL_U16:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0 || int_val > 65535) {
                hml_runtime_error("Value %" PRId64 " out of range for u16 [0, 65535]", int_val);
            }
            return hml_val_u16((uint16_t)int_val);

        case HML_VAL_U32:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0 || int_val > 4294967295LL) {
                hml_runtime_error("Value %" PRId64 " out of range for u32 [0, 4294967295]", int_val);
            }
            return hml_val_u32((uint32_t)int_val);

        case HML_VAL_U64:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0) {
                hml_runtime_error("Value %" PRId64 " out of range for u64 [0, 18446744073709551615]", int_val);
            }
            return hml_val_u64((uint64_t)int_val);

        case HML_VAL_F32:
            if (is_source_float) {
                return hml_val_f32((float)float_val);
            } else {
                return hml_val_f32((float)int_val);
            }

        case HML_VAL_F64:
            if (is_source_float) {
                return hml_val_f64(float_val);
            } else {
                return hml_val_f64((double)int_val);
            }

        case HML_VAL_RUNE:
            if (is_source_float) int_val = (int64_t)float_val;
            if (int_val < 0 || int_val > 0x10FFFF) {
                hml_runtime_error("Value %ld out of range for rune [0, 0x10FFFF]", int_val);
            }
            return hml_val_rune((uint32_t)int_val);

        case HML_VAL_BOOL:
            // String -> bool is handled above with early return
            // Allow conversion from numeric types to bool (0 = false, non-zero = true)
            if (is_source_float) {
                return hml_val_bool(float_val != 0.0);
            }
            return hml_val_bool(int_val != 0);

        case HML_VAL_STRING:
            // Allow conversion from rune to string (match interpreter behavior)
            if (val.type == HML_VAL_RUNE) {
                char rune_bytes[5];  // Max 4 bytes + null terminator
                int rune_len = utf8_encode_rune(val.as.as_rune, rune_bytes);
                rune_bytes[rune_len] = '\0';
                return hml_val_string(rune_bytes);
            }
            // Allow conversion from bool to string
            if (val.type == HML_VAL_BOOL) {
                return hml_val_string(val.as.as_bool ? "true" : "false");
            }
            // Allow conversion from numeric types to string
            if (hml_is_integer_type(val)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRId64, hml_val_to_int64(val));
                return hml_val_string(buf);
            }
            if (hml_is_float_type(val)) {
                char buf[64];
                double fval = hml_val_to_double(val);
                // Use %g to avoid trailing zeros, but ensure we get enough precision
                snprintf(buf, sizeof(buf), "%.17g", fval);
                return hml_val_string(buf);
            }
            hml_runtime_error("Cannot convert %s to string", hml_type_name(val.type));
            return hml_val_null();

        default:
            // For other types, return as-is
            return val;
    }
}

// Parse a value to a target type (for type constructors like i32("42"))
// This function ALLOWS string parsing, unlike hml_convert_to_type
HmlValue hml_parse_string_to_type(HmlValue val, HmlValueType target_type) {
    // If already the target type, return as-is
    if (val.type == target_type) {
        return val;
    }

    // Handle string parsing for type constructors
    if (val.type == HML_VAL_STRING && target_type == HML_VAL_BOOL) {
        // String to bool: check for "true" or "false"
        HmlString *str = val.as.as_string;
        if (str && str->length == 4 &&
            str->data[0] == 't' && str->data[1] == 'r' &&
            str->data[2] == 'u' && str->data[3] == 'e') {
            return hml_val_bool(1);
        } else if (str && str->length == 5 &&
            str->data[0] == 'f' && str->data[1] == 'a' &&
            str->data[2] == 'l' && str->data[3] == 's' && str->data[4] == 'e') {
            return hml_val_bool(0);
        }
        hml_runtime_error("Cannot parse string as bool (expected 'true' or 'false')");
        return hml_val_null();
    } else if (val.type == HML_VAL_STRING && hml_is_numeric_target_type(target_type)) {
        // String to numeric conversion - parse the string
        HmlString *str = val.as.as_string;
        int64_t int_val = 0;
        double float_val = 0.0;
        int is_float = 0;

        if (str && str->length > 0) {
            // Create null-terminated copy for parsing
            char *cstr = malloc(str->length + 1);
            memcpy(cstr, str->data, str->length);
            cstr[str->length] = '\0';

            // Try to parse as number
            char *endptr;

            // Check for float (contains '.' or 'e'/'E')
            int has_decimal = 0;
            for (int64_t i = 0; i < str->length; i++) {
                if (cstr[i] == '.' || cstr[i] == 'e' || cstr[i] == 'E') {
                    has_decimal = 1;
                    break;
                }
            }

            if (has_decimal) {
                float_val = strtod(cstr, &endptr);
                if (endptr == cstr || *endptr != '\0') {
                    hml_runtime_error("Cannot parse '%s' as number", cstr);
                }
                is_float = 1;
            } else {
                int_val = strtoll(cstr, &endptr, 0);  // base 0 supports hex, octal
                if (endptr == cstr || *endptr != '\0') {
                    hml_runtime_error("Cannot parse '%s' as integer", cstr);
                }
            }
            free(cstr);
        } else {
            hml_runtime_error("Cannot convert empty string to number");
        }

        // Check for NaN/Inf/out-of-range before float-to-int cast (which is UB in C)
        if (is_float && hml_is_integer_target_type(target_type)) {
            if (isnan(float_val)) {
                hml_runtime_error("Cannot convert NaN to %s", hml_type_name(target_type));
            }
            if (isinf(float_val)) {
                hml_runtime_error("Cannot convert %sInfinity to %s",
                        float_val < 0 ? "-" : "", hml_type_name(target_type));
            }
            if (float_val > 9.223372036854775e+18 || float_val < -9.223372036854776e+18) {
                hml_runtime_error("Float value out of range for integer conversion to %s", hml_type_name(target_type));
            }
        }

        // Now convert to target type with range checking
        switch (target_type) {
            case HML_VAL_I8:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -128 || int_val > 127) {
                    hml_runtime_error("Value %" PRId64 " out of range for i8 [-128, 127]", int_val);
                }
                return hml_val_i8((int8_t)int_val);

            case HML_VAL_I16:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -32768 || int_val > 32767) {
                    hml_runtime_error("Value %" PRId64 " out of range for i16 [-32768, 32767]", int_val);
                }
                return hml_val_i16((int16_t)int_val);

            case HML_VAL_I32:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < -2147483648LL || int_val > 2147483647LL) {
                    hml_runtime_error("Value %" PRId64 " out of range for i32", int_val);
                }
                return hml_val_i32((int32_t)int_val);

            case HML_VAL_I64:
                if (is_float) int_val = (int64_t)float_val;
                return hml_val_i64(int_val);

            case HML_VAL_U8:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > 255) {
                    hml_runtime_error("Value %" PRId64 " out of range for u8 [0, 255]", int_val);
                }
                return hml_val_u8((uint8_t)int_val);

            case HML_VAL_U16:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > 65535) {
                    hml_runtime_error("Value %" PRId64 " out of range for u16 [0, 65535]", int_val);
                }
                return hml_val_u16((uint16_t)int_val);

            case HML_VAL_U32:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0 || int_val > 4294967295LL) {
                    hml_runtime_error("Value %" PRId64 " out of range for u32", int_val);
                }
                return hml_val_u32((uint32_t)int_val);

            case HML_VAL_U64:
                if (is_float) int_val = (int64_t)float_val;
                if (int_val < 0) {
                    hml_runtime_error("Value %" PRId64 " out of range for u64", int_val);
                }
                return hml_val_u64((uint64_t)int_val);

            case HML_VAL_F32:
                if (is_float) {
                    return hml_val_f32((float)float_val);
                } else {
                    return hml_val_f32((float)int_val);
                }

            case HML_VAL_F64:
                if (is_float) {
                    return hml_val_f64(float_val);
                } else {
                    return hml_val_f64((double)int_val);
                }

            default:
                break;
        }
    }

    // For non-string values, fall back to regular hml_convert_to_type
    return hml_convert_to_type(val, target_type);
}

// ========== ASSERTIONS ==========

void hml_assert(HmlValue condition, HmlValue message) {
    if (!hml_to_bool(condition)) {
        // Throw catchable exception (match interpreter behavior)
        HmlValue exception_msg;
        if (message.type == HML_VAL_STRING && message.as.as_string) {
            // hml_throw consumes its argument, but `message` is a borrowed
            // builtin parameter - take our own reference to hand over.
            exception_msg = message;
            hml_retain(&exception_msg);
        } else {
            exception_msg = hml_val_string("assertion failed");
        }
        hml_throw(exception_msg);
    }
}

void hml_panic(HmlValue message) {
    fprintf(stderr, "panic: ");
    print_value_to(stderr, message);
    fprintf(stderr, "\n");
    exit(1);
}
