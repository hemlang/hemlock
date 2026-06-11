/*
 * Hemlock Runtime Library - Terminal Control
 *
 * __term_* builtins backing @stdlib/termios and @stdlib/terminal:
 * raw mode, single-byte reads with timeout, terminal size, tty check.
 * The platform work (POSIX termios / Windows console API) lives in
 * src/shared/term_core.c, compiled into this library.
 */

#include "builtins_internal.h"
#include "term_core.h"

// __term_is_tty() -> bool
HmlValue hml_term_is_tty(void) {
    return hml_val_bool(hml_termcore_is_tty());
}

// __term_raw(enable: bool) -> bool (true on success)
HmlValue hml_term_raw(HmlValue enable) {
    if (enable.type != HML_VAL_BOOL) {
        hml_runtime_error("__term_raw() expects 1 bool argument");
    }
    return hml_val_bool(hml_termcore_raw(enable.as.as_bool) == 0);
}

// __term_read_byte(timeout_ms: i32) -> i32 (-1 on timeout/EOF; timeout < 0 blocks)
HmlValue hml_term_read_byte(HmlValue timeout_val) {
    int timeout_ms;
    if (timeout_val.type == HML_VAL_I32) {
        timeout_ms = timeout_val.as.as_i32;
    } else if (timeout_val.type == HML_VAL_I64) {
        timeout_ms = (int)timeout_val.as.as_i64;
    } else {
        hml_runtime_error("__term_read_byte() expects 1 integer argument (timeout ms)");
    }
    return hml_val_i32(hml_termcore_read_byte(timeout_ms));
}

// __term_size() -> { rows, cols } | null
HmlValue hml_term_size(void) {
    int rows = 0, cols = 0;
    if (hml_termcore_size(&rows, &cols) != 0) {
        return hml_val_null();
    }
    HmlValue result = hml_val_object();
    hml_object_set_field(result, "rows", hml_val_i32(rows));
    hml_object_set_field(result, "cols", hml_val_i32(cols));
    return result;
}

// Builtin wrappers for function-as-value usage
HmlValue hml_builtin_term_is_tty(HmlClosureEnv *env) {
    (void)env;
    return hml_term_is_tty();
}

HmlValue hml_builtin_term_raw(HmlClosureEnv *env, HmlValue enable) {
    (void)env;
    return hml_term_raw(enable);
}

HmlValue hml_builtin_term_read_byte(HmlClosureEnv *env, HmlValue timeout_val) {
    (void)env;
    return hml_term_read_byte(timeout_val);
}

HmlValue hml_builtin_term_size(HmlClosureEnv *env) {
    (void)env;
    return hml_term_size();
}
